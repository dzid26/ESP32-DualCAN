#pragma once

/* Minimal host stand-in for the ESP-IDF TWAI driver header. The Berry host
 * suites only need the frame shape the CAN cache stores, not the driver. */

#include <stdint.h>

typedef struct {
    uint32_t identifier;
    uint8_t data_length_code;
    uint8_t data[8];
} twai_message_t;
