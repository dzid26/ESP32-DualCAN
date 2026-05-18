/*
 * Tesla BLE central — scan + connect + GATT implementation.
 *
 * Coexists with the peripheral role (ble_transport.c): NimBLE supports
 * simultaneous central + peripheral with CONFIG_BT_NIMBLE_MAX_CONNECTIONS>=2,
 * and we already require that for the WebUI pairing path.
 *
 * Tesla VCSEC service UUID: 00000211-b2d1-43f0-9b88-960cebf8b91e.
 * Tesla cars advertise that UUID in the complete-128-bit-services field,
 * plus a short local name shaped "S<8-hex-VIN-hash>C".
 *
 * Connect / discover state machine:
 *   IDLE → CONNECTING → DISC_SVC → DISC_RX_CHR → DISC_TX_CHR
 *        → DISC_CCCD → SUBSCRIBING → READY
 * Each step kicks off the next one from its completion callback.
 */

#include "tesla_ble/tesla_central.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

static const char *TAG = "tesla_central";

/* ---- Shared UUIDs ---- */

/* 00000211-b2d1-43f0-9b88-960cebf8b91e  (NimBLE little-endian) */
static const ble_uuid128_t TESLA_VCSEC_SVC_UUID = {
    .u   = { .type = BLE_UUID_TYPE_128 },
    .value = { 0x1e, 0x91, 0xf8, 0xeb, 0xce, 0x60, 0x88, 0x9b,
               0xf0, 0x43, 0xd1, 0xb2, 0x11, 0x02, 0x00, 0x00 },
};
/* 00000212-b2d1-43f0-9b88-960cebf8b91e  (write to car) */
static const ble_uuid128_t TESLA_RX_CHR_UUID = {
    .u   = { .type = BLE_UUID_TYPE_128 },
    .value = { 0x1e, 0x91, 0xf8, 0xeb, 0xce, 0x60, 0x88, 0x9b,
               0xf0, 0x43, 0xd1, 0xb2, 0x12, 0x02, 0x00, 0x00 },
};
/* 00000213-b2d1-43f0-9b88-960cebf8b91e  (car sends indications) */
static const ble_uuid128_t TESLA_TX_CHR_UUID = {
    .u   = { .type = BLE_UUID_TYPE_128 },
    .value = { 0x1e, 0x91, 0xf8, 0xeb, 0xce, 0x60, 0x88, 0x9b,
               0xf0, 0x43, 0xd1, 0xb2, 0x13, 0x02, 0x00, 0x00 },
};

/* ---- Scan state ---- */

#define MAX_RESULTS 8

static struct {
    bool                  active;
    tesla_scan_done_cb_t  cb;
    void                 *ctx;
    tesla_scan_result_t   results[MAX_RESULTS];
    size_t                count;
} s_scan;

static bool addr_already_seen(const uint8_t addr[6])
{
    for (size_t i = 0; i < s_scan.count; i++) {
        if (memcmp(s_scan.results[i].addr, addr, 6) == 0) return true;
    }
    return false;
}

static bool adv_contains_tesla_uuid(const uint8_t *data, uint8_t len)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, data, len) != 0) return false;
    if (fields.uuids128 == NULL) return false;
    for (uint8_t i = 0; i < fields.num_uuids128; i++) {
        if (memcmp(fields.uuids128[i].value,
                   TESLA_VCSEC_SVC_UUID.value, 16) == 0) return true;
    }
    return false;
}

static void copy_adv_name(const uint8_t *data, uint8_t len,
                          char *out, size_t out_sz)
{
    struct ble_hs_adv_fields fields;
    out[0] = '\0';
    if (ble_hs_adv_parse_fields(&fields, data, len) != 0) return;
    if (!fields.name || !fields.name_len) return;
    size_t n = fields.name_len < out_sz - 1 ? fields.name_len : out_sz - 1;
    memcpy(out, fields.name, n);
    out[n] = '\0';
}

static void finish_scan(void)
{
    if (!s_scan.active) return;
    s_scan.active = false;
    tesla_scan_done_cb_t cb  = s_scan.cb;
    void                *ctx = s_scan.ctx;
    s_scan.cb  = NULL;
    s_scan.ctx = NULL;
    if (cb) cb(s_scan.results, s_scan.count, ctx);
}

static int scan_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (!adv_contains_tesla_uuid(event->disc.data,
                                     event->disc.length_data)) return 0;
        if (addr_already_seen(event->disc.addr.val)) return 0;
        if (s_scan.count >= MAX_RESULTS) {
            ESP_LOGW(TAG, "scan buffer full, dropping hit");
            return 0;
        }
        tesla_scan_result_t *r = &s_scan.results[s_scan.count++];
        memcpy(r->addr, event->disc.addr.val, 6);
        r->addr_type = event->disc.addr.type;
        r->rssi      = event->disc.rssi;
        copy_adv_name(event->disc.data, event->disc.length_data,
                      r->name, sizeof(r->name));
        ESP_LOGI(TAG, "found Tesla \"%s\" rssi=%d", r->name, r->rssi);
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "scan done: %u match(es)", (unsigned)s_scan.count);
        finish_scan();
        return 0;
    default:
        return 0;
    }
}

esp_err_t tesla_central_scan_start(uint32_t duration_ms,
                                   tesla_scan_done_cb_t cb,
                                   void *ctx)
{
    if (s_scan.active) return ESP_ERR_INVALID_STATE;
    if (!ble_hs_is_enabled()) return ESP_ERR_INVALID_STATE;

    s_scan.active = true;
    s_scan.cb     = cb;
    s_scan.ctx    = ctx;
    s_scan.count  = 0;

    struct ble_gap_disc_params params = {
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .passive           = 1,
        .filter_duplicates = 1,
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, duration_ms, &params,
                          scan_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: rc=%d", rc);
        s_scan.active = false;
        s_scan.cb     = NULL;
        s_scan.ctx    = NULL;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "scan started (%u ms)", (unsigned)duration_ms);
    return ESP_OK;
}

/* ---- Connect / GATT discovery state machine ---- */

typedef enum {
    TC_IDLE,
    TC_CONNECTING,
    TC_DISC_SVC,
    TC_DISC_RX_CHR,
    TC_DISC_TX_CHR,
    TC_DISC_CCCD,
    TC_SUBSCRIBING,
    TC_READY,
} tc_state_t;

static struct {
    tc_state_t    state;
    uint16_t      conn_handle;
    uint16_t      svc_start;
    uint16_t      svc_end;
    uint16_t      rx_val_handle;
    uint16_t      tx_val_handle;
    uint16_t      tx_def_handle; /* def = definition handle, for CCCD search range */
    uint16_t      cccd_handle;

    tesla_central_connected_cb_t    on_connected;
    tesla_central_disconnected_cb_t on_disconnected;
    tesla_central_rx_cb_t           on_rx;
    void                           *ctx;
} s_conn = { .state = TC_IDLE,
             .conn_handle = BLE_HS_CONN_HANDLE_NONE };

/* forward declarations */
static int conn_gap_event_cb(struct ble_gap_event *e, void *arg);
static void start_disc_rx_chr(void);
static void start_disc_tx_chr(void);
static void start_disc_cccd(void);
static void start_subscribe(void);

static void conn_fail(const char *why)
{
    ESP_LOGE(TAG, "connect failed: %s", why);
    if (s_conn.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        s_conn.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    s_conn.state = TC_IDLE;
    if (s_conn.on_connected) {
        s_conn.on_connected(false, s_conn.ctx);
        s_conn.on_connected = NULL;
    }
}

/* --- Service discovery --- */
static int disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == 0 && svc) {
        s_conn.svc_start = svc->start_handle;
        s_conn.svc_end   = svc->end_handle;
        ESP_LOGD(TAG, "VCSEC svc found: handles %u–%u",
                 svc->start_handle, svc->end_handle);
    } else if (error->status == BLE_HS_EDONE) {
        if (s_conn.svc_start == 0) { conn_fail("VCSEC service not found"); return 0; }
        start_disc_rx_chr();
    } else {
        conn_fail("svc disc error");
    }
    return 0;
}

/* --- RX characteristic discovery --- */
static int disc_rx_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0 && chr) {
        s_conn.rx_val_handle = chr->val_handle;
        ESP_LOGD(TAG, "RX chr val_handle=%u", chr->val_handle);
    } else if (error->status == BLE_HS_EDONE) {
        if (s_conn.rx_val_handle == 0) { conn_fail("RX chr not found"); return 0; }
        start_disc_tx_chr();
    } else {
        conn_fail("RX chr disc error");
    }
    return 0;
}

/* --- TX characteristic discovery --- */
static int disc_tx_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                          const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0 && chr) {
        s_conn.tx_val_handle = chr->val_handle;
        s_conn.tx_def_handle = chr->def_handle;
        ESP_LOGD(TAG, "TX chr val_handle=%u def_handle=%u",
                 chr->val_handle, chr->def_handle);
    } else if (error->status == BLE_HS_EDONE) {
        if (s_conn.tx_val_handle == 0) { conn_fail("TX chr not found"); return 0; }
        start_disc_cccd();
    } else {
        conn_fail("TX chr disc error");
    }
    return 0;
}

/* --- CCCD descriptor discovery --- */
static int disc_cccd_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                        void *arg)
{
    if (error->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            s_conn.cccd_handle = dsc->handle;
            ESP_LOGD(TAG, "CCCD handle=%u", dsc->handle);
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (s_conn.cccd_handle == 0) { conn_fail("CCCD not found"); return 0; }
        start_subscribe();
    } else {
        conn_fail("CCCD disc error");
    }
    return 0;
}

/* --- CCCD write completion (subscribe) --- */
static int subscribe_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr, void *arg)
{
    if (error->status != 0) {
        conn_fail("CCCD write failed");
        return 0;
    }
    ESP_LOGI(TAG, "subscribed to Tesla TX indications — ready");
    s_conn.state = TC_READY;
    if (s_conn.on_connected) {
        s_conn.on_connected(true, s_conn.ctx);
        s_conn.on_connected = NULL;
    }
    return 0;
}

static void start_disc_rx_chr(void)
{
    s_conn.state = TC_DISC_RX_CHR;
    int rc = ble_gattc_disc_chrs_by_uuid(s_conn.conn_handle,
                                          s_conn.svc_start, s_conn.svc_end,
                                          &TESLA_RX_CHR_UUID.u,
                                          disc_rx_chr_cb, NULL);
    if (rc != 0) conn_fail("start RX chr disc failed");
}

static void start_disc_tx_chr(void)
{
    s_conn.state = TC_DISC_TX_CHR;
    int rc = ble_gattc_disc_chrs_by_uuid(s_conn.conn_handle,
                                          s_conn.svc_start, s_conn.svc_end,
                                          &TESLA_TX_CHR_UUID.u,
                                          disc_tx_chr_cb, NULL);
    if (rc != 0) conn_fail("start TX chr disc failed");
}

static void start_disc_cccd(void)
{
    s_conn.state = TC_DISC_CCCD;
    /* Descriptors live between the TX chr def_handle+1 and next chr/svc_end. */
    int rc = ble_gattc_disc_all_dscs(s_conn.conn_handle,
                                      s_conn.tx_def_handle + 1,
                                      s_conn.svc_end,
                                      disc_cccd_cb, NULL);
    if (rc != 0) conn_fail("start CCCD disc failed");
}

static void start_subscribe(void)
{
    s_conn.state = TC_SUBSCRIBING;
    /* 0x0002 = indicate enable. Tesla uses indications, not notifications. */
    static const uint8_t indicate_val[2] = { 0x02, 0x00 };
    int rc = ble_gattc_write_flat(s_conn.conn_handle, s_conn.cccd_handle,
                                   indicate_val, sizeof(indicate_val),
                                   subscribe_write_cb, NULL);
    if (rc != 0) conn_fail("CCCD write initiate failed");
}

/* --- GAP event handler for the central connection --- */
static int conn_gap_event_cb(struct ble_gap_event *e, void *arg)
{
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status != 0) {
            ESP_LOGE(TAG, "connect failed status=%d", e->connect.status);
            s_conn.state       = TC_IDLE;
            s_conn.conn_handle = BLE_HS_CONN_HANDLE_NONE;
            if (s_conn.on_connected) {
                s_conn.on_connected(false, s_conn.ctx);
                s_conn.on_connected = NULL;
            }
            return 0;
        }
        s_conn.conn_handle = e->connect.conn_handle;
        ESP_LOGI(TAG, "connected handle=%u — discovering VCSEC service",
                 s_conn.conn_handle);
        s_conn.state = TC_DISC_SVC;
        ble_gattc_disc_svc_by_uuid(s_conn.conn_handle,
                                    &TESLA_VCSEC_SVC_UUID.u,
                                    disc_svc_cb, NULL);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected from car reason=%d",
                 e->disconnect.reason);
        {
            tesla_central_disconnected_cb_t dcb = s_conn.on_disconnected;
            void *dctx = s_conn.ctx;
            s_conn.state          = TC_IDLE;
            s_conn.conn_handle    = BLE_HS_CONN_HANDLE_NONE;
            s_conn.svc_start      = 0;
            s_conn.svc_end        = 0;
            s_conn.rx_val_handle  = 0;
            s_conn.tx_val_handle  = 0;
            s_conn.cccd_handle    = 0;
            s_conn.on_connected   = NULL;
            s_conn.on_disconnected = NULL;
            s_conn.on_rx          = NULL;
            s_conn.ctx            = NULL;
            if (dcb) dcb(e->disconnect.reason, dctx);
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (e->notify_rx.attr_handle == s_conn.tx_val_handle
            && s_conn.on_rx) {
            uint16_t len = OS_MBUF_PKTLEN(e->notify_rx.om);
            uint8_t  buf[512];
            if (len > sizeof(buf)) len = sizeof(buf);
            os_mbuf_copydata(e->notify_rx.om, 0, len, buf);
            s_conn.on_rx(buf, len, s_conn.ctx);
        }
        return 0;

    default:
        return 0;
    }
}

/* ---- Public API ---- */

esp_err_t tesla_central_connect(const uint8_t addr[6], uint8_t addr_type,
                                 tesla_central_connected_cb_t on_connected,
                                 tesla_central_disconnected_cb_t on_disconnected,
                                 tesla_central_rx_cb_t on_rx,
                                 void *ctx)
{
    if (s_conn.state != TC_IDLE) return ESP_ERR_INVALID_STATE;
    if (!ble_hs_is_enabled())    return ESP_ERR_INVALID_STATE;

    s_conn.state          = TC_CONNECTING;
    s_conn.conn_handle    = BLE_HS_CONN_HANDLE_NONE;
    s_conn.svc_start      = 0;
    s_conn.rx_val_handle  = 0;
    s_conn.tx_val_handle  = 0;
    s_conn.cccd_handle    = 0;
    s_conn.on_connected   = on_connected;
    s_conn.on_disconnected = on_disconnected;
    s_conn.on_rx          = on_rx;
    s_conn.ctx            = ctx;

    ble_addr_t peer;
    peer.type = addr_type;
    memcpy(peer.val, addr, 6);

    /* Default connection parameters — fine for most Tesla hardware. */
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peer,
                              30000 /* 30 s timeout */,
                              NULL  /* default params */,
                              conn_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed: rc=%d", rc);
        s_conn.state = TC_IDLE;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "connecting to %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    return ESP_OK;
}

esp_err_t tesla_central_write(const uint8_t *data, size_t len)
{
    if (s_conn.state != TC_READY) return ESP_ERR_INVALID_STATE;
    int rc = ble_gattc_write_no_rsp_flat(s_conn.conn_handle,
                                          s_conn.rx_val_handle,
                                          data, (uint16_t)len);
    if (rc != 0) {
        ESP_LOGE(TAG, "write_no_rsp failed: rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void tesla_central_disconnect(void)
{
    if (s_conn.conn_handle != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(s_conn.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

bool tesla_central_is_connected(void)
{
    return s_conn.state == TC_READY;
}
