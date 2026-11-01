// tests/test_net_http.c —— HTTPS 封装的主机测试（esp_http_client 行为桩）。
//
// 覆盖的不是 TLS 本身（那只能在真机上验证），而是封装层的四条出口：
// 成功、截断、非 2xx、传输失败，以及"每条路径都必须销毁客户端"。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "demo_http_client_stub.h"
#include "net_http.h"

static void set_scenario(int status, const char *body, size_t body_len) {
    demo_http_client_scenario_t sc;
    memset(&sc, 0, sizeof sc);
    sc.status_code = status;
    sc.body = body;
    sc.body_len = body_len;
    sc.fetch_headers_result = (int64_t)body_len;
    sc.read_error_after = -1;
    sc.chunk_size = 0;
    sc.content_type = "application/json; charset=utf-8";
    demo_http_client_set_scenario(&sc);
}

static void test_success_streams_the_body_in_chunks(void) {
    demo_http_client_reset();
    set_scenario(200, "hello world", 11);

    demo_http_client_scenario_t sc;
    memset(&sc, 0, sizeof sc);
    sc.status_code = 200;
    sc.body = "hello world";
    sc.body_len = 11;
    sc.fetch_headers_result = 11;
    sc.read_error_after = -1;
    sc.chunk_size = 3; // 强制多轮读取
    sc.content_type = "application/json";
    demo_http_client_set_scenario(&sc);

    char body[32];
    size_t len = 0;
    assert(net_http_get("https://example.test/x", body, sizeof body, &len, 1000) ==
           HTTP_RESULT_OK);
    assert(len == 11);
    assert(strcmp(body, "hello world") == 0);
    assert(demo_http_client_init_count() == 1);
    assert(demo_http_client_destroy_count() == 1);
}

static void test_oversized_body_is_truncated_and_flagged(void) {
    demo_http_client_reset();
    set_scenario(200, "0123456789", 10);

    char body[4]; // 只能放 3 字节内容
    size_t len = 0;
    assert(net_http_get("https://example.test/x", body, sizeof body, &len, 1000) ==
           HTTP_RESULT_TRUNCATED);
    assert(len == 3);
    assert(strcmp(body, "012") == 0);
    assert(demo_http_client_destroy_count() == 1);
}

static void test_non_2xx_is_reported_even_with_a_body(void) {
    demo_http_client_reset();
    set_scenario(404, "not found", 9);

    char body[32];
    size_t len = 0;
    assert(net_http_get("https://example.test/x", body, sizeof body, &len, 1000) ==
           HTTP_RESULT_HTTP_STATUS);
    assert(demo_http_client_destroy_count() == 1);
}

static void test_empty_2xx_body_is_ok(void) {
    demo_http_client_reset();
    set_scenario(204, NULL, 0);

    char body[8];
    size_t len = 123;
    assert(net_http_get("https://example.test/x", body, sizeof body, &len, 1000) ==
           HTTP_RESULT_OK);
    assert(len == 0);
    assert(body[0] == '\0');
}

static void test_open_timeout_is_distinguished(void) {
    demo_http_client_reset();
    set_scenario(200, "x", 1);
    demo_http_client_scenario_t sc;
    memset(&sc, 0, sizeof sc);
    sc.status_code = 200;
    sc.fetch_headers_result = 0;
    sc.read_error_after = -1;
    sc.open_error = ESP_ERR_TIMEOUT;
    demo_http_client_set_scenario(&sc);

    char body[8];
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_TIMEOUT);
    assert(demo_http_client_destroy_count() == 1);
}

static void test_open_failure_is_transport(void) {
    demo_http_client_reset();
    demo_http_client_scenario_t sc;
    memset(&sc, 0, sizeof sc);
    sc.open_error = ESP_FAIL;
    sc.fetch_headers_result = 0;
    sc.read_error_after = -1;
    demo_http_client_set_scenario(&sc);

    char body[8];
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_TRANSPORT);
    assert(demo_http_client_destroy_count() == 1);
}

static void test_header_failure_is_transport(void) {
    demo_http_client_reset();
    demo_http_client_scenario_t sc;
    memset(&sc, 0, sizeof sc);
    sc.status_code = 200;
    sc.fetch_headers_result = -1;
    sc.read_error_after = -1;
    demo_http_client_set_scenario(&sc);

    char body[8];
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_TRANSPORT);
    // 取头失败也必须关掉连接并销毁客户端。
    assert(demo_http_client_destroy_count() == 1);
}

static void test_read_error_discards_the_partial_body(void) {
    demo_http_client_reset();
    demo_http_client_scenario_t sc;
    memset(&sc, 0, sizeof sc);
    sc.status_code = 200;
    sc.body = "partial";
    sc.body_len = 7;
    sc.fetch_headers_result = 7;
    sc.read_error_after = 1; // 先读一块，然后断开
    sc.chunk_size = 2;
    demo_http_client_set_scenario(&sc);

    char body[32];
    size_t len = 99;
    assert(net_http_get("https://example.test/x", body, sizeof body, &len, 1000) ==
           HTTP_RESULT_TRANSPORT);
    assert(len == 0); // 半截内容不能被当成完整响应
    assert(demo_http_client_destroy_count() == 1);
}

static void test_content_type_guards_successful_responses(void) {
    demo_http_client_scenario_t sc;
    char body[64];

    // 200 + text/html：门户页伪装成正常响应，必须按内容类型拒掉。
    demo_http_client_reset();
    memset(&sc, 0, sizeof sc);
    sc.status_code = 200;
    sc.body = "<html>login</html>";
    sc.body_len = 18;
    sc.fetch_headers_result = 18;
    sc.read_error_after = -1;
    sc.content_type = "text/html; charset=utf-8";
    demo_http_client_set_scenario(&sc);
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_CONTENT_TYPE);

    // 大小写与参数都不影响判断。
    demo_http_client_reset();
    sc.content_type = "Application/JSON; charset=utf-8";
    demo_http_client_set_scenario(&sc);
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_OK);

    // 服务器没发 Content-Type：不猜，交给 JSON 解析。
    demo_http_client_reset();
    sc.content_type = NULL;
    demo_http_client_set_scenario(&sc);
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_OK);

    // 非 2xx 仍然先报状态码：那比"内容类型不对"更具体。
    demo_http_client_reset();
    sc.status_code = 500;
    sc.content_type = "text/html";
    demo_http_client_set_scenario(&sc);
    assert(net_http_get("https://example.test/x", body, sizeof body, NULL, 1000) ==
           HTTP_RESULT_HTTP_STATUS);

    // 截断也先报截断。
    demo_http_client_reset();
    sc.status_code = 200;
    sc.content_type = "text/html";
    demo_http_client_set_scenario(&sc);
    assert(net_http_get("https://example.test/x", body, 4, NULL, 1000) ==
           HTTP_RESULT_TRUNCATED);
}

static void test_invalid_arguments_do_not_touch_the_network(void) {
    demo_http_client_reset();

    char body[8];
    size_t len = 99;
    assert(net_http_get(NULL, body, sizeof body, &len, 1000) == HTTP_RESULT_TRANSPORT);
    assert(len == 0);
    assert(net_http_get("https://example.test/x", NULL, 8, &len, 1000) ==
           HTTP_RESULT_TRANSPORT);
    assert(net_http_get("https://example.test/x", body, 0, &len, 1000) ==
           HTTP_RESULT_TRANSPORT);
    assert(demo_http_client_init_count() == 0);
    assert(demo_http_client_destroy_count() == 0);
}

int main(void) {
    test_success_streams_the_body_in_chunks();
    test_oversized_body_is_truncated_and_flagged();
    test_non_2xx_is_reported_even_with_a_body();
    test_empty_2xx_body_is_ok();
    test_open_timeout_is_distinguished();
    test_open_failure_is_transport();
    test_header_failure_is_transport();
    test_read_error_discards_the_partial_body();
    test_content_type_guards_successful_responses();
    test_invalid_arguments_do_not_touch_the_network();
    puts("net http tests: PASS");
    return 0;
}
