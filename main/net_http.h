// main/net_http.h —— 一次 HTTPS GET 的设备侧封装。
//
// 只做三件事：用证书 bundle 建立连接、把响应体灌进调用方的静态缓冲、把结果
// 归成 http_result_t。缓冲、截断标记与状态码分类都在 http_body.c 里，本文件
// 不重复实现，也不做 JSON 解析。
//
// 内存：响应体缓冲由调用方提供（net worker 的静态数组），本函数只额外用 128 B
// 栈上的读取块；esp_http_client 与 mbedTLS 自己的分配不可避免，且同一时刻只允许
// 一个请求在跑。
//
// 线程：阻塞调用，只能在 net worker 任务里使用。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "http_body.h"

// 单次请求预算。TLS 握手 + 小报文在 2.4 GHz 上通常 1-3 秒，8 秒给弱信号留余量。
#define NET_HTTP_TIMEOUT_MS 8000u

// 服务器不需要认证，但带一个可识别的 UA 便于对方排查。
#define NET_HTTP_USER_AGENT "FoloToy-AiPassport/1.0"

// 取 url 到 body[0..body_cap)。调用方负责 body 的生命周期。
// 返回 OK/TRUNCATED 时 *body_len 是实际收下的字节数（不含 NUL）；
// 其余结果 *body_len 为 0，body 内容不可用。
// body_len 可为 NULL。
//
// Content-Type：2xx 且响应头明确不含 "json"（大小写不敏感）时返回
// HTTP_RESULT_CONTENT_TYPE，用来识别"200 + 门户页"。响应头缺失时不做判断，
// 因为部分服务器会省略它；这种情况下由 JSON 解析兜底。
//
// 时钟：校验证书有效期需要可信时间，调用方必须先确认 app_time_is_valid()。
http_result_t net_http_get(const char *url, char *body, size_t body_cap, size_t *body_len,
                           uint32_t timeout_ms);
