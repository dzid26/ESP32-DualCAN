#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

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
#include "tesla_ble/tesla_ble.h"
#include "tesla_ble/tesla_vehicle.h"
#include "wifi/wifi.h"
#include "ota/ota.h"

static const char *TAG = "dorky";

#define BLUE_LED_GPIO        GPIO_NUM_15
#define BOOT_BTN_GPIO        GPIO_NUM_9

#define PIN_CAN0_TX     GPIO_NUM_2
#define PIN_CAN0_RX     GPIO_NUM_1
#define PIN_CAN0_STB    GPIO_NUM_19

#define PIN_CAN1_TX     GPIO_NUM_4
#define PIN_CAN1_RX     GPIO_NUM_3
#define PIN_CAN1_STB    GPIO_NUM_18

#define BITRATE_KBPS    500

static can_t bus0, bus1;

static uint32_t load_bus_bitrate_nvs(int bus_id)
{
    char key[4];
    snprintf(key, sizeof(key), "b%d", bus_id);
    char buf[8];
    if (state_get("busconfig", key, buf, sizeof(buf)) == ESP_OK) {
        uint32_t kbps = (uint32_t)atoi(buf);
        if (kbps == 125 || kbps == 250 || kbps == 500 || kbps == 1000) return kbps;
    }
    return BITRATE_KBPS;
}

static void hw_init(void)
{
    gpio_reset_pin(BLUE_LED_GPIO);
    gpio_set_direction(BLUE_LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BOOT_BTN_GPIO);
    gpio_set_direction(BOOT_BTN_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BTN_GPIO, GPIO_PULLUP_ONLY);

    led_rgb_init();

    state_nvs_init();  /* must come before reading bitrates from NVS */

    can_bus_config_t c0 = {
        .bus_id = 0, .tx_pin = PIN_CAN0_TX, .rx_pin = PIN_CAN0_RX,
        .stb_pin = PIN_CAN0_STB, .bitrate_kbps = load_bus_bitrate_nvs(0),
    };
    can_bus_config_t c1 = {
        .bus_id = 1, .tx_pin = PIN_CAN1_TX, .rx_pin = PIN_CAN1_RX,
        .stb_pin = PIN_CAN1_STB, .bitrate_kbps = load_bus_bitrate_nvs(1),
    };
    ESP_ERROR_CHECK(can_bus_install(&c0));
    ESP_ERROR_CHECK(can_bus_install(&c1));

    can_init(&bus0, 0, vehicle_dbc_bin, vehicle_dbc_bin_len, tesla_finalize_tx);
    can_init(&bus1, 1, NULL, 0, NULL);

    fs_init();
    wifi_init();
    tesla_ble_init();
    tesla_vehicle_init();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Dorky Commander starting (version %s)", DORKY_FIRMWARE_VERSION);
    ota_log_boot_info();
    vTaskDelay(pdMS_TO_TICKS(2000));

    hw_init();

    bvm *vm = be_vm_new();
    berry_set_buses(&bus0, &bus1);
    berry_register_bindings(vm);

    static script_loader_t loader;
    script_loader_scan(&loader, vm);
    /* Defer restore_enabled until we've seen a CAN frame OR the startup window
     * elapses — keeps a faulty script from sending into a dead bus on boot. */
    protocol_init(&loader);
    dorky_ble_init(protocol_on_ble_write, NULL);

    /* Startup-delay guardrail: don't enable user scripts until we've seen the
     * buses come alive, or until this timeout fires. */
    const uint32_t STARTUP_DELAY_MS = 5000;
    bool scripts_restored = false;
    uint32_t boot_ms = (uint32_t)(esp_timer_get_time() / 1000);

    uint32_t tick = 0;
    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        int rx_count = 0;
        protocol_tick();
        rx_count += can_poll(&bus0, now);
        rx_count += can_poll(&bus1, now);
        berry_timer_tick(now);

        if (!scripts_restored) {
            bool elapsed = (now - boot_ms) >= STARTUP_DELAY_MS;
            if (rx_count > 0 || elapsed) {
                ESP_LOGI(TAG, "enabling persisted scripts (%s)",
                         rx_count > 0 ? "bus alive" : "timeout");
                script_loader_restore_enabled(&loader);
                scripts_restored = true;
            }
        }

        /* BOOT button (active-low).
         * Short press (<15 s): unlock pairing window.
         * Hold 15 s: factory reset (wipe NVS + LittleFS, reboot). */
#define FACTORY_RESET_HOLD_MS 15000U
        static bool btn_prev      = true;
        static bool btn_held      = false;
        static uint32_t btn_press_ms = 0;

        bool btn_now = gpio_get_level(BOOT_BTN_GPIO);
        if (btn_prev && !btn_now) {
            /* falling edge */
            btn_held     = true;
            btn_press_ms = now;
        } else if (!btn_prev && btn_now && btn_held) {
            /* rising edge — short press */
            btn_held = false;
            if ((now - btn_press_ms) < FACTORY_RESET_HOLD_MS)
                dorky_ble_unlock_pairing();
        } else if (btn_held && !btn_now && (now - btn_press_ms) >= FACTORY_RESET_HOLD_MS) {
            /* held 15 s — factory reset */
            btn_held = false;
            ESP_LOGW(TAG, "BOOT held 15 s — factory reset");
            gpio_set_level(BLUE_LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            fs_format();
            state_nvs_erase_all();
            esp_restart();
        }
        btn_prev = btn_now;

        /* Blue LED: fast-blink as countdown warning while BOOT held > 3 s;
         * solid when pairing window open; brief blink on CAN activity. */
        bool led_override = btn_held && (now - btn_press_ms) >= 3000;
        if (led_override) {
            uint32_t held = now - btn_press_ms;
            uint32_t period = held >= 10000 ? 100 : 300;
            gpio_set_level(BLUE_LED_GPIO, (now / period) % 2);
        } else {
            static uint32_t led_on_until = 0;
            static uint32_t led_last_blink = 0;
            if (rx_count > 0 && (now - led_last_blink) >= 200) {
                led_on_until = now + 50;
                led_last_blink = now;
            }
            gpio_set_level(BLUE_LED_GPIO,
                           dorky_ble_pairing_open() || now < led_on_until ? 1 : 0);
        }

        if ((tick % 1000) == 0) {
            ESP_LOGD(TAG, "tick %" PRIu32 " | free heap: %" PRIu32 " bytes",
                     tick, (uint32_t)esp_get_free_heap_size());
            
            static char stats[1024];
            // vTaskGetRunTimeStats(stats);
            // printf("--- CPU usage ---\n%s\n", stats);
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
