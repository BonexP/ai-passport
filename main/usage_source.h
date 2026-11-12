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

// 每天额度上限是"每账户、随账户变化"的运行时值：页面上限 32 位足够，
// 用 int32_t 表示并允许"未知"（INT32_MIN）。绝不编译进默认值。
#define USAGE_POINTS_UNKNOWN INT32_MIN

// 当天请求/错误/token 计数的上界。超出即认为报文不对劲，判解析失败而不是截断显示。
#define USAGE_COUNT_LIMIT 1000000000ll // 1e9

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
// daily_cost_limit_usd 缺失时 has_limit=false，其余字段照常解析。
bool usage_parse(const char *json, usage_state_t *out);

// points（万分位整数）→ "3.2500" / "0.0032" / "1234.5678"。缓冲不足返回 false 并置空串。
bool usage_format_points(int32_t points_1e4, char *buf, size_t cap);

// 大数的紧凑显示：2100 → "2100"，1234567 → "1.2M"，88200123 → "88.2M"，999999999 → "1.0B"。
// 只用于 token 计数这类不需要精确到个位的行。负值按 0 处理。永不输出 "$"。
bool usage_format_compact(int64_t value, char *buf, size_t cap);

// "2026-11-12T00:00:00+08:00" → "00:00"。找不到 'T'、时:分不是两位数字时返回 false（buf 清空）。
// 不转换时区：偏移量可能是任何值，硬编码 +08:00 才是错的；文档口径是北京时间。
bool usage_format_reset_clock(const char *iso, char *buf, size_t cap);
