// main/weather_view.c —— 天气页状态与文案的纯逻辑实现。契约见 weather_view.h。
#include "weather_view.h"

#include <stdio.h>

#include "app_clock.h"

weather_view_state_t weather_view_state(bool connected, bool located, bool time_valid,
                                        bool has_data, bool waited_long) {
    // 顺序即优先级。这里有两件事同时成立，不能简单二选一：
    //
    //   1. 离线时仍然要显示上一次存下的天气（页面用 "Offline - saved" 标明来源）。
    //   2. 已连上、但这个网络没配地点时，绝不能拿别的网络的快照顶上去。
    //
    // 快照只存天气本身，不记它属于哪个网络，开机 load_saved() 又会无条件发布出来。
    // 于是"连上了但没地点"与"根本没网"必须分开判断：前者是 Location not set 的
    // 终态，后者才允许回放离线数据。
    //
    // 2026-11-10 设备实证：换到一个只填了 SSID/密码的网络后，页面先闪
    // Location not set，随即显示另一个网络（zhenjiang）的天气与地名。
    if (!connected) return has_data ? WEATHER_VIEW_DATA : WEATHER_VIEW_NO_NETWORK;
    if (!located) return WEATHER_VIEW_NO_LOCATION;
    if (has_data) return WEATHER_VIEW_DATA;
    if (!time_valid) return WEATHER_VIEW_WAIT_TIME;
    if (waited_long) return WEATHER_VIEW_RETRYING;
    return WEATHER_VIEW_UPDATING;
}

const char *weather_view_message(weather_view_state_t state) {
    switch (state) {
    case WEATHER_VIEW_DATA:        return NULL;
    case WEATHER_VIEW_NO_NETWORK:  return "No network";
    case WEATHER_VIEW_NO_LOCATION: return "Location not set";
    case WEATHER_VIEW_WAIT_TIME:   return "Waiting for time";
    case WEATHER_VIEW_UPDATING:    return "Updating...";
    case WEATHER_VIEW_RETRYING:    return "No data - retrying";
    }
    return NULL;
}

const char *weather_view_place(weather_view_state_t state, const char *saved_place,
                               const char *location_place, const char *ssid) {
    if (state == WEATHER_VIEW_DATA) {
        if (saved_place != NULL && saved_place[0] != '\0') return saved_place;
        if (location_place != NULL && location_place[0] != '\0') return location_place;
        if (ssid != NULL && ssid[0] != '\0') return ssid;
        return "Weather";
    }
    const char *message = weather_view_message(state);
    return message != NULL ? message : "";
}

// epoch 秒 → "YYYY-MM-DD HH:MMZ"（UTC）。实现已上移到 app_clock：
// 用量页要显示同一件事，日历算法不该有两个副本。这里保留旧名字，
// 因为调用方（以及它们的测试）只关心"天气页的这行字"。
bool weather_view_format_utc(int32_t epoch, char *buf, size_t cap) {
    return app_clock_format_utc(epoch, buf, cap);
}

bool weather_view_updated(weather_view_state_t state, bool stale, int64_t age_ms,
                          int32_t fetched_epoch, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (state != WEATHER_VIEW_DATA) {
        return false;
    }

    if (stale) {
        // 跨重启的相对时间没有意义，只能用快照里存的绝对时刻。
        if (!weather_view_format_utc(fetched_epoch, buf, cap)) {
            snprintf(buf, cap, "Offline - saved");
        }
        return true;
    }

    if (age_ms < 0) {
        return false; // 不知道什么时候取的，就不编一个数字
    }

    int64_t minutes = age_ms / 60000;
    if (minutes < 1) {
        snprintf(buf, cap, "Updated just now");
    } else if (minutes <= 90) {
        snprintf(buf, cap, "Updated %d min ago", (int)minutes);
    } else {
        snprintf(buf, cap, "Updated %d h ago", (int)(minutes / 60));
    }
    return true;
}
