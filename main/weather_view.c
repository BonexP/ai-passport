// main/weather_view.c —— 天气页状态与文案的纯逻辑实现。契约见 weather_view.h。
#include "weather_view.h"

#include <stdio.h>
#include <string.h>

weather_view_state_t weather_view_state(bool connected, bool located, bool time_valid,
                                        bool has_data, bool waited_long) {
    // 顺序即优先级：有旧数据 > 没网 > 没地点 > 没校时 > 等太久 > 正在取。
    if (has_data) return WEATHER_VIEW_DATA;
    if (!connected) return WEATHER_VIEW_NO_NETWORK;
    if (!located) return WEATHER_VIEW_NO_LOCATION;
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

// epoch 秒 → 公历年月日（Howard Hinnant 的 civil_from_days），不依赖 libc 的 gmtime：
// 主机与固件行为一致，也避免时区环境变量影响结果。
static void civil_from_days(int64_t days, int *year, unsigned *month, unsigned *day) {
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;

    *year = y + (m <= 2 ? 1 : 0);
    *month = m;
    *day = d;
}

bool weather_view_format_utc(int32_t epoch, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (epoch <= 0) {
        return false;
    }

    int64_t days = epoch / 86400;
    int64_t seconds = epoch % 86400;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, &year, &month, &day);

    int written = snprintf(buf, cap, "%04d-%02u-%02u %02u:%02uZ", year, month, day,
                           (unsigned)(seconds / 3600), (unsigned)((seconds % 3600) / 60));
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
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
