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

// 用来断言"每条路径都清理了客户端"。
int demo_http_client_init_count(void);
int demo_http_client_cleanup_count(void);

// ---- 请求头记录 ----
//
// 桩以"同名覆盖"记录请求头，与 esp-idf 的 esp_http_client_set_header 一致：
// 后写的同名（大小写不敏感）头替换掉先写的，即每个键至多一条——这就是线上的
// 最终形态，被覆盖的项不再计数。键与值都拷贝进静态存储。记录不清零容量
// 上限，超出部分计入溢出计数——这样断言"恰好一次"永远不会因为记录被截断而
// 误通过。
#define DEMO_HTTP_MAX_HEADERS    8
#define DEMO_HTTP_MAX_HEADER_LEN 128

// 当前记录的请求头数量（不含溢出部分）。键与值均为 NUL 结尾的副本。
int         demo_http_client_header_count(void);
const char *demo_http_client_header_name(int index);
const char *demo_http_client_header_value(int index);

// 某个键（大小写不敏感）出现的次数。用来断言"恰好一次"/"一次都不设"。
int demo_http_client_header_times(const char *name);

// 因超出容量而没被记录的次数。
int demo_http_client_header_overflow(void);
