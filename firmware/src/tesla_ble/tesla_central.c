/*
 * Tesla BLE central — scan implementation.
 *
 * Coexists with the peripheral role (ble_transport.c): NimBLE supports
 * simultaneous central + peripheral with CONFIG_BT_NIMBLE_MAX_CONNECTIONS>=2,
 * and we already require that for the WebUI pairing path.
 *
 * Tesla VCSEC service UUID: 00000211-b2d1-43f0-9b88-960cebf8b91e.
 * Tesla cars advertise that UUID in the complete-128-bit-services field,
 * plus a short local name shaped "S<8-hex-VIN-hash>C".
 */

#include "tesla_ble/tesla_central.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"

static const char *TAG = "tesla_central";

/* 00000211-b2d1-43f0-9b88-960cebf8b91e in NimBLE little-endian order. */
static const uint8_t TESLA_VCSEC_UUID_LE[16] = {
    0x1e, 0x91, 0xf8, 0xeb, 0xce, 0x60, 0x88, 0x9b,
    0xf0, 0x43, 0xd1, 0xb2, 0x11, 0x02, 0x00, 0x00,
};

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

    /* Check both incomplete and complete UUID128 lists. */
    if (fields.uuids128 != NULL) {
        for (uint8_t i = 0; i < fields.num_uuids128; i++) {
            if (memcmp(fields.uuids128[i].value, TESLA_VCSEC_UUID_LE, 16) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void copy_adv_name(const uint8_t *data, uint8_t len, char *out, size_t out_sz)
{
    struct ble_hs_adv_fields fields;
    out[0] = '\0';
    if (ble_hs_adv_parse_fields(&fields, data, len) != 0) return;
    if (fields.name == NULL || fields.name_len == 0) return;
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
        if (!adv_contains_tesla_uuid(event->disc.data, event->disc.length_data)) {
            return 0;
        }
        if (addr_already_seen(event->disc.addr.val)) return 0;
        if (s_scan.count >= MAX_RESULTS) {
            ESP_LOGW(TAG, "scan result buffer full, dropping further hits");
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
        ESP_LOGI(TAG, "scan complete: %u match(es), reason=%d",
                 (unsigned)s_scan.count, event->disc_complete.reason);
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
        .itvl              = 0,           /* default */
        .window            = 0,           /* default */
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,           /* don't request scan-response data */
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
