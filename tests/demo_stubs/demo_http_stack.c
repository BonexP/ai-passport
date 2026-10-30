// tests/demo_stubs/demo_http_stack.c —— 有状态的 HTTP 服务假件。
//
// 刻意**不创建任务**：本仓的主机测试用 test_latest_task / test_auto_ack 驱动 worker，
// 凭空多一个任务会让 auto_ack 跑到 httpd 上。测试直接调用注册进来的 handler，
// 因此"注册了什么路由"和"handler 怎么应答"都能断言。
//
// httpd_start 与每次 register 都参与 AP 步骤编号（配网服务的第 11/12/13 步），
// 这样"启动到 HTTP 这一步失败"也能被逐步骤回滚矩阵覆盖。
#include "demo_test_stubs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_HTTPD_URI_SLOTS_LOCAL 4

httpd_uri_t test_httpd_uris[TEST_HTTPD_URI_SLOTS];
unsigned    test_httpd_uri_count;
unsigned    test_httpd_starts;
unsigned    test_httpd_stops;
unsigned    test_httpd_sends;
unsigned    test_httpd_send_errs;
unsigned    test_httpd_recv_calls;
unsigned    test_httpd_recv_timeout_at;
char        test_httpd_last_status[32];
char        test_httpd_last_type[48];
char        test_httpd_last_body[TEST_HTTPD_BODY_MAX];
size_t      test_httpd_last_len;

static char        s_body[TEST_HTTPD_BODY_MAX];
static size_t      s_body_len;
static size_t      s_body_off;
static char        s_content_length[24];
static bool        s_has_content_length;
static bool        s_started;
static httpd_req_t s_req;

void test_httpd_reset(void) {
    test_httpd_uri_count = 0;
    test_httpd_starts = 0;
    test_httpd_stops = 0;
    test_httpd_sends = 0;
    test_httpd_send_errs = 0;
    test_httpd_recv_calls = 0;
    test_httpd_recv_timeout_at = 0;
    memset(test_httpd_uris, 0, sizeof test_httpd_uris);
    memset(test_httpd_last_status, 0, sizeof test_httpd_last_status);
    memset(test_httpd_last_type, 0, sizeof test_httpd_last_type);
    memset(test_httpd_last_body, 0, sizeof test_httpd_last_body);
    test_httpd_last_len = 0;
    memset(s_body, 0, sizeof s_body);
    s_body_len = 0;
    s_body_off = 0;
    memset(s_content_length, 0, sizeof s_content_length);
    s_has_content_length = false;
    s_started = false;
    memset(&s_req, 0, sizeof s_req);
}

void test_httpd_set_request(const char *body, size_t len, const char *content_length) {
    assert(len <= sizeof s_body);
    s_body_len = len;
    s_body_off = 0;
    if (body != NULL && len > 0) memcpy(s_body, body, len);
    s_has_content_length = (content_length != NULL);
    if (s_has_content_length) {
        size_t n = strlen(content_length);
        if (n >= sizeof s_content_length) n = sizeof s_content_length - 1;
        memcpy(s_content_length, content_length, n);
        s_content_length[n] = '\0';
    } else {
        s_content_length[0] = '\0';
    }
    s_req.content_len = len;
}

httpd_req_t *test_httpd_req(void) { return &s_req; }

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config) {
    (void)config;
    esp_err_t step = test_ap_next_step();
    if (step != ESP_OK) return step;
    test_httpd_starts++;
    s_started = true;
    if (handle != NULL) *handle = (httpd_handle_t)&s_started;
    return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
    (void)handle;
    test_httpd_stops++;
    s_started = false;
    test_httpd_uri_count = 0; // 停止后路由表必须为空，测试据此断言"无残留"
    memset(test_httpd_uris, 0, sizeof test_httpd_uris);
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri) {
    (void)handle;
    assert(uri != NULL && uri->uri != NULL && uri->handler != NULL);
    esp_err_t step = test_ap_next_step();
    if (step != ESP_OK) return step;
    assert(test_httpd_uri_count < TEST_HTTPD_URI_SLOTS_LOCAL);
    test_httpd_uris[test_httpd_uri_count++] = *uri;
    return ESP_OK;
}

esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle, const char *uri,
                                       httpd_method_t method) {
    (void)handle;
    unsigned out = 0;
    for (unsigned i = 0; i < test_httpd_uri_count; i++) {
        bool hit = strcmp(test_httpd_uris[i].uri, uri) == 0 &&
                   (method == test_httpd_uris[i].method || method == HTTP_ANY);
        if (!hit) test_httpd_uris[out++] = test_httpd_uris[i];
    }
    test_httpd_uri_count = out;
    return ESP_OK;
}

int httpd_req_recv(httpd_req_t *req, char *buf, size_t buf_len) {
    (void)req;
    test_httpd_recv_calls++;
    if (test_httpd_recv_timeout_at != 0 && test_httpd_recv_calls == test_httpd_recv_timeout_at) {
        return HTTPD_SOCK_ERR_TIMEOUT;
    }
    size_t remaining = s_body_len - s_body_off;
    // 声明的长度比实际正文多：真机上就是对端提前关连接。
    if (remaining == 0) return HTTPD_SOCK_ERR_FAIL;
    size_t n = (remaining < buf_len) ? remaining : buf_len;
    memcpy(buf, s_body + s_body_off, n);
    s_body_off += n;
    return (int)n;
}

size_t httpd_req_get_hdr_value_len(httpd_req_t *req, const char *field) {
    (void)req;
    if (!s_has_content_length || field == NULL) return 0;
    if (strcmp(field, "Content-Length") != 0) return 0;
    return strlen(s_content_length);
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val,
                                      size_t val_size) {
    (void)req;
    if (!s_has_content_length || field == NULL || strcmp(field, "Content-Length") != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = strlen(s_content_length);
    if (val == NULL || val_size <= n) return ESP_ERR_INVALID_ARG;
    memcpy(val, s_content_length, n + 1);
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status) {
    (void)req;
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(status);
    if (n >= sizeof test_httpd_last_status) n = sizeof test_httpd_last_status - 1;
    memcpy(test_httpd_last_status, status, n);
    test_httpd_last_status[n] = '\0';
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) {
    (void)req;
    if (type == NULL) return ESP_ERR_INVALID_ARG;
    size_t n = strlen(type);
    if (n >= sizeof test_httpd_last_type) n = sizeof test_httpd_last_type - 1;
    memcpy(test_httpd_last_type, type, n);
    test_httpd_last_type[n] = '\0';
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len) {
    (void)req;
    if (buf == NULL) return ESP_ERR_INVALID_ARG;
    if (buf_len < 0 && buf_len != HTTPD_RESP_USE_STRLEN) return ESP_ERR_INVALID_ARG;
    size_t n = (buf_len == HTTPD_RESP_USE_STRLEN) ? strlen(buf) : (size_t)buf_len;
    if (n >= sizeof test_httpd_last_body) return ESP_ERR_INVALID_ARG;
    memcpy(test_httpd_last_body, buf, n);
    test_httpd_last_body[n] = '\0';
    test_httpd_last_len = n;
    test_httpd_sends++;
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *msg) {
    (void)req;
    (void)msg;
    static const char *const NAMES[] = {
        "400 Bad Request", "408 Request Timeout", "411 Length Required",
        "413 Payload Too Large", "500 Internal Server Error",
    };
    test_httpd_send_errs++;
    const char *name = (error >= 0 && (unsigned)error < sizeof NAMES / sizeof NAMES[0])
                           ? NAMES[error]
                           : "500 Internal Server Error";
    size_t n = strlen(name);
    if (n >= sizeof test_httpd_last_status) n = sizeof test_httpd_last_status - 1;
    memcpy(test_httpd_last_status, name, n);
    test_httpd_last_status[n] = '\0';
    return ESP_OK;
}
