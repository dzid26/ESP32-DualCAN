#pragma once

/*
 * tesla_vehicle — glues TeslaBLE::Vehicle to our NimBLE central transport.
 *
 * Vehicle owns a second TeslaBLE::Client instance (separate from the
 * Phase-1 singleton in tesla_ble.cpp).  Both share the same private key
 * via the NVS-backed StorageAdapter: our "tesla"/"pem" key is served
 * under the "private_key" storage key Vehicle expects.
 *
 * Lifecycle:
 *   tesla_vehicle_init()  — once at boot, starts the loop task.
 *   tesla_vehicle_pair()  — scan result → connect → send whitelist msg.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fired when the pair request sequence completes (or fails).
 * success=true means the whitelist-add-key message was accepted by the
 * car.  The user must still physically approve via a NFC card / existing
 * phone key inside the car within ~30 seconds.
 * error is NULL on success, human-readable string on failure. */
typedef void (*tesla_vehicle_done_cb_t)(bool success, const char *error,
                                        void *ctx);

/* Initialize the module: constructs the Vehicle object, loads key + VIN
 * from NVS, starts the loop task.  Must be called once at boot after
 * tesla_ble_init().  Idempotent. */
esp_err_t tesla_vehicle_init(void);

/* Connect to a car at the given BD_ADDR and initiate the VCSEC whitelist
 * add-key ("pair") handshake.  cb fires when done or on failure; on
 * ESP_OK the connection is in flight and cb owns ctx. */
esp_err_t tesla_vehicle_pair(const uint8_t addr[6], uint8_t addr_type,
                              tesla_vehicle_done_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
