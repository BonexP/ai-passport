// main/net_http.c —— HTTPS GET 的实现。契约见 net_http.h。
#include "net_http.h"

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"

// 栈上读取块。128 B 足以让 TLS 记录层高效工作，又不会在 6 KB 任务栈里占太多。
#define NET_HTTP_CHUNK 128

static char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// 大小写不敏感的 contains。不用 strcasestr：那是 GNU 扩展，主机与固件的
// feature 宏不同，声明并不可靠。
static bool contains_ci(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }

    for (const char *h = haystack; *h != '\0'; h++) {
        const char *a = h;
        const char *b = needle;
        while (*a != '\0' && *b != '\0' && ascii_lower(*a) == ascii_lower(*b)) {
            a++;
            b++;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

http_result_t net_http_get(const char *url, char *body, size_t body_cap, size_t *body_len,
                           uint32_t timeout_ms) {
    if (body_len != NULL) {
        *body_len = 0;
    }
    if (url == NULL || body == NULL || body_cap == 0) {
        return HTTP_RESULT_TRANSPORT;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = (int)timeout_ms,
        // 证书 bundle 做完整校验（链 + 域名 + 有效期）。有效期依赖 SNTP 已同步：
        // 时间不可信时调用方根本不该走到这里。
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return HTTP_RESULT_TRANSPORT;
    }
    (void)esp_http_client_set_header(client, "User-Agent", NET_HTTP_USER_AGENT);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_destroy(client);
        return err == ESP_ERR_TIMEOUT ? HTTP_RESULT_TIMEOUT : HTTP_RESULT_TRANSPORT;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        esp_http_client_close(client);
        esp_http_client_destroy(client);
        return HTTP_RESULT_TRANSPORT;
    }

    const int status = esp_http_client_get_status_code(client);
    // 头指针在 close 之前有效。缺失时返回 NULL：有些服务器省略 Content-Type，
    // 这种情况不做判断，交给 JSON 解析兜底。
    const char *content_type = esp_http_client_get_header(client, "Content-Type");

    http_body_t buffer;
    http_body_init(&buffer, body, body_cap);

    http_result_t result = HTTP_RESULT_TRANSPORT;
    bool read_ok = true;
    for (;;) {
        char chunk[NET_HTTP_CHUNK];
        int n = esp_http_client_read(client, chunk, (int)sizeof chunk);
        if (n < 0) {
            read_ok = false; // 连接中途断开：内容不可信
            break;
        }
        if (n == 0) {
            break; // 对端关闭 / 内容读完
        }
        http_body_write(&buffer, chunk, (size_t)n);
    }

    if (read_ok) {
        (void)http_body_finish(&buffer);
        if (body_len != NULL) {
            *body_len = http_body_len(&buffer);
        }
        result = http_result_from_status(status, http_body_truncated(&buffer));
        // 门户页 / 错误页经常以 200 + text/html 返回。只有"本应成功"的响应才升级
        // 成 CONTENT_TYPE；非 2xx 仍报状态码，截断仍报截断（那是更具体的原因）。
        if (result == HTTP_RESULT_OK && content_type != NULL &&
            !contains_ci(content_type, "json")) {
            result = HTTP_RESULT_CONTENT_TYPE;
        }
    }

    esp_http_client_close(client);
    esp_http_client_destroy(client);
    return result;
}
