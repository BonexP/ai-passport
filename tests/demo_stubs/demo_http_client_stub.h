// tests/demo_stubs/demo_http_client_stub.h —— esp_http_client 行为桩的控制接口。
#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef struct {
    int         status_code;          // 200 / 404 / …
    const char *body;                 // 响应体（不必以 NUL 结尾）
    size_t      body_len;
    esp_err_t   open_error;           // 非 0 时 open 直接失败
    int64_t     fetch_headers_result; // < 0 表示取头失败
    int         read_error_after;     // -1 = 不注入；0 = 第一次读就失败
    size_t      chunk_size;           // 0 = 一次给完
    const char *content_type;         // NULL = 服务器没发 Content-Type
} demo_http_client_scenario_t;

// 清空场景与计数。每个用例开始时调用一次。
void demo_http_client_reset(void);

// 覆盖下一次请求的行为。结构体按值拷贝，调用方不必保持生命周期。
void demo_http_client_set_scenario(const demo_http_client_scenario_t *scenario);

// 用来断言"每条路径都销毁了客户端"。
int demo_http_client_init_count(void);
int demo_http_client_destroy_count(void);
