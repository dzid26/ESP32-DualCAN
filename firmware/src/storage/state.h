#pragma once

#include "esp_err.h"

/* Initialize the NVS flash (call once at boot). */
esp_err_t state_nvs_init(void);

/* Per-script state stored in NVS. Namespace is derived from the script
 * filename to isolate scripts from each other.
 * Values are stored as strings (Berry's native serializable type). */

esp_err_t state_set(const char *ns, const char *key, const char *value);
esp_err_t state_get(const char *ns, const char *key, char *buf, size_t buf_size);
esp_err_t state_remove(const char *ns, const char *key);

/* Erase every NVS entry (bonds, WiFi creds, secrets, Tesla key, settings).
 * The flash is re-initialized so the next state_* call sees an empty store. */
esp_err_t state_nvs_erase_all(void);
