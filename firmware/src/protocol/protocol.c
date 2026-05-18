#include "protocol/protocol.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_timer.h"

#include "ble/ble_transport.h"
#include "protocol/frame_buf.h"
#include "scripting/berry_bindings.h"
#include "can/can.h"
#include "can/can_driver.h"
#include "dbc/dbc_pack.h"
#include "tesla_ble/tesla_ble.h"
#include "tesla_ble/tesla_central.h"
#include "wifi/wifi.h"
#include "ota/ota.h"
#include "storage/state.h"
#include "storage/fs.h"
#include "nvs.h"

static const char *TAG = "proto";

#define RX_BUF_MAX          (16 * 1024)     /* enough for a 10 KB script */
#define FRAME_HDR_LEN       4
#define DISPATCH_QUEUE_LEN  8

typedef struct { uint8_t *data; size_t len; uint8_t type; } frame_item_t;

static script_loader_t *s_loader;
static uint8_t          s_rx_buf[RX_BUF_MAX];
static frame_buf_t      s_rx_fb;
static QueueHandle_t    s_frame_queue;

/* ---- trace state ---- */
/* Bit 0 = bus 0 enabled, bit 1 = bus 1 enabled. 0 = trace off. */
static uint8_t          s_trace_buses;

/* ---- bus status monitor ---- */
/* Order must mirror bus_status_t in can_driver.h */
static const char *const BUS_STATUS_STR[] = { "idle", "good", "tx_error", "rx_error", "error" };
static uint8_t  s_bus_status[CAN_BUS_COUNT];   /* initialised to 0xFF in protocol_init */
static bool     s_ble_was_connected;
static int      s_connect_pushes;   /* retry counter — push N times after reconnect */

/* ---- signal subscriptions ----
 * Each entry uses can_on_change with a unique tag = SIG_SUB_TAG_BASE | slot.
 * Tags above 0x10000000 never collide with script_ids (allocated 1..N from
 * script_loader), so berry_cleanup_script never touches these. */
#define SIG_SUB_MAX        16
#define SIG_SUB_NAME_MAX   48
#define SIG_SUB_TAG_BASE   0x10000000

typedef struct {
    bool in_use;
    int  bus;
    int  sig_idx;
    char name[SIG_SUB_NAME_MAX];
} sig_sub_t;
static sig_sub_t s_sig_subs[SIG_SUB_MAX];

static int sig_sub_slot_from_ctx(void *ctx) { return (int)(intptr_t)ctx; }

/* ---- helpers ---- */

static int find_script_idx(const char *filename)
{
    for (int i = 0; i < s_loader->count; i++) {
        if (strcmp(s_loader->scripts[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

static void send_frame(cJSON *resp)
{
    char *json = cJSON_PrintUnformatted(resp);
    if (!json) return;

    size_t jlen = strlen(json);
    uint8_t *frame = malloc(FRAME_HDR_LEN + jlen);
    if (!frame) { free(json); return; }

    frame[0] = (uint8_t)(jlen & 0xFF);
    frame[1] = (uint8_t)((jlen >> 8) & 0xFF);
    frame[2] = (uint8_t)((jlen >> 16) & 0xFF);
    frame[3] = (uint8_t)((jlen >> 24) & 0xFF);
    memcpy(frame + FRAME_HDR_LEN, json, jlen);

    /* BLE notifications are limited to MTU-3 (~509 B). Chunk larger frames.
     * Bail out mid-chunk if the link drops — without this, a disconnect during
     * a big frame floods nimble_host with failed-notify calls and overflows
     * its stack. */
    const size_t CHUNK = 200; /* conservative; negotiated MTU may be smaller */
    size_t total = FRAME_HDR_LEN + jlen;
    size_t off = 0;
    while (off < total) {
        if (!dorky_ble_connected()) break;
        size_t n = total - off;
        if (n > CHUNK) n = CHUNK;
        if (dorky_ble_notify(frame + off, n) != 0) break;
        off += n;
    }

    free(frame);
    free(json);
}

static void send_ok(int id, cJSON *result /* takes ownership, may be NULL */)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON_AddBoolToObject(resp, "ok", true);
    if (result) {
        cJSON_AddItemToObject(resp, "result", result);
    }
    send_frame(resp);
    cJSON_Delete(resp);
}

static void send_err(int id, const char *msg)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "id", id);
    cJSON_AddBoolToObject(resp, "ok", false);
    cJSON_AddStringToObject(resp, "error", msg);
    send_frame(resp);
    cJSON_Delete(resp);
}

/* ---- unsolicited push ---- */

static void log_push(const char *level, const char *src, const char *msg)
{
    cJSON *push = cJSON_CreateObject();
    cJSON_AddStringToObject(push, "type", "log");
    cJSON_AddStringToObject(push, "level", level);
    cJSON_AddStringToObject(push, "src", src);
    cJSON_AddStringToObject(push, "msg", msg);
    send_frame(push);
    cJSON_Delete(push);
}

/* Called by Berry print() via berry_set_log_handler. */
static void berry_log_handler(const char *msg)
{
    log_push("info", "berry", msg);
}

/* ---- vprintf hook: mirrors ESP_LOG output to the webui log panel ---- */

static vprintf_like_t s_orig_vprintf;
static bool           s_hook_active;  /* recursion guard: NimBLE logs from within notify path */

/* Strip ANSI escape sequences (\033[...m) and trailing newline from src. */
static size_t strip_ansi(const char *src, char *dst, size_t dst_sz)
{
    size_t i = 0, j = 0;
    while (src[i] && j + 1 < dst_sz) {
        if (src[i] == '\033') {
            i++;                            /* skip '[' */
            while (src[i] && src[i] != 'm') i++;
            if (src[i]) i++;                /* skip 'm' */
        } else {
            dst[j++] = src[i++];
        }
    }
    while (j > 0 && (dst[j - 1] == '\n' || dst[j - 1] == '\r')) j--;
    dst[j] = '\0';
    return j;
}

static void log_push_raw(const char *raw)
{
    cJSON *push = cJSON_CreateObject();
    cJSON_AddStringToObject(push, "type", "log");
    cJSON_AddStringToObject(push, "raw", raw);
    send_frame(push);
    cJSON_Delete(push);
}

static int log_vprintf_hook(const char *fmt, va_list args)
{
    va_list args2;
    va_copy(args2, args);
    int ret = s_orig_vprintf(fmt, args);

    if (!s_hook_active && dorky_ble_connected()) {
        s_hook_active = true;
        char buf[320];
        char raw[320];
        vsnprintf(buf, sizeof(buf), fmt, args2);
        if (strip_ansi(buf, raw, sizeof(raw)) > 0)
            log_push_raw(raw);
        s_hook_active = false;
    }
    va_end(args2);
    return ret;
}

static const char HEX[] = "0123456789ABCDEF";

static void trace_observer(int bus_id, const twai_message_t *frame, uint32_t now_ms)
{
    if (!s_trace_buses) return;
    if (bus_id < 0 || bus_id > 7) return;
    if (!(s_trace_buses & (1u << bus_id))) return;
    if (!dorky_ble_connected()) return;

    /* Hex-encode the data inline (max 16 chars + NUL). cJSON will copy it. */
    char hex[17];
    int dlc = frame->data_length_code;
    if (dlc > 8) dlc = 8;
    for (int i = 0; i < dlc; i++) {
        hex[2*i]     = HEX[(frame->data[i] >> 4) & 0xF];
        hex[2*i + 1] = HEX[frame->data[i] & 0xF];
    }
    hex[2*dlc] = '\0';

    cJSON *push = cJSON_CreateObject();
    cJSON_AddStringToObject(push, "type", "trace");
    cJSON_AddNumberToObject(push, "bus", bus_id);
    cJSON_AddNumberToObject(push, "id",  (double)frame->identifier);
    cJSON_AddNumberToObject(push, "ts",  (double)now_ms);
    cJSON_AddStringToObject(push, "data", hex);
    send_frame(push);
    cJSON_Delete(push);
}

static void push_bus_status(int bus, bus_status_t status)
{
    cJSON *push = cJSON_CreateObject();
    cJSON_AddStringToObject(push, "type", "bus_status");
    cJSON_AddNumberToObject(push, "bus", bus);
    cJSON_AddStringToObject(push, "status", BUS_STATUS_STR[status]);
    send_frame(push);
    cJSON_Delete(push);
}

static void signal_sub_cb(int sig_idx, float value, float prev, void *ctx)
{
    (void)sig_idx;
    if (!dorky_ble_connected()) return;
    int slot = sig_sub_slot_from_ctx(ctx);
    if (slot < 0 || slot >= SIG_SUB_MAX) return;
    sig_sub_t *s = &s_sig_subs[slot];
    if (!s->in_use) return;

    cJSON *push = cJSON_CreateObject();
    cJSON_AddStringToObject(push, "type", "signal");
    cJSON_AddStringToObject(push, "name",  s->name);
    cJSON_AddNumberToObject(push, "bus",   s->bus);
    cJSON_AddNumberToObject(push, "value", (double)value);
    cJSON_AddNumberToObject(push, "prev",  (double)prev);
    send_frame(push);
    cJSON_Delete(push);
}

/* ---- command handlers ---- */

static void handle_ping(int id)
{
    send_ok(id, cJSON_CreateString("pong"));
}

void reboot_task(void *arg)
{
    (void)arg;
    /* Give BLE stack time to flush the final notify. */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void handle_system_info(int id)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "fw_version", DORKY_FIRMWARE_VERSION);
    /* Bumped when the JSON op set changes incompatibly. UI uses this to gate
     * features that the firmware doesn't recognise yet. */
    cJSON_AddNumberToObject(result, "proto_version", 6);
    send_ok(id, result);
}

static void factory_reset_task(void *arg)
{
    (void)arg;
    /* Let the ok response notify drain before we yank the storage out. */
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGW(TAG, "Factory reset: wiping NVS + LittleFS");
    fs_format();
    state_nvs_erase_all();
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

static void handle_system_factory_reset(int id)
{
    if (ota_in_progress()) {
        ota_abort();
    }
    send_ok(id, NULL);
    xTaskCreate(factory_reset_task, "factory_rst", 4096, NULL, 5, NULL);
}

static void handle_system_reboot(int id)
{
    /* A mid-flash reboot would leave a half-written app partition; the bootloader
     * may then refuse to boot or fall back to the previous slot in an unclear
     * state. Abort the OTA session cleanly first. */
    if (ota_in_progress()) {
        ota_abort();
    }
    send_ok(id, NULL);
    /* Defer the restart so the BLE notify has time to flush. */
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
}

static void handle_script_list(int id)
{
    script_loader_scan(s_loader, s_loader->vm);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_loader->count; i++) {
        const script_entry_t *s = &s_loader->scripts[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "filename", s->filename);
        cJSON_AddBoolToObject(o, "enabled", s->enabled);
        cJSON_AddBoolToObject(o, "errored", s->errored);
        if (s->errored) {
            cJSON_AddStringToObject(o, "error", s->error);
        }
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "scripts", arr);
    send_ok(id, result);
}

static void handle_script_write(int id, cJSON *req)
{
    const cJSON *fn = cJSON_GetObjectItem(req, "filename");
    const cJSON *code = cJSON_GetObjectItem(req, "code");
    if (!cJSON_IsString(fn) || !cJSON_IsString(code)) {
        send_err(id, "missing filename or code");
        return;
    }
    if (script_loader_write(fn->valuestring, code->valuestring,
                            strlen(code->valuestring)) != 0) {
        send_err(id, "write failed");
        return;
    }
    /* Disable existing instance before rescan so a re-upload forces re-enable. */
    int idx = find_script_idx(fn->valuestring);
    if (idx >= 0) {
        script_loader_disable(s_loader, idx);
    }
    int n = script_loader_scan(s_loader, s_loader->vm);
    ESP_LOGI(TAG, "script.write %s: %d scripts total", fn->valuestring, n);
    send_ok(id, NULL);
}

static void handle_script_read(int id, cJSON *req)
{
    const cJSON *fn = cJSON_GetObjectItem(req, "filename");
    if (!cJSON_IsString(fn)) { send_err(id, "missing filename"); return; }

    static char buf[16 * 1024];
    int n = script_loader_read(fn->valuestring, buf, sizeof(buf));
    if (n < 0) { send_err(id, "read failed"); return; }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "code", buf);
    send_ok(id, result);
}

static void handle_script_enable(int id, cJSON *req)
{
    const cJSON *fn = cJSON_GetObjectItem(req, "filename");
    if (!cJSON_IsString(fn)) { send_err(id, "missing filename"); return; }

    int idx = find_script_idx(fn->valuestring);
    if (idx < 0) { send_err(id, "no such script"); return; }

    if (script_loader_enable(s_loader, idx) != 0) {
        send_err(id, s_loader->scripts[idx].error);
        return;
    }
    script_loader_save_enabled(s_loader);
    send_ok(id, NULL);
}

static void handle_script_disable(int id, cJSON *req)
{
    const cJSON *fn = cJSON_GetObjectItem(req, "filename");
    if (!cJSON_IsString(fn)) { send_err(id, "missing filename"); return; }

    int idx = find_script_idx(fn->valuestring);
    if (idx < 0) { send_err(id, "no such script"); return; }

    script_loader_disable(s_loader, idx);
    script_loader_save_enabled(s_loader);
    send_ok(id, NULL);
}

static void handle_script_delete(int id, cJSON *req)
{
    const cJSON *fn = cJSON_GetObjectItem(req, "filename");
    if (!cJSON_IsString(fn)) { send_err(id, "missing filename"); return; }

    if (script_loader_delete(s_loader, fn->valuestring) != 0) {
        send_err(id, "delete failed");
        return;
    }
    send_ok(id, NULL);
}

static void handle_action_list(int id)
{
    const char *names[64];
    int n = berry_actions_snapshot(names, 64);
    if (n > 64) n = 64;

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "actions", arr);
    send_ok(id, result);
}

static void handle_action_invoke(int id, cJSON *req)
{
    const cJSON *name = cJSON_GetObjectItem(req, "name");
    if (!cJSON_IsString(name)) { send_err(id, "missing name"); return; }

    int rc = berry_action_invoke(name->valuestring);
    if (rc == -1)      send_err(id, "no such action");
    else if (rc == -2) send_err(id, "action raised");
    else               send_ok(id, NULL);
}

static void handle_trace_start(int id, cJSON *req)
{
    /* Optional: { "buses": [0, 1] } — defaults to all buses. */
    uint8_t mask = 0;
    const cJSON *buses = cJSON_GetObjectItem(req, "buses");
    if (cJSON_IsArray(buses)) {
        cJSON *b;
        cJSON_ArrayForEach(b, buses) {
            if (cJSON_IsNumber(b)) {
                int idx = b->valueint;
                if (idx >= 0 && idx <= 7) mask |= (1u << idx);
            }
        }
    }
    if (mask == 0) mask = 0x03;   /* default: bus0+bus1 */
    s_trace_buses = mask;
    ESP_LOGI(TAG, "trace.start mask=0x%02x", mask);
    send_ok(id, NULL);
}

static void handle_trace_stop(int id)
{
    s_trace_buses = 0;
    ESP_LOGI(TAG, "trace.stop");
    send_ok(id, NULL);
}

static void handle_ble_status(int id)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "pairing_open", dorky_ble_pairing_open());
    cJSON_AddNumberToObject(result, "bond_count", dorky_ble_bond_count());
    send_ok(id, result);
}

static void handle_ble_unlock_pairing(int id)
{
    dorky_ble_unlock_pairing();
    send_ok(id, NULL);
}

static void handle_ble_reset_pairs(int id)
{
    /* Reply BEFORE wiping — the wipe terminates this very connection. */
    send_ok(id, NULL);
    /* Tiny delay to let the notify drain before the disconnect. */
    vTaskDelay(pdMS_TO_TICKS(50));
    dorky_ble_reset_pairings();
}

/* ---- Tesla BLE ops ----
 * Phase 1: keypair lifecycle only. The actual VCSEC pairing handshake is
 * not yet implemented; see docs/tesla-ble.md for the staged plan. */

static cJSON *tesla_ble_status_obj(void)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "has_key", tesla_ble_has_key());
    if (tesla_ble_has_key()) {
        uint8_t pub[TESLA_BLE_PUBKEY_LEN];
        if (tesla_ble_get_public_key(pub) == ESP_OK) {
            char hex[TESLA_BLE_PUBKEY_LEN * 2 + 1];
            static const char H[] = "0123456789abcdef";
            for (int i = 0; i < TESLA_BLE_PUBKEY_LEN; i++) {
                hex[i * 2]     = H[pub[i] >> 4];
                hex[i * 2 + 1] = H[pub[i] & 0xF];
            }
            hex[sizeof(hex) - 1] = '\0';
            cJSON_AddStringToObject(r, "public_key_hex", hex);
        }
    }
    return r;
}

static void handle_tesla_status(int id)
{
    send_ok(id, tesla_ble_status_obj());
}

static void handle_tesla_gen_key(int id)
{
    if (tesla_ble_generate_key() != ESP_OK) {
        send_err(id, "gen_key failed");
        return;
    }
    send_ok(id, tesla_ble_status_obj());
}

static void handle_tesla_reset(int id)
{
    tesla_ble_reset();
    send_ok(id, NULL);
}

typedef struct { int id; } tesla_scan_ctx_t;

static void tesla_scan_done_cb(const tesla_scan_result_t *results,
                               size_t count, void *ctx)
{
    tesla_scan_ctx_t *sc = (tesla_scan_ctx_t *)ctx;
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        const tesla_scan_result_t *r = &results[i];
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 r->addr[5], r->addr[4], r->addr[3],
                 r->addr[2], r->addr[1], r->addr[0]);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "addr", addr_str);
        cJSON_AddStringToObject(o, "name", r->name);
        cJSON_AddNumberToObject(o, "rssi", r->rssi);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "devices", arr);
    send_ok(sc->id, result);
    free(sc);
}

static void handle_tesla_scan(int id, cJSON *req)
{
    cJSON *dur_item = cJSON_GetObjectItem(req, "duration_ms");
    uint32_t duration_ms = cJSON_IsNumber(dur_item)
                           ? (uint32_t)dur_item->valueint : 5000;
    if (duration_ms < 500)   duration_ms = 500;
    if (duration_ms > 30000) duration_ms = 30000;

    tesla_scan_ctx_t *sc = malloc(sizeof(*sc));
    if (!sc) { send_err(id, "oom"); return; }
    sc->id = id;

    esp_err_t err = tesla_central_scan_start(duration_ms,
                                              tesla_scan_done_cb, sc);
    if (err == ESP_ERR_INVALID_STATE) {
        free(sc);
        send_err(id, "scan already in progress");
    } else if (err != ESP_OK) {
        free(sc);
        send_err(id, "scan failed to start — BLE host not ready?");
    }
    /* On ESP_OK the callback owns sc and will reply + free it. */
}

static void handle_wifi_status(int id)
{
    char ssid[33], ip[16];
    wifi_get_ssid(ssid, sizeof(ssid));
    wifi_get_ip(ip, sizeof(ip));

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject  (result, "connected", wifi_connected());
    cJSON_AddStringToObject(result, "ssid",      ssid);
    cJSON_AddStringToObject(result, "ip",        ip);
    send_ok(id, result);
}

static void handle_wifi_set_creds(int id, cJSON *req)
{
    const cJSON *ssid_j = cJSON_GetObjectItem(req, "ssid");
    const cJSON *psk_j  = cJSON_GetObjectItem(req, "psk");
    if (!cJSON_IsString(ssid_j)) { send_err(id, "missing ssid"); return; }
    const char *psk = cJSON_IsString(psk_j) ? psk_j->valuestring : "";

    if (wifi_set_creds(ssid_j->valuestring, psk) != 0) {
        send_err(id, "set failed");
        return;
    }
    send_ok(id, NULL);
}

#define SECRETS_NS    "secrets"
#define BUSCONFIG_NS  "busconfig"

static void handle_settings_set_secret(int id, cJSON *req)
{
    const cJSON *name_j  = cJSON_GetObjectItem(req, "name");
    const cJSON *value_j = cJSON_GetObjectItem(req, "value");
    if (!cJSON_IsString(name_j)  || name_j->valuestring[0]  == '\0' ||
        !cJSON_IsString(value_j) || value_j->valuestring[0] == '\0') {
        send_err(id, "missing name or value");
        return;
    }
    if (strlen(name_j->valuestring) > 15) {
        send_err(id, "name too long (max 15)");
        return;
    }
    if (state_set(SECRETS_NS, name_j->valuestring, value_j->valuestring) != ESP_OK) {
        send_err(id, "write failed");
        return;
    }
    send_ok(id, NULL);
}

static void handle_settings_has_secret(int id, cJSON *req)
{
    const cJSON *name_j = cJSON_GetObjectItem(req, "name");
    if (!cJSON_IsString(name_j) || name_j->valuestring[0] == '\0') {
        send_err(id, "missing name");
        return;
    }
    char buf[4];
    esp_err_t err = state_get(SECRETS_NS, name_j->valuestring, buf, sizeof(buf));
    bool has = (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "set", has);
    send_ok(id, result);
}

static void handle_settings_get_secret(int id, cJSON *req)
{
    const cJSON *name_j = cJSON_GetObjectItem(req, "name");
    if (!cJSON_IsString(name_j) || name_j->valuestring[0] == '\0') {
        send_err(id, "missing name");
        return;
    }
    char buf[256];
    cJSON *result = cJSON_CreateObject();
    esp_err_t err = state_get(SECRETS_NS, name_j->valuestring, buf, sizeof(buf));
    if (err == ESP_OK) {
        cJSON_AddStringToObject(result, "value", buf);
    } else {
        cJSON_AddNullToObject(result, "value");
    }
    send_ok(id, result);
}

static void handle_settings_clear_secret(int id, cJSON *req)
{
    const cJSON *name_j = cJSON_GetObjectItem(req, "name");
    if (!cJSON_IsString(name_j) || name_j->valuestring[0] == '\0') {
        send_err(id, "missing name");
        return;
    }
    esp_err_t err = state_remove(SECRETS_NS, name_j->valuestring);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        send_err(id, "clear failed");
        return;
    }
    send_ok(id, NULL);
}

static void handle_bus_get_config(int id)
{
    cJSON *arr = cJSON_CreateArray();
    for (int b = 0; b < CAN_BUS_COUNT; b++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "bus", b);
        cJSON_AddNumberToObject(o, "bitrate_kbps", (double)can_bus_get_bitrate(b));
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "buses", arr);
    send_ok(id, result);
}

static void handle_bus_set_bitrate(int id, cJSON *req)
{
    const cJSON *bus_j  = cJSON_GetObjectItem(req, "bus");
    const cJSON *kbps_j = cJSON_GetObjectItem(req, "bitrate_kbps");
    if (!cJSON_IsNumber(bus_j) || !cJSON_IsNumber(kbps_j)) {
        send_err(id, "missing bus or bitrate_kbps"); return;
    }
    int bus = bus_j->valueint;
    uint32_t kbps = (uint32_t)kbps_j->valuedouble;

    /* Persist first — if the hot reconfigure fails, the saved value is still valid
     * and the user can reboot to apply it. */
    char key[4], val[8];
    snprintf(key, sizeof(key), "b%d", bus);
    snprintf(val, sizeof(val), "%" PRIu32, kbps);
    if (state_set(BUSCONFIG_NS, key, val) != ESP_OK) {
        send_err(id, "NVS write failed"); return;
    }

    esp_err_t err = can_bus_set_bitrate(bus, kbps);
    if (err != ESP_OK) {
        send_err(id, esp_err_to_name(err)); return;
    }
    send_ok(id, NULL);
}

static void handle_log_set_level(int id, cJSON *req)
{
    const cJSON *level_j = cJSON_GetObjectItem(req, "level");
    if (!cJSON_IsString(level_j)) { send_err(id, "missing level"); return; }
    const char *lvl = level_j->valuestring;

    esp_log_level_t level;
    if      (strcmp(lvl, "none")    == 0) level = ESP_LOG_NONE;
    else if (strcmp(lvl, "error")   == 0) level = ESP_LOG_ERROR;
    else if (strcmp(lvl, "warn")    == 0) level = ESP_LOG_WARN;
    else if (strcmp(lvl, "info")    == 0) level = ESP_LOG_INFO;
    else if (strcmp(lvl, "debug")   == 0) level = ESP_LOG_DEBUG;
    else if (strcmp(lvl, "verbose") == 0) level = ESP_LOG_VERBOSE;
    else { send_err(id, "invalid level"); return; }

    /* Optional tag filter; "*" applies to every tag. Levels above
     * CONFIG_LOG_MAXIMUM_LEVEL have nothing to show (compile-time strip). */
    const cJSON *tag_j = cJSON_GetObjectItem(req, "tag");
    const char *tag = (cJSON_IsString(tag_j) && tag_j->valuestring[0]) ? tag_j->valuestring : "*";
    esp_log_level_set(tag, level);
    /* esp_log_level_set("*", …) clears all per-tag overrides in ESP-IDF v5.
     * Re-apply suppressions for components that log from within their own
     * send/notify path; without this they flood the hook and overflow the stack. */
    if (strcmp(tag, "*") == 0)
        esp_log_level_set("NimBLE", ESP_LOG_WARN);
    send_ok(id, NULL);
}

static void handle_sim_set(int id, cJSON *req)
{
    const cJSON *enabled_j = cJSON_GetObjectItem(req, "enabled");
    if (!cJSON_IsBool(enabled_j)) { send_err(id, "missing enabled"); return; }
    bool enabled = cJSON_IsTrue(enabled_j);

    /* Optional bus filter; default to both buses. */
    int bus_filter = -1;
    const cJSON *bus_j = cJSON_GetObjectItem(req, "bus");
    if (cJSON_IsNumber(bus_j)) bus_filter = bus_j->valueint;

    for (int b = 0; b < 2; b++) {
        if (bus_filter >= 0 && bus_filter != b) continue;
        can_t *eng = berry_get_bus(b);
        if (eng) can_set_sim_mode(eng, enabled);
    }
    send_ok(id, NULL);
}

static void handle_signal_subscribe(int id, cJSON *req)
{
    const cJSON *name_j = cJSON_GetObjectItem(req, "name");
    if (!cJSON_IsString(name_j)) { send_err(id, "missing name"); return; }
    int bus_idx = 0;
    const cJSON *bus_j = cJSON_GetObjectItem(req, "bus");
    if (cJSON_IsNumber(bus_j)) bus_idx = bus_j->valueint;

    can_t *eng = berry_get_bus(bus_idx);
    if (!eng) { send_err(id, "invalid bus"); return; }
    if (!eng->loaded) { send_err(id, "no DBC loaded for bus"); return; }

    int sig_idx = dbc_find_signal(&eng->dbc, name_j->valuestring);
    if (sig_idx < 0) { send_err(id, "no such signal"); return; }

    /* Idempotent re-subscribe: existing slot for the same (bus, sig)
     * stays as-is so the can_on_change registration doesn't double up. */
    int slot = -1;
    for (int i = 0; i < SIG_SUB_MAX; i++) {
        if (s_sig_subs[i].in_use &&
            s_sig_subs[i].bus == bus_idx &&
            s_sig_subs[i].sig_idx == sig_idx) {
            send_ok(id, NULL);
            return;
        }
        if (slot < 0 && !s_sig_subs[i].in_use) slot = i;
    }
    if (slot < 0) { send_err(id, "subscription table full"); return; }

    s_sig_subs[slot].in_use  = true;
    s_sig_subs[slot].bus     = bus_idx;
    s_sig_subs[slot].sig_idx = sig_idx;
    strncpy(s_sig_subs[slot].name, name_j->valuestring, SIG_SUB_NAME_MAX - 1);
    s_sig_subs[slot].name[SIG_SUB_NAME_MAX - 1] = '\0';

    int tag = SIG_SUB_TAG_BASE | slot;
    if (can_on_change(eng, name_j->valuestring, signal_sub_cb,
                      (void *)(intptr_t)slot, tag) != 0) {
        s_sig_subs[slot].in_use = false;
        send_err(id, "can_on_change failed");
        return;
    }
    send_ok(id, NULL);
}

static void handle_signal_unsubscribe(int id, cJSON *req)
{
    /* No name → clear all. With name → match (bus, name). */
    const cJSON *name_j = cJSON_GetObjectItem(req, "name");
    int bus_idx = 0;
    const cJSON *bus_j = cJSON_GetObjectItem(req, "bus");
    if (cJSON_IsNumber(bus_j)) bus_idx = bus_j->valueint;
    bool clear_all = !cJSON_IsString(name_j);

    for (int i = 0; i < SIG_SUB_MAX; i++) {
        if (!s_sig_subs[i].in_use) continue;
        bool drop = clear_all ||
            (s_sig_subs[i].bus == bus_idx &&
             strcmp(s_sig_subs[i].name, name_j->valuestring) == 0);
        if (!drop) continue;
        can_t *eng = berry_get_bus(s_sig_subs[i].bus);
        if (eng) can_off_by_tag(eng, SIG_SUB_TAG_BASE | i);
        s_sig_subs[i].in_use = false;
    }
    send_ok(id, NULL);
}

/* ---- OTA handlers ---- */

static void handle_ota_begin(int id)
{
    char err[128];
    size_t max_size = 0;
    if (ota_begin(&max_size, err, sizeof(err)) != 0) {
        send_err(id, err);
        return;
    }
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "max_size", (double)max_size);
    send_ok(id, result);
}

static void handle_ota_end(int id, cJSON *req)
{
    bool reboot = true;
    const cJSON *reboot_j = cJSON_GetObjectItem(req, "reboot");
    if (cJSON_IsBool(reboot_j)) reboot = cJSON_IsTrue(reboot_j);

    /* Optional client-claimed total. If present, ota_end fails the call
     * when the actual write count doesn't match — catches silently lost
     * chunks before we flip the boot partition. */
    size_t expected_len = 0;
    const cJSON *size_j = cJSON_GetObjectItem(req, "size");
    if (cJSON_IsNumber(size_j) && size_j->valuedouble >= 0) {
        expected_len = (size_t)size_j->valuedouble;
    }

    /* Validate + set boot partition, but DON'T reboot inside ota_end —
     * we need to send the ok response first so the client sees success
     * instead of a 15 s timeout. */
    char err[128];
    if (ota_end(false, expected_len, err, sizeof(err)) != 0) {
        send_err(id, err);
        return;
    }
    send_ok(id, NULL);

    if (reboot) {
        /* Deferred reboot: let the BLE notify flush before restart. */
        xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    }
}

static void handle_ota_abort(int id)
{
    ota_abort();
    send_ok(id, NULL);
}

static void handle_signal_value(int id, cJSON *req)
{
    const cJSON *name_j = cJSON_GetObjectItem(req, "name");
    if (!cJSON_IsString(name_j)) { send_err(id, "missing name"); return; }

    int bus_idx = 0;
    const cJSON *bus_j = cJSON_GetObjectItem(req, "bus");
    if (cJSON_IsNumber(bus_j)) bus_idx = bus_j->valueint;

    can_t *eng = berry_get_bus(bus_idx);
    if (!eng) { send_err(id, "invalid bus"); return; }

    const signal_state_t *s = can_signal(eng, name_j->valuestring);
    if (!s) { send_ok(id, NULL); return; }   /* null result = not found */

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "value",   s->value);
    cJSON_AddNumberToObject(result, "prev",    s->prev);
    cJSON_AddBoolToObject  (result, "changed", s->changed);
    send_ok(id, result);
}

/* ---- frame parser / dispatch ---- */

static void dispatch_frame(const uint8_t *payload, size_t len)
{
    cJSON *req = cJSON_ParseWithLength((const char *)payload, len);
    if (!req) {
        ESP_LOGW(TAG, "invalid JSON frame (%zu bytes)", len);
        return;
    }

    const cJSON *op = cJSON_GetObjectItem(req, "op");
    const cJSON *id_node = cJSON_GetObjectItem(req, "id");
    int id = cJSON_IsNumber(id_node) ? id_node->valueint : 0;

    if (!cJSON_IsString(op)) {
        send_err(id, "missing op");
        cJSON_Delete(req);
        return;
    }

    const char *op_s = op->valuestring;
    ESP_LOGI(TAG, "op=%s id=%d", op_s, id);

    if (strcmp(op_s, "ping") == 0)                handle_ping(id);
    else if (strcmp(op_s, "system.info") == 0)    handle_system_info(id);
    else if (strcmp(op_s, "system.reboot") == 0)  handle_system_reboot(id);
    else if (strcmp(op_s, "system.factory_reset") == 0) handle_system_factory_reset(id);
    else if (strcmp(op_s, "script.list") == 0)    handle_script_list(id);
    else if (strcmp(op_s, "script.write") == 0)   handle_script_write(id, req);
    else if (strcmp(op_s, "script.read") == 0)    handle_script_read(id, req);
    else if (strcmp(op_s, "script.enable") == 0)  handle_script_enable(id, req);
    else if (strcmp(op_s, "script.disable") == 0) handle_script_disable(id, req);
    else if (strcmp(op_s, "script.delete") == 0)  handle_script_delete(id, req);
    else if (strcmp(op_s, "action.list") == 0)    handle_action_list(id);
    else if (strcmp(op_s, "action.invoke") == 0)  handle_action_invoke(id, req);
    else if (strcmp(op_s, "signal.value") == 0)        handle_signal_value(id, req);
    else if (strcmp(op_s, "signal.subscribe") == 0)    handle_signal_subscribe(id, req);
    else if (strcmp(op_s, "signal.unsubscribe") == 0)  handle_signal_unsubscribe(id, req);
    else if (strcmp(op_s, "trace.start") == 0)    handle_trace_start(id, req);
    else if (strcmp(op_s, "trace.stop") == 0)     handle_trace_stop(id);
    else if (strcmp(op_s, "bus.get_config") == 0)  handle_bus_get_config(id);
    else if (strcmp(op_s, "bus.set_bitrate") == 0) handle_bus_set_bitrate(id, req);
    else if (strcmp(op_s, "sim.set") == 0)         handle_sim_set(id, req);
    else if (strcmp(op_s, "log.set_level") == 0)   handle_log_set_level(id, req);
    else if (strcmp(op_s, "tesla.status") == 0)        handle_tesla_status(id);
    else if (strcmp(op_s, "tesla.gen_key") == 0)       handle_tesla_gen_key(id);
    else if (strcmp(op_s, "tesla.reset") == 0)         handle_tesla_reset(id);
    else if (strcmp(op_s, "tesla.scan") == 0)          handle_tesla_scan(id, req);
    else if (strcmp(op_s, "ble.status") == 0)          handle_ble_status(id);
    else if (strcmp(op_s, "ble.unlock_pairing") == 0)  handle_ble_unlock_pairing(id);
    else if (strcmp(op_s, "ble.reset_pairs") == 0)     handle_ble_reset_pairs(id);
    else if (strcmp(op_s, "wifi.status") == 0)    handle_wifi_status(id);
    else if (strcmp(op_s, "wifi.set_creds") == 0) handle_wifi_set_creds(id, req);
    else if (strcmp(op_s, "settings.set_secret") == 0)   handle_settings_set_secret(id, req);
    else if (strcmp(op_s, "settings.get_secret") == 0)   handle_settings_get_secret(id, req);
    else if (strcmp(op_s, "settings.has_secret") == 0)   handle_settings_has_secret(id, req);
    else if (strcmp(op_s, "settings.clear_secret") == 0) handle_settings_clear_secret(id, req);
    else if (strcmp(op_s, "ota.begin") == 0)      handle_ota_begin(id);
    else if (strcmp(op_s, "ota.end") == 0)        handle_ota_end(id, req);
    else if (strcmp(op_s, "ota.abort") == 0)      handle_ota_abort(id);
    else send_err(id, "unknown op");

    cJSON_Delete(req);
}

void protocol_on_ble_write(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;

    if (frame_buf_append(&s_rx_fb, data, len) != 0) {
        ESP_LOGE(TAG, "rx buffer overflow, resetting");
        return;
    }

    /* Drain every complete frame and hand it to the main task — keeps Berry
     * VM access on one task (NimBLE only frames, never dispatches). */
    for (;;) {
        const uint8_t *payload;
        size_t plen;
        int rc = frame_buf_next(&s_rx_fb, &payload, &plen);
        if (rc == 0) break;
        if (rc < 0) {
            ESP_LOGE(TAG, "frame length exceeds buffer, resetting");
            return;
        }

        uint8_t *copy = malloc(plen);
        if (copy) {
            memcpy(copy, payload, plen);
            frame_item_t fi = { copy, plen, frame_buf_last_type(&s_rx_fb) };
            if (xQueueSend(s_frame_queue, &fi, 0) != pdTRUE) {
                ESP_LOGW(TAG, "dispatch queue full, frame dropped");
                free(copy);
            }
        }
        frame_buf_consume(&s_rx_fb);
    }
}

/* Binary OTA write frame layout (after the 4-byte length header whose top
 * nibble = FRAME_TYPE_OTA_WRITE):
 *   bytes [0..4)  : u32 LE request id
 *   bytes [4..N)  : raw firmware bytes
 * Response is a normal JSON {ok,id} or {error,id} so the client uses the
 * same pending-promise machinery as JSON requests. */
static void dispatch_ota_write_binary(const uint8_t *payload, size_t len)
{
    if (len < 4) {
        ESP_LOGW(TAG, "binary ota.write frame too short (%zu)", len);
        return;
    }
    int id = (int)((uint32_t)payload[0]
                 | ((uint32_t)payload[1] << 8)
                 | ((uint32_t)payload[2] << 16)
                 | ((uint32_t)payload[3] << 24));

    char err[128];
    if (ota_write(payload + 4, len - 4, err, sizeof(err)) != 0) {
        send_err(id, err);
        return;
    }
    send_ok(id, NULL);
}

void protocol_tick(void)
{
    frame_item_t fi;
    while (xQueueReceive(s_frame_queue, &fi, 0) == pdTRUE) {
        switch (fi.type) {
            case FRAME_TYPE_OTA_WRITE:
                dispatch_ota_write_binary(fi.data, fi.len);
                break;
            case FRAME_TYPE_JSON:
            default:
                dispatch_frame(fi.data, fi.len);
                break;
        }
        free(fi.data);
    }

    /* Bus status monitor: evaluate every 500 ms, push on change.
     * On BLE reconnect force re-push by resetting s_bus_status. */
    static uint32_t s_status_ms = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    bool ble_up = dorky_ble_connected();
    if (ble_up && !s_ble_was_connected) {
        memset(s_bus_status, 0xFF, sizeof(s_bus_status));
        s_status_ms = now;   /* delay first push 200 ms — ATT channel needs time to stabilize */
        s_connect_pushes = 5; /* retry: push status 5× after connect in case early notifs are dropped */
    }
    s_ble_was_connected = ble_up;

    if (ble_up && (now - s_status_ms) >= 200) {
        s_status_ms = now;
        for (int b = 0; b < CAN_BUS_COUNT; b++) {
            bus_status_t ns = can_bus_health(b, now, can_last_rx_ms(b));
            bool changed = (uint8_t)ns != s_bus_status[b];
            if (changed || s_connect_pushes > 0) {
                s_bus_status[b] = (uint8_t)ns;
                push_bus_status(b, ns);
                if (changed) {
                    ESP_LOGI(TAG, "bus%d status -> %s", b, BUS_STATUS_STR[ns]);
                }
            }
        }
        if (s_connect_pushes > 0) s_connect_pushes--;
    }
}

void protocol_init(script_loader_t *loader)
{
    s_loader = loader;
    frame_buf_init(&s_rx_fb, s_rx_buf, sizeof(s_rx_buf));
    s_frame_queue = xQueueCreate(DISPATCH_QUEUE_LEN, sizeof(frame_item_t));
    berry_set_log_handler(berry_log_handler);
    can_set_raw_observer(trace_observer);
    memset(s_bus_status, 0xFF, sizeof(s_bus_status));  /* force push on first check */
    s_orig_vprintf = esp_log_set_vprintf(log_vprintf_hook);
}
