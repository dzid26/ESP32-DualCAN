#include "storage/fs.h"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "fs";

esp_err_t fs_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("littlefs", &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: %zu / %zu bytes used", used, total);
    return ESP_OK;
}
