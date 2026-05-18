/*
 * Glue between our C protocol layer and yoziru/tesla-ble (C++).
 *
 * Owns a single TeslaBLE::Client instance whose private key is persisted
 * to NVS as PEM (because that's the format Client::load_private_key
 * accepts — mbedtls_pk_parse_key under the hood).
 */
extern "C" {
#include "tesla_ble/tesla_ble.h"
#include "storage/state.h"
}

#include <cstring>
#include <memory>

#include "esp_log.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"

#include "client.h"   /* TeslaBLE::Client */

namespace {

constexpr const char *TAG          = "tesla_ble";
constexpr const char *NVS_NS       = "tesla";
constexpr const char *NVS_KEY_PEM  = "pem";
/* P-256 EC private key in PEM (BEGIN/END EC PRIVATE KEY) is ~227 bytes
 * including newlines. 384 leaves room for line-ending variation and NUL. */
constexpr size_t PEM_BUF_LEN = 384;

/* Lazy singleton — held via unique_ptr so tesla_ble_reset() can throw away
 * the in-memory key by destroying the client. */
std::unique_ptr<TeslaBLE::Client> s_client;

TeslaBLE::Client &client() {
    if (!s_client) s_client = std::make_unique<TeslaBLE::Client>();
    return *s_client;
}

/* Derive the SEC1-uncompressed (65-byte) public key from a PEM private key.
 * The library doesn't expose Client::public_key publicly; rather than
 * shimming a friend, parse the same PEM with mbedtls directly. */
int derive_pubkey_from_pem(const char *pem, size_t pem_len_with_nul,
                           uint8_t out[TESLA_BLE_PUBKEY_LEN])
{
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_key(&pk,
                                  reinterpret_cast<const uint8_t *>(pem),
                                  pem_len_with_nul,
                                  nullptr, 0,
                                  nullptr, nullptr);
    if (rc != 0) { mbedtls_pk_free(&pk); return -1; }
    if (mbedtls_pk_get_type(&pk) != MBEDTLS_PK_ECKEY) { mbedtls_pk_free(&pk); return -2; }

    mbedtls_ecp_keypair *kp = mbedtls_pk_ec(pk);
    size_t olen = 0;
    rc = mbedtls_ecp_point_write_binary(&kp->MBEDTLS_PRIVATE(grp),
                                        &kp->MBEDTLS_PRIVATE(Q),
                                        MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &olen, out, TESLA_BLE_PUBKEY_LEN);
    mbedtls_pk_free(&pk);
    return (rc == 0 && olen == TESLA_BLE_PUBKEY_LEN) ? 0 : -3;
}

}  /* anonymous namespace */

extern "C" esp_err_t tesla_ble_init(void)
{
    char pem[PEM_BUF_LEN];
    if (state_get(NVS_NS, NVS_KEY_PEM, pem, sizeof pem) != ESP_OK) {
        ESP_LOGI(TAG, "no keypair in NVS (use tesla.gen_key)");
        return ESP_OK;
    }
    /* mbedtls_pk_parse_key for PEM expects the buffer to include the NUL byte. */
    size_t pem_with_nul = std::strlen(pem) + 1;
    int rc = client().load_private_key(reinterpret_cast<const uint8_t *>(pem),
                                       pem_with_nul);
    if (rc != 0) {
        ESP_LOGW(TAG, "stored keypair is corrupted (rc=%d), ignoring", rc);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "loaded existing keypair from NVS");
    return ESP_OK;
}

extern "C" bool tesla_ble_has_key(void)
{
    return s_client && s_client->has_private_key();
}

extern "C" esp_err_t tesla_ble_generate_key(void)
{
    /* Reset the client so create_private_key starts from a clean state. */
    s_client.reset();

    int rc = client().create_private_key();
    if (rc != 0) {
        ESP_LOGE(TAG, "create_private_key failed: rc=%d", rc);
        return ESP_FAIL;
    }

    pb_byte_t pem[PEM_BUF_LEN];
    size_t pem_len = 0;
    rc = client().get_private_key(pem, sizeof pem, &pem_len);
    if (rc != 0 || pem_len == 0 || pem_len >= sizeof pem) {
        ESP_LOGE(TAG, "get_private_key failed: rc=%d len=%u", rc, (unsigned)pem_len);
        return ESP_FAIL;
    }
    pem[pem_len] = '\0';

    if (state_set(NVS_NS, NVS_KEY_PEM,
                  reinterpret_cast<const char *>(pem)) != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist private key");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "generated new P-256 keypair, persisted to NVS (%u bytes PEM)",
             (unsigned)pem_len);
    return ESP_OK;
}

extern "C" esp_err_t tesla_ble_get_public_key(uint8_t out[TESLA_BLE_PUBKEY_LEN])
{
    if (!tesla_ble_has_key()) return ESP_ERR_NOT_FOUND;
    pb_byte_t pem[PEM_BUF_LEN];
    size_t pem_len = 0;
    if (client().get_private_key(pem, sizeof pem, &pem_len) != 0
        || pem_len == 0 || pem_len >= sizeof pem) {
        return ESP_FAIL;
    }
    pem[pem_len] = '\0';
    return derive_pubkey_from_pem(reinterpret_cast<const char *>(pem),
                                  pem_len + 1, out) == 0
        ? ESP_OK : ESP_FAIL;
}

extern "C" esp_err_t tesla_ble_reset(void)
{
    state_remove(NVS_NS, NVS_KEY_PEM);
    s_client.reset();   /* drop in-memory key */
    ESP_LOGI(TAG, "keypair wiped");
    return ESP_OK;
}
