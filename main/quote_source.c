// main/quote_source.c —— 行情请求组装、标的映射与报文解析。契约见 quote_source.h。
#include "quote_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 腾讯行情端点。无 key、无鉴权；响应是 GBK 文本，本模块只按字节定位 '~' 字段，
// 名称字段不进显示缓冲，因此不需要编码转换。
static const char QUOTE_URL[] = "https://qt.gtimg.cn/q=";

// 把 symbol 映射到腾讯接口代码。映射规则见 quote_source.h 的说明：
//   - A 股指数/ETF：6 位数字，按交易所加 sh/sz 前缀；
//   - 美股：1-5 位字母，加 us 前缀；
//   - 默认值里两个无 `~` 格式数据的 symbol 用可解析的近似标的替代。
bool quote_symbol_to_code(const char *symbol, char *out, size_t cap) {
    if (symbol == NULL || out == NULL || cap == 0) {
        return false;
    }
    out[0] = '\0';
    if (symbol[0] == '\0') {
        return false;
    }

    const char *prefix = NULL;
    if (strcmp(symbol, "930713") == 0) {
        // 中证 AI 指数在腾讯无 `~` 数据，退到上证指数作大盘参考。
        prefix = "sh000001";
    } else if (strcmp(symbol, "XAU") == 0) {
        // 伦敦金是逗号分隔格式，退到黄金 ETF（美元，可解析）。
        prefix = "usGLD";
    } else {
        const size_t len = strlen(symbol);
        bool all_digits = true;
        for (size_t i = 0; i < len; i++) {
            if (symbol[i] < '0' || symbol[i] > '9') {
                all_digits = false;
                break;
            }
        }
        if (all_digits && len == 6) {
            // A 股 6 位代码：6/5/9 开头多为沪市，0/3 开头为深市。
            const char *ex = (symbol[0] == '6' || symbol[0] == '5' || symbol[0] == '9')
                                 ? "sh"
                                 : "sz";
            if (cap < strlen(ex) + len + 1) {
                return false;
            }
            int n = snprintf(out, cap, "%s%s", ex, symbol);
            return n > 0 && (size_t)n < cap;
        }
        // 其余按美股处理（字母代码）。
        prefix = "us";
        if (cap < strlen(prefix) + len + 1) {
            return false;
        }
        int n = snprintf(out, cap, "%s%s", prefix, symbol);
        return n > 0 && (size_t)n < cap;
    }

    if (prefix == NULL) {
        return false;
    }
    const size_t n = strlen(prefix);
    if (cap < n + 1) {
        return false;
    }
    memcpy(out, prefix, n + 1);
    return true;
}

bool quote_url_build(char **codes, size_t count, char *buf, size_t cap) {
    if (codes == NULL || buf == NULL || cap == 0 || count == 0 || count > QUOTE_COUNT_MAX) {
        if (buf != NULL && cap > 0) {
            buf[0] = '\0';
        }
        return false;
    }

    size_t used = 0;
    const size_t base = strlen(QUOTE_URL);
    if (cap <= base) {
        buf[0] = '\0';
        return false;
    }
    memcpy(buf, QUOTE_URL, base);
    used = base;
    buf[used] = '\0';

    for (size_t i = 0; i < count; i++) {
        const char *code = codes[i];
        if (code == NULL) {
            buf[0] = '\0';
            return false;
        }
        const size_t clen = strlen(code);
        // +1 for the comma between codes.
        if (used + clen + (i > 0 ? 1u : 0u) + 1u > cap) {
            buf[0] = '\0';
            return false;
        }
        if (i > 0) {
            buf[used++] = ',';
        }
        memcpy(buf + used, code, clen);
        used += clen;
        buf[used] = '\0';
    }
    return true;
}

// 解析一个形如 v_sh000001="1~name~code~..." 的记录。
// 返回该记录里的 '~' 字段数（>=0 表示找到引号内的载荷），失败返回 -1。
// payload 指向第一个字段的起始，len 为引号内字节数。
static int extract_payload(const char *body, const char *code, const char **payload,
                           size_t *payload_len) {
    if (code == NULL) {
        return -1;
    }
    // 记录名形如 v_sh000001= 或 v_usNVDA=；code 已含前缀。
    char key[QUOTE_SYMBOL_MAX + 4];
    const int kn = snprintf(key, sizeof key, "v_%s=", code);
    if (kn <= 0 || (size_t)kn >= sizeof key) {
        return -1;
    }
    const char *start = strstr(body, key);
    if (start == NULL) {
        return -1;
    }
    const char *q1 = strchr(start + kn, '"');
    if (q1 == NULL) {
        return -1;
    }
    const char *q2 = strchr(q1 + 1, '"');
    if (q2 == NULL) {
        return -1;
    }
    *payload = q1 + 1;
    *payload_len = (size_t)(q2 - (q1 + 1));
    return (int)(*payload_len);
}

// 把 '~' 分隔的第 index 个字段复制到 out。字段不存在或过长返回 false。
// 用「找第 index 个 '~' 的下一个字节」的写法，与 strsep/split 的语义一致，
// 避免边界计数错误（payload 的末尾不一定有分隔符）。
static bool field_at(const char *payload, size_t len, int index, char *out, size_t cap) {
    if (payload == NULL || out == NULL || cap == 0) {
        return false;
    }
    const char *end = payload + len;
    const char *p = payload;
    int idx = 0;
    const char *field_start = payload;
    // 与 strsep/split 完全一致：逐字节前进，见到 '~' 就结算当前字段。
    for (const char *c = payload; c <= end; c++) {
        if (c < end && *c != '~') {
            continue;
        }
        if (idx == index) {
            field_start = p;
            const size_t flen = (size_t)(c - p);
            if (flen == 0 || flen >= cap) {
                return false;
            }
            memcpy(out, field_start, flen);
            out[flen] = '\0';
            return true;
        }
        idx++;
        p = c + 1;
    }
    return false;
}

// 宽松的十进制浮点数解析：只接受可选 '-' + 数字 + '.' + 数字，
// 不依赖 strtod 的 locale，固件与主机行为一致。失败返回 false。
static bool parse_fixed(const char *s, int64_t scale, int64_t *out) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    bool neg = false;
    if (*s == '-' || *s == '+') {
        neg = (*s == '-');
        s++;
    }
    int64_t whole = 0;
    int64_t frac = 0;
    int frac_digits = 0;
    bool in_frac = false;
    bool seen_digit = false;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == '.') {
            if (in_frac) {
                return false; // 第二个小数点
            }
            in_frac = true;
            continue;
        }
        if (*p < '0' || *p > '9') {
            return false;
        }
        seen_digit = true;
        const int d = *p - '0';
        if (in_frac) {
            if (frac_digits < 4) {
                frac = frac * 10 + d; // 只保留前 4 位小数（1e-4 精度）
            }
            frac_digits++;
        } else {
            whole = whole * 10 + d;
        }
    }
    if (!seen_digit) {
        return false;
    }
    // 小数不足 4 位时右补零：1.3 → 1.3000 → 13000。
    while (frac_digits < 4) {
        frac *= 10;
        frac_digits++;
    }
    (void)scale;
    const int64_t magnitude = whole * 10000 + frac;
    *out = neg ? -magnitude : magnitude;
    return true;
}

bool quote_parse(const char *body, char **codes, char **labels, size_t count,
                 quote_state_t *out) {
    if (body == NULL || out == NULL || count == 0 || count > QUOTE_COUNT_MAX ||
        codes == NULL || labels == NULL) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->has_data = false;

    for (size_t i = 0; i < count; i++) {
        quote_item_t *item = &out->items[out->count];
        memset(item, 0, sizeof *item);
        if (labels[i] != NULL) {
            const size_t n = strlen(labels[i]);
            if (n < sizeof item->label) {
                memcpy(item->label, labels[i], n + 1);
            }
        }
        if (codes[i] != NULL) {
            const size_t n = strlen(codes[i]);
            if (n < sizeof item->code) {
                memcpy(item->code, codes[i], n + 1);
            }
        }

        const char *payload = NULL;
        size_t len = 0;
        if (extract_payload(body, codes[i], &payload, &len) < 0) {
            item->valid = false;
            continue;
        }

        char f_price[32], f_change[32], f_pct[32], f_amount[48];
        if (!field_at(payload, len, 3, f_price, sizeof f_price) ||
            !field_at(payload, len, 31, f_change, sizeof f_change) ||
            !field_at(payload, len, 32, f_pct, sizeof f_pct) ||
            !field_at(payload, len, 37, f_amount, sizeof f_amount)) {
            item->valid = false;
            continue;
        }
        if (!parse_fixed(f_price, 10000, &item->price_1e4) ||
            !parse_fixed(f_change, 10000, &item->change_1e4) ||
            !parse_fixed(f_pct, 10000, &item->pct_1e4)) {
            item->valid = false;
            continue;
        }
        const size_t an = strlen(f_amount);
        if (an >= sizeof item->amount) {
            item->valid = false;
            continue;
        }
        memcpy(item->amount, f_amount, an + 1);
        item->valid = true;
        out->count++;
    }
    out->has_data = (out->count > 0);
    return true;
}

bool quote_format_price(int64_t price_1e4, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    const bool neg = price_1e4 < 0;
    const int64_t m = neg ? -price_1e4 : price_1e4;
    const int n = snprintf(buf, cap, "%s%lld.%04lld", neg ? "-" : "",
                           (long long)(m / 10000), (long long)(m % 10000));
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool quote_format_pct(int64_t pct_1e4, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    const bool neg = pct_1e4 < 0;
    const int64_t m = neg ? -pct_1e4 : pct_1e4;
    const int n = snprintf(buf, cap, "%s%lld.%02lld%%", neg ? "-" : "",
                           (long long)(m / 10000), (long long)((m % 10000) / 100));
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool quote_format_amount(const char *code, const char *amount_raw, char *buf, size_t cap) {
    if (code == NULL || amount_raw == NULL || buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    // 只接受纯整数原串。
    int64_t value = 0;
    for (const char *p = amount_raw; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + (*p - '0');
    }
    const bool is_us = (strncmp(code, "us", 2) == 0);
    int n;
    if (is_us) {
        // 美元：原串即美元。按 B/M/k 缩写，带 '$' 前缀。
        if (value >= 1000000000) {
            n = snprintf(buf, cap, "$%lld.%lldB", (long long)(value / 1000000000),
                         (long long)((value / 100000000) % 10));
        } else if (value >= 1000000) {
            n = snprintf(buf, cap, "$%lld.%lldM", (long long)(value / 1000000),
                         (long long)((value / 100000) % 10));
        } else if (value >= 1000) {
            n = snprintf(buf, cap, "$%lld.%lldk", (long long)(value / 1000),
                         (long long)((value / 100) % 10));
        } else {
            n = snprintf(buf, cap, "$%lld", (long long)value);
        }
    } else {
        // A 股/ETF：原串单位是万元 → ×10000 得元，再按 亿/万 缩写。
        const int64_t yuan = value * 10000;
        if (yuan >= 100000000) {
            n = snprintf(buf, cap, "%lld.%lld亿", (long long)(yuan / 100000000),
                         (long long)((yuan / 10000000) % 10));
        } else {
            n = snprintf(buf, cap, "%lld.%lld万", (long long)(yuan / 10000),
                         (long long)((yuan / 1000) % 10));
        }
    }
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}
