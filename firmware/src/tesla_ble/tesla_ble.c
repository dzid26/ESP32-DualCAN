#include "tesla_ble/tesla_ble.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "nvs.h"

#include "storage/state.h"

static const char *TAG = "tesla_ble";

#define NVS_NS       "tesla"
#define NVS_KEY_PRIV "priv"   /* 64-char hex string of 32 raw bytes */

/* Loaded keypair (valid iff s_has_key). Private scalar is sensitive;
 * keep it only here, never returned through any API. */
static bool    s_has_key;
static uint8_t s_priv[32];
static uint8_t s_pub[TESLA_BLE_PUBKEY_LEN];

/* ---- Hex helpers ---- */

static void hex_encode(const uint8_t *in, size_t in_len, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2]     = H[in[i] >> 4];
        out[i * 2 + 1] = H[in[i] & 0xF];
    }
    out[in_len * 2] = '\0';
}

static int hex_decode(const char *in, uint8_t *out, size_t out_len)
{
    if (strlen(in) != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; i++) {
        char h = in[i * 2], l = in[i * 2 + 1];
        int hv = (h <= '9') ? h - '0' : (h | 0x20) - 'a' + 10;
        int lv = (l <= '9') ? l - '0' : (l | 0x20) - 'a' + 10;
        if (hv < 0 || hv > 15 || lv < 0 || lv > 15) return -1;
        out[i] = (uint8_t)((hv << 4) | lv);
    }
    return 0;
}

/* ---- Crypto ---- */

/* Derive the SEC1 uncompressed public key from a raw private scalar.
 * Writes 65 bytes (0x04 || X || Y) into pub_out. */
static int derive_pubkey(const uint8_t priv[32], uint8_t pub_out[TESLA_BLE_PUBKEY_LEN])
{
    int rc = -1;
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) goto out;
    if (mbedtls_mpi_read_binary(&d, priv, 32) != 0) goto out;
    if (mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, NULL, NULL) != 0) goto out;

    size_t olen = 0;
    if (mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &olen, pub_out, TESLA_BLE_PUBKEY_LEN) != 0) {
        goto out;
    }
    if (olen != TESLA_BLE_PUBKEY_LEN) goto out;
    rc = 0;

out:
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return rc;
}

/* ---- NVS load/save ---- */

static esp_err_t load_from_nvs(void)
{
    char hex[65];   /* 32 bytes * 2 + null */
    esp_err_t err = state_get(NVS_NS, NVS_KEY_PRIV, hex, sizeof(hex));
    if (err != ESP_OK) return err;

    uint8_t priv[32];
    if (hex_decode(hex, priv, 32) != 0) {
        ESP_LOGW(TAG, "corrupted private key in NVS, ignoring");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t pub[TESLA_BLE_PUBKEY_LEN];
    if (derive_pubkey(priv, pub) != 0) {
        ESP_LOGE(TAG, "failed to derive pubkey from stored priv");
        return ESP_FAIL;
    }

    memcpy(s_priv, priv, sizeof(s_priv));
    memcpy(s_pub,  pub,  sizeof(s_pub));
    s_has_key = true;
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t tesla_ble_init(void)
{
    if (s_has_key) return ESP_OK;   /* already initialized */
    esp_err_t err = load_from_nvs();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "loaded existing keypair from NVS");
    } else {
        ESP_LOGI(TAG, "no keypair in NVS (use tesla.gen_key to create one)");
    }
    return ESP_OK;
}

bool tesla_ble_has_key(void)
{
    return s_has_key;
}

esp_err_t tesla_ble_generate_key(void)
{
    int rc = -1;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ecp_keypair kp;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ecp_keypair_init(&kp);

    const char *pers = "dorky-tesla-ble";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0) {
        ESP_LOGE(TAG, "ctr_drbg_seed failed");
        goto out;
    }
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &kp,
                            mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
        ESP_LOGE(TAG, "ecp_gen_key failed");
        goto out;
    }

    uint8_t priv[32];
    if (mbedtls_mpi_write_binary(&kp.MBEDTLS_PRIVATE(d), priv, sizeof(priv)) != 0) {
        ESP_LOGE(TAG, "mpi_write_binary failed");
        goto out;
    }

    uint8_t pub[TESLA_BLE_PUBKEY_LEN];
    size_t olen = 0;
    if (mbedtls_ecp_point_write_binary(&kp.MBEDTLS_PRIVATE(grp),
                                       &kp.MBEDTLS_PRIVATE(Q),
                                       MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &olen, pub, sizeof(pub)) != 0
        || olen != sizeof(pub)) {
        ESP_LOGE(TAG, "ecp_point_write_binary failed");
        goto out;
    }

    char priv_hex[65];
    hex_encode(priv, sizeof(priv), priv_hex);
    if (state_set(NVS_NS, NVS_KEY_PRIV, priv_hex) != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist private key");
        goto out;
    }

    memcpy(s_priv, priv, sizeof(s_priv));
    memcpy(s_pub,  pub,  sizeof(s_pub));
    s_has_key = true;
    ESP_LOGI(TAG, "generated new P-256 keypair, persisted to NVS");
    rc = 0;

out:
    mbedtls_ecp_keypair_free(&kp);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t tesla_ble_get_public_key(uint8_t out[TESLA_BLE_PUBKEY_LEN])
{
    if (!s_has_key) return ESP_ERR_NOT_FOUND;
    memcpy(out, s_pub, TESLA_BLE_PUBKEY_LEN);
    return ESP_OK;
}

esp_err_t tesla_ble_reset(void)
{
    /* Zero the in-memory copy first so a fault between this and the NVS
     * remove can't leave a dangling key in RAM. */
    memset(s_priv, 0, sizeof(s_priv));
    memset(s_pub,  0, sizeof(s_pub));
    s_has_key = false;
    state_remove(NVS_NS, NVS_KEY_PRIV);
    ESP_LOGI(TAG, "keypair wiped");
    return ESP_OK;
}
