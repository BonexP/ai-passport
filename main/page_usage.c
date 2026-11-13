// main/page_usage.c —— AMD 用量页的实现。契约见 page_usage.h。
//
// 渲染只依赖两份快照：Wi-Fi（是否连着网）与 app_data（用量记录 + 状态）。
// 页面不判断"该不该取数"，也不判断"该说什么"——后者全在 usage_view。
//
// 版面（240x320）：
//   页眉   AI PASSPORT / 电量
//   标题   AMD Cloud
//   主卡   剩余点数（36 px）+ 明细行 + 2 px 进度条
//   四行   Reset / Today / 30d / 状态行
//   页脚   OK refresh   UP/DOWN page
#include "page_usage.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_data.h"
#include "app_data_shared.h"
#include "app_time.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "usage_view.h"
#include "ui_product.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

#define USAGE_TICK_MS       1000u
#define USAGE_BATTERY_EVERY 10u // 10 秒刷一次电量：走 I2C，没必要每秒读

// 进度条几何。坐标相对主卡（卡片在屏上位于 16,80，尺寸 208x120）。
// 底槽与已用部分同起点，已用部分按千分比改宽度。
#define USAGE_BAR_X 18
#define USAGE_BAR_Y 96
#define USAGE_BAR_W 172
#define USAGE_BAR_H 2

static lv_obj_t *s_scr;
static lv_obj_t *s_battery;
static lv_obj_t *s_big;      // 剩余点数，或没有数据时的状态文案
static lv_obj_t *s_detail;   // "1.5488 / 2.0000 used"
static lv_obj_t *s_reset;
static lv_obj_t *s_today;
static lv_obj_t *s_days30;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_bar;      // 已用部分；宽度为 0 时隐藏
static lv_timer_t *s_timer;

static unsigned s_ticks;
static uint16_t s_shown_wifi_seq;
static uint16_t s_shown_usage_seq;
static bool     s_shown_valid;
static int64_t  s_build_ms; // 首次请求迟迟不回时，用它把 "Updating..." 换成"重试中"

static void show(lv_obj_t *obj, bool visible) {
    if (obj == NULL) return;
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// 进度条宽度：只有 usage_view 给出合法千分比时才画。
static void render_bar(int permille) {
    if (s_bar == NULL) return;
    if (permille < 0) {
        show(s_bar, false);
        return;
    }
    const int width = USAGE_BAR_W * permille / 1000;
    if (width <= 0) {
        show(s_bar, false);
        return;
    }
    lv_obj_set_size(s_bar, width, USAGE_BAR_H);
    show(s_bar, true);
}

static void render(void) {
    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    // 用量只要求"连着网"：它不关心本机有没有配坐标（上次踩过这个坑，见 net_worker.c）。
    const bool connected = (wifi.phase == WIFI_PHASE_CONNECTED);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool waited_long = (now_ms - s_build_ms) >= 20000;

    // key 是否配了：空串与 NULL 等价，都是"没配"。
    const char *key = app_config_amd_key();
    const bool has_key = (key != NULL && key[0] != '\0');

    const usage_record_t *u = &data.usage;
    const usage_view_state_t state =
        usage_view_state(has_key, connected, app_time_is_valid(), u->usage.has_data, u->status,
                         waited_long);

    const char *message = usage_view_message(state);
    const bool has_view = (message == NULL);

    // 主卡：有数据画点数，没数据画状态文案。
    //
    // 同一个 label 承担两种角色，所以字体要换：36 px 只够放下 "1.5488" 这种短数字，
    // "Key not configured" 用它一定会被截断成几个字符。状态文案降到 20 px。
    // 这不是视觉偏好，是"文案必须完整可读"的硬要求。
    char buf[64];
    if (has_view) {
        lv_obj_set_style_text_font(s_big, &lv_font_montserrat_36, 0);
        if (usage_view_remaining(state, &u->usage, buf, sizeof buf)) {
            lv_label_set_text(s_big, buf);
        } else {
            lv_label_set_text(s_big, "--");
        }
    } else {
        lv_obj_set_style_text_font(s_big, &lv_font_montserrat_20, 0);
        lv_label_set_text(s_big, message != NULL ? message : "");
    }

    // 明细行只在与主卡同一状态时才显示，否则会在大字状态下面挂一串没意义的数字。
    const bool detail_visible =
        has_view && usage_view_used_line(state, &u->usage, buf, sizeof buf);
    if (detail_visible) {
        lv_label_set_text(s_detail, buf);
    } else {
        lv_label_set_text(s_detail, "");
    }
    show(s_detail, detail_visible);

    render_bar(has_view ? usage_view_used_permille(state, &u->usage) : -1);

    const bool reset_visible = usage_view_reset_line(state, &u->usage, buf, sizeof buf);
    if (reset_visible) lv_label_set_text(s_reset, buf);
    show(s_reset, reset_visible);

    const bool today_visible = usage_view_today_line(state, &u->usage, buf, sizeof buf);
    if (today_visible) lv_label_set_text(s_today, buf);
    show(s_today, today_visible);

    const bool days30_visible = usage_view_30d_line(state, &u->usage, buf, sizeof buf);
    if (days30_visible) lv_label_set_text(s_days30, buf);
    show(s_days30, days30_visible);

    // 状态行：没有数据时它就是那句话，有数据时它说明"这份数据有多旧、上次怎么了"。
    //
    // stale 恒为 false：用量不做持久化（见 project-plan.md §9 第 13 条），
    // 所以永远不存在"从 NVS 读回来的旧副本"这种东西。重启后 fetched_uptime_ms
    // 是 -1，此时 age_ms 也是 -1，状态行会退回只报前缀。
    const int64_t age_ms =
        u->fetched_uptime_ms >= 0 ? now_ms - u->fetched_uptime_ms : -1;
    char status[64];
    if (usage_view_status_line(state, u->status, false, age_ms, u->fetched_epoch, status,
                               sizeof status)) {
        lv_label_set_text(s_status, status);
        show(s_status, true);
    } else {
        lv_label_set_text(s_status, "");
        show(s_status, false);
    }

    ui_product_battery_text(s_battery, bsp_battery_soc());
}

static void tick(lv_timer_t *timer) {
    (void)timer;
    if (s_scr == NULL) return;

    s_ticks++;
    const bool battery_due = (s_ticks % USAGE_BATTERY_EVERY) == 0u;

    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    // 两份快照都没变就不重画；"多久前更新"由 1 s 的定时器保证，不依赖 seq。
    if (s_shown_valid && wifi.seq == s_shown_wifi_seq && data.usage.seq == s_shown_usage_seq &&
        !battery_due) {
        return;
    }

    render();
    s_shown_wifi_seq = wifi.seq;
    s_shown_usage_seq = data.usage.seq;
    s_shown_valid = true;
}

void page_usage_refresh(void) {
    if (s_scr == NULL) return;
    render();
    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);
    s_shown_wifi_seq = wifi.seq;
    s_shown_usage_seq = data.usage.seq;
    s_shown_valid = true;
}

void page_usage_build(void) {
    if (s_scr != NULL) return; // 幂等

    s_scr = ui_product_screen();
    s_battery = ui_product_header(s_scr);

    (void)ui_product_text(s_scr, 18, 36, 204, 24, "AMD Cloud", &lv_font_montserrat_20,
                          UI_P_CARD);
    ui_product_rule(s_scr, 18, 66, 204);

    lv_obj_t *card = ui_product_card(s_scr, 16, 80, 208, 120, UI_P_CARD);
    ui_product_seal_bar(s_scr, 16, 80, 6, 120);

    // 主卡大字。状态文案（"Key not configured" 等）也用这个 label：
    // 20 px 在 172 px 宽里能放下最长的那句，36 px 只用来放点数。
    s_big = ui_product_text(card, 18, 14, 172, 48, "--", &lv_font_montserrat_36, UI_P_INK);
    s_detail = ui_product_text(card, 18, 62, 172, 20, "", &lv_font_montserrat_14, UI_P_MUTED);

    // 进度条的底槽与已用部分。底槽恒在，已用部分按千分比改宽度。
    // 两者同起点，已用部分盖在底槽上；宽度为 0 时直接隐藏。
    (void)ui_product_card(card, USAGE_BAR_X, USAGE_BAR_Y, USAGE_BAR_W, USAGE_BAR_H, UI_P_RULE);
    s_bar = ui_product_card(card, USAGE_BAR_X, USAGE_BAR_Y, USAGE_BAR_W, USAGE_BAR_H, UI_P_SEAL);

    s_reset = ui_product_text(s_scr, 18, 208, 204, 18, "", &lv_font_montserrat_14, UI_P_DIM);
    s_today = ui_product_text(s_scr, 18, 226, 204, 18, "", &lv_font_montserrat_14, UI_P_DIM);
    s_days30 = ui_product_text(s_scr, 18, 244, 204, 18, "", &lv_font_montserrat_14, UI_P_DIM);
    s_status = ui_product_text(s_scr, 18, 264, 204, 18, "", &lv_font_montserrat_14, UI_P_DIM);

    s_hint = ui_product_text(s_scr, 0, 294, 240, 18, "OK refresh   UP/DOWN page",
                             &lv_font_montserrat_14, UI_P_DIM);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    s_ticks = 0;
    s_shown_valid = false;
    s_build_ms = esp_timer_get_time() / 1000;
    page_usage_refresh(); // 首帧不留空标签
    s_timer = lv_timer_create(tick, USAGE_TICK_MS, NULL);
    lv_screen_load(s_scr);
}

void page_usage_destroy(void) {
    // 顺序不能反：定时器会碰 label，必须先删定时器再删对象树。
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr != NULL) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_battery = NULL;
    s_big = NULL;
    s_detail = NULL;
    s_reset = NULL;
    s_today = NULL;
    s_days30 = NULL;
    s_status = NULL;
    s_hint = NULL;
    s_bar = NULL;
    s_ticks = 0;
    s_shown_valid = false;
}
