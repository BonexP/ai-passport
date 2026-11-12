// main/net_http.h —— 一次 HTTPS GET 的设备侧封装。
//
// 只做四件事：用证书 bundle 建立连接、按需附加请求头、把响应体灌进调用方的静态
// 缓冲、把结果归成 http_result_t 并交出状态码。缓冲、截断标记与状态码分类都在
// http_body.c 里，本文件不重复实现，也不做 JSON 解析。
//
// 内存：响应体缓冲由调用方提供（net worker 的静态数组），请求头列表由调用方静态
// 声明（不复制、不拥有）。本函数只额外用 128 B 栈上的读取块；esp_http_client 与
// mbedTLS 自己的分配不可避免，且同一时刻只允许一个请求在跑。
//
// 线程：阻塞调用，只能在 net worker 任务里使用。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "http_body.h"

// 单次请求预算。TLS 握手 + 小报文在 2.4 GHz 上通常 1-3 秒，8 秒给弱信号留余量。
#define NET_HTTP_TIMEOUT_MS 8000u

// 服务器大多不需要认证，但带一个可识别的 UA 便于对方排查。
#define NET_HTTP_USER_AGENT "FoloToy-AiPassport/1.0"

// 一个请求头。name / value 都是调用方持有的字符串，net_http 不复制、不释放，
// 生命周期必须覆盖整个调用。value 里可能含凭据（Bearer token），不得写日志。
typedef struct {
    const char *name;
    const char *value;
} net_http_header_t;

// 取一次数的扩展形态：在 net_http_get() 的基础上带一组请求头，并交出状态码。
//
// 用结构体而不是继续加参数，是为了让未来的字段（方法、POST 体、认证方式）在原地
// 增加，调用方不必每加一个就改一次实参列表，也不会出现两个同类型参数的顺序错误。
typedef struct {
    const char              *url;          // 必填
    const net_http_header_t *headers;      // 可为 NULL
    size_t                   header_count; // headers 为 NULL 时必须为 0；否则两者必须成对
    char                    *body;        // 必填，调用方拥有的静态缓冲
    size_t                   body_cap;    // 含结尾 NUL；0 视为非法
    size_t                  *body_len;    // 可为 NULL
    int                     *status_out;  // 可为 NULL
    uint32_t                 timeout_ms;  // 0 表示使用 NET_HTTP_TIMEOUT_MS
} net_http_request_t;

// 取 req->url 到 req->body[0..req->body_cap)。调用方负责缓冲的生命周期。
// 返回 OK/TRUNCATED 时 *body_len 是实际收下的字节数（不含 NUL）；
// 其余结果 *body_len 为 0，body 内容不可用。
//
// 请求头：req->headers 里每一项都在 open 之前交给 esp_http_client_set_header
// 一次，顺序与数组一致。本函数自己不注入 Authorization、Cookie 一类内容头，
// 只固定带 User-Agent（调用方列表里若有同名项，以调用方给的为准，不会重复发送）。
//
// 状态码：只要读到了响应头就写 *status_out，包括 404、429、502 这些非 2xx——
// 调用方正是靠它们区分"key 被拒"、"被限流"与"服务不可用"。连接失败、取头失败
// 或参数非法时不写；调用方应先把它置 0，再用"是否仍为 0"判断有没有状态码可用。
//
// Content-Type：2xx 且响应头明确不含 "json"（大小写不敏感）时返回
// HTTP_RESULT_CONTENT_TYPE，用来识别"200 + 门户页"。响应头缺失时不做判断，
// 因为部分服务器会省略它；这种情况下由 JSON 解析兜底。
//
// 时钟：校验证书有效期需要可信时间，调用方必须先确认 app_time_is_valid()。
//
// 日志：本函数不打日志。它可能携带凭据头，所以任何经过这里的 URL 或头都不能被
// 打印——包括调试构建。
http_result_t net_http_get_ex(const net_http_request_t *req);

// 不带请求头、不关心状态码的薄包装。天气路径用它，行为与 P5 之前逐字一致：
// status_out 传 NULL，timeout_ms = 0 时回落到 NET_HTTP_TIMEOUT_MS。
http_result_t net_http_get(const char *url, char *body, size_t body_cap, size_t *body_len,
                           uint32_t timeout_ms);
