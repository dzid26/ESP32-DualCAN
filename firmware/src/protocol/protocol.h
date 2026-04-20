#pragma once

/*
 * Wire protocol between the web UI and firmware.
 *
 * Framing: each request/response is prefixed with a 4-byte little-endian
 * length header, followed by that many bytes of UTF-8 JSON. BLE writes may
 * split a frame across multiple callbacks; the parser buffers until a full
 * frame is assembled.
 *
 * Requests:
 *   {"op": <string>, "id": <int>, ...op-specific fields}
 * Responses:
 *   {"id": <int>, "ok": true, "result": ...}
 *   {"id": <int>, "ok": false, "error": <string>}
 */

#include <stddef.h>
#include <stdint.h>

#include "scripting/script_loader.h"

/* Feed raw bytes from the BLE RX characteristic. Parses assembled frames
 * and dispatches them to the correct handler. Responses are sent via
 * dorky_ble_notify(). */
void protocol_on_ble_write(const uint8_t *data, size_t len, void *ctx);

/* Initialize the protocol layer. The loader is shared state used to
 * service script.* commands. */
void protocol_init(script_loader_t *loader);
