// tests/test_quote_view.c —— 行情页状态机、文案与行格式化的主机测试。
//
// 三条设计规则也写在这里：
//   1. 失败不清空数据：有数据时 quote_view_state 永远是 DATA，状态行只补充说明。
//   2. 所有函数不 include ESP-IDF / LVGL，可在主机逐格断言，不需要屏幕。
//   3. 文案全 ASCII：默认字体 Montserrat 没有中文字形。
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "quote_view.h"

// 实测报文里的英伟达：156.14，-6.71，-4.12%，成交额 52678055451 美元。
static quote_item_t nvda_item(void) {
    quote_item_t item;
    memset(&item, 0, sizeof item);
    item.valid = true;
    snprintf(item.label, sizeof item.label, "%s", "NVDA");
    item.price_1e4 = 1561400;
    item.change_1e4 = -67100;
    item.pct_1e4 = -41200;
    snprintf(item.amount, sizeof item.amount, "%s", "52678055451");
    return item;
}

static void test_state_priority(void) {
    // 有数据 → DATA，压倒一切：失败不清空已经取到的行情。
    assert(quote_view_state(true, true, true, false) == QUOTE_VIEW_DATA);
    assert(quote_view_state(false, false, true, true) == QUOTE_VIEW_DATA);

    // 没网 → NO_NET，且优先于没校时与等待过久。
    assert(quote_view_state(false, true, false, false) == QUOTE_VIEW_NO_NET);
    assert(quote_view_state(false, false, false, true) == QUOTE_VIEW_NO_NET);

    // 有网没校时 → WAIT_TIME，优先于 RETRYING。
    assert(quote_view_state(true, false, false, false) == QUOTE_VIEW_WAIT_TIME);
    assert(quote_view_state(true, false, false, true) == QUOTE_VIEW_WAIT_TIME);

    // 有网已校时：等太久才叫 RETRYING，否则是 UPDATING。
    assert(quote_view_state(true, true, false, true) == QUOTE_VIEW_RETRYING);
    assert(quote_view_state(true, true, false, false) == QUOTE_VIEW_UPDATING);
}

static void test_messages_cover_every_state(void) {
    assert(quote_view_message(QUOTE_VIEW_DATA) == NULL);
    assert(strcmp(quote_view_message(QUOTE_VIEW_NO_NET), "No network") == 0);
    assert(strcmp(quote_view_message(QUOTE_VIEW_WAIT_TIME), "Waiting for time") == 0);
    assert(strcmp(quote_view_message(QUOTE_VIEW_UPDATING), "Updating...") == 0);
    assert(strcmp(quote_view_message(QUOTE_VIEW_RETRYING), "No data - retrying") == 0);
    assert(strcmp(quote_view_message(QUOTE_VIEW_BAD_BODY), "Bad data") == 0);
}

static void test_row_formats_real_values(void) {
    char buf[96];
    quote_item_t item = nvda_item();
    assert(quote_view_row(&item, "usNVDA", buf, sizeof buf));
    assert(strcmp(buf, "NVDA  156.1400  -4.12%  -6.7100  $52.6B") == 0);

    // 涨跌额与涨跌幅自带符号；零值不带 '+'。
    item.pct_1e4 = 0;
    item.change_1e4 = 0;
    assert(quote_view_row(&item, "usNVDA", buf, sizeof buf));
    assert(strcmp(buf, "NVDA  156.1400  0.00%  0.0000  $52.6B") == 0);

    // code 决定成交额单位：同一原串换市场，文案必须不同（不引入非 ASCII 字面量）。
    item = nvda_item();
    snprintf(item.label, sizeof item.label, "%s", "SSE");
    item.price_1e4 = 35817300;
    item.change_1e4 = -208400;
    item.pct_1e4 = -5800;
    snprintf(item.amount, sizeof item.amount, "%s", "55046378");
    char us[96], cn[96];
    assert(quote_view_row(&item, "usNVDA", us, sizeof us));
    assert(quote_view_row(&item, "sh000001", cn, sizeof cn));
    assert(strcmp(us, cn) != 0);
}

static void test_row_rejects_invalid_and_short_buffer(void) {
    char buf[96];
    quote_item_t item = nvda_item();

    // 无效标的、空指针都不画，缓冲先清空。
    item.valid = false;
    assert(!quote_view_row(&item, "usNVDA", buf, sizeof buf));
    assert(buf[0] == '\0');
    assert(!quote_view_row(NULL, "usNVDA", buf, sizeof buf));
    assert(buf[0] == '\0');
    assert(!quote_view_row(&item, "usNVDA", NULL, 0));

    // 恰好放下 39 个字符 + NUL 可以；少一个字节就整行不显示。
    item = nvda_item();
    char exact[40];
    assert(quote_view_row(&item, "usNVDA", exact, sizeof exact));
    assert(strlen(exact) == 39);
    char short_buf[39];
    assert(!quote_view_row(&item, "usNVDA", short_buf, sizeof short_buf));
    assert(short_buf[0] == '\0');
    char tiny[4];
    assert(!quote_view_row(&item, "usNVDA", tiny, sizeof tiny));
    assert(tiny[0] == '\0');
}

static void test_status_line_without_data(void) {
    char buf[64];
    const quote_view_state_t states[] = {QUOTE_VIEW_NO_NET, QUOTE_VIEW_WAIT_TIME,
                                         QUOTE_VIEW_UPDATING, QUOTE_VIEW_RETRYING,
                                         QUOTE_VIEW_BAD_BODY};
    for (size_t i = 0; i < sizeof states / sizeof states[0]; i++) {
        assert(quote_view_status_line(states[i], QUOTE_STATUS_NONE, -1, 0, buf, sizeof buf));
        assert(strcmp(buf, quote_view_message(states[i])) == 0);
    }

    char tiny[4];
    assert(!quote_view_status_line(QUOTE_VIEW_RETRYING, QUOTE_STATUS_NONE, -1, 0, tiny,
                                   sizeof tiny));
    assert(tiny[0] == '\0');
    assert(!quote_view_status_line(QUOTE_VIEW_NO_NET, QUOTE_STATUS_NONE, -1, 0, NULL, 0));
}

static void test_status_line_with_data(void) {
    char buf[64];

    // 不知道多久前：只报前缀，不编一个数字。
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, -1, 0, buf, sizeof buf));
    assert(strcmp(buf, "Updated") == 0);

    // 与 weather_view_updated 同一套分钟/小时分档。
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 0, 0, buf, sizeof buf));
    assert(strcmp(buf, "Updated just now") == 0);
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 59 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated just now") == 0);
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 5 * 60 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated 5 min ago") == 0);
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 90 * 60 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated 90 min ago") == 0);
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 91 * 60 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated 1 h ago") == 0);
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 3 * 3600 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated 3 h ago") == 0);

    // 失败不清空数据：最近一次请求失败也只补一句时间，行情照常显示。
    // status 与 fetched_epoch 按契约不改变这句话（行情不做持久化）。
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_UNAVAILABLE, 5 * 60 * 1000,
                                  1767225600, buf, sizeof buf));
    assert(strcmp(buf, "Updated 5 min ago") == 0);
    assert(quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_BAD_BODY, -1, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated") == 0);

    char tiny[8];
    assert(!quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 5 * 60 * 1000, 0, tiny,
                                   sizeof tiny));
    assert(tiny[0] == '\0');
    assert(!quote_view_status_line(QUOTE_VIEW_DATA, QUOTE_STATUS_OK, 0, 0, NULL, 0));
}

int main(void) {
    test_state_priority();
    test_messages_cover_every_state();
    test_row_formats_real_values();
    test_row_rejects_invalid_and_short_buffer();
    test_status_line_without_data();
    test_status_line_with_data();
    puts("test_quote_view: PASS");
    return 0;
}
