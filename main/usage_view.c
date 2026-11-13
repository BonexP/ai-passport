// main/usage_view.c —— 用量页状态与文案的纯逻辑实现。契约见 usage_view.h。
#include "usage_view.h"

#include <stdio.h>

#include "app_clock.h"

// 页面文案一律 ASCII：Montserrat 没有中文字形，中文只出现在源码注释里
// （见 docs/development/engineering/coding-conventions.md 的字体清单）。
// 单位固定 "pts"，永不 "$"：_usd 后缀里的数值是 Token Factory points，
// 文档两处明确说明，与账户额度无关。

usage_view_state_t usage_view_state(bool has_key, bool connected, bool time_valid, bool has_data,
                                    usage_status_t status, bool waited_long) {
    // 有数据就画数据。失败只改状态行，不把已经取到的点数藏起来——
    // 与天气页"离线仍显示快照"是同一条规矩。
    if (has_data) return USAGE_VIEW_DATA;

    // 以下都是"还没有数据"，按确定性从高到低排：
    // 本机的构建形态 → 本机的校时 → 远端给出的明确结论 → 远端猜测。
    if (!has_key) return USAGE_VIEW_NO_KEY;

    if (status == USAGE_STATUS_KEY_REJECTED) return USAGE_VIEW_KEY_REJECTED;
    if (status == USAGE_STATUS_RATE_LIMITED) return USAGE_VIEW_RATE_LIMITED;
    if (status == USAGE_STATUS_BAD_BODY) return USAGE_VIEW_BAD_BODY;
    if (status == USAGE_STATUS_TRUNCATED) return USAGE_VIEW_BAD_BODY;

    if (!connected) return USAGE_VIEW_UNAVAILABLE;

    // 校时未完成时发不出 HTTPS（证书有效期校验会失败），所以这一支排在
    // "服务不可用"之前：它是本机状态，不是对方的错。
    if (!time_valid) return USAGE_VIEW_WAIT_TIME;

    if (status == USAGE_STATUS_UNAVAILABLE) return USAGE_VIEW_UNAVAILABLE;

    return waited_long ? USAGE_VIEW_RETRYING : USAGE_VIEW_UPDATING;
}

const char *usage_view_message(usage_view_state_t state) {
    switch (state) {
    case USAGE_VIEW_DATA:         return NULL;
    case USAGE_VIEW_NO_KEY:       return "Key not configured";
    case USAGE_VIEW_KEY_REJECTED: return "Key rejected";
    case USAGE_VIEW_RATE_LIMITED: return "Rate limited";
    case USAGE_VIEW_UNAVAILABLE:  return "No network";
    case USAGE_VIEW_WAIT_TIME:    return "Waiting for time";
    case USAGE_VIEW_UPDATING:     return "Updating...";
    case USAGE_VIEW_RETRYING:     return "No data - retrying";
    case USAGE_VIEW_BAD_BODY:     return "Bad response";
    }
    return NULL;
}

// 有数据时状态行描述"这份数据有多旧"。与天气页的区别：这里永远显示一行，
// 因为"点数是什么时候的"比"温度是什么时候的"更要紧——额度每天清零。
bool usage_view_status_line(usage_view_state_t state, usage_status_t status, bool stale,
                            int64_t age_ms, int32_t fetched_epoch, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';

    if (state != USAGE_VIEW_DATA) {
        // 没有数据时，状态行就是状态文案本身；NO_KEY 之类也有话说。
        const char *message = usage_view_message(state);
        if (message == NULL) {
            return false;
        }
        const int n = snprintf(buf, cap, "%s", message);
        if (n < 0 || (size_t)n >= cap) {
            buf[0] = '\0';
            return false;
        }
        return true;
    }

    // 以下都有数据：先报"这次请求怎么了"，再报数据时间。
    const char *prefix = NULL;
    switch (status) {
    case USAGE_STATUS_OK:           prefix = NULL; break;
    case USAGE_STATUS_KEY_REJECTED: prefix = "Key rejected"; break;
    case USAGE_STATUS_RATE_LIMITED: prefix = "Rate limited"; break;
    case USAGE_STATUS_UNAVAILABLE:  prefix = "Offline"; break;
    case USAGE_STATUS_TRUNCATED:    prefix = "Bad response"; break;
    case USAGE_STATUS_BAD_BODY:     prefix = "Bad response"; break;
    case USAGE_STATUS_NO_KEY:       prefix = "Key not configured"; break;
    case USAGE_STATUS_NONE:         prefix = NULL; break;
    default:                        prefix = NULL; break;
    }

    char when[40];
    bool has_when = false;
    bool when_is_phrase = false; // "just now" / "Offline - saved" 已自带说法，不再加 "Updated"
    if (stale) {
        // 跨重启的相对时间没有意义，只能用快照里存的绝对时刻。
        has_when = app_clock_format_utc(fetched_epoch, when, sizeof when);
        if (!has_when) {
            snprintf(when, sizeof when, "Offline - saved");
            has_when = true;
            when_is_phrase = true;
        }
    } else if (age_ms >= 0) {
        const int64_t minutes = age_ms / 60000;
        if (minutes < 1) {
            snprintf(when, sizeof when, "just now");
            when_is_phrase = true;
        } else if (minutes <= 90) {
            snprintf(when, sizeof when, "%d min ago", (int)minutes);
        } else {
            snprintf(when, sizeof when, "%d h ago", (int)(minutes / 60));
        }
        has_when = true;
    }

    int written = 0;
    if (prefix != NULL && has_when) {
        written = snprintf(buf, cap, "%s - %s", prefix, when);
    } else if (prefix != NULL) {
        written = snprintf(buf, cap, "%s", prefix);
    } else if (has_when && when_is_phrase) {
        // "just now" 本身就不是"多久前"的通用说法，单独成句更自然。
        written = snprintf(buf, cap, "%s", when);
    } else if (has_when) {
        written = snprintf(buf, cap, "Updated %s", when);
    } else {
        return false; // 什么都不知道，就不编一行字
    }

    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool usage_view_remaining(usage_view_state_t state, const usage_state_t *usage, char *buf,
                          size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (state != USAGE_VIEW_DATA || usage == NULL || !usage->has_data) {
        return false;
    }
    // remaining 是接口独立给出的字段，即使 limit 缺失也照常显示。
    return usage_format_points(usage->remaining_1e4, buf, cap);
}

bool usage_view_used_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                          size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (state != USAGE_VIEW_DATA || usage == NULL || !usage->has_data) {
        return false;
    }

    char used[24];
    if (!usage_format_points(usage->used_1e4, used, sizeof used)) {
        return false;
    }

    // 额度未知时只说用掉多少。绝不能把"未知"画成"0.0000 额度"，
    // 那会变成一条 100% 用满的红条——一句谎话。
    if (!usage->has_limit) {
        const int written = snprintf(buf, cap, "%s used", used);
        return written >= 0 && (size_t)written < cap;
    }

    char limit[24];
    if (!usage_format_points(usage->limit_1e4, limit, sizeof limit)) {
        return false;
    }
    const int written = snprintf(buf, cap, "%s / %s used", used, limit);
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

int usage_view_used_permille(usage_view_state_t state, const usage_state_t *usage) {
    if (state != USAGE_VIEW_DATA || usage == NULL || !usage->has_data) {
        return -1;
    }
    // 额度未知 → 没有比例可言。额度为 0 → 也不算比例：除零没有意义，
    // 而"额度 0"本身更应该单独说明（v1 不显示进度条）。
    if (!usage->has_limit || usage->limit_1e4 <= 0) {
        return -1;
    }

    // 用整数算，不绕回浮点：两个值都是万分位，比例就是它们的商。
    const int64_t permille =
        ((int64_t)usage->used_1e4 * 1000 + usage->limit_1e4 / 2) / usage->limit_1e4;

    if (permille < 0) return 0;
    if (permille > 1000) return 1000; // 超额使用按满格画，不画到框外
    return (int)permille;
}

bool usage_view_reset_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                           size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (state != USAGE_VIEW_DATA || usage == NULL || !usage->has_data) {
        return false;
    }

    char clock[8];
    if (!usage_format_reset_clock(usage->reset_at, clock, sizeof clock)) {
        return false; // 接口没给重置时刻：这一行不显示，不编 "00:00"
    }

    const int written = snprintf(buf, cap, "Reset %s", clock);
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool usage_view_today_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                           size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (state != USAGE_VIEW_DATA || usage == NULL || !usage->has_data) {
        return false;
    }

    char tokens[16];
    if (!usage_format_compact(usage->today_total_tokens, tokens, sizeof tokens)) {
        return false;
    }

    // 三个数字挤在 204 px 一行里：req / tok / err 各留一个词，
    // 单位用缩写，页面字体是 14 px 不会换行。
    const int written = snprintf(buf, cap, "Today %ld req  %s tok  %ld err",
                                 (long)usage->today_requests, tokens,
                                 (long)usage->today_errors);
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool usage_view_30d_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                         size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (state != USAGE_VIEW_DATA || usage == NULL || !usage->has_data) {
        return false;
    }
    // 报文没给 last_30_days.cost：不显示这一行，也不编一个 0。
    if (!usage->has_days30) {
        return false;
    }

    char points[24];
    if (!usage_format_points(usage->days30_cost_1e4, points, sizeof points)) {
        return false;
    }

    const int written = snprintf(buf, cap, "30d %s pts", points);
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}
