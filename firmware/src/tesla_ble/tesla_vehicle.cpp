/*
 * tesla_vehicle — TeslaBLE::Vehicle + adapters for our NimBLE transport.
 *
 * Two adapters bridge the library's abstract interfaces to our platform:
 *
 *   NvsStorageAdapter  — maps library storage keys to our NVS helpers.
 *     "private_key"              → NVS "tesla"/"pem"  (PEM with NUL)
 *     "session_vcsec"            → NVS "tesla"/"s_vcsec"  (hex-encoded)
 *     "session_infotainment"     → NVS "tesla"/"s_info"   (hex-encoded)
 *
 *   NimbleBleAdapter  — Vehicle::write() → tesla_central_write().
 *     Vehicle never calls connect()/disconnect() so those are no-ops.
 *
 * Connection ownership: WE call tesla_central_connect(); on success we
 * call vehicle->set_connected(true), then enqueue the command.
 * Vehicle::loop() is pumped every 100 ms from a dedicated FreeRTOS task.
 */

extern "C" {
#include "tesla_ble/tesla_vehicle.h"
#include "tesla_ble/tesla_ble.h"
#include "tesla_ble/tesla_central.h"
#include "storage/state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "adapters.h"
#include "vehicle.h"
#include "keys.pb.h"
#include "vcsec.pb.h"

static const char *TAG = "tesla_vehicle";

/* ---- StorageAdapter: NVS ---- */

namespace {

static const char *nvs_key_for(const std::string &k)
{
    if (k == "private_key")           return "pem";
    if (k == "session_vcsec")         return "s_vcsec";
    if (k == "session_infotainment")  return "s_info";
    /* Fallback: truncate to NVS 15-char limit (shouldn't happen). */
    static char buf[16];
    std::strncpy(buf, k.c_str(), 15);
    buf[15] = '\0';
    return buf;
}

static std::string to_hex(const std::vector<uint8_t> &v)
{
    static const char H[] = "0123456789abcdef";
    std::string s;
    s.reserve(v.size() * 2 + 1);
    for (uint8_t b : v) { s += H[b >> 4]; s += H[b & 0xF]; }
    return s;
}

static bool from_hex(const char *hex, std::vector<uint8_t> &out)
{
    size_t n = std::strlen(hex);
    if (n % 2 != 0) return false;
    out.resize(n / 2);
    for (size_t i = 0; i < n; i += 2) {
        auto nib = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out[i / 2] = (nib(hex[i]) << 4) | nib(hex[i + 1]);
    }
    return true;
}

class NvsStorageAdapter : public TeslaBLE::StorageAdapter {
public:
    bool load(const std::string &key, std::vector<uint8_t> &buf) override
    {
        if (key == "private_key") {
            char pem[384];
            if (state_get("tesla", "pem", pem, sizeof pem) != ESP_OK) return false;
            size_t len = std::strlen(pem) + 1;   /* +1: mbedtls needs the NUL */
            buf.assign(pem, pem + len);
            return true;
        }
        char hexval[1025];
        if (state_get("tesla", nvs_key_for(key), hexval, sizeof hexval) != ESP_OK)
            return false;
        return from_hex(hexval, buf);
    }

    bool save(const std::string &key, const std::vector<uint8_t> &buf) override
    {
        if (key == "private_key") {
            /* buf is PEM bytes (with NUL).  Store as string directly. */
            const char *pem = reinterpret_cast<const char *>(buf.data());
            return state_set("tesla", "pem", pem) == ESP_OK;
        }
        auto hexval = to_hex(buf);
        return state_set("tesla", nvs_key_for(key), hexval.c_str()) == ESP_OK;
    }

    bool remove(const std::string &key) override
    {
        state_remove("tesla", nvs_key_for(key));
        return true;
    }
};

/* ---- BleAdapter: wraps tesla_central_write() ---- */

class NimbleBleAdapter : public TeslaBLE::BleAdapter {
public:
    void connect(const std::string &) override { /* driven externally */ }
    void disconnect() override { tesla_central_disconnect(); }

    bool write(const std::vector<uint8_t> &data) override
    {
        return tesla_central_write(data.data(), data.size()) == ESP_OK;
    }
};

/* ---- Singleton Vehicle ---- */

std::shared_ptr<TeslaBLE::Vehicle> s_vehicle;

/* Pending pair callback, set in tesla_vehicle_pair(). */
tesla_vehicle_done_cb_t s_pair_cb  = nullptr;
void                   *s_pair_ctx = nullptr;

/* ---- Vehicle loop task ---- */

static void vehicle_loop_task(void *)
{
    for (;;) {
        if (s_vehicle) s_vehicle->loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ---- Central callbacks (called from NimBLE host task) ---- */

static void on_central_connected(bool success, void *ctx)
{
    if (!s_vehicle) return;
    s_vehicle->set_connected(success);

    if (!success) {
        ESP_LOGE(TAG, "connect to car failed");
        if (s_pair_cb) {
            s_pair_cb(false, "BLE connect failed", s_pair_ctx);
            s_pair_cb  = nullptr;
            s_pair_ctx = nullptr;
        }
        return;
    }

    /* Connected — now set VIN and enqueue the whitelist add-key command. */
    char vin[TESLA_VIN_LEN + 2];
    if (tesla_ble_get_vin(vin, sizeof vin) == ESP_OK)
        s_vehicle->set_vin(vin);

    s_vehicle->send_command_bool(
        UniversalMessage_Domain_DOMAIN_VEHICLE_SECURITY,
        "Whitelist Add Key",
        [](TeslaBLE::Client *c, uint8_t *buf, size_t *len) {
            return c->build_white_list_message(
                Keys_Role_ROLE_OWNER,
                VCSEC_KeyFormFactor_KEY_FORM_FACTOR_NFC_CARD,
                buf, len);
        },
        [](bool ok) {
            tesla_central_disconnect();
            if (s_pair_cb) {
                s_pair_cb(ok,
                          ok ? nullptr : "whitelist message rejected by car",
                          s_pair_ctx);
                s_pair_cb  = nullptr;
                s_pair_ctx = nullptr;
            }
        });

    ESP_LOGI(TAG, "whitelist add-key command queued");
}

static void on_central_disconnected(int reason, void *ctx)
{
    if (s_vehicle) s_vehicle->set_connected(false);
    ESP_LOGI(TAG, "car disconnected reason=%d", reason);
}

static void on_central_rx(const uint8_t *data, size_t len, void *ctx)
{
    if (s_vehicle)
        s_vehicle->on_rx_data(std::vector<uint8_t>(data, data + len));
}

}  /* anonymous namespace */

/* ---- Public C API ---- */

extern "C" esp_err_t tesla_vehicle_init(void)
{
    if (s_vehicle) return ESP_OK;   /* idempotent */

    auto storage = std::make_shared<NvsStorageAdapter>();
    auto ble     = std::make_shared<NimbleBleAdapter>();
    s_vehicle = std::make_shared<TeslaBLE::Vehicle>(ble, storage);

    /* VIN may already be in NVS — set it now so the client is ready. */
    char vin[TESLA_VIN_LEN + 2];
    if (tesla_ble_get_vin(vin, sizeof vin) == ESP_OK)
        s_vehicle->set_vin(vin);

    xTaskCreate(vehicle_loop_task, "tesla_veh", 8192, nullptr,
                tskIDLE_PRIORITY + 1, nullptr);

    ESP_LOGI(TAG, "Vehicle object initialized");
    return ESP_OK;
}

extern "C" esp_err_t tesla_vehicle_pair(const uint8_t addr[6],
                                         uint8_t addr_type,
                                         tesla_vehicle_done_cb_t cb,
                                         void *ctx)
{
    if (!s_vehicle) return ESP_ERR_INVALID_STATE;
    if (tesla_central_is_connected()) return ESP_ERR_INVALID_STATE;
    if (!tesla_ble_has_key()) return ESP_ERR_NOT_FOUND;

    s_pair_cb  = cb;
    s_pair_ctx = ctx;

    return tesla_central_connect(addr, addr_type,
                                  on_central_connected,
                                  on_central_disconnected,
                                  on_central_rx,
                                  nullptr);
}
