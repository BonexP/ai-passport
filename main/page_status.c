// main/page_status.c —— 状态页：把快照画成三行键值 + 一行主文案。
//
// 线程模型：页面只读快照（走 portMUX 临界区），从不反向持有 LVGL 锁；服务侧完全
// 不碰 LVGL。因此锁序唯一，不存在"临界区里等互斥量"的路径。
#include "page_status.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "ui_product.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

#define STATUS_TICK_MS        250u
#define STATUS_BATTERY_EVERY  40u // 10 秒刷一次电量：走 I2C，没必要每 250 ms 一次
#define STATUS_SSID_SHOWN     20u // 20 字符 + 省略号，保证落在两行内

static lv_obj_t *s_scr;
static lv_obj_t *s_headline;
static lv_obj_t *s_detail;
static lv_obj_t *s_key[3];
static lv_obj_t *s_value[3];
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;
static lv_timer_t *s_timer;

// seq 为 0 表示"还没有任何写者发布过"（服务 worker 尚未起来）。用它区分
// "未开始"与"服务已发布 IDLE"，否则开机第一秒会写着一个误导性的 Idle。
static uint16_t s_shown_seq;
static bool     s_shown_valid;
static unsigned s_ticks;

static const char *const KEY_TEXT[3] = { "Address", "Saved", "Signal" };

// 32 字符的 SSID 加上 "Connecting: " 前缀会撑到三行。截到 20 个字符并把最后
// 三个换成省略号：宁可少显示，也不要让用户以为名字就这么长。
static void clamp_ssid(wifi_status_snapshot_t *view) {
    size_t len = strlen(view->ssid);
    if (len <= STATUS_SSID_SHOWN) return;
    size_t cut = STATUS_SSID_SHOWN - 3u;
    view->ssid[cut] = '.';
    view->ssid[cut + 1] = '.';
    view->ssid[cut + 2] = '.';
    view->ssid[STATUS_SSID_SHOWN] = '\0';
}

// 固定 7 次文本写入：主行 / 次行 / 三个值 / 提示 / 电量。测试按这个顺序断言。
static void render(const wifi_status_snapshot_t *snap) {
    char buf[48];

    if (snap->seq == 0) {
        lv_label_set_text(s_headline, "Starting Wi-Fi...");
        lv_label_set_text(s_detail, "");
        lv_label_set_text(s_value[0], "-");
        lv_label_set_text(s_value[1], "-");
        lv_label_set_text(s_value[2], "-");
        lv_label_set_text(s_hint, "Hold OK: menu");
        ui_product_battery_text(s_battery, bsp_battery_soc());
        return;
    }

    wifi_status_snapshot_t view = *snap;
    clamp_ssid(&view);

    if (wifi_status_format_headline(&view, buf, sizeof buf) == 0) buf[0] = '\0';
    lv_label_set_text(s_headline, buf);

    if (wifi_status_format_detail(snap, buf, sizeof buf) == 0) buf[0] = '\0';
    lv_label_set_text(s_detail, buf);
    lv_obj_set_style_text_color(
        s_detail, lv_color_hex(snap->fail == WIFI_FAIL_NONE ? UI_P_MUTED : UI_P_SEAL), 0);

    lv_label_set_text(s_value[0], snap->ip[0] != '\0' ? snap->ip : "-");

    snprintf(buf, sizeof buf, "%u saved", (unsigned)snap->profile_count);
    lv_label_set_text(s_value[1], buf);

    if (snap->rssi > -128) {
        snprintf(buf, sizeof buf, "%d dBm", (int)snap->rssi);
        lv_label_set_text(s_value[2], buf);
    } else {
        lv_label_set_text(s_value[2], "-");
    }

    lv_label_set_text(s_hint, "Hold OK: menu");
    ui_product_battery_text(s_battery, bsp_battery_soc());
}

static void tick(lv_timer_t *timer) {
    (void)timer;
    if (s_scr == NULL) return;

    s_ticks++;
    wifi_status_snapshot_t snap;
    wifi_status_read(wifi_status_shared(), &snap);

    // seq 没变就不重画；电量单独按低频周期刷。
    bool battery_due = (s_ticks % STATUS_BATTERY_EVERY) == 0u;
    if (s_shown_valid && snap.seq == s_shown_seq && !battery_due) return;

    render(&snap);
    s_shown_seq = snap.seq;
    s_shown_valid = true;
}

void status_page_refresh(void) {
    if (s_scr == NULL) return;
    wifi_status_snapshot_t snap;
    wifi_status_read(wifi_status_shared(), &snap);
    render(&snap);
    s_shown_seq = snap.seq;
    s_shown_valid = true;
}

void status_page_build(void) {
    if (s_scr != NULL) return; // 幂等

    s_scr = ui_product_screen();
    s_battery = ui_product_header(s_scr);

    lv_obj_t *card = ui_product_card(s_scr, 16, 44, 208, 216, UI_P_CARD);
    ui_product_seal_bar(s_scr, 16, 44, 6, 216);

    s_headline = ui_product_text(card, 18, 16, 172, 58, "Starting Wi-Fi...",
                                 &lv_font_montserrat_20, UI_P_INK);
    s_detail = ui_product_text(card, 18, 80, 172, 20, "", &lv_font_montserrat_14, UI_P_MUTED);
    ui_product_rule(card, 18, 110, 172);

    for (int i = 0; i < 3; i++) {
        int y = 122 + i * 30;
        s_key[i] = ui_product_text(card, 18, y, 76, 20, KEY_TEXT[i], &lv_font_montserrat_14,
                                   UI_P_MUTED);
        s_value[i] = ui_product_text(card, 100, y, 90, 20, "-", &lv_font_montserrat_14, UI_P_INK);
    }

    s_hint = ui_product_text(s_scr, 0, 274, 240, 18, "Hold OK: menu", &lv_font_montserrat_14,
                             UI_P_DIM);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    s_shown_seq = 0;
    s_shown_valid = false;
    s_ticks = 0;
    status_page_refresh(); // 首帧不留空标签
    s_timer = lv_timer_create(tick, STATUS_TICK_MS, NULL);
    lv_screen_load(s_scr);
}

void status_page_destroy(void) {
    // 顺序不能反：定时器会碰 label，必须先删定时器再删对象树。
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_scr != NULL) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    s_headline = NULL;
    s_detail = NULL;
    s_hint = NULL;
    s_battery = NULL;
    for (int i = 0; i < 3; i++) {
        s_key[i] = NULL;
        s_value[i] = NULL;
    }
    s_shown_seq = 0;
    s_shown_valid = false;
    s_ticks = 0;
}
