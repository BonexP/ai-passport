// main/usage_source.c —— AMD Radeon Cloud 用量请求的实现。契约见 usage_source.h。
#include "usage_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_field.h"

// 端点与网关前缀硬编码在这里，不拼进任何用户输入。
// include_recent 不传：默认 false，recent 是报文里唯一无上界的部分。
static const char USAGE_URL[] =
    "https://developer.amd.com.cn/radeon/api/v1/usage";

// 万分位的十进制缩放。点数到 4 位小数。
#define POINTS_SCALE 10000.0

bool usage_url_build(char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (cap < sizeof USAGE_URL) {
        return false;
    }
    memcpy(buf, USAGE_URL, sizeof USAGE_URL);
    return true;
}

static bool points_in_range(double raw, int32_t *out) {
    if (raw < 0.0 || raw > (double)INT32_MAX / POINTS_SCALE) {
        return false;
    }
    *out = (int32_t)(raw * POINTS_SCALE + 0.5);
    return true;
}

static bool read_points(const char *doc, const char *path, int32_t *out) {
    double raw = 0.0;
    if (!json_field_number(doc, path, &raw)) {
        return false;
    }
    return points_in_range(raw, out);
}

// limit 可选：缺失 / null / 类型不符（如字符串）→ 未知（*found = false），不是 0 额度；
// 但数值越界（含 NaN/Inf，json_field 已挡）→ 整报文失败：单位可能不是 points。
static bool read_points_opt(const char *doc, const char *path, bool *found, int32_t *out) {
    double raw = 0.0;
    if (!json_field_number(doc, path, &raw)) {
        *found = false;
        *out = 0;
        return true;
    }
    if (!points_in_range(raw, out)) {
        return false;
    }
    *found = true;
    return true;
}

static bool read_count32(const char *doc, const char *path, int32_t *out) {
    double raw = 0.0;
    if (!json_field_number(doc, path, &raw)) {
        return false;
    }
    if (raw < 0.0 || raw > (double)USAGE_COUNT_LIMIT) {
        return false;
    }
    *out = (int32_t)(raw + 0.5);
    return true;
}

static bool read_count64(const char *doc, const char *path, int64_t *out) {
    double raw = 0.0;
    if (!json_field_number(doc, path, &raw)) {
        return false;
    }
    if (raw < 0.0 || raw > (double)USAGE_COUNT_LIMIT) {
        return false;
    }
    *out = (int64_t)(raw + 0.5);
    return true;
}

bool usage_parse(const char *json, usage_state_t *out) {
    if (json == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof *out);

    int32_t limit_1e4 = 0;
    bool limit_found = false;
    if (!read_points_opt(json, "daily_cost_limit_usd", &limit_found, &limit_1e4)) {
        return false;
    }
    out->has_limit = limit_found;
    out->limit_1e4 = limit_1e4;

    if (!read_points(json, "daily_cost_used_usd", &out->used_1e4)) {
        return false;
    }
    if (!read_points(json, "daily_cost_remaining_usd", &out->remaining_1e4)) {
        return false;
    }

    if (!read_count32(json, "today.requests", &out->today_requests)) {
        return false;
    }
    if (!read_count32(json, "today.errors", &out->today_errors)) {
        return false;
    }
    if (!read_count64(json, "today.total_tokens", &out->today_total_tokens)) {
        return false;
    }

    (void)json_field_string(json, "daily_reset_at", out->reset_at, sizeof out->reset_at);

    out->has_data = true;
    return true;
}

bool usage_format_points(int32_t points_1e4, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';

    const int64_t value = points_1e4;
    const bool negative = value < 0;
    const int64_t magnitude = negative ? -value : value;

    const int written = snprintf(buf, cap, "%s%lld.%04lld", negative ? "-" : "",
                                 (long long)(magnitude / 10000),
                                 (long long)(magnitude % 10000));
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool usage_format_compact(int64_t value, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';

    if (value < 0) {
        value = 0;
    }

    int written = 0;
    if (value < 1000000) {
        written = snprintf(buf, cap, "%lld", (long long)value);
    } else if (value < 1000000000) {
        // 四舍五入到 0.1 M，再拆整数位与小数位。
        const long long tenths = (value + 50000) / 100000;
        written = snprintf(buf, cap, "%lld.%lldM", tenths / 10, tenths % 10);
    } else {
        written = snprintf(buf, cap, "1.0B+");
    }
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool usage_format_reset_clock(const char *iso, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (iso == NULL) {
        return false;
    }

    const char *t = strchr(iso, 'T');
    if (t == NULL) {
        return false;
    }
    if (t[1] < '0' || t[1] > '9' || t[2] < '0' || t[2] > '9') {
        return false;
    }
    if (t[3] != ':' || t[4] < '0' || t[4] > '9' || t[5] < '0' || t[5] > '9') {
        return false;
    }

    if (cap < 6) {
        return false;
    }
    memcpy(buf, t + 1, 5);
    buf[5] = '\0';
    return true;
}
