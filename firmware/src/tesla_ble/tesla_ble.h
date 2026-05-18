#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * Tesla BLE — vehicle key management for Tesla's VCSEC pairing protocol.
 *
 * SCOPE OF THIS HEADER:
 *   - P-256 keypair generation (NIST secp256r1, SEC1 uncompressed pubkey).
 *   - NVS-backed persistence so the same identity survives reboots.
 *   - The actual BLE-central handshake with the car's VCSEC service is NOT
 *     implemented here — see docs/tesla-ble.md for the staged plan.
 *
 * The public key emitted by tesla_ble_get_public_key() is the value the user
 * pastes (or transmits via BLE) into the car's "Add Key" flow. Once Tesla's
 * BLE central role is implemented, this same key signs every command.
 */

/* SEC1 uncompressed P-256 public key: 0x04 || X(32) || Y(32). */
#define TESLA_BLE_PUBKEY_LEN  65

/* Initialize the module. Loads an existing keypair from NVS if present;
 * otherwise leaves the module in the "no key" state until generate_key is
 * called. Must be called after state_nvs_init(). Safe to call multiple times. */
esp_err_t tesla_ble_init(void);

/* True iff a keypair is loaded (either from NVS at boot or freshly generated). */
bool tesla_ble_has_key(void);

/* Generate a fresh P-256 keypair, persist to NVS, and load it into memory.
 * Overwrites any existing key. Returns ESP_OK on success. */
esp_err_t tesla_ble_generate_key(void);

/* Copy the current SEC1 uncompressed public key into `out`. Returns ESP_OK
 * on success, ESP_ERR_NOT_FOUND if no key is loaded. */
esp_err_t tesla_ble_get_public_key(uint8_t out[TESLA_BLE_PUBKEY_LEN]);

/* Wipe the keypair from NVS and memory. After this, tesla_ble_has_key()
 * returns false until a new key is generated. */
esp_err_t tesla_ble_reset(void);
