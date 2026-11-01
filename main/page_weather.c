// main/page_weather.c —— 天气页的实现。契约见 page_weather.h。
//
// 渲染只依赖两份快照：Wi-Fi（是否连上、地点）与 app_data（温度、天气码、观测时间）。
// 页面不判断"该不该取数"，只把四种状态画清楚：无网络、无地点、等待校时、有数据。
#include "page_weather.h"

#include <stdio.h>
#include <string.h>

#include "app_data.h"
#include "app_data_shared.h"
#include "app_time.h"
#include "bsp_battery.h"
#include "esp_timer.h"
#include "ui_product.h"
#include "weather_source.h"
#include "weather_view.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

#define WEATHER_TICK_MS       1000u
#define WEATHER_BATTERY_EVERY 10u // 10 秒刷一次电量：走 I2C，没必要每秒读

static lv_obj_t *s_scr;
static lv_obj_t *s_battery;
static lv_obj_t *s_place;
static lv_obj_t *s_temp;
static lv_obj_t *s_unit;
static lv_obj_t *s_cond;
static lv_obj_t *s_status;   // 无数据时替代温度/状况的一行说明
static lv_obj_t *s_observed;
static lv_obj_t *s_updated;
static lv_obj_t *s_hint;
static lv_timer_t *s_timer;

static unsigned s_ticks;
static uint16_t s_shown_wifi_seq;
static uint16_t s_shown_weather_seq;
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

static void render(void) {
    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    const bool connected = (wifi.phase == WIFI_PHASE_CONNECTED);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const bool waited_long = (now_ms - s_build_ms) >= 20000;

    // 所有"该显示什么"的判断都在 weather_view（纯逻辑，有主机测试）。
    const weather_view_state_t state =
        weather_view_state(connected, wifi.location.has_location, app_time_is_valid(),
                           data.weather.has_data, waited_long);

    lv_label_set_text(s_place, weather_view_place(state, data.weather.place,
                                                  wifi.location.place, wifi.ssid));

    const char *message = weather_view_message(state);
    const bool has_view = (message == NULL);
    if (has_view) {
        char number[16];
        if (weather_format_temp(data.weather.temp_dc, number, sizeof number)) {
            lv_label_set_text(s_temp, number);
        }
        lv_label_set_text(s_cond, weather_code_text(data.weather.weather_code));

        char hhmm[8];
        if (weather_format_observed(data.weather.observed, hhmm, sizeof hhmm)) {
            char line[24];
            snprintf(line, sizeof line, "Observed %s", hhmm);
            lv_label_set_text(s_observed, line);
        } else {
            lv_label_set_text(s_observed, "");
        }
    } else {
        lv_label_set_text(s_status, message != NULL ? message : "");
        lv_label_set_text(s_observed, "");
    }

    show(s_temp, has_view);
    show(s_unit, has_view);
    show(s_cond, has_view);
    show(s_status, !has_view);

    char updated[40];
    const int64_t age_ms =
        data.weather.fetched_uptime_ms >= 0 ? now_ms - data.weather.fetched_uptime_ms : -1;
    if (weather_view_updated(state, data.weather.stale, age_ms, data.weather.fetched_epoch,
                             updated, sizeof updated)) {
        lv_label_set_text(s_updated, updated);
    } else {
        lv_label_set_text(s_updated, "");
    }

    ui_product_battery_text(s_battery, bsp_battery_soc());
}

static void tick(lv_timer_t *timer) {
    (void)timer;
    if (s_scr == NULL) return;

    s_ticks++;
    const bool battery_due = (s_ticks % WEATHER_BATTERY_EVERY) == 0u;

    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    // 两份快照都没变就不重画；"多久前更新"由 1 s 的定时器保证，不依赖 seq。
    if (s_shown_valid && wifi.seq == s_shown_wifi_seq &&
        data.weather.seq == s_shown_weather_seq && !battery_due) {
        return;
    }

    render();
    s_shown_wifi_seq = wifi.seq;
    s_shown_weather_seq = data.weather.seq;
    s_shown_valid = true;
}

void page_weather_refresh(void) {
    if (s_scr == NULL) return;
    render();
    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);
    s_shown_wifi_seq = wifi.seq;
    s_shown_weather_seq = data.weather.seq;
    s_shown_valid = true;
}

void page_weather_build(void) {
    if (s_scr != NULL) return; // 幂等

    s_scr = ui_product_screen();
    s_battery = ui_product_header(s_scr);

    s_place = ui_product_text(s_scr, 18, 36, 204, 24, "-", &lv_font_montserrat_20, UI_P_CARD);
    ui_product_rule(s_scr, 18, 66, 204);

    lv_obj_t *card = ui_product_card(s_scr, 16, 80, 208, 120, UI_P_CARD);
    ui_product_seal_bar(s_scr, 16, 80, 6, 120);

    s_temp = ui_product_text(card, 18, 14, 120, 52, "--", &lv_font_montserrat_36, UI_P_INK);
    s_unit = ui_product_text(card, 142, 44, 20, 20, "C", &lv_font_montserrat_14, UI_P_MUTED);
    s_cond = ui_product_text(card, 18, 80, 172, 24, "", &lv_font_montserrat_14, UI_P_MUTED);
    s_status = ui_product_text(card, 18, 58, 172, 44, "", &lv_font_montserrat_20, UI_P_MUTED);

    s_observed = ui_product_text(s_scr, 18, 214, 204, 20, "", &lv_font_montserrat_14, UI_P_DIM);
    s_updated = ui_product_text(s_scr, 18, 236, 204, 20, "", &lv_font_montserrat_14, UI_P_DIM);

    s_hint = ui_product_text(s_scr, 0, 280, 240, 18, "OK refresh   UP/DOWN page",
                             &lv_font_montserrat_14, UI_P_DIM);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    s_ticks = 0;
    s_shown_valid = false;
    s_build_ms = esp_timer_get_time() / 1000;
    page_weather_refresh(); // 首帧不留空标签
    s_timer = lv_timer_create(tick, WEATHER_TICK_MS, NULL);
    lv_screen_load(s_scr);
}

void page_weather_destroy(void) {
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
    s_place = NULL;
    s_temp = NULL;
    s_unit = NULL;
    s_cond = NULL;
    s_status = NULL;
    s_observed = NULL;
    s_updated = NULL;
    s_hint = NULL;
    s_ticks = 0;
    s_shown_valid = false;
}
