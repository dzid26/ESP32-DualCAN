#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "berry.h"
#include "can/can_bus.h"
#include "can/can_engine.h"
#include "led/led_rgb.h"
#include "scripting/berry_bindings.h"
#include "ble/ble_transport.h"
#include "dbc/vehicle_dbc_embedded.h"
#include "storage/fs.h"
#include "storage/state.h"
#include "scripting/script_loader.h"

static const char *TAG = "dorky";

#define LED_GPIO        GPIO_NUM_15

#define PIN_CAN0_TX     GPIO_NUM_2
#define PIN_CAN0_RX     GPIO_NUM_1
#define PIN_CAN0_STB    GPIO_NUM_19

#define PIN_CAN1_TX     GPIO_NUM_4
#define PIN_CAN1_RX     GPIO_NUM_3
#define PIN_CAN1_STB    GPIO_NUM_18

#define BITRATE_KBPS    500

static can_engine_t engine0, engine1;

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

    can_engine_init(&engine0, 0, vehicle_dbc_bin, vehicle_dbc_bin_len);
    can_engine_init(&engine1, 1, NULL, 0);

    state_nvs_init();
    fs_init();
    dorky_ble_init(NULL, NULL);
}

static const char *DEMO_SCRIPT =
    "var tick = 0\n"
    "def loop()\n"
    "  # TX on CAN0\n"
    "  var b = bytes()\n"
    "  b.add(tick & 0xFF)\n"
    "  b.add((tick >> 8) & 0xFF)\n"
    "  b.add((tick >> 16) & 0xFF)\n"
    "  b.add((tick >> 24) & 0xFF)\n"
    "  can_send(0, 0x123, b)\n"
    "  tick += 1\n"
    "\n"
    "  # Check CAN1 for loopback\n"
    "  var rx = can_receive(1)\n"
    "  if rx != nil\n"
    "    led_set(0, 32, 0)\n"
    "  else\n"
    "    led_set(32, 0, 0)\n"
    "  end\n"
    "\n"
    "  # Bridge: forward CAN0 rx to CAN1\n"
    "  rx = can_receive(0)\n"
    "  while rx != nil\n"
    "    can_send(1, rx[0], rx[1])\n"
    "    rx = can_receive(0)\n"
    "  end\n"
    "end\n";

void app_main(void)
{
    ESP_LOGI(TAG, "Dorky Commander starting (version %s)", DORKY_FIRMWARE_VERSION);
    vTaskDelay(pdMS_TO_TICKS(2000));

    hw_init();

    bvm *vm = be_vm_new();
    berry_set_engines(&engine0, &engine1);
    berry_register_bindings(vm);

    if (be_loadstring(vm, DEMO_SCRIPT) != 0 || be_pcall(vm, 0) != 0) {
        ESP_LOGE(TAG, "Berry load error: %s", be_tostring(vm, -1));
        be_pop(vm, 1);
    }
    be_pop(vm, 1);

    ESP_LOGI(TAG, "Berry demo loaded, entering main loop");

    uint32_t tick = 0;
    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        can_engine_poll(&engine0, now);
        can_engine_poll(&engine1, now);

        if (be_getglobal(vm, "loop")) {
            if (be_pcall(vm, 0) != 0) {
                ESP_LOGE(TAG, "Berry error: %s", be_tostring(vm, -1));
                be_pop(vm, 1);
            }
            be_pop(vm, 1);
        }

        gpio_set_level(LED_GPIO, tick & 1);
        if ((tick % 100) == 0) {
            ESP_LOGI(TAG, "tick %" PRIu32 " | free heap: %" PRIu32 " bytes",
                     tick, (uint32_t)esp_get_free_heap_size());
#if configGENERATE_RUN_TIME_STATS
            static char stats[512];
            vTaskGetRunTimeStats(stats);
            printf("--- CPU usage ---\n%s\n", stats);
#endif
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
