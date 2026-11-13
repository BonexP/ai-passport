// main/usage_source.h —— AMD Radeon Cloud 用量请求的组装、解析与格式化（纯逻辑）。
//
// 只认识一种报文形状：GET https://developer.amd.com.cn/radeon/api/v1/usage，
// Bearer 认证，返回对象顶层的 daily_cost_* 与 today.* 字段。不建树、不 malloc，
// 复用 json_field 的路径提取，因此可以在 worker 任务里逐次调用。
//
// 单位约定（与文档一致，切勿"顺手"改成美元）：
//   - *_usd 后缀里的数值是 **Token Factory points**，不是货币。文档两处明确说明，
//     且与账户额度无关。本模块把它叫 points，格式化时标 "pts"，永不输出 "$"。
//   - 点位精度 1e-4。内部用 int32_t 存"万分位点数"（points_1e4），
//     3.2500 pts → 32500，避免浮点参与显示与边界比较。
//
// 所有函数不 include ESP-IDF / LVGL，可在主机上用抓到的真实报文测试。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 实际端点约 52 字节；128 给网关前缀与环境参数留余量。
#define USAGE_URL_MAX 128

// 当天请求/错误/token 计数的上界。超出即认为报文不对劲，判解析失败而不是截断显示。
#define USAGE_COUNT_LIMIT 1000000000ll // 1e9

// 一次请求的结局。放在记录旁边而不是 usage_view 里，是因为它有两个消费方向：
// worker 把结果连同记录一起发布，页面再把它翻译成人话。页面不应该反向定义数据。
//
// 与 http_result_t 的区别：那是传输层的机械分类（超时/TLS/截断），这里是"对用户
// 意味着什么"。401 与 403 在传输层是同一个 HTTP_STATUS，对用户却是同一句话；
// 它们与 429、5xx 又必须区分开——那是这张卡片空着时唯一能给出的解释。
typedef enum {
    USAGE_STATUS_NONE = 0,     // 本次开机还没有请求过
    USAGE_STATUS_OK,           // 最近一次成功
    USAGE_STATUS_NO_KEY,       // 编译期没配 key：没开 socket，也就没有网络结果
    USAGE_STATUS_KEY_REJECTED, // 401 / 403：key 缺失或已被吊销
    USAGE_STATUS_RATE_LIMITED, // 429：按 Retry-After 退避
    USAGE_STATUS_UNAVAILABLE,  // 5xx、超时、TLS、传输层失败
    USAGE_STATUS_TRUNCATED,    // 报文超过缓冲：内容不完整，不显示半截数字
    USAGE_STATUS_BAD_BODY,     // 200 但不是可解析的用量报文（含门户页）
} usage_status_t;

// 一次成功的用量观测。
typedef struct {
    bool    has_data;

    // 额度（points，万分位整数）
    bool    has_limit;      // 报文里给了 daily_cost_limit_usd
    int32_t limit_1e4;
    int32_t used_1e4;
    int32_t remaining_1e4;

    // 今日计数
    int32_t today_requests;
    int32_t today_errors;
    int64_t today_total_tokens;

    // 近 30 天花费（points，万分位整数）。页面显示它作为"长期趋势"的唯一代表：
    // by_model / recent 是数组，json_field 不下降，v1 也不显示。
    // 可选：报文缺失时为 has_days30 = false，页面据此隐藏那一行，而不是显示 0。
    bool    has_days30;
    int32_t days30_cost_1e4;

    // 重置时刻：原样保存接口给人的 +08:00 串（如 "2026-11-12T00:00:00+08:00"），
    // 只由 usage_format_reset_clock() 取出钟点，不做 epoch 运算。
    char    reset_at[32];
} usage_state_t;

// 组装用量请求 URL。**永远不会包含 key**：key 只走 Authorization 头。
// 失败（cap 不足）返回 false 并把 buf 置空串。
bool usage_url_build(char *buf, size_t cap);

// 解析一次 /v1/usage 的响应体。成功时 out->has_data = true 并填齐所有字段。
//
// 失败条件（任一即 false，out 内容未定义，调用方只应在 true 时使用）：
//   - json 为 NULL；doc 不是以 '{' 开头的对象
//   - 任何必需数字字段缺失、为 null、是字符串/布尔/数组/对象
//   - 点数为负，或计数超出 USAGE_COUNT_LIMIT
//
// 容错：响应里额外的 by_model / recent / all_time 等键与数组被忽略
// （json_field 只沿对象路径下降，数组整体当不透明值跳过）；
// daily_cost_limit_usd 为可选：缺失、null 或类型不符（如字符串）→ has_limit=false，
// 其余字段照常解析；但如果它是缺了值/类型不符之外的数值且超出 214748.3647 上界，
// 单位可能不是 points，整个报文判失败而不是显示半截额度。
bool usage_parse(const char *json, usage_state_t *out);

// points（万分位整数）→ "3.2500" / "0.0032" / "1234.5678"。缓冲不足返回 false 并置空串。
bool usage_format_points(int32_t points_1e4, char *buf, size_t cap);

// 大数的紧凑显示（整数运算，永不输出 "$"）：
//   999 → "999"，88214 → "88.2k"，1234567 → "1.2M"，1000000000 → "1.0B+"。
// 千以下保留个位：把 999 写成 "0.9k" 既丢精度又不更像人话。
// 只用于 token 计数这类不需要精确到个位的行。负值按 0 处理。
bool usage_format_compact(int64_t value, char *buf, size_t cap);

// "2026-11-12T00:00:00+08:00" → "00:00"。找不到 'T'、时:分不是两位数字时返回 false（buf 清空）。
// 不转换时区：偏移量可能是任何值，硬编码 +08:00 才是错的；文档口径是北京时间。
bool usage_format_reset_clock(const char *iso, char *buf, size_t cap);
