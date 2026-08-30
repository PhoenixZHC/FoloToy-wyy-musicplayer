#include <assert.h>
#include <string.h>

#include "netease_official_client.h"

static void test_canonical_excludes_sign_empty_and_sorts_ascii(void)
{
    char result[256] = {0};
    static const official_param_t params[] = {
        {"timestamp", "1724918400000"},
        {"sign", "ignored"},
        {"appId", "app-test"},
        {"bizContent", "{\"type\":2}"},
        {"accessToken", ""},
        {"signType", "RSA_SHA256"},
    };

    assert(official_build_canonical(params, 6, result, sizeof(result)) == ESP_OK);
    assert(strcmp(result,
                  "appId=app-test&bizContent={\"type\":2}&signType=RSA_SHA256&timestamp=1724918400000") == 0);
}

static void test_url_encode_uses_uppercase_rfc3986_bytes(void)
{
    char result[64] = {0};
    assert(official_url_encode("abc XYZ-_.~:/?&=歌", result, sizeof(result)) == ESP_OK);
    assert(strcmp(result, "abc%20XYZ-_.~%3A%2F%3F%26%3D%E6%AD%8C") == 0);
}

static void test_url_encode_rejects_small_buffer(void)
{
    char result[4] = {0};
    assert(official_url_encode("abcd", result, sizeof(result)) == ESP_ERR_INVALID_SIZE);
}

static void test_device_json_contains_official_values(void)
{
    char result[256] = {0};
    assert(official_build_device_json("esp32c3-test", "1.0.0", result, sizeof(result)) == ESP_OK);
    assert(strcmp(result,
                  "{\"netStatus\":\"4g\",\"channel\":\"iotapitest\",\"deviceId\":\"esp32c3-test\",\"deviceType\":\"openapi\",\"appVer\":\"1.0.0\",\"os\":\"openapi\",\"osVer\":\"14\",\"brand\":\"iotapitest\",\"model\":\"GDI-W09\",\"clientIp\":\"127.0.0.1\"}") == 0);
}

static void test_parse_envelope_success_keeps_data(void)
{
    char json[] = "{\"code\":200,\"data\":{\"token\":\"ok\"},\"message\":\"\"}";
    official_api_result_t result = {0};
    assert(official_parse_envelope(json, 200, &result) == ESP_OK);
    assert(result.http_status == 200);
    assert(result.code == 200);
    assert(result.root != 0);
    assert(result.data != 0);
    official_api_result_clear(&result);
    assert(result.root == 0);
    assert(result.data == 0);
}

static void test_parse_envelope_business_error_has_no_success_data(void)
{
    char json[] = "{\"code\":401,\"subCode\":\"TOKEN_EXPIRED\",\"message\":\"expired\",\"data\":{\"ignored\":true}}";
    official_api_result_t result = {0};
    assert(official_parse_envelope(json, 200, &result) == ESP_FAIL);
    assert(result.code == 401);
    assert(strcmp(result.sub_code, "TOKEN_EXPIRED") == 0);
    assert(strcmp(result.message, "expired") == 0);
    assert(result.data == 0);
    official_api_result_clear(&result);
}

static void test_parse_envelope_rejects_malformed_missing_or_bad_data(void)
{
    official_api_result_t result = {0};
    char missing_code[] = "{\"data\":{}}";
    assert(official_parse_envelope(missing_code, 200, &result) == ESP_ERR_INVALID_RESPONSE);
    assert(result.data == 0);
    official_api_result_clear(&result);

    char bad_data[] = "{\"code\":200,\"data\":[]}";
    assert(official_parse_envelope(bad_data, 200, &result) == ESP_ERR_INVALID_RESPONSE);
    assert(result.data == 0);
    official_api_result_clear(&result);

    char malformed[] = "{\"code\":200";
    assert(official_parse_envelope(malformed, 200, &result) == ESP_ERR_INVALID_RESPONSE);
    assert(result.data == 0);
    official_api_result_clear(&result);
}

static void test_parse_envelope_rejects_oversized_response(void)
{
    char oversized[NETEASE_OFFICIAL_RESPONSE_MAX_BYTES + 2];
    memset(oversized, ' ', sizeof(oversized));
    oversized[0] = '{';
    oversized[sizeof(oversized) - 1] = '\0';
    official_api_result_t result = {0};
    assert(official_parse_envelope(oversized, 200, &result) == ESP_ERR_INVALID_SIZE);
}

static void test_host_signer_and_http_fail_closed(void)
{
    char signature[64] = {0};
    assert(official_sign_rsa_sha256_base64("not-a-key", "a=b", signature, sizeof(signature)) == ESP_ERR_INVALID_STATE);
    assert(signature[0] == '\0');

    char response[64] = {0};
    int http_status = 123;
    netease_official_client_t client = {0};
    assert(official_http_request(&client,
                                 NETEASE_OFFICIAL_PATH_QR_START,
                                 HTTP_METHOD_GET,
                                 "a=b",
                                 response,
                                 sizeof(response),
                                 &http_status) == ESP_ERR_INVALID_STATE);
    assert(response[0] == '\0');
    assert(http_status == 0);
}

int main(void)
{
    test_canonical_excludes_sign_empty_and_sorts_ascii();
    test_url_encode_uses_uppercase_rfc3986_bytes();
    test_url_encode_rejects_small_buffer();
    test_device_json_contains_official_values();
    test_parse_envelope_success_keeps_data();
    test_parse_envelope_business_error_has_no_success_data();
    test_parse_envelope_rejects_malformed_missing_or_bad_data();
    test_parse_envelope_rejects_oversized_response();
    test_host_signer_and_http_fail_closed();
    return 0;
}
