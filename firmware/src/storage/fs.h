#pragma once

#include "esp_err.h"

/* Mount the LittleFS partition at /littlefs.
 * Creates the mount point and formats on first boot. */
esp_err_t fs_init(void);

/* Wipe every file on the LittleFS partition. Safe to call while mounted —
 * the underlying call unmounts, formats, and the next fs_init() (or natural
 * remount) will see an empty filesystem. */
esp_err_t fs_format(void);
