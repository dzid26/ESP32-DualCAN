#include "can/can_bus.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can";

static twai_handle_t s_buses[CAN_BUS_COUNT];

static twai_timing_config_t timing_for(uint32_t kbps)
{
    if (kbps == 1000) { twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();   return t; }
    if (kbps ==  500) { twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS(); return t; }
    if (kbps ==  250) { twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS(); return t; }
    if (kbps ==  125) { twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS(); return t; }
    ESP_LOGW(TAG, "unsupported bitrate %" PRIu32 " kbps, defaulting to 500", kbps);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    return t;
}

esp_err_t can_bus_install(const can_bus_config_t *cfg)
{
    if (!cfg || cfg->bus_id < 0 || cfg->bus_id >= CAN_BUS_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_buses[cfg->bus_id] != NULL) {
        ESP_LOGW(TAG, "bus%d already installed", cfg->bus_id);
        return ESP_ERR_INVALID_STATE;
    }

    // TCAN1044: STB high = standby, STB low = normal operation.
    if (cfg->stb_pin != GPIO_NUM_NC) {
        gpio_reset_pin(cfg->stb_pin);
        gpio_set_direction(cfg->stb_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(cfg->stb_pin, 0);
    }

    twai_general_config_t gcfg = TWAI_GENERAL_CONFIG_DEFAULT_V2(
        cfg->bus_id, cfg->tx_pin, cfg->rx_pin, TWAI_MODE_NORMAL);
    gcfg.tx_queue_len = cfg->tx_queue_len ? cfg->tx_queue_len : 8;
    gcfg.rx_queue_len = cfg->rx_queue_len ? cfg->rx_queue_len : 16;

    twai_timing_config_t tcfg = timing_for(cfg->bitrate_kbps);
    twai_filter_config_t fcfg = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install_v2(&gcfg, &tcfg, &fcfg,
                                           &s_buses[cfg->bus_id]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus%d install failed: %s",
                 cfg->bus_id, esp_err_to_name(err));
        return err;
    }

    err = twai_start_v2(s_buses[cfg->bus_id]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus%d start failed: %s",
                 cfg->bus_id, esp_err_to_name(err));
        twai_driver_uninstall_v2(s_buses[cfg->bus_id]);
        s_buses[cfg->bus_id] = NULL;
        return err;
    }

    ESP_LOGI(TAG, "bus%d up (tx=%d rx=%d stb=%d, %" PRIu32 " kbit/s)",
             cfg->bus_id, cfg->tx_pin, cfg->rx_pin, cfg->stb_pin,
             cfg->bitrate_kbps);
    return ESP_OK;
}

esp_err_t can_bus_send(int bus_id, uint32_t id,
                       const uint8_t *data, uint8_t len,
                       uint32_t timeout_ms)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT || s_buses[bus_id] == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > 8) {
        return ESP_ERR_INVALID_ARG;
    }

    twai_message_t msg = {0};
    msg.identifier = id;
    msg.data_length_code = len;
    if (data && len) {
        memcpy(msg.data, data, len);
    }
    return twai_transmit_v2(s_buses[bus_id], &msg, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t can_bus_receive(int bus_id, twai_message_t *out, uint32_t timeout_ms)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT || s_buses[bus_id] == NULL || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_receive_v2(s_buses[bus_id], out, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t can_bus_status(int bus_id, twai_status_info_t *out)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT || s_buses[bus_id] == NULL || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_get_status_info_v2(s_buses[bus_id], out);
}
