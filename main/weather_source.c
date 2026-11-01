// main/weather_source.c —— Open-Meteo 请求与解析的实现。契约见 weather_source.h。
#include "weather_source.h"

#include <stdio.h>
#include <string.h>

#include "app_location.h"
#include "json_field.h"

// Open-Meteo 的当前天气端点。timezone=auto 让 current.time 直接是当地观测时间，
// 省掉一份时区表。
static const char WEATHER_URL_PREFIX[] =
    "https://api.open-meteo.com/v1/forecast?latitude=";
static const char WEATHER_URL_MIDDLE[] =
    "&longitude=";
static const char WEATHER_URL_SUFFIX[] =
    "&current=temperature_2m,weather_code&timezone=auto";

bool weather_url_build(const app_location_t *loc, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (loc == NULL || !loc->has_location) {
        return false;
    }

    // 坐标以整数微度拼成 4 位小数，不经过浮点格式化。
    char lat[16];
    char lon[16];
    if (!app_location_format_4dp(loc->lat_udeg, lat, sizeof lat)) {
        return false;
    }
    if (!app_location_format_4dp(loc->lon_udeg, lon, sizeof lon)) {
        return false;
    }

    int written = snprintf(buf, cap, "%s%s%s%s%s", WEATHER_URL_PREFIX, lat,
                           WEATHER_URL_MIDDLE, lon, WEATHER_URL_SUFFIX);
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

const char *weather_code_text(int code) {
    // WMO 4677 的子集：Open-Meteo 当前天气只用到这些码。
    switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: return "Fog";
    case 48: return "Rime fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Dense drizzle";
    case 56: return "Freezing drizzle";
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: return "Freezing rain";
    case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Rain showers";
    case 81: return "Rain showers";
    case 82: return "Violent showers";
    case 85: return "Snow showers";
    case 86: return "Heavy snow showers";
    case 95: return "Thunderstorm";
    case 96: return "Storm with hail";
    case 99: return "Storm with hail";
    default: return "Unknown";
    }
}

bool weather_format_temp(int16_t temp_dc, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }

    // 先扩到 int32：INT16_MIN 取反会在 int16 溢出。
    int32_t value = temp_dc;
    bool negative = value < 0;
    int32_t magnitude = negative ? -value : value;

    int written = snprintf(buf, cap, "%s%ld.%ld", negative ? "-" : "",
                           (long)(magnitude / 10), (long)(magnitude % 10));
    if (written < 0 || (size_t)written >= cap) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

bool weather_format_observed(const char *iso, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    if (iso == NULL || cap < 6) {
        return false;
    }

    const char *t = strchr(iso, 'T');
    if (t == NULL) {
        return false;
    }
    const char *h = t + 1;

    // 只要 HH:MM 五位；后面有没有 :SS 都无所谓。
    if (h[0] < '0' || h[0] > '9' || h[1] < '0' || h[1] > '9' || h[2] != ':' ||
        h[3] < '0' || h[3] > '9' || h[4] < '0' || h[4] > '9') {
        return false;
    }
    memcpy(buf, h, 5);
    buf[5] = '\0';
    return true;
}

// 十进制度 → 十分之一度，四舍五入。不用 libm：加减 0.5 后截断即可，
// 天气温度不存在需要 round-half-to-even 的场景。
static bool temp_to_tenths(double celsius, int16_t *out) {
    double scaled = celsius * 10.0;
    // 先挡住明显越界的值，保证下面的 double → int32 转换安全。
    if (scaled > 40000.0 || scaled < -40000.0) {
        return false;
    }

    // 四舍五入后再比较：拿 +0.5 之前的 double 去比上限，会把 100.0 判成越界。
    double rounded = scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5;
    int32_t tenths = (int32_t)rounded;
    if (tenths > WEATHER_TEMP_LIMIT_DC || tenths < -WEATHER_TEMP_LIMIT_DC) {
        return false;
    }
    *out = (int16_t)tenths;
    return true;
}

bool weather_parse_current(const char *json, weather_state_t *out) {
    if (json == NULL || out == NULL) {
        return false;
    }

    double celsius = 0.0;
    if (!json_field_number(json, "current.temperature_2m", &celsius)) {
        return false;
    }

    double code = 0.0;
    if (!json_field_number(json, "current.weather_code", &code)) {
        return false;
    }
    if (code < 0.0 || code > (double)WEATHER_CODE_MAX) {
        return false; // 明显不是 WMO 码，宁可当作解析失败
    }

    int16_t temp_dc = 0;
    if (!temp_to_tenths(celsius, &temp_dc)) {
        return false;
    }

    memset(out, 0, sizeof *out);
    out->has_data = true;
    out->stale = false;
    out->temp_dc = temp_dc;
    out->weather_code = (int16_t)code;

    // 观测时间缺失不影响"有温度"这个事实，只是少一行信息。
    (void)json_field_string(json, "current.time", out->observed, sizeof out->observed);
    return true;
}
