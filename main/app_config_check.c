// main/app_config_check.c —— 配置自检的实现。契约见 app_config_check.h。
#include "app_config_check.h"

#include <string.h>

#include "app_location.h"
#include "wifi_profile.h"

// 最多保留多少个"前面已经合法的键"用于查重。配置本身不会超过这两个上界，
// 留一倍余量避免坏配置把栈撑大。
#define CHECK_SEEN_MAX 32

// 自选表的单字段上限：symbol 用于请求，label 用于屏幕。
#define CHECK_SYMBOL_MAX 16
#define CHECK_LABEL_MAX 24

static void record(app_config_issue_t *issues, size_t cap, size_t *count,
                   app_config_issue_code_t code, size_t index) {
    if (issues != NULL && *count < cap) {
        issues[*count].code = code;
        issues[*count].index = index;
    }
    (*count)++;
}

static size_t bounded_strlen(const char *s, size_t cap) {
    size_t n = 0;

    if (s == NULL) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

// 只允许可打印 ASCII。本阶段 UI 没有中文字形，提前挡住比烧录后看到方框好。
static bool printable_ascii(const char *s) {
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20u || c > 0x7Eu) return false;
    }
    return true;
}

static bool quote_fields_valid(const app_config_quote_t *q) {
    if (q == NULL || q->symbol == NULL || q->label == NULL) return false;

    size_t symbol_len = bounded_strlen(q->symbol, CHECK_SYMBOL_MAX);
    size_t label_len = bounded_strlen(q->label, CHECK_LABEL_MAX);
    if (symbol_len == 0 || symbol_len >= CHECK_SYMBOL_MAX) return false;
    if (label_len == 0 || label_len >= CHECK_LABEL_MAX) return false;

    if (!printable_ascii(q->symbol) || !printable_ascii(q->label)) return false;

    // 请求参数里带空格几乎一定是把显示名和代码写反了。
    if (strchr(q->symbol, ' ') != NULL) return false;
    return true;
}

// key 的长度上限。Radeon Cloud 的 key 形如 "rc-" + 一串字母数字，几十字节；
// 128 给足余量，超过它几乎一定是把别的东西粘进来了。
#define CHECK_AMD_KEY_MAX 128

bool app_config_amd_key_valid(const char *key) {
    if (key == NULL) {
        return true; // 没配是合法形态：页面显示 Key not configured
    }
    size_t n = bounded_strlen(key, CHECK_AMD_KEY_MAX);
    if (n == 0 || n >= CHECK_AMD_KEY_MAX) {
        return false;
    }
    return printable_ascii(key);
}

size_t app_config_check(const app_config_wifi_t *wifi, size_t wifi_count,
                        const app_config_quote_t *quotes, size_t quote_count,
                        app_config_issue_t *issues, size_t cap) {
    size_t found = 0;

    // ---- Wi-Fi ----
    if (wifi_count > WIFI_PROFILE_MAX) {
        record(issues, cap, &found, APP_CONFIG_ISSUE_WIFI_OVERFLOW, WIFI_PROFILE_MAX);
    }

    const char *seen_ssid[CHECK_SEEN_MAX];
    size_t seen_count = 0;

    for (size_t i = 0; wifi != NULL && i < wifi_count; i++) {
        const app_config_wifi_t *w = &wifi[i];
        wifi_profile_t probe;

        // 字段校验复用 store 的规则：上限只有一份。
        if (!wifi_profile_set_fields(&probe, w->name, w->ssid, w->password)) {
            record(issues, cap, &found, APP_CONFIG_ISSUE_WIFI_FIELDS, i);
            continue;
        }

        // 地点非法不阻止网络可用：与注入时的降级行为一致。
        if (w->has_location) {
            app_location_t loc;
            if (!app_location_set(&loc, w->lat_udeg, w->lon_udeg, w->place)) {
                record(issues, cap, &found, APP_CONFIG_ISSUE_WIFI_LOCATION, i);
            }
        }

        bool duplicate = false;
        for (size_t k = 0; k < seen_count; k++) {
            if (strcmp(seen_ssid[k], w->ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            record(issues, cap, &found, APP_CONFIG_ISSUE_WIFI_DUPLICATE, i);
            continue;
        }
        if (seen_count < CHECK_SEEN_MAX) {
            seen_ssid[seen_count++] = w->ssid;
        }
    }

    // ---- 自选表 ----
    if (quote_count > APP_CONFIG_QUOTE_MAX) {
        record(issues, cap, &found, APP_CONFIG_ISSUE_QUOTE_OVERFLOW, APP_CONFIG_QUOTE_MAX);
    }

    const char *seen_symbol[CHECK_SEEN_MAX];
    size_t symbol_count = 0;

    for (size_t i = 0; quotes != NULL && i < quote_count; i++) {
        const app_config_quote_t *q = &quotes[i];

        if (!quote_fields_valid(q)) {
            record(issues, cap, &found, APP_CONFIG_ISSUE_QUOTE_FIELDS, i);
            continue;
        }

        bool duplicate = false;
        for (size_t k = 0; k < symbol_count; k++) {
            if (strcmp(seen_symbol[k], q->symbol) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            record(issues, cap, &found, APP_CONFIG_ISSUE_QUOTE_DUPLICATE, i);
            continue;
        }
        if (symbol_count < CHECK_SEEN_MAX) {
            seen_symbol[symbol_count++] = q->symbol;
        }
    }

    return found;
}

const char *app_config_issue_text(app_config_issue_code_t code) {
    switch (code) {
    case APP_CONFIG_ISSUE_WIFI_FIELDS:    return "wifi fields invalid, entry will be skipped";
    case APP_CONFIG_ISSUE_WIFI_LOCATION:  return "wifi location invalid, network kept without weather";
    case APP_CONFIG_ISSUE_WIFI_DUPLICATE: return "duplicate SSID, later entry will be skipped";
    case APP_CONFIG_ISSUE_WIFI_OVERFLOW:  return "more presets than profile slots";
    case APP_CONFIG_ISSUE_QUOTE_FIELDS:   return "quote symbol or label invalid";
    case APP_CONFIG_ISSUE_QUOTE_DUPLICATE:return "duplicate quote symbol";
    case APP_CONFIG_ISSUE_QUOTE_OVERFLOW: return "more quote symbols than the screen holds";
    case APP_CONFIG_ISSUE_AMD_KEY_INVALID: return "amd key is empty or not printable ascii";
    }
    return "unknown";
}
