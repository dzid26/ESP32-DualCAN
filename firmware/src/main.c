#include <inttypes.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "berry.h"
#include "can/can.h"
#include "can/can_driver.h"
#include "can/tesla.h"
#include "led/led_rgb.h"
#include "scripting/berry_bindings.h"
#include "ble/ble_transport.h"
#include "dbc/vehicle_dbc_embedded.h"
#include "storage/fs.h"
#include "storage/state.h"
#include "scripting/script_loader.h"
#include "protocol/protocol.h"

static const char *TAG = "dorky";

#define LED_GPIO        GPIO_NUM_15

#define PIN_CAN0_TX     GPIO_NUM_2
#define PIN_CAN0_RX     GPIO_NUM_1
#define PIN_CAN0_STB    GPIO_NUM_19

#define PIN_CAN1_TX     GPIO_NUM_4
#define PIN_CAN1_RX     GPIO_NUM_3
#define PIN_CAN1_STB    GPIO_NUM_18

#define BITRATE_KBPS    500

static can_t bus0, bus1;

static void hw_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    led_rgb_init();

    can_bus_config_t c0 = {
        .bus_id = 0, .tx_pin = PIN_CAN0_TX, .rx_pin = PIN_CAN0_RX,
        .stb_pin = PIN_CAN0_STB, .bitrate_kbps = BITRATE_KBPS,
    };
    can_bus_config_t c1 = {
        .bus_id = 1, .tx_pin = PIN_CAN1_TX, .rx_pin = PIN_CAN1_RX,
        .stb_pin = PIN_CAN1_STB, .bitrate_kbps = BITRATE_KBPS,
    };
    ESP_ERROR_CHECK(can_bus_install(&c0));
    ESP_ERROR_CHECK(can_bus_install(&c1));

    can_init(&bus0, 0, vehicle_dbc_bin, vehicle_dbc_bin_len, tesla_finalize_tx);
    can_init(&bus1, 1, NULL, 0, NULL);

    state_nvs_init();
    fs_init();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Dorky Commander starting (version %s)", DORKY_FIRMWARE_VERSION);
    vTaskDelay(pdMS_TO_TICKS(2000));

    hw_init();

    bvm *vm = be_vm_new();
    berry_set_buses(&bus0, &bus1);
    berry_register_bindings(vm);

    static script_loader_t loader;
    script_loader_scan(&loader, vm);
    protocol_init(&loader);
    dorky_ble_init(protocol_on_ble_write, NULL);

    uint32_t tick = 0;
    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        int rx_count = 0;
        rx_count += can_poll(&bus0, now);
        rx_count += can_poll(&bus1, now);
        berry_timer_tick(now);

        static uint32_t led_on_until = 0;
        if (rx_count > 0) {
            led_on_until = now + 50;
        }
        gpio_set_level(LED_GPIO, now < led_on_until ? 1 : 0);

        if ((tick % 1000) == 0) {
            ESP_LOGI(TAG, "tick %" PRIu32 " | free heap: %" PRIu32 " bytes",
                     tick, (uint32_t)esp_get_free_heap_size());
            
            static char stats[1024];
            // vTaskGetRunTimeStats(stats);
            // printf("--- CPU usage ---\n%s\n", stats);
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
