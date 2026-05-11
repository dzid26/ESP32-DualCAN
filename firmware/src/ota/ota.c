/*
 * OTA firmware update implementation using ESP-IDF's esp_ota_ops.
 *
 * The flow is:
 *   1. ota_begin()  — find next OTA partition, erase it, open an OTA handle
 *   2. ota_write()  — stream raw firmware bytes in chunks (called many times)
 *   3. ota_end()    — validate, set boot partition, optionally reboot
 *
 * If something goes wrong at any stage the caller should invoke ota_abort()
 * to clean up and let the user retry.
 */

#include "ota/ota.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static bool s_in_progress = false;
static size_t s_written = 0;

void ota_init(void)
{
    /* Nothing to do — state is all zeroed. */
}

bool ota_in_progress(void)
{
    return s_in_progress;
}

int ota_begin(size_t *max_size, char *err_buf, size_t err_buf_len)
{
    if (s_in_progress) {
        snprintf(err_buf, err_buf_len, "OTA session already active");
        return -1;
    }

    /* Find the next OTA slot (the one we're NOT running from). */
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        snprintf(err_buf, err_buf_len, "no OTA partition found");
        return -1;
    }

    ESP_LOGI(TAG, "OTA target: %s  offset=0x%08" PRIx32 "  size=%" PRIu32,
             s_update_partition->label,
             (uint32_t)s_update_partition->address,
             (uint32_t)s_update_partition->size);

    /* OTA_SIZE_UNKNOWN lets us write until ota_end validates the image. */
    esp_err_t err = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (err != ESP_OK) {
        snprintf(err_buf, err_buf_len, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_update_partition = NULL;
        return -1;
    }

    s_in_progress = true;
    s_written = 0;
    if (max_size) *max_size = s_update_partition->size;

    ESP_LOGI(TAG, "OTA session started (max %" PRIu32 " bytes)", (uint32_t)s_update_partition->size);
    return 0;
}

int ota_write(const uint8_t *data, size_t len, char *err_buf, size_t err_buf_len)
{
    if (!s_in_progress) {
        snprintf(err_buf, err_buf_len, "no OTA session active");
        return -1;
    }
    if (len == 0) return 0;

    esp_err_t err = esp_ota_write(s_ota_handle, data, len);
    if (err != ESP_OK) {
        snprintf(err_buf, err_buf_len, "esp_ota_write failed: %s", esp_err_to_name(err));
        ota_abort();
        return -1;
    }

    s_written += len;

    /* Log progress every ~64 KB to avoid flooding. */
    if ((s_written & 0xFFFF) < len) {
        ESP_LOGI(TAG, "OTA written %" PRIu32 " KB", (uint32_t)(s_written / 1024));
    }
    return 0;
}

int ota_end(bool reboot, size_t expected_len, char *err_buf, size_t err_buf_len)
{
    if (!s_in_progress) {
        snprintf(err_buf, err_buf_len, "no OTA session active");
        return -1;
    }

    if (expected_len != 0 && expected_len != s_written) {
        snprintf(err_buf, err_buf_len,
                 "size mismatch: expected %" PRIu32 ", got %" PRIu32,
                 (uint32_t)expected_len, (uint32_t)s_written);
        ota_abort();
        return -1;
    }

    esp_err_t err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        snprintf(err_buf, err_buf_len, "esp_ota_end failed (bad image?): %s",
                 esp_err_to_name(err));
        s_in_progress = false;
        s_update_partition = NULL;
        return -1;
    }

    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) {
        snprintf(err_buf, err_buf_len, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        s_in_progress = false;
        s_update_partition = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "OTA complete — %" PRIu32 " bytes written to %s",
             (uint32_t)s_written, s_update_partition->label);

    s_in_progress = false;
    s_update_partition = NULL;

    if (reboot) {
        ESP_LOGI(TAG, "Rebooting in 500 ms…");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    return 0;
}

void ota_abort(void)
{
    if (s_in_progress) {
        esp_ota_abort(s_ota_handle);
        ESP_LOGW(TAG, "OTA aborted (%" PRIu32 " bytes written)", (uint32_t)s_written);
    }
    s_in_progress = false;
    s_update_partition = NULL;
    s_ota_handle = 0;
    s_written = 0;
}
