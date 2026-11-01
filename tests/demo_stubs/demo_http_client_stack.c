// tests/demo_stubs/demo_http_client_stack.c —— esp_http_client 的行为桩。
//
// 让 net_http.c 的每一条分支都能在主机上走一遍：正常分块、截断、非 2xx、
// 打开超时、读错误、Content-Type 判定。桩同一时刻只服务一个客户端。
#include "demo_http_client_stub.h"

#include <stddef.h>
#include <string.h>

#include "esp_http_client.h"

static demo_http_client_scenario_t s_scenario;
static esp_http_client_config_t    s_config; // 保存 event_handler / user_data
static size_t                      s_offset;
static int                         s_reads;
static int                         s_init_count;
static int                         s_cleanup_count;

// 供测试直接构造句柄指针；不需要真实资源。
static int s_handle_token;

void demo_http_client_reset(void) {
    memset(&s_scenario, 0, sizeof s_scenario);
    memset(&s_config, 0, sizeof s_config);
    s_scenario.status_code = 200;
    s_scenario.fetch_headers_result = 0;
    s_scenario.read_error_after = -1;
    s_offset = 0;
    s_reads = 0;
    s_init_count = 0;
    s_cleanup_count = 0;
}

void demo_http_client_set_scenario(const demo_http_client_scenario_t *scenario) {
    if (scenario != NULL) {
        s_scenario = *scenario;
    }
}

int demo_http_client_init_count(void) {
    return s_init_count;
}

int demo_http_client_cleanup_count(void) {
    return s_cleanup_count;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    s_init_count++;
    if (config != NULL) {
        s_config = *config;
    }
    return (esp_http_client_handle_t)&s_handle_token;
}

void esp_http_client_cleanup(esp_http_client_handle_t client) {
    (void)client;
    s_cleanup_count++;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key,
                                     const char *value) {
    (void)client;
    (void)key;
    (void)value;
    return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len) {
    (void)client;
    (void)write_len;
    s_offset = 0;
    s_reads = 0;
    return s_scenario.open_error;
}

// 真实实现会在收到响应头时回调 event_handler；这里用场景里的 content_type 模拟一次。
static void deliver_content_type_header(void) {
    if (s_config.event_handler == NULL || s_scenario.content_type == NULL) {
        return;
    }

    esp_http_client_event_t evt;
    memset(&evt, 0, sizeof evt);
    evt.event_id = HTTP_EVENT_ON_HEADER;
    evt.header_key = (char *)"Content-Type";
    evt.header_value = (char *)s_scenario.content_type;
    evt.user_data = s_config.user_data;
    (void)s_config.event_handler(&evt);
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
    (void)client;
    deliver_content_type_header();
    return s_scenario.fetch_headers_result;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    (void)client;
    return s_scenario.status_code;
}

int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int len) {
    (void)client;

    if (s_scenario.read_error_after >= 0 && s_reads >= s_scenario.read_error_after) {
        return -1;
    }
    if (s_scenario.body == NULL || s_offset >= s_scenario.body_len) {
        return 0;
    }
    if (len <= 0) {
        return 0;
    }

    size_t take = (size_t)len;
    if (s_scenario.chunk_size > 0 && take > s_scenario.chunk_size) {
        take = s_scenario.chunk_size;
    }
    size_t remaining = s_scenario.body_len - s_offset;
    if (take > remaining) {
        take = remaining;
    }

    memcpy(buffer, s_scenario.body + s_offset, take);
    s_offset += take;
    s_reads++;
    return (int)take;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

esp_err_t esp_crt_bundle_attach(void *conf) {
    (void)conf;
    return ESP_OK;
}
