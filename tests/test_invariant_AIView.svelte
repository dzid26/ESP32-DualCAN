#include <check.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <string.h>

#define ESP32_BASE_URL "http://192.168.4.1"

typedef struct {
    char *data;
    size_t size;
} response_buffer;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buffer *mem = (response_buffer *)userp;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

START_TEST(test_protected_endpoints_reject_unauthenticated)
{
    // Invariant: Protected endpoints must reject requests without valid authentication
    const char *endpoints[] = {
        "/api/scripts/list",
        "/api/scripts/execute",
        "/api/can/send"
    };
    
    const char *auth_headers[] = {
        NULL,                                    // No auth header
        "Authorization: Bearer expired_token",   // Expired/invalid token
        "Authorization: Bearer "                 // Malformed token
    };

    CURL *curl = curl_easy_init();
    ck_assert_ptr_nonnull(curl);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            response_buffer resp = {NULL, 0};
            char url[256];
            snprintf(url, sizeof(url), "%s%s", ESP32_BASE_URL, endpoints[i]);
            
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            
            struct curl_slist *headers = NULL;
            if (auth_headers[j]) {
                headers = curl_slist_append(headers, auth_headers[j]);
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }
            
            CURLcode res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            if (headers) curl_slist_free_all(headers);
            free(resp.data);
            
            ck_assert_msg(http_code == 401 || http_code == 403,
                "Endpoint %s with auth[%d] returned %ld, expected 401/403",
                endpoints[i], j, http_code);
        }
    }
    
    curl_easy_cleanup(curl);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s = suite_create("Security");
    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_protected_endpoints_reject_unauthenticated);
    suite_add_tcase(s, tc_core);
    return s;
}

int main(void)
{
    Suite *s = security_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}