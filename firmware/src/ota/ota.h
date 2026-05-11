#pragma once

/*
 * OTA (Over-the-Air) firmware update via BLE.
 *
 * Three-phase protocol:
 *   1. ota.begin  — erase next OTA partition, return max firmware size
 *   2. ota.write  — receive base64-encoded chunks and flash them
 *   3. ota.end    — validate written image, switch boot partition, reboot
 *
 * Caller is the protocol dispatcher; OTA state is module-private.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Must be called once (from protocol_init or app_main). */
void ota_init(void);

/* Returns true if an OTA session is in progress. */
bool ota_in_progress(void);

/* Phase 1: prepare the next OTA partition.
 * Returns 0 on success, populating *max_size with the partition capacity.
 * On error returns -1 and writes a human-readable message into err_buf. */
int ota_begin(size_t *max_size, char *err_buf, size_t err_buf_len);

/* Phase 2: write a chunk of raw firmware bytes.
 * `data` / `len` are the raw (already-decoded) bytes.
 * Returns 0 on success, -1 on error (message in err_buf). */
int ota_write(const uint8_t *data, size_t len, char *err_buf, size_t err_buf_len);

/* Phase 3: finalise, validate the image, set boot partition.
 * If `reboot` is true the chip restarts after ~500 ms.
 * If `expected_len` is non-zero, it is compared against the number of bytes
 * actually written; a mismatch fails the call with a clear error.
 * Returns 0 on success, -1 on error (message in err_buf). */
int ota_end(bool reboot, size_t expected_len, char *err_buf, size_t err_buf_len);

/* Cancel an in-progress OTA and free resources. Safe to call even when
 * no session is active. */
void ota_abort(void);
