// main/quote_view.c —— 行情页状态与文案的纯逻辑实现。契约见 quote_view.h。
#include "quote_view.h"

#include <stdio.h>

// 页面文案一律 ASCII：Montserrat 没有中文字形，中文只出现在源码注释里
// （见 docs/development/engineering/coding-conventions.md 的字体清单）。

quote_view_state_t quote_view_state(bool connected, bool time_valid, bool has_data,
                                    bool waited_long) {
    // 顺序即优先级。有数据就画数据：失败只改状态行，不把已经取到的行情藏起来——
    // 与 usage_view 第 1 条规则、天气页"离线仍显示快照"是同一条规矩。
    if (has_data) return QUOTE_VIEW_DATA;

    // 以下都是"还没有数据"。本机事实先于等待时长：没网时谈校时没有意义，
    // 没校时则连 HTTPS 都发不出去（证书有效期校验会失败）。
    if (!connected) return QUOTE_VIEW_NO_NET;
    if (!time_valid) return QUOTE_VIEW_WAIT_TIME;

    // 有网、已校时，剩下只是"刚发起"还是"等太久"。
    return waited_long ? QUOTE_VIEW_RETRYING : QUOTE_VIEW_UPDATING;
}

const char *quote_view_message(quote_view_state_t state) {
    switch (state) {
    case QUOTE_VIEW_DATA:      return NULL;
    case QUOTE_VIEW_NO_NET:    return "No network";
    case QUOTE_VIEW_WAIT_TIME: return "Waiting for time";
    case QUOTE_VIEW_UPDATING:  return "Updating...";
    case QUOTE_VIEW_RETRYING:  return "No data - retrying";
    case QUOTE_VIEW_BAD_BODY:  return "Bad data";
    }
    return NULL;
}

bool quote_view_row(const quote_item_t *item, const char *code, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (item == NULL || !item->valid || code == NULL) {
        return false;
    }

    // 定点数到字符串的转换全部复用 quote_source，不在 view 层重做一遍。
    char price[32], pct[24], change[32], amount[32];
    if (!quote_format_price(item->price_1e4, price, sizeof price) ||
        !quote_format_pct(item->pct_1e4, pct, sizeof pct) ||
        !quote_format_price(item->change_1e4, change, sizeof change) ||
        !quote_format_amount(code, item->amount, amount, sizeof amount)) {
        return false;
    }

    // 两个空格分隔：204 px 的一行里不再写字段名，只留数值与单位。
    const int n = snprintf(buf, cap, "%s  %s  %s  %s  %s", item->label, price, pct, change,
                           amount);
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool quote_view_status_line(quote_view_state_t state, quote_status_t status, int64_t age_ms,
                            int32_t fetched_epoch, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';

    // 签名与页面侧其他 view 保持一致：status 是最近一次请求结局，fetched_epoch
    // 是快照时刻。行情不做持久化，两者都不参与这句话；按契约，有数据时状态行
    // 只补充"这份行情有多旧"。
    (void)status;
    (void)fetched_epoch;

    if (state != QUOTE_VIEW_DATA) {
        // 没有数据时，状态行就是大字占位文案本身。
        const char *message = quote_view_message(state);
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

    // 有数据：与 weather_view_updated 同一套分钟/小时分档，只是这里即使
    // 不知道时间也报一个前缀，不让整行消失。
    int n;
    if (age_ms < 0) {
        n = snprintf(buf, cap, "Updated");
    } else {
        const int64_t minutes = age_ms / 60000;
        if (minutes < 1) {
            n = snprintf(buf, cap, "Updated just now");
        } else if (minutes <= 90) {
            n = snprintf(buf, cap, "Updated %d min ago", (int)minutes);
        } else {
            n = snprintf(buf, cap, "Updated %d h ago", (int)(minutes / 60));
        }
    }
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}
