// tests/demo_stubs/esp_http_client.h —— 主机测试用的 esp_http_client 最小声明。
//
// 只声明 net_http.c 实际用到的字段与函数；行为由 demo_http_client_stack.c 提供。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct esp_http_client *esp_http_client_handle_t;

typedef struct {
    const char *url;
    int         timeout_ms;
    esp_err_t (*crt_bundle_attach)(void *conf);
    int         buffer_size;
    int         buffer_size_tx;
    bool        keep_alive_enable;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
void                     esp_http_client_destroy(esp_http_client_handle_t client);
esp_err_t                esp_http_client_set_header(esp_http_client_handle_t client,
                                                    const char *key, const char *value);
esp_err_t                esp_http_client_open(esp_http_client_handle_t client, int write_len);
int64_t                  esp_http_client_fetch_headers(esp_http_client_handle_t client);
int                      esp_http_client_get_status_code(esp_http_client_handle_t client);
char                    *esp_http_client_get_header(esp_http_client_handle_t client,
                                                    const char *key);
int                      esp_http_client_read(esp_http_client_handle_t client, char *buffer,
                                              int len);
esp_err_t                esp_http_client_close(esp_http_client_handle_t client);
