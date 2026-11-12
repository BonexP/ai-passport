// tests/test_app_config_check.c —— 编译期配置自检的主机测试。
//
// 两类用途：
//   1. 用构造的坏表逐条断言问题码，证明"写错了会被指出来"；
//   2. 对真实的 app_config 表跑一遍——本机存在本地配置时，这条断言会把
//      开发者自己的配置错误挡在门禁里。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_config_check.h"
#include "wifi_profile.h" // WIFI_PROFILE_MAX：overflow 用例要用真实上界

static app_config_wifi_t wifi_entry(const char *ssid) {
    app_config_wifi_t w;
    memset(&w, 0, sizeof w);
    w.ssid = ssid;
    return w;
}

static app_config_quote_t quote_entry(const char *symbol, const char *label) {
    app_config_quote_t q;
    q.symbol = symbol;
    q.label = label;
    return q;
}

static void test_valid_tables_have_no_issues(void) {
    const app_config_wifi_t wifi[] = {wifi_entry("home"), wifi_entry("office")};
    const app_config_quote_t quotes[] = {
        {"NVDA", "NVIDIA"},
        {"TSLA", "TESLA"},
    };
    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];

    assert(app_config_check(wifi, 2, quotes, 2, issues, APP_CONFIG_ISSUE_MAX) == 0);
}

static void test_wifi_field_errors_are_reported(void) {
    static const char ssid_33[] = "123456789012345678901234567890123";
    static const char pass_65[] =
        "00000000000000000000000000000000000000000000000000000000000000000";
    static const char name_24[] = "123456789012345678901234";

    app_config_wifi_t wifi[5];
    memset(wifi, 0, sizeof wifi);
    wifi[0].ssid = NULL;          // 必填
    wifi[1].ssid = "";            // 空
    wifi[2].ssid = ssid_33;       // 33 > 32
    wifi[3].ssid = "ok";
    wifi[3].password = pass_65;   // 65 > 64
    wifi[4].ssid = "ok2";
    wifi[4].name = name_24;       // 24 不合法

    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];
    size_t found = app_config_check(wifi, 5, NULL, 0, issues, APP_CONFIG_ISSUE_MAX);

    assert(found == 5);
    for (size_t i = 0; i < found; i++) {
        assert(issues[i].code == APP_CONFIG_ISSUE_WIFI_FIELDS);
        assert(issues[i].index == i);
    }
}

static void test_bad_location_is_reported_but_network_survives(void) {
    app_config_wifi_t wifi[2];
    memset(wifi, 0, sizeof wifi);
    wifi[0] = wifi_entry("home");
    wifi[0].has_location = true;
    wifi[0].lat_udeg = 90000001; // 越界 1 微度
    wifi[1] = wifi_entry("office");
    wifi[1].has_location = true;
    wifi[1].lat_udeg = 0;
    wifi[1].lon_udeg = 0;

    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];
    size_t found = app_config_check(wifi, 2, NULL, 0, issues, APP_CONFIG_ISSUE_MAX);

    assert(found == 1);
    assert(issues[0].code == APP_CONFIG_ISSUE_WIFI_LOCATION);
    assert(issues[0].index == 0);
}

static void test_duplicate_ssid_is_reported_on_the_later_entry(void) {
    const app_config_wifi_t wifi[] = {wifi_entry("home"), wifi_entry("home")};
    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];

    size_t found = app_config_check(wifi, 2, NULL, 0, issues, APP_CONFIG_ISSUE_MAX);
    assert(found == 1);
    assert(issues[0].code == APP_CONFIG_ISSUE_WIFI_DUPLICATE);
    assert(issues[0].index == 1);
}

static void test_wifi_overflow_is_reported_once(void) {
    app_config_wifi_t wifi[WIFI_PROFILE_MAX + 1];
    char ssids[WIFI_PROFILE_MAX + 1][8];

    for (size_t i = 0; i < WIFI_PROFILE_MAX + 1; i++) {
        snprintf(ssids[i], sizeof ssids[i], "ap%u", (unsigned)i);
        wifi[i] = wifi_entry(ssids[i]);
    }

    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];
    size_t found = app_config_check(wifi, WIFI_PROFILE_MAX + 1, NULL, 0, issues,
                                    APP_CONFIG_ISSUE_MAX);
    assert(found == 1);
    assert(issues[0].code == APP_CONFIG_ISSUE_WIFI_OVERFLOW);
    assert(issues[0].index == WIFI_PROFILE_MAX);
}

static void test_quote_field_errors_are_reported(void) {
    static const char symbol_16[] = "1234567890123456";
    static const char label_24[] = "123456789012345678901234";

    const app_config_quote_t quotes[] = {
        {NULL, "X"},                    // 缺 symbol
        {"", "X"},                      // 空 symbol
        {"A", NULL},                    // 缺 label
        {"B", ""},                      // 空 label
        {symbol_16, "X"},               // symbol 太长
        {"C", label_24},                // label 太长
        {"D", "\xE4\xB8\xAD"},          // 非 ASCII
        {"E F", "X"},                   // symbol 里有空格
    };

    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];
    size_t found = app_config_check(NULL, 0, quotes, 8, issues, APP_CONFIG_ISSUE_MAX);

    assert(found == 8);
    for (size_t i = 0; i < found; i++) {
        assert(issues[i].code == APP_CONFIG_ISSUE_QUOTE_FIELDS);
        assert(issues[i].index == i);
    }
}

static void test_duplicate_quote_symbol_is_reported(void) {
    const app_config_quote_t quotes[] = {
        {"NVDA", "NVIDIA"},
        {"NVDA", "NVIDIA again"},
    };
    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];

    size_t found = app_config_check(NULL, 0, quotes, 2, issues, APP_CONFIG_ISSUE_MAX);
    assert(found == 1);
    assert(issues[0].code == APP_CONFIG_ISSUE_QUOTE_DUPLICATE);
    assert(issues[0].index == 1);
}

static void test_quote_overflow_is_reported_once(void) {
    app_config_quote_t quotes[APP_CONFIG_QUOTE_MAX + 1];
    char symbols[APP_CONFIG_QUOTE_MAX + 1][8];

    for (size_t i = 0; i < APP_CONFIG_QUOTE_MAX + 1; i++) {
        snprintf(symbols[i], sizeof symbols[i], "S%u", (unsigned)i);
        quotes[i] = quote_entry(symbols[i], "Label");
    }

    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];
    size_t found = app_config_check(NULL, 0, quotes, APP_CONFIG_QUOTE_MAX + 1, issues,
                                    APP_CONFIG_ISSUE_MAX);
    assert(found == 1);
    assert(issues[0].code == APP_CONFIG_ISSUE_QUOTE_OVERFLOW);
    assert(issues[0].index == APP_CONFIG_QUOTE_MAX);
}

static void test_capacity_limit_only_truncates_the_list(void) {
    const app_config_wifi_t wifi[] = {wifi_entry(""), wifi_entry(""), wifi_entry("")};
    app_config_issue_t one[1];

    size_t found = app_config_check(wifi, 3, NULL, 0, one, 1);
    assert(found == 3); // 总数照实返回
    assert(one[0].code == APP_CONFIG_ISSUE_WIFI_FIELDS);

    // NULL 数组只计数，不崩。
    assert(app_config_check(NULL, 0, NULL, 0, NULL, 0) == 0);
}

static void test_issue_text_is_ascii_and_total(void) {
    for (int code = APP_CONFIG_ISSUE_WIFI_FIELDS; code <= APP_CONFIG_ISSUE_QUOTE_OVERFLOW;
         code++) {
        const char *text = app_config_issue_text((app_config_issue_code_t)code);
        assert(text != NULL);
        for (const char *p = text; *p != '\0'; p++) {
            assert((unsigned char)*p > 0x1F && (unsigned char)*p < 0x7F);
        }
    }
    assert(strcmp(app_config_issue_text((app_config_issue_code_t)99), "unknown") == 0);
}

// 这一条同时是"开发者本地配置"的门禁：本地表有问题时，主机测试直接失败。
static void test_the_real_config_is_clean(void) {
    app_config_issue_t issues[APP_CONFIG_ISSUE_MAX];
    size_t found = app_config_check(app_config_wifi_table(), app_config_wifi_count(),
                                    app_config_quote_table(), app_config_quote_count(),
                                    issues, APP_CONFIG_ISSUE_MAX);

    if (found > 0) {
        for (size_t i = 0; i < found && i < APP_CONFIG_ISSUE_MAX; i++) {
            printf("config issue #%u: %s\n", (unsigned)issues[i].index,
                   app_config_issue_text(issues[i].code));
        }
    }
    assert(found == 0);
}

// ---- AMD key：四种形态 ----

static void test_amd_key_absent_is_valid(void) {
    assert(app_config_amd_key_valid(NULL));
}

static void test_amd_key_empty_is_invalid(void) {
    assert(!app_config_amd_key_valid(""));
}

static void test_amd_key_printable_is_valid(void) {
    // 形态与真实 key 一致，但由片段拼出，保持测试源码自身干净。
    char key[40];
    strncpy(key, "rc-", sizeof key - 1);
    strncat(key, "a1B2c3D4e5F6g7H8i9J0", sizeof key - strlen(key) - 1);
    assert(app_config_amd_key_valid(key));
}

static void test_amd_key_unprintable_or_too_long_is_invalid(void) {
    assert(!app_config_amd_key_valid("rc-ke\ty"));
    assert(!app_config_amd_key_valid("rc-\u4e2d")); // 非 ASCII 一律拒绝
    char long_key[APP_CONFIG_ISSUE_MAX * 12];
    memset(long_key, 'a', sizeof long_key - 1);
    long_key[sizeof long_key - 1] = '\0';
    assert(!app_config_amd_key_valid(long_key));
}

int main(void) {
    test_valid_tables_have_no_issues();
    test_wifi_field_errors_are_reported();
    test_bad_location_is_reported_but_network_survives();
    test_duplicate_ssid_is_reported_on_the_later_entry();
    test_wifi_overflow_is_reported_once();
    test_quote_field_errors_are_reported();
    test_duplicate_quote_symbol_is_reported();
    test_quote_overflow_is_reported_once();
    test_capacity_limit_only_truncates_the_list();
    test_issue_text_is_ascii_and_total();
    test_the_real_config_is_clean();
    test_amd_key_absent_is_valid();
    test_amd_key_empty_is_invalid();
    test_amd_key_printable_is_valid();
    test_amd_key_unprintable_or_too_long_is_invalid();
    puts("app config check tests: PASS");
    return 0;
}
