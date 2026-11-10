#pragma once

/* Host stand-in for the subset of esp_err.h the scripting bindings touch. */

typedef int esp_err_t;

#define ESP_OK   0
#define ESP_FAIL (-1)
