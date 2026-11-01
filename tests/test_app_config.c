// tests/test_app_config.c —— 编译期配置访问器的契约测试。
//
// 本机存在 app_config.local.h 时表内容由开发者决定，所以这里只断言"结构自洽"：
// 计数与指针一致、字段在 profile 的容量内、坐标合法、标的符号不重复。
// 没有本地文件时（CI）走 Wi-Fi 表为空的路径，同样必须自洽。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"

static size_t bounded_len(const char *s, size_t cap) {
    size_t n = 0;

    if (s == NULL) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

static bool ascii_printable(const char *s) {
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20u || c > 0x7Eu) return false;
    }
    return true;
}

static void test_wifi_table_is_self_consistent(void) {
    size_t count = app_config_wifi_count();
    const app_config_wifi_t *table = app_config_wifi_table();

    if (count == 0) {
        assert(table == NULL); // 空表必须是 NULL，调用方据此免于解引用
        return;
    }
    assert(table != NULL);

    for (size_t i = 0; i < count; i++) {
        const app_config_wifi_t *w = &table[i];

        // 与 wifi_profile_set_fields 的上限保持一致：越界的条目会在注入时被跳过。
        size_t ssid_len = bounded_len(w->ssid, 33);
        assert(ssid_len > 0 && ssid_len <= 32);
        if (w->password != NULL) {
            assert(bounded_len(w->password, 65) <= 64);
        }
        if (w->name != NULL) {
            assert(bounded_len(w->name, 24) < 24);
        }
        if (w->has_location) {
            assert(w->lat_udeg >= -90000000 && w->lat_udeg <= 90000000);
            assert(w->lon_udeg >= -180000000 && w->lon_udeg <= 180000000);
            if (w->place != NULL) {
                assert(bounded_len(w->place, 24) < 24);
            }
        }
    }
}

static void test_quote_table_is_self_consistent(void) {
    size_t count = app_config_quote_count();
    const app_config_quote_t *table = app_config_quote_table();

    // 内置默认表保证全新 checkout 与 CI 都能得到可用列表。
    assert(count > 0);
    assert(table != NULL);
    assert(count <= 16); // 屏上放不下更多；超过这个数说明配置写错了地方

    for (size_t i = 0; i < count; i++) {
        const app_config_quote_t *q = &table[i];

        assert(q->symbol != NULL && q->symbol[0] != '\0');
        assert(q->label != NULL && q->label[0] != '\0');
        assert(ascii_printable(q->symbol));
        assert(ascii_printable(q->label)); // 本阶段 UI 只有 ASCII 字形
        assert(strlen(q->symbol) < 16);
        assert(strlen(q->label) < 24);

        for (size_t j = i + 1; j < count; j++) {
            assert(strcmp(q->symbol, table[j].symbol) != 0);
        }
    }
}

int main(void) {
    test_wifi_table_is_self_consistent();
    test_quote_table_is_self_consistent();
    puts("app config tests: PASS");
    return 0;
}
