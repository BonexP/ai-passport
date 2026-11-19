// main/page_quote.c —— 行情页的实现。契约见 page_quote.h。
//
// 渲染只读两份快照：Wi-Fi（是否连着网）与 app_data（行情记录 + 状态）。
// 所有判断都在 quote_view（纯逻辑、有主机测试），本文件只把文案贴到 label 上。
//
// 版面（240x320），沿用产品视觉壳 ui_product（不是 demo 测试外壳）：
//   页眉   AI PASSPORT / 电量
//   标题   Quotes
//   8 格   每格两行：上行「名称  现价  涨跌幅」，下行「涨跌额  成交额」
//   页脚   OK refresh   UP/DOWN page
//
// 名称用 app_config 的 ASCII label（价格数据里带的是 GBK 中文名，不进显示缓冲，
// 默认 Montserrat 没有中文字形）。
#include "page_quote.h"

#include <stdio.h>
#include <string.h>

#include "app_data.h"
#include "app_data_shared.h"
#include "app_time.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "quote_view.h"
#include "ui_product.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

#define QUOTE_TICK_MS       1000u
#define QUOTE_BATTERY_EVERY 10u

#define QUOTE_ROW_COUNT     8u
#define QUOTE_ROW_H         28
#define QUOTE_ROW_Y0        70   // 标题(36..60)与分隔线(66)之后起排

static lv_obj_t *s_scr;
static lv_obj_t *s_battery;
static lv_obj_t *s_title;
static lv_obj_t *s_rows[QUOTE_ROW_COUNT][2]; // [i][0] 上行，[i][1] 下行
static lv_obj_t *s_hint;
static lv_timer_t *s_timer;

static unsigned s_ticks;
static uint16_t s_shown_wifi_seq;
static uint16_t s_shown_quote_seq;
static bool     s_shown_valid;
static int64_t  s_build_ms;

static void show(lv_obj_t *obj, bool visible) {
    if (obj == NULL) return;
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// 定点数（×10000）→ 两位小数字符串，带符号。价格显示到分即可，不必 4 位。
static void fmt_price2(int64_t v_1e4, char *buf, size_t cap) {
    const bool neg = v_1e4 < 0;
    const int64_t m = neg ? -v_1e4 : v_1e4;
    // 四舍五入到分：1e-4 → 1e-2，除以 100。
    const int64_t cents = (m + 50) / 100;
    snprintf(buf, cap, "%s%lld.%02lld", neg ? "-" : "", (long long)(cents / 100),
             (long long)(cents % 100));
}

static void render_row(int index, const quote_item_t *item, const char *code) {
    char upper[64], lower[64];
    if (item != NULL && item->valid) {
        char price[24], pct[24], change[24], amount[32];
        fmt_price2(item->price_1e4, price, sizeof price);
        fmt_price2(item->change_1e4, change, sizeof change);
        quote_format_pct(item->pct_1e4, pct, sizeof pct);
        quote_format_amount(code, item->amount, amount, sizeof amount);
        snprintf(upper, sizeof upper, "%-10s %10s  %s", item->label, price, pct);
        snprintf(lower, sizeof lower, "  %s  %s", change, amount);
        lv_label_set_text(s_rows[index][0], upper);
        lv_label_set_text(s_rows[index][1], lower);
        show(s_rows[index][0], true);
        show(s_rows[index][1], true);
    } else {
        show(s_rows[index][0], false);
        show(s_rows[index][1], false);
    }
}

static void render(void) {
    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    const bool connected = (wifi.phase == WIFI_PHASE_CONNECTED);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool waited_long = (now_ms - s_build_ms) >= 20000;

    const quote_record_t *q = &data.quote;
    const bool has_data = q->quote.has_data;
    // 解析失败的首要原因走 BAD_BODY：有数据时它的语义由状态行补充说明，
    // 没数据时它是大字占位。quote_view_state 不带 status，所以这里直接判定。
    quote_view_state_t state = quote_view_state(connected, app_time_is_valid(), has_data,
                                                waited_long);
    if (!has_data && q->status == QUOTE_STATUS_BAD_BODY) {
        state = QUOTE_VIEW_BAD_BODY;
    }

    if (has_data) {
        for (size_t i = 0; i < QUOTE_ROW_COUNT; i++) {
            if (i < q->quote.count) {
                const quote_item_t *item = &q->quote.items[i];
                render_row((int)i, item, item->code);
            } else {
                render_row((int)i, NULL, NULL);
            }
        }
    } else {
        // 没有数据：清空所有行，把它们让给大字标题下方的状态文案。
        for (size_t i = 0; i < QUOTE_ROW_COUNT; i++) {
            show(s_rows[i][0], false);
            show(s_rows[i][1], false);
        }
    }

    // 标题位一物两用：有数据时是 "Quotes" + 简短的"多久前更新"，
    // 没数据时就是状态文案（大字占位）。窄屏放不下独立状态行，故合并。
    char status[64];
    const int64_t age_ms = q->fetched_uptime_ms >= 0 ? now_ms - q->fetched_uptime_ms : -1;
    if (has_data) {
        char line[96];
        if (quote_view_status_line(state, q->status, age_ms, q->fetched_epoch, status,
                                   sizeof status)) {
            snprintf(line, sizeof line, "Quotes  -  %s", status);
            lv_label_set_text(s_title, line);
        } else {
            lv_label_set_text(s_title, "Quotes");
        }
    } else {
        const char *message = quote_view_message(state);
        lv_label_set_text(s_title, message != NULL ? message : "");
    }

    ui_product_battery_text(s_battery, bsp_battery_soc());
}

static void tick(lv_timer_t *timer) {
    (void)timer;
    if (s_scr == NULL) return;

    s_ticks++;
    const bool battery_due = (s_ticks % QUOTE_BATTERY_EVERY) == 0u;

    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    if (s_shown_valid && wifi.seq == s_shown_wifi_seq && data.quote.seq == s_shown_quote_seq &&
        !battery_due) {
        return;
    }

    render();
    s_shown_wifi_seq = wifi.seq;
    s_shown_quote_seq = data.quote.seq;
    s_shown_valid = true;
}

void page_quote_refresh(void) {
    if (s_scr == NULL) return;
    render();
    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);
    s_shown_wifi_seq = wifi.seq;
    s_shown_quote_seq = data.quote.seq;
    s_shown_valid = true;
}

void page_quote_build(void) {
    if (s_scr != NULL) return; // 幂等

    s_scr = ui_product_screen();
    s_battery = ui_product_header(s_scr);
    s_title = ui_product_text(s_scr, 18, 36, 204, 24, "Quotes", &lv_font_montserrat_20,
                              UI_P_CARD);
    ui_product_rule(s_scr, 18, 66, 204);

    for (size_t i = 0; i < QUOTE_ROW_COUNT; i++) {
        const int y = QUOTE_ROW_Y0 + (int)i * QUOTE_ROW_H;
        s_rows[i][0] = ui_product_text(s_scr, 18, y, 204, 16, "", &lv_font_montserrat_14,
                                       UI_P_CARD);
        s_rows[i][1] = ui_product_text(s_scr, 26, y + 14, 196, 14, "", &lv_font_montserrat_14,
                                       UI_P_DIM);
    }

    s_hint = ui_product_text(s_scr, 0, 300, 240, 18, "OK refresh   UP/DOWN page",
                             &lv_font_montserrat_14, UI_P_DIM);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    s_ticks = 0;
    s_shown_valid = false;
    s_build_ms = esp_timer_get_time() / 1000;
    page_quote_refresh();
    s_timer = lv_timer_create(tick, QUOTE_TICK_MS, NULL);
    lv_screen_load(s_scr);
}

void page_quote_destroy(void) {
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr != NULL) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_battery = NULL;
    s_title = NULL;
    for (size_t i = 0; i < QUOTE_ROW_COUNT; i++) {
        s_rows[i][0] = NULL;
        s_rows[i][1] = NULL;
    }
    s_hint = NULL;
    s_ticks = 0;
    s_shown_valid = false;
}
