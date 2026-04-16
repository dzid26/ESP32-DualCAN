#pragma once

#include "esp_err.h"

esp_err_t led_rgb_init(void);
esp_err_t led_rgb_set(uint8_t r, uint8_t g, uint8_t b);
esp_err_t led_rgb_off(void);
