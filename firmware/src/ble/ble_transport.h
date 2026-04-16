#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Callback for incoming requests from the BLE client (web UI). */
typedef void (*ble_request_cb_t)(const uint8_t *data, size_t len, void *ctx);

/* Initialize the BLE GATT server. Starts advertising as "Dorky".
 * `on_request` is called when the client writes to the RX characteristic. */
int dorky_ble_init(ble_request_cb_t on_request, void *ctx);

/* Send a notification to the connected client via the TX characteristic.
 * Silently drops if no client is connected. */
int dorky_ble_notify(const uint8_t *data, size_t len);

/* Returns true if a client is currently connected. */
bool dorky_ble_connected(void);
