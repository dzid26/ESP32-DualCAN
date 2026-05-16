/*
 * BLE GATT server for the Dorky Commander web UI transport.
 *
 * Custom service with two characteristics:
 *   - RX (write): client sends requests (CBOR-encoded commands).
 *   - TX (notify): server sends responses and event notifications.
 *
 * Uses NimBLE (the only BLE stack available on ESP32-C6).
 *
 * Pairing security:
 *   - Locked by default; only `s_conn_authorized` sessions can write GATT.
 *     A connect is authorized when the pairing window is open OR the peer
 *     is BLE-bonded (peer_is_bonded checks the NVS bond store).
 *   - Unauthorized connections are silently terminated after 100 ms.
 *   - dorky_ble_unlock_pairing() opens a 60 s window (BOOT button / UI),
 *     during which any device may connect and is offered BLE pairing via
 *     ble_gap_security_initiate (needed for Android to prompt the user).
 *   - sm_bonding follows the window so unauthorized devices can never bond.
 *   - On bond establishment (ENC_CHANGE success) the window closes early.
 */
#include "ble/ble_transport.h"

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/nimble_npl.h"

static const char *TAG = "ble";

/* Custom service UUID: randomly generated, stable.
 * 6e400001-b5a3-f393-e0a9-e50e24dcca9e  (Nordic UART Service convention) */
static const ble_uuid128_t DORKY_SVC_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

/* RX characteristic: client writes commands here. */
static const ble_uuid128_t DORKY_RX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

/* TX characteristic: server sends notifications here. */
static const ble_uuid128_t DORKY_TX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_reject_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_attr_handle;
static ble_request_cb_t s_on_request;
static void *s_on_request_ctx;
static bool s_pairing_open    = false;
static bool s_conn_authorized = false;

static struct ble_npl_callout s_adv_callout;
static struct ble_npl_callout s_reject_callout;
static struct ble_npl_callout s_lock_callout;

/* ---- Bond helpers ---- */

static bool peer_is_bonded(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0) return false;
    struct ble_store_key_sec key;
    memset(&key, 0, sizeof key);
    key.peer_addr = desc.peer_id_addr;
    struct ble_store_value_sec val;
    return ble_store_read_peer_sec(&key, &val) == 0;
}

/* ---- Callout callbacks ---- */

static void reject_callout_fn(struct ble_npl_event *ev)
{
    uint16_t h = s_reject_handle;
    s_reject_handle = BLE_HS_CONN_HANDLE_NONE;
    if (h == BLE_HS_CONN_HANDLE_NONE) return;
    ESP_LOGI(TAG, "rejecting unauthorized handle=%d", h);
    ble_gap_terminate(h, BLE_ERR_AUTH_FAIL);
}

static void lock_callout_fn(struct ble_npl_event *ev)
{
    s_pairing_open = false;
    ble_hs_cfg.sm_bonding = 0;
    ESP_LOGI(TAG, "pairing window closed");
}

static int gap_event_cb(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

static void adv_callout_fn(struct ble_npl_event *ev) { start_advertising(); }

static void schedule_advertising(void)
{
    ble_npl_callout_reset(&s_adv_callout, ble_npl_time_ms_to_ticks32(200));
}

/* ---- Service definition ---- */

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        /* Drop writes from any connection that's not the active authorized session. */
        if (conn_handle != s_conn_handle || !s_conn_authorized) return 0;

        struct os_mbuf *om = ctxt->om;
        uint16_t len = OS_MBUF_PKTLEN(om);
        uint8_t buf[512];
        if (len > sizeof(buf)) len = sizeof(buf);
        os_mbuf_copydata(om, 0, len, buf);

        if (s_on_request) {
            s_on_request(buf, len, s_on_request_ctx);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &DORKY_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &DORKY_RX_UUID.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &DORKY_TX_UUID.u,
                .access_cb = gatt_access_cb,
                .val_handle = &s_tx_attr_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

/* ---- GAP event handler ---- */

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen(ble_svc_gap_device_name());
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    /* Also advertise the service UUID in scan response. */
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.uuids128 = (ble_uuid128_t[]){ DORKY_SVC_UUID };
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &adv_params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start failed: %d", rc);
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            uint16_t new_h = event->connect.conn_handle;
            ble_att_set_preferred_mtu(512);
            bool authorized = s_pairing_open || peer_is_bonded(new_h);

            if (!authorized && s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                /* Unauthorized new device — reject it, keep the existing session. */
                ESP_LOGI(TAG, "connected (handle=%d) — unauthorized, rejecting without kick",
                         new_h);
                s_reject_handle = new_h;
                ble_npl_callout_reset(&s_reject_callout, ble_npl_time_ms_to_ticks32(100));
            } else {
                if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                    ESP_LOGI(TAG, "new authorized connection, kicking handle=%d", s_conn_handle);
                    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                }
                s_conn_handle = new_h;
                if (!authorized) {
                    ESP_LOGI(TAG, "connected (handle=%d) — not bonded, rejecting in 2.5s", new_h);
                    s_conn_authorized = false;
                    s_reject_handle = new_h;
                    ble_npl_callout_reset(&s_reject_callout, ble_npl_time_ms_to_ticks32(100));
                } else {
                    ESP_LOGI(TAG, "connected (handle=%d) — authorized", new_h);
                    s_conn_authorized = true;
                    if (s_pairing_open) {
                        /* Trigger OS pairing dialog on the central (Android bonds properly;
                         * BlueZ/Chrome ignores it gracefully — connection still works). */
                        ble_gap_security_initiate(new_h);
                        s_pairing_open = false;
                        ble_npl_callout_stop(&s_lock_callout);
                    }
                }
            }

            schedule_advertising();
        } else if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE: {
        uint16_t h = event->enc_change.conn_handle;
        if (h != s_conn_handle) break;
        bool bonded = peer_is_bonded(h);
        ESP_LOGI(TAG, "enc_change handle=%d status=%d bonded=%d",
                 h, event->enc_change.status, bonded);
        if (event->enc_change.status == 0 && s_pairing_open && bonded) {
            s_pairing_open = false;
            ble_npl_callout_stop(&s_lock_callout);
            ESP_LOGI(TAG, "bond established, pairing window closed");
        }
        break;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
        uint16_t h = event->disconnect.conn.conn_handle;
        ESP_LOGI(TAG, "disconnected handle=%d (reason=%d)", h, event->disconnect.reason);
        if (h == s_reject_handle) {
            ble_npl_callout_stop(&s_reject_callout);
            s_reject_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        if (h == s_conn_handle) {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_conn_authorized = false;
            start_advertising();
        }
        break;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %d", event->mtu.value);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe: attr=%d, notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        break;

    default:
        break;
    }
    return 0;
}

/* ---- NimBLE host sync callback ---- */

static void on_sync(void)
{
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    int bond_count = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bond_count);
    if (bond_count == 0) {
        dorky_ble_unlock_pairing();
    }

    start_advertising();
    ESP_LOGI(TAG, "BLE ready as \"%s\" — pairing %s (%d bond(s))",
             ble_svc_gap_device_name(), s_pairing_open ? "OPEN" : "locked", bond_count);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset (reason=%d)", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---- Public API ---- */

int dorky_ble_init(ble_request_cb_t on_request, void *ctx)
{
    s_on_request = on_request;
    s_on_request_ctx = ctx;

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return -1;
    }

    /* NimBLE logs a line per GATT notify at DEBUG level — too noisy in normal use. */
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;

    /* Security manager: Just Works, bonding toggled by pairing window. */
    ble_hs_cfg.sm_io_cap        = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc            = 1;
    ble_hs_cfg.sm_bonding       = 0;   /* set to 1 in on_sync if no bonds exist */
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_count_cfg: %d", rc); return -1; }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_add_svcs: %d", rc); return -1; }

    ble_svc_gap_device_name_set("Dorky");

    ble_npl_callout_init(&s_adv_callout,    nimble_port_get_dflt_eventq(), adv_callout_fn,    NULL);
    ble_npl_callout_init(&s_reject_callout, nimble_port_get_dflt_eventq(), reject_callout_fn, NULL);
    ble_npl_callout_init(&s_lock_callout,   nimble_port_get_dflt_eventq(), lock_callout_fn,   NULL);

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE transport initialized");
    return 0;
}

int dorky_ble_notify(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return -1;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return -1;

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
    }
    return rc;
}

bool dorky_ble_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool dorky_ble_pairing_open(void)
{
    return s_pairing_open;
}

void dorky_ble_unlock_pairing(void)
{
    s_pairing_open = true;
    ble_hs_cfg.sm_bonding = 1;
    ble_npl_callout_reset(&s_lock_callout, ble_npl_time_ms_to_ticks32(60000));
    ESP_LOGI(TAG, "pairing window open for 60 s");
}
