// tests/demo_stubs/demo_http_client_stack.c —— esp_http_client 的行为桩。
//
// 让 net_http.c 的每一条分支都能在主机上走一遍：正常分块、截断、非 2xx、
// 打开超时、读错误。桩同一时刻只服务一个客户端，足够覆盖本仓库的串行用法。
#include "demo_http_client_stub.h"

#include <stddef.h>
#include <string.h>

#include "esp_http_client.h"

static demo_http_client_scenario_t s_scenario;
static size_t                      s_offset;
static int                         s_reads;
static int                         s_init_count;
static int                         s_destroy_count;

// 供测试直接构造句柄指针；不需要真实资源。
static int s_handle_token;

void demo_http_client_reset(void) {
    memset(&s_scenario, 0, sizeof s_scenario);
    s_scenario.status_code = 200;
    s_scenario.fetch_headers_result = 0;
    s_scenario.read_error_after = -1;
    s_offset = 0;
    s_reads = 0;
    s_init_count = 0;
    s_destroy_count = 0;
}

void demo_http_client_set_scenario(const demo_http_client_scenario_t *scenario) {
    if (scenario != NULL) {
        s_scenario = *scenario;
    }
}

int demo_http_client_init_count(void) {
    return s_init_count;
}

int demo_http_client_destroy_count(void) {
    return s_destroy_count;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    (void)config;
    s_init_count++;
    return (esp_http_client_handle_t)&s_handle_token;
}

void esp_http_client_destroy(esp_http_client_handle_t client) {
    (void)client;
    s_destroy_count++;
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

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
    (void)client;
    return s_scenario.fetch_headers_result;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    (void)client;
    return s_scenario.status_code;
}

char *esp_http_client_get_header(esp_http_client_handle_t client, const char *key) {
    (void)client;
    if (key == NULL || s_scenario.content_type == NULL) {
        return NULL;
    }
    // net_http 只问 Content-Type，桩按精确键名返回即可。
    if (strcmp(key, "Content-Type") != 0) {
        return NULL;
    }
    return (char *)s_scenario.content_type;
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
