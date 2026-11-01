// main/net_http.c —— HTTPS GET 的实现。契约见 net_http.h。
#include "net_http.h"

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"

// 栈上读取块。128 B 足以让 TLS 记录层高效工作，又不会在 6 KB 任务栈里占太多。
#define NET_HTTP_CHUNK 128

// Content-Type 只需要判断"是不是 JSON"，64 B 足以装下常见值。
#define NET_HTTP_CT_MAX 64

typedef struct {
    char content_type[NET_HTTP_CT_MAX];
    bool present; // 服务器是否真的发了 Content-Type
} http_header_probe_t;

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

static bool key_equals_ci(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0' && ascii_lower(*a) == ascii_lower(*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void copy_text(char *dst, size_t cap, const char *src) {
    if (cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = 0;
    while (n + 1 < cap && src[n] != '\0') {
        n++;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// esp_http_client 的头读取接口在不同 IDF 版本里签名变过，事件回调则是稳定的：
// HTTP_EVENT_ON_HEADER 把 key/value 交给我们，复制一份即可。
static esp_err_t on_http_event(esp_http_client_event_t *evt) {
    if (evt == NULL || evt->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_OK;
    }

    http_header_probe_t *probe = evt->user_data;
    if (probe == NULL || evt->header_key == NULL || evt->header_value == NULL) {
        return ESP_OK;
    }
    if (!key_equals_ci(evt->header_key, "Content-Type")) {
        return ESP_OK;
    }

    copy_text(probe->content_type, sizeof probe->content_type, evt->header_value);
    probe->present = true;
    return ESP_OK;
}

http_result_t net_http_get(const char *url, char *body, size_t body_cap, size_t *body_len,
                           uint32_t timeout_ms) {
    if (body_len != NULL) {
        *body_len = 0;
    }
    if (url == NULL || body == NULL || body_cap == 0) {
        return HTTP_RESULT_TRANSPORT;
    }

    http_header_probe_t probe = {0};
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = (int)timeout_ms,
        // 证书 bundle 做完整校验（链 + 域名 + 有效期）。有效期依赖 SNTP 已同步：
        // 时间不可信时调用方根本不该走到这里。
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
        .event_handler = on_http_event,
        .user_data = &probe,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return HTTP_RESULT_TRANSPORT;
    }
    (void)esp_http_client_set_header(client, "User-Agent", NET_HTTP_USER_AGENT);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err == ESP_ERR_TIMEOUT ? HTTP_RESULT_TIMEOUT : HTTP_RESULT_TRANSPORT;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return HTTP_RESULT_TRANSPORT;
    }

    const int status = esp_http_client_get_status_code(client);

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
        if (result == HTTP_RESULT_OK && probe.present &&
            !contains_ci(probe.content_type, "json")) {
            result = HTTP_RESULT_CONTENT_TYPE;
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}
