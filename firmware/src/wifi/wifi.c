#include "wifi/wifi.h"
#include "storage/state.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

#define WIFI_NS                "wifi"
#define WIFI_KEY_SSID          "ssid"
#define WIFI_KEY_PSK           "psk"
#define WIFI_RETRY_BACKOFF_MS  5000

static esp_netif_t   *s_sta_netif;
static EventGroupHandle_t s_event_group;
#define BIT_CONNECTED (1 << 0)

static char s_current_ip[16];

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_current_ip[0] = '\0';
        xEventGroupClearBits(s_event_group, BIT_CONNECTED);
        ESP_LOGW(TAG, "disconnected, retrying in %d ms", WIFI_RETRY_BACKOFF_MS);
        vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_BACKOFF_MS));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_current_ip, sizeof(s_current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip %s", s_current_ip);
        xEventGroupSetBits(s_event_group, BIT_CONNECTED);
    }
}

static int load_creds(char *ssid, size_t ssid_len, char *psk, size_t psk_len)
{
    if (state_get(WIFI_NS, WIFI_KEY_SSID, ssid, ssid_len) != ESP_OK) return -1;
    if (state_get(WIFI_NS, WIFI_KEY_PSK,  psk,  psk_len)  != ESP_OK) {
        psk[0] = '\0';   /* allow open networks */
    }
    if (ssid[0] == '\0') return -1;
    return 0;
}

static int apply_creds(const char *ssid, const char *psk)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid)     - 1);
    strncpy((char *)cfg.sta.password, psk,  sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;   /* let the AP dictate */
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) return -1;
    return 0;
}

void wifi_init(void)
{
    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    char ssid[33] = {0}, psk[65] = {0};
    if (load_creds(ssid, sizeof(ssid), psk, sizeof(psk)) == 0) {
        ESP_LOGI(TAG, "connecting to %s", ssid);
        if (apply_creds(ssid, psk) == 0) {
            ESP_ERROR_CHECK(esp_wifi_start());
        } else {
            ESP_LOGE(TAG, "apply_creds failed for %s", ssid);
        }
    } else {
        /* No creds yet — start the driver so the API is alive but don't
         * try to connect. wifi_set_creds() will (re)start the connection. */
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "no NVS credentials; STA idle");
    }
}

bool wifi_connected(void)
{
    return s_event_group &&
           (xEventGroupGetBits(s_event_group) & BIT_CONNECTED) != 0;
}

void wifi_get_ip(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return;
    strncpy(buf, s_current_ip, buf_len - 1);
    buf[buf_len - 1] = '\0';
}

void wifi_get_ssid(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return;
    if (state_get(WIFI_NS, WIFI_KEY_SSID, buf, buf_len) != ESP_OK) {
        buf[0] = '\0';
    }
}

int wifi_set_creds(const char *ssid, const char *psk)
{
    if (!ssid || ssid[0] == '\0') return -1;
    if (!psk) psk = "";

    if (state_set(WIFI_NS, WIFI_KEY_SSID, ssid) != ESP_OK) return -1;
    if (state_set(WIFI_NS, WIFI_KEY_PSK,  psk)  != ESP_OK) return -1;

    /* Bring the link down, push new config, bring it back up. */
    esp_wifi_disconnect();
    if (apply_creds(ssid, psk) != 0) return -1;
    esp_wifi_connect();
    ESP_LOGI(TAG, "credentials updated; reconnecting to %s", ssid);
    return 0;
}
