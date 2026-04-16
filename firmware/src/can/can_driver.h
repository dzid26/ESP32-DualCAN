#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_err.h"

#define CAN_BUS_COUNT 2

typedef struct {
    int        bus_id;          // 0 or 1
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    gpio_num_t stb_pin;         // transceiver STB (active-low enable); GPIO_NUM_NC to skip
    uint32_t   bitrate_kbps;    // 125, 250, 500, 1000
    uint32_t   tx_queue_len;    // 0 -> default 8
    uint32_t   rx_queue_len;    // 0 -> default 16
} can_bus_config_t;

esp_err_t can_bus_install(const can_bus_config_t *cfg);

esp_err_t can_bus_send(int bus_id, uint32_t id,
                       const uint8_t *data, uint8_t len,
                       uint32_t timeout_ms);

esp_err_t can_bus_receive(int bus_id, twai_message_t *out,
                          uint32_t timeout_ms);

esp_err_t can_bus_status(int bus_id, twai_status_info_t *out);
