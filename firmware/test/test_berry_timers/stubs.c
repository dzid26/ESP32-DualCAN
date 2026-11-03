/*
 * Host clock for the Berry registry test. esp_timer is the only hardware
 * symbol the registry links against.
 */
#include <stdint.h>

#include "esp_timer.h"

static int64_t g_now_us;

void stub_set_now_ms(unsigned ms) { g_now_us = (int64_t)ms * 1000; }

int64_t esp_timer_get_time(void) { return g_now_us; }
