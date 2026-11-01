// main/app_location.c —— 地点模型的实现。对外契约见 app_location.h。
#include "app_location.h"

#include <stdio.h>
#include <string.h>

// 只在 place 缓冲区内找结尾，避免被坏数据带出边界。
static size_t bounded_strlen(const char *s, size_t cap) {
    size_t n = 0;

    if (s == NULL) {
        return 0;
    }
    while (n < cap && s[n] != '\0') {
        n++;
    }
    return n;
}

// 控制字符（含 NUL）会把一行文本变成两段，或被终端/网页当成指令，一律拒绝。
static bool place_chars_valid(const char *place, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)place[i];

        if (c < 0x20u || c == 0x7Fu) {
            return false;
        }
    }
    return true;
}

void app_location_clear(app_location_t *loc) {
    if (loc != NULL) {
        memset(loc, 0, sizeof *loc);
    }
}

bool app_location_set(app_location_t *loc, int32_t lat_udeg, int32_t lon_udeg, const char *place) {
    if (loc == NULL) {
        return false;
    }

    // 先清空：任何一条校验失败都必须表现为"未配置"，而不是半条地点。
    app_location_clear(loc);

    if (lat_udeg < -APP_LOCATION_LAT_UDEG_MAX || lat_udeg > APP_LOCATION_LAT_UDEG_MAX) {
        return false;
    }
    if (lon_udeg < -APP_LOCATION_LON_UDEG_MAX || lon_udeg > APP_LOCATION_LON_UDEG_MAX) {
        return false;
    }

    size_t place_len = place != NULL ? strlen(place) : 0;
    if (place_len >= sizeof loc->place) {
        return false;
    }
    if (place != NULL && !place_chars_valid(place, place_len)) {
        return false;
    }

    loc->has_location = true;
    loc->lat_udeg = lat_udeg;
    loc->lon_udeg = lon_udeg;
    if (place_len > 0) {
        memcpy(loc->place, place, place_len);
    }
    return true;
}

bool app_location_is_valid(const app_location_t *loc) {
    if (loc == NULL) {
        return false;
    }

    size_t place_len = bounded_strlen(loc->place, sizeof loc->place);

    if (!loc->has_location) {
        // 未配置却带着地点名，说明某处只改了一半，按不自洽处理。
        return place_len == 0;
    }

    if (place_len >= sizeof loc->place) {
        return false; // 缓冲区内没有结尾
    }
    if (!place_chars_valid(loc->place, place_len)) {
        return false;
    }
    if (loc->lat_udeg < -APP_LOCATION_LAT_UDEG_MAX || loc->lat_udeg > APP_LOCATION_LAT_UDEG_MAX) {
        return false;
    }
    if (loc->lon_udeg < -APP_LOCATION_LON_UDEG_MAX || loc->lon_udeg > APP_LOCATION_LON_UDEG_MAX) {
        return false;
    }
    return true;
}

bool app_location_parse_coord(const char *text, int32_t limit_udeg, int32_t *out_udeg) {
    if (text == NULL || out_udeg == NULL || limit_udeg <= 0) {
        return false;
    }

    const char *p = text;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = (*p == '-');
        p++;
    }

    // 整数部分：至少一位。9 位上限保证 int64 累乘不溢出，越界交给后面的范围检查。
    int64_t whole = 0;
    size_t whole_digits = 0;
    while (*p >= '0' && *p <= '9') {
        if (whole_digits >= 9) {
            return false;
        }
        whole = whole * 10 + (*p - '0');
        whole_digits++;
        p++;
    }
    if (whole_digits == 0) {
        return false;
    }

    // 小数部分：可选。最多取前 6 位，多余的位仍然要消费掉（否则会被当成非法字符）。
    int64_t frac = 0;
    size_t frac_digits = 0;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (frac_digits < 6) {
                frac = frac * 10 + (*p - '0');
                frac_digits++;
            }
            p++;
        }
        if (frac_digits == 0) {
            return false; // "12." 不是合法数字
        }
    }

    // 尾随空白允许，其他任何字符都算非法（包括第二个小数点、指数、逗号）。
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '\0') {
        return false;
    }

    // 补齐到 6 位小数。
    while (frac_digits < 6) {
        frac *= 10;
        frac_digits++;
    }

    int64_t value = whole * 1000000 + frac;
    if (negative) {
        value = -value;
    }
    if (value > limit_udeg || value < -limit_udeg) {
        return false;
    }

    *out_udeg = (int32_t)value;
    return true;
}

bool app_location_format_4dp(int32_t udeg, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }

    // 截断到 4 位小数（向零），避免四舍五入把 89.99999 变成 90.0000 之外的意外。
    int64_t scaled = udeg / 100;
    bool negative = scaled < 0;
    int64_t magnitude = negative ? -scaled : scaled;
    long whole = (long)(magnitude / 10000);
    long frac = (long)(magnitude % 10000);

    int written = snprintf(buf, cap, "%s%ld.%04ld", negative ? "-" : "", whole, frac);
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}
