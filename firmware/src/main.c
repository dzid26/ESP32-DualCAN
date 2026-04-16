#include <inttypes.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "berry.h"
#include "can/can_bus.h"

static const char *TAG = "dorky";

#define LED_GPIO        GPIO_NUM_15

#define PIN_CAN0_TX     GPIO_NUM_2
#define PIN_CAN0_RX     GPIO_NUM_1
#define PIN_CAN0_STB    GPIO_NUM_19

#define PIN_CAN1_TX     GPIO_NUM_4
#define PIN_CAN1_RX     GPIO_NUM_3
#define PIN_CAN1_STB    GPIO_NUM_18

#define BITRATE_KBPS    500

static void led_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
}

static void buses_init(void)
{
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
}

static void drain_rx(int bus_id)
{
    twai_message_t rx;
    while (can_bus_receive(bus_id, &rx, 0) == ESP_OK) {
        ESP_LOGI(TAG, "bus%d rx id=0x%03" PRIx32 " dlc=%d",
                 bus_id, (uint32_t)rx.identifier, (int)rx.data_length_code);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Dorky Commander starting (version %s)", DORKY_FIRMWARE_VERSION);

    led_init();
    buses_init();

    vTaskDelay(pdMS_TO_TICKS(2000));

    bvm *vm = be_vm_new();
    be_dostring(vm, "print('Berry VM alive on Dorky Commander')");
    be_dostring(vm, "print('1 + 2 = ' .. str(1 + 2))");
    be_vm_delete(vm);

    uint32_t tick = 0;
    bool led_on = false;
    while (1) {
        uint8_t payload[4] = {
            (uint8_t)(tick),       (uint8_t)(tick >> 8),
            (uint8_t)(tick >> 16), (uint8_t)(tick >> 24),
        };
        esp_err_t tx_err = can_bus_send(0, 0x123, payload, sizeof(payload), 50);

        drain_rx(0);
        drain_rx(1);

        led_on = !led_on;
        gpio_set_level(LED_GPIO, led_on);

        ESP_LOGI(TAG, "tick %" PRIu32 " tx=%s",
                 tick++, tx_err == ESP_OK ? "ok" : esp_err_to_name(tx_err));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
