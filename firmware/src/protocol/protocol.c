#include "protocol/protocol.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble/ble_transport.h"
#include "protocol/frame_buf.h"
#include "scripting/berry_bindings.h"
#include "can/can.h"

static const char *TAG = "proto";

#define RX_BUF_MAX          (16 * 1024)     /* enough for a 10 KB script */
#define FRAME_HDR_LEN       4
#define DISPATCH_QUEUE_LEN  8

typedef struct { uint8_t *data; size_t len; } frame_item_t;

static script_loader_t *s_loader;
static uint8_t          s_rx_buf[RX_BUF_MAX];
static frame_buf_t      s_rx_fb;
static QueueHandle_t    s_frame_queue;

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

    /* BLE notifications are limited to MTU-3 (~509 B). Chunk larger frames. */
    const size_t CHUNK = 200; /* conservative; negotiated MTU may be smaller */
    size_t total = FRAME_HDR_LEN + jlen;
    size_t off = 0;
    while (off < total) {
        size_t n = total - off;
        if (n > CHUNK) n = CHUNK;
        dorky_ble_notify(frame + off, n);
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

static void log_push_handler(const char *msg)
{
    cJSON *push = cJSON_CreateObject();
    cJSON_AddStringToObject(push, "type", "log");
    cJSON_AddStringToObject(push, "msg", msg);
    send_frame(push);
    cJSON_Delete(push);
}

/* ---- command handlers ---- */

static void handle_ping(int id)
{
    send_ok(id, cJSON_CreateString("pong"));
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
    else if (strcmp(op_s, "script.list") == 0)    handle_script_list(id);
    else if (strcmp(op_s, "script.write") == 0)   handle_script_write(id, req);
    else if (strcmp(op_s, "script.read") == 0)    handle_script_read(id, req);
    else if (strcmp(op_s, "script.enable") == 0)  handle_script_enable(id, req);
    else if (strcmp(op_s, "script.disable") == 0) handle_script_disable(id, req);
    else if (strcmp(op_s, "script.delete") == 0)  handle_script_delete(id, req);
    else if (strcmp(op_s, "action.list") == 0)    handle_action_list(id);
    else if (strcmp(op_s, "action.invoke") == 0)  handle_action_invoke(id, req);
    else if (strcmp(op_s, "signal.value") == 0)   handle_signal_value(id, req);
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
            frame_item_t fi = { copy, plen };
            if (xQueueSend(s_frame_queue, &fi, 0) != pdTRUE) {
                ESP_LOGW(TAG, "dispatch queue full, frame dropped");
                free(copy);
            }
        }
        frame_buf_consume(&s_rx_fb);
    }
}

void protocol_tick(void)
{
    frame_item_t fi;
    while (xQueueReceive(s_frame_queue, &fi, 0) == pdTRUE) {
        dispatch_frame(fi.data, fi.len);
        free(fi.data);
    }
}

void protocol_init(script_loader_t *loader)
{
    s_loader = loader;
    frame_buf_init(&s_rx_fb, s_rx_buf, sizeof(s_rx_buf));
    s_frame_queue = xQueueCreate(DISPATCH_QUEUE_LEN, sizeof(frame_item_t));
    berry_set_log_handler(log_push_handler);
}
