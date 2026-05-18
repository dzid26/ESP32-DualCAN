#pragma once

/*
 * Tesla BLE — central role.
 *
 * Phase 2 step 1: scan for cars advertising the Tesla VCSEC service.
 * The actual pair / connect / discover flow gets layered on top of this
 * in later steps once we know scanning works on the hardware.
 *
 * Threading: the result callback fires from the NimBLE host task. cJSON
 * + dorky_ble_notify are safe from that context (the existing trace +
 * log push paths already use them), so the protocol layer can synthesize
 * its response directly inside the callback.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  addr[6];       /* BD_ADDR, NimBLE little-endian order */
    uint8_t  addr_type;     /* BLE_ADDR_PUBLIC / _RANDOM / _RPA_* */
    int8_t   rssi;
    char     name[31];      /* GAP local name (truncated, NUL-terminated) */
} tesla_scan_result_t;

/* Invoked once when the scan window ends. `results` is freed after the
 * callback returns — copy what you need. `count` may be 0. */
typedef void (*tesla_scan_done_cb_t)(const tesla_scan_result_t *results,
                                     size_t count,
                                     void *ctx);

/* Start a scan filtered on the Tesla VCSEC service UUID. Returns
 * ESP_ERR_INVALID_STATE if a scan is already in progress or the host
 * isn't ready yet. On ESP_OK the callback is invoked exactly once when
 * the scan window ends; on any other return the callback is NOT called
 * and the caller owns the ctx. */
esp_err_t tesla_central_scan_start(uint32_t duration_ms,
                                   tesla_scan_done_cb_t cb,
                                   void *ctx);

#ifdef __cplusplus
}
#endif
