#pragma once

#include "esp_err.h"

/* Mount the LittleFS partition at /littlefs.
 * Creates the mount point and formats on first boot. */
esp_err_t fs_init(void);
