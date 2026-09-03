#include "wifi.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* TEMP test: WiFi compiled out to measure flash savings. All functions are
 * stubbed — the ESP-IDF WiFi component is disabled in sdkconfig.defaults
 * (CONFIG_ESP_WIFI_ENABLED is not set), so nothing links against esp_wifi.
 * Call sites (main.c, protocol.c, zmq_can.c) still compile against these
 * signatures. Restore wifi.c's real implementation to re-enable WiFi. */

void wifi_init(void) {}

bool wifi_connected(void) { return false; }

void wifi_get_ip(char *buf, size_t buf_len) { if (buf && buf_len) buf[0] = '\0'; }

void wifi_get_ssid(char *buf, size_t buf_len) { if (buf && buf_len) buf[0] = '\0'; }

int wifi_set_creds(const char *ssid, const char *psk) { (void)ssid; (void)psk; return 0; }