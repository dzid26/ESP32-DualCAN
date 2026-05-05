#pragma once

#include <stdbool.h>
#include <stddef.h>

/* WiFi station bringup. Reads SSID/PSK from NVS namespace "wifi". If no
 * credentials are stored the driver is initialized but stays disconnected
 * — call wifi_set_creds() to provide them. Safe to call once at boot.
 *
 * Connect/disconnect events run automatically; this module reconnects on
 * its own with a small backoff. */
void wifi_init(void);

bool wifi_connected(void);

/* Copies the current STA IPv4 address into buf as "x.x.x.x", or "" if not
 * connected. buf must be at least 16 bytes. */
void wifi_get_ip(char *buf, size_t buf_len);

/* Persists the credentials to NVS and (re)starts the connection. Returns 0
 * on success, -1 if either string is empty or the NVS write fails. */
int  wifi_set_creds(const char *ssid, const char *psk);

/* Returns the currently-stored SSID into buf (or "" if none). buf must be
 * at least 33 bytes (32-char SSID + NUL). The PSK is intentionally not
 * exposed — once set, it can only be replaced. */
void wifi_get_ssid(char *buf, size_t buf_len);
