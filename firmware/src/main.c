#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dorky";

void app_main(void)
{
    ESP_LOGI(TAG, "Dorky Commander starting (version %s)", DORKY_FIRMWARE_VERSION);

    uint32_t tick = 0;
    while (1) {
        ESP_LOGI(TAG, "tick %" PRIu32, tick++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
