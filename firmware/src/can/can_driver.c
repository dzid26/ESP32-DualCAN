#include "can/can_driver.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can";

static twai_handle_t s_buses[CAN_BUS_COUNT];

/* RX edge monitor — detects bus activity regardless of TWAI decode success.
 * ISR fires on the first negedge (dominant bit) then disables itself to avoid
 * flooding. Re-armed by can_bus_health() each call interval. */
typedef struct { int bus_id; gpio_num_t pin; } rx_ctx_t;
static rx_ctx_t      s_rx_ctx[CAN_BUS_COUNT];
static volatile bool s_rx_edge[CAN_BUS_COUNT];
static uint32_t      s_last_edge_ms[CAN_BUS_COUNT];
static uint32_t      s_last_tx_ms[CAN_BUS_COUNT];

static void IRAM_ATTR rx_edge_isr(void *arg)
{
    rx_ctx_t *ctx = (rx_ctx_t *)arg;
    s_rx_edge[ctx->bus_id] = true;
    gpio_intr_disable(ctx->pin);  /* re-armed by can_bus_health() */
}

/* Re-apply negedge interrupt type after any operation that resets GPIO config
 * (twai_driver_install_v2 routes the pin through the GPIO matrix which clears it).
 * Also clears stale timestamps so a bitrate change starts with a clean slate. */
static void rx_monitor_rearm(int bus_id)
{
    s_rx_edge[bus_id]     = false;
    s_last_edge_ms[bus_id] = 0;
    s_last_tx_ms[bus_id]   = 0;
    gpio_set_intr_type(s_rx_ctx[bus_id].pin, GPIO_INTR_NEGEDGE);
}

bus_status_t can_bus_health(int bus_id, uint32_t now_ms, uint32_t last_rx_ms)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT) return BUS_IDLE;

    /* Drain edge flag and record timestamp while re-arming the interrupt. */
    if (s_rx_edge[bus_id]) {
        s_rx_edge[bus_id] = false;
        s_last_edge_ms[bus_id] = now_ms;
        gpio_intr_enable(s_rx_ctx[bus_id].pin);
    }

    twai_status_info_t st;
    bool have_st = (can_bus_status(bus_id, &st) == ESP_OK);
    if (have_st && st.state == TWAI_STATE_BUS_OFF)
        return BUS_ERROR;
    /* A successful TX (ACKed by the bus) is as good as a received frame. */
    uint32_t last_good = last_rx_ms > s_last_tx_ms[bus_id] ? last_rx_ms : s_last_tx_ms[bus_id];
    if (last_good && (now_ms - last_good) < 1000)
        return BUS_GOOD;
    if (s_last_edge_ms[bus_id] && (now_ms - s_last_edge_ms[bus_id]) < 1000) {
        /* TX errors with no RX means we're the source of the bus activity — report
         * as a hard error rather than rx_error (which implies a bitrate/format mismatch). */
        if (have_st && st.tx_error_counter > 0)
            return BUS_TX_ERR;
        return BUS_RX_ERR;
    }
    return BUS_IDLE;
}

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

    /* RX edge monitor context — handler registered after TWAI install so
     * twai_driver_install_v2's GPIO matrix setup doesn't clear our intr type. */
    s_rx_ctx[cfg->bus_id] = (rx_ctx_t){ .bus_id = cfg->bus_id, .pin = cfg->rx_pin };
    gpio_install_isr_service(0);  /* idempotent — ESP_ERR_INVALID_STATE if already done */

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
    esp_err_t err = twai_transmit_v2(s_buses[bus_id], &msg, pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_OK) {
        s_last_tx_ms[bus_id] = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGD(TAG, "bus%d tx 0x%03" PRIx32 " [%d]", bus_id, id, len);
    }
    return err;
}

esp_err_t can_bus_receive(int bus_id, twai_message_t *out, uint32_t timeout_ms)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT || s_buses[bus_id] == NULL || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = twai_receive_v2(s_buses[bus_id], out, pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "bus%d rx 0x%03" PRIx32 " [%d]", bus_id, out->identifier, out->data_length_code);
    }
    return err;
}

esp_err_t can_bus_status(int bus_id, twai_status_info_t *out)
{
    if (bus_id < 0 || bus_id >= CAN_BUS_COUNT || s_buses[bus_id] == NULL || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_get_status_info_v2(s_buses[bus_id], out);
}
