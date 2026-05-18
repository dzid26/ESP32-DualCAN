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

/* ---- Connect / discover / communicate ---- */

/* Fires once when the connect+discover+subscribe flow finishes.
 * success=true means READY; success=false means something failed. */
typedef void (*tesla_central_connected_cb_t)(bool success, void *ctx);

/* Fires when the connection drops (reason is a NimBLE BLE_ERR_* code). */
typedef void (*tesla_central_disconnected_cb_t)(int reason, void *ctx);

/* Fires for each indication received from the car's TX characteristic. */
typedef void (*tesla_central_rx_cb_t)(const uint8_t *data, size_t len, void *ctx);

/* Connect to a car at addr/addr_type (from a scan result).
 * Automatically discovers VCSEC service + RX/TX chars + CCCD and
 * subscribes to TX indications before firing on_connected(true,...).
 * Returns ESP_OK if the procedure was initiated; on_connected fires
 * asynchronously. Returns non-OK if already connecting or host not ready
 * (on_connected NOT called in that case). */
esp_err_t tesla_central_connect(const uint8_t addr[6], uint8_t addr_type,
                                 tesla_central_connected_cb_t on_connected,
                                 tesla_central_disconnected_cb_t on_disconnected,
                                 tesla_central_rx_cb_t on_rx,
                                 void *ctx);

/* Write-without-response to the car's RX characteristic.
 * Only valid when is_connected() == true. */
esp_err_t tesla_central_write(const uint8_t *data, size_t len);

/* Terminate the connection (on_disconnected will fire). */
void tesla_central_disconnect(void);

/* True when fully connected and ready (subscribe complete). */
bool tesla_central_is_connected(void);

#ifdef __cplusplus
}
#endif
