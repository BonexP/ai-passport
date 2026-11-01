// main/http_body.h —— 有界 HTTP 响应体缓冲：静态存储、截断标记、结果分类。
//
// 纯逻辑：不 include 任何 ESP-IDF 头文件，也不分配堆内存。调用方把一块静态缓冲交给
// http_body_init()，再把 esp_http_client 读出的字节原样喂给 http_body_write()。
//
// 为什么溢出后还要继续收：esp_http_client 的读取循环必须读到连接结束，提前停止会让
// TLS 连接无法正常收尾，下一次请求可能读到上一条响应的残留。所以本模块在缓冲写满后
// 只置 truncated 标记并丢弃后续字节，不阻断调用方继续排空，也不做任何解析。
//
// 线程模型：无内部锁。约定由同一个 worker 任务独占一个 http_body_t，与 LVGL 任务
// 没有共享；解析完成后再把结果整体拷进状态快照。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 一次取回的结果。传输层负责产生 TIMEOUT / TLS / TRANSPORT；
// 本模块的 http_result_from_status() 只产生 OK / TRUNCATED / HTTP_STATUS。
typedef enum {
    HTTP_RESULT_OK = 0,      // 2xx 且响应体完整收下
    HTTP_RESULT_TRUNCATED,   // 2xx 但响应体超过缓冲：内容不完整，不能解析
    HTTP_RESULT_HTTP_STATUS, // 服务器应答了非 2xx（含 3xx/4xx/5xx）
    HTTP_RESULT_TIMEOUT,     // 连接或读取超时
    HTTP_RESULT_TLS,         // 证书校验、握手等 TLS 层失败
    HTTP_RESULT_TRANSPORT,   // DNS、socket、连接被拒等其余传输层失败
    // 2xx 但 Content-Type 明确不是 JSON：多半是门户页或错误页伪装成 200。
    // 响应头缺失时无法判断，按正常处理（见 net_http.h）。
    HTTP_RESULT_CONTENT_TYPE,
} http_result_t;

// 调用方拥有 buf；结构体本身可以放在静态区，不持有指针以外的东西。
typedef struct {
    char  *buf;       // 静态缓冲，生命周期必须覆盖整个请求；不拥有
    size_t cap;       // 缓冲总字节数，含结尾 NUL；cap >= 1 才是有效配置
    size_t len;       // 已收下的字节数，不含 NUL
    bool   truncated; // 是否丢过字节；一旦为 true 不再复位
    bool   closed;    // finish 之后为 true，之后的 write 一律忽略
} http_body_t;

// 绑定缓冲并清空状态。buf == NULL 或 cap == 0 是退化配置：不写任何字节，
// 任何非空响应都会被标记为截断。body 为 NULL 时什么都不做。
void http_body_init(http_body_t *body, char *buf, size_t cap);

// 收下一段响应体。永远"消费"全部 len 字节：放得下的部分写入缓冲，放不下的丢弃并
// 置 truncated。数据为 NULL、长度为 0、已 finish 或已截断时直接返回。
// 缓冲始终以 NUL 结尾，调用方可以安全地当作 C 字符串读取。
void http_body_write(http_body_t *body, const char *data, size_t len);

// 结束本次接收并返回"内容是否完整"。空响应体（204）返回 true：
// 完整性判断不包含"非空"，空内容有没有意义由解析层决定。
bool http_body_finish(http_body_t *body);

// 响应体文本。永不为 NULL；退化配置或 body 为 NULL 时返回空串。
const char *http_body_text(const http_body_t *body);

// 已收下的字节数。body 为 NULL 时返回 0。
size_t http_body_len(const http_body_t *body);

// 是否发生过截断。body 为 NULL 时返回 true（没有可用内容）。
bool http_body_truncated(const http_body_t *body);

// 把 HTTP 状态码和截断标记归成结果。status <= 0 表示根本没拿到状态行。
// 非 2xx 优先于截断：服务器已经明确报错时，"内容不完整"不是最该告诉用户的事。
http_result_t http_result_from_status(int http_status, bool truncated);
