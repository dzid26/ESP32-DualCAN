#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * Tesla BLE — keypair + vehicle identity management.
 *
 * SCOPE OF THIS HEADER:
 *   - P-256 keypair generation (NIST secp256r1, SEC1 uncompressed pubkey).
 *   - VIN storage (required by the library for signing AAD).
 *   - NVS-backed persistence so identity survives reboots.
 *
 * The public key emitted by tesla_ble_get_public_key() is what the user
 * presents to the car's "Add Key" flow. The VIN is used by the library
 * to filter advertisements (via vin_utils::matches_vin) and to populate
 * the cryptographic AAD in every signed command.
 */

/* SEC1 uncompressed P-256 public key: 0x04 || X(32) || Y(32). */
#define TESLA_BLE_PUBKEY_LEN  65

/* Tesla VIN is exactly 17 ASCII characters (plus NUL). */
#define TESLA_VIN_LEN         17

/* Initialize the module. Loads keypair + VIN from NVS if present.
 * Must be called after state_nvs_init(). Safe to call multiple times. */
esp_err_t tesla_ble_init(void);

/* True iff a keypair is loaded. */
bool tesla_ble_has_key(void);

/* Generate a fresh P-256 keypair, persist to NVS, load into memory. */
esp_err_t tesla_ble_generate_key(void);

/* Copy the current SEC1 uncompressed public key into `out`.
 * Returns ESP_ERR_NOT_FOUND if no key is loaded. */
esp_err_t tesla_ble_get_public_key(uint8_t out[TESLA_BLE_PUBKEY_LEN]);

/* Wipe keypair + VIN from NVS and memory. */
esp_err_t tesla_ble_reset(void);

/* Store VIN (must be exactly 17 chars, NUL-terminated). Persists to NVS
 * and updates the in-memory Client so the library can use it immediately. */
esp_err_t tesla_ble_set_vin(const char *vin);

/* Copy the stored VIN into out (size must be >= 18). Returns
 * ESP_ERR_NOT_FOUND if no VIN has been set. */
esp_err_t tesla_ble_get_vin(char *out, size_t out_sz);

/* True iff a VIN is stored. */
bool tesla_ble_has_vin(void);
