#include "netease_official_client.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

static int official_esp_rng(void *ctx, unsigned char *output, size_t output_len)
{
    (void)ctx;
    if (output == NULL) {
        return -1;
    }
    esp_fill_random(output, output_len);
    return 0;
}
#endif

esp_err_t official_sign_rsa_sha256_base64(const char *private_key_pem,
                                          const char *canonical,
                                          char *signature,
                                          size_t signature_size)
{
    if (private_key_pem == NULL || canonical == NULL || signature == NULL || signature_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    signature[0] = '\0';

#ifndef ESP_PLATFORM
    return ESP_ERR_INVALID_STATE;
#else
    unsigned char hash[32] = {0};
    unsigned char raw_signature[MBEDTLS_PK_SIGNATURE_MAX_SIZE] = {0};
    size_t raw_signature_len = 0;
    size_t base64_len = 0;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int rc = mbedtls_pk_parse_key(&pk,
                                  (const unsigned char *)private_key_pem,
                                  strlen(private_key_pem) + 1,
                                  NULL,
                                  0,
                                  official_esp_rng,
                                  NULL);
    if (rc != 0) {
        mbedtls_pk_free(&pk);
        mbedtls_platform_zeroize(hash, sizeof(hash));
        mbedtls_platform_zeroize(raw_signature, sizeof(raw_signature));
        return ESP_FAIL;
    }

    if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA)) {
        mbedtls_pk_free(&pk);
        mbedtls_platform_zeroize(hash, sizeof(hash));
        mbedtls_platform_zeroize(raw_signature, sizeof(raw_signature));
        return ESP_ERR_INVALID_ARG;
    }

    rc = mbedtls_sha256((const unsigned char *)canonical, strlen(canonical), hash, 0);
    if (rc == 0) {
        rc = mbedtls_pk_sign(&pk,
                             MBEDTLS_MD_SHA256,
                             hash,
                             sizeof(hash),
                             raw_signature,
                             sizeof(raw_signature),
                             &raw_signature_len,
                             official_esp_rng,
                             NULL);
    }
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        mbedtls_platform_zeroize(hash, sizeof(hash));
        mbedtls_platform_zeroize(raw_signature, sizeof(raw_signature));
        return ESP_FAIL;
    }

    rc = mbedtls_base64_encode((unsigned char *)signature,
                               signature_size,
                               &base64_len,
                               raw_signature,
                               raw_signature_len);
    if (rc != 0 || base64_len >= signature_size) {
        signature[0] = '\0';
        mbedtls_platform_zeroize(hash, sizeof(hash));
        mbedtls_platform_zeroize(raw_signature, sizeof(raw_signature));
        return ESP_ERR_INVALID_SIZE;
    }
    signature[base64_len] = '\0';

    mbedtls_platform_zeroize(hash, sizeof(hash));
    mbedtls_platform_zeroize(raw_signature, sizeof(raw_signature));
    return ESP_OK;
#endif
}
