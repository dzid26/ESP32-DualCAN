#include "led/led_rgb.h"

#include "led_strip.h"
#include "esp_log.h"

#define RGB_LED_GPIO    8
#define RGB_LED_COUNT   1

static led_strip_handle_t s_strip;

esp_err_t led_rgb_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip);
    if (err == ESP_OK) {
        led_strip_clear(s_strip);
    }
    return err;
}

esp_err_t led_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip);
    }
    return err;
}

esp_err_t led_rgb_off(void)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    return led_strip_clear(s_strip);
}
