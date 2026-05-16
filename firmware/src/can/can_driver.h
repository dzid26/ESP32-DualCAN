#pragma once

#include <stdbool.h>
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

uint32_t  can_bus_get_bitrate(int bus_id);
esp_err_t can_bus_set_bitrate(int bus_id, uint32_t kbps);

typedef enum {
    BUS_IDLE    = 0,  /* no electrical activity */
    BUS_GOOD    = 1,  /* TWAI frames received or TX ACKed */
    BUS_TX_ERR  = 2,  /* TX errors present — we're the bus source but nobody ACKs */
    BUS_RX_ERR  = 3,  /* GPIO edges seen but no valid TWAI frames (wrong bitrate, noise, …) */
    BUS_ERROR   = 4,  /* TWAI bus-off */
} bus_status_t;

/* If bus is in bus-off state, initiate the 128-recessive-bit recovery sequence.
 * Call from can_poll() every tick so recovery is not gated on BLE connectivity. */
void can_bus_off_recover(int bus_id);

/* Compute current bus health. Drains the RX-edge flag internally.
 * `last_rx_ms` — timestamp of the last successfully received TWAI frame (0 = never).
 * `now_ms`     — current time in milliseconds. */
bus_status_t can_bus_health(int bus_id, uint32_t now_ms, uint32_t last_rx_ms);
