// tests/test_weather_source.c —— Open-Meteo 请求与解析的主机测试。
//
// 报文样本按真实响应的形状手写：嵌套 current 对象、浮点温度、WMO 码、
// 以及一个带转义的 units 字符串。断言只依赖"接口约定"（URL 黄金串、码到文案、
// 十分之一度），不依赖实现细节。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "weather_source.h"

static app_location_t make_location(int32_t lat, int32_t lon, const char *place) {
    app_location_t loc;
    assert(app_location_set(&loc, lat, lon, place));
    return loc;
}

static void test_url_uses_four_decimals_and_the_current_endpoint(void) {
    app_location_t loc = make_location(39904200, 116407400, "Beijing");
    char url[WEATHER_URL_MAX];

    assert(weather_url_build(&loc, url, sizeof url));
    assert(strcmp(url,
                  "https://api.open-meteo.com/v1/forecast?latitude=39.9042&longitude=116.4074"
                  "&current=temperature_2m,weather_code&timezone=auto") == 0);
}

static void test_url_keeps_negative_coordinates(void) {
    app_location_t loc = make_location(-33500000, -70600000, "Santiago");
    char url[WEATHER_URL_MAX];

    assert(weather_url_build(&loc, url, sizeof url));
    assert(strstr(url, "latitude=-33.5000") != NULL);
    assert(strstr(url, "longitude=-70.6000") != NULL);
}

static void test_url_requires_a_location(void) {
    app_location_t loc;
    app_location_clear(&loc);
    char url[WEATHER_URL_MAX];

    assert(!weather_url_build(&loc, url, sizeof url));
    assert(url[0] == '\0');
    assert(!weather_url_build(NULL, url, sizeof url));
}

static void test_url_buffer_is_all_or_nothing(void) {
    app_location_t loc = make_location(0, 0, NULL);
    char url[16];
    memset(url, 'Z', sizeof url);

    assert(!weather_url_build(&loc, url, sizeof url));
    assert(url[0] == '\0');

    assert(!weather_url_build(&loc, NULL, 64));
    assert(!weather_url_build(&loc, url, 0));
}

static void test_weather_codes_map_to_ascii(void) {
    assert(strcmp(weather_code_text(0), "Clear") == 0);
    assert(strcmp(weather_code_text(2), "Partly cloudy") == 0);
    assert(strcmp(weather_code_text(3), "Overcast") == 0);
    assert(strcmp(weather_code_text(45), "Fog") == 0);
    assert(strcmp(weather_code_text(65), "Heavy rain") == 0);
    assert(strcmp(weather_code_text(75), "Heavy snow") == 0);
    assert(strcmp(weather_code_text(95), "Thunderstorm") == 0);
    assert(strcmp(weather_code_text(99), "Storm with hail") == 0);
    assert(strcmp(weather_code_text(42), "Unknown") == 0);
    assert(strcmp(weather_code_text(-1), "Unknown") == 0);

    for (int code = -2; code < 120; code += 7) {
        const char *text = weather_code_text(code);
        assert(text != NULL);
        for (const char *p = text; *p != '\0'; p++) {
            assert((unsigned char)*p > 0x1F && (unsigned char)*p < 0x7F);
        }
    }
}

static void test_temperature_is_one_decimal(void) {
    char buf[16];

    assert(weather_format_temp(123, buf, sizeof buf));
    assert(strcmp(buf, "12.3") == 0);

    assert(weather_format_temp(-5, buf, sizeof buf));
    assert(strcmp(buf, "-0.5") == 0);

    assert(weather_format_temp(0, buf, sizeof buf));
    assert(strcmp(buf, "0.0") == 0);

    assert(weather_format_temp(-32768, buf, sizeof buf));
    assert(strcmp(buf, "-3276.8") == 0);

    memset(buf, 'Z', sizeof buf);
    assert(!weather_format_temp(123, buf, 4)); // "12.3" 需要 5 字节含 NUL
    assert(buf[0] == '\0');

    assert(!weather_format_temp(123, NULL, 16));
}

static void test_observed_time_is_trimmed_to_hh_mm(void) {
    char buf[8];

    assert(weather_format_observed("2026-11-01T14:00", buf, sizeof buf));
    assert(strcmp(buf, "14:00") == 0);

    assert(weather_format_observed("2026-11-01T09:05:00", buf, sizeof buf));
    assert(strcmp(buf, "09:05") == 0);

    assert(!weather_format_observed("2026-11-01 14:00", buf, sizeof buf)); // 没有 T
    assert(!weather_format_observed("2026-11-01T1", buf, sizeof buf));
    assert(!weather_format_observed("2026-11-01T14-00", buf, sizeof buf));
    assert(!weather_format_observed(NULL, buf, sizeof buf));
    assert(buf[0] == '\0');

    memset(buf, 'Z', sizeof buf);
    assert(!weather_format_observed("2026-11-01T14:00", buf, 5)); // 需要 6 字节
    assert(buf[0] == '\0');
}

// 与 Open-Meteo 实际响应同构：多余字段、嵌套对象、带转义的单位字符串。
static const char *SAMPLE =
    "{\"latitude\":39.875,\"longitude\":116.375,\"generationtime_ms\":0.05,"
    "\"utc_offset_seconds\":28800,\"timezone\":\"Asia/Shanghai\","
    "\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\","
    "\"temperature_2m\":\"\\u00b0C\",\"weather_code\":\"wmo code\"},"
    "\"current\":{\"time\":\"2026-11-01T14:00\",\"interval\":900,"
    "\"temperature_2m\":12.3,\"weather_code\":3}}";

static void test_parse_current_extracts_the_three_fields(void) {
    weather_state_t w;

    assert(weather_parse_current(SAMPLE, &w));
    assert(w.has_data);
    assert(!w.stale);
    assert(w.temp_dc == 123);
    assert(w.weather_code == 3);
    assert(strcmp(w.observed, "2026-11-01T14:00") == 0);
}

static void test_parse_negative_and_rounded_temperatures(void) {
    weather_state_t w;

    assert(weather_parse_current(
        "{\"current\":{\"temperature_2m\":-0.5,\"weather_code\":0}}", &w));
    assert(w.temp_dc == -5);

    // 12.36 → 123.6 → 四舍五入到 124。
    assert(weather_parse_current(
        "{\"current\":{\"temperature_2m\":12.36,\"weather_code\":1}}", &w));
    assert(w.temp_dc == 124);
}

static void test_physical_bounds_are_enforced(void) {
    weather_state_t w;

    // 边界内照收。
    assert(weather_parse_current(
        "{\"current\":{\"temperature_2m\":100.0,\"weather_code\":99}}", &w));
    assert(w.temp_dc == 1000);
    assert(weather_parse_current(
        "{\"current\":{\"temperature_2m\":-100.0,\"weather_code\":0}}", &w));
    assert(w.temp_dc == -1000);

    // 越界按解析失败处理：999 度不该画到屏幕上。
    assert(!weather_parse_current(
        "{\"current\":{\"temperature_2m\":100.1,\"weather_code\":0}}", &w));
    assert(!weather_parse_current(
        "{\"current\":{\"temperature_2m\":-100.1,\"weather_code\":0}}", &w));
    assert(!weather_parse_current(
        "{\"current\":{\"temperature_2m\":999,\"weather_code\":0}}", &w));
    assert(!weather_parse_current(
        "{\"current\":{\"temperature_2m\":0,\"weather_code\":100}}", &w));
    assert(!weather_parse_current(
        "{\"current\":{\"temperature_2m\":0,\"weather_code\":-1}}", &w));
}

static void test_parse_rejects_incomplete_or_wrong_typed_payloads(void) {
    weather_state_t w;
    static const char *bad[] = {
        "",
        "{}",
        "{\"current\":{}}",
        "{\"current\":{\"temperature_2m\":12.3}}",                       // 缺天气码
        "{\"current\":{\"weather_code\":3}}",                            // 缺温度
        "{\"current\":{\"temperature_2m\":\"12.3\",\"weather_code\":3}}", // 类型错
        "{\"current\":{\"temperature_2m\":12.3,\"weather_code\":\"3\"}}",
        "{\"current\":{\"temperature_2m\":12.3,\"weather_code\":2000}}", // 越界码
        "{\"current\":{\"temperature_2m\":99999,\"weather_code\":3}}",   // 温度溢出
        "{\"current\":{\"temperature_2m\":12.,\"weather_code\":3}}",     // 数值截断
    };

    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        assert(!weather_parse_current(bad[i], &w));
    }
    assert(!weather_parse_current(NULL, &w));
    assert(!weather_parse_current(SAMPLE, NULL));
}

static void test_parse_tolerates_a_missing_observation_time(void) {
    weather_state_t w;

    assert(weather_parse_current(
        "{\"current\":{\"temperature_2m\":12.3,\"weather_code\":3}}", &w));
    assert(w.observed[0] == '\0');
}

int main(void) {
    test_url_uses_four_decimals_and_the_current_endpoint();
    test_url_keeps_negative_coordinates();
    test_url_requires_a_location();
    test_url_buffer_is_all_or_nothing();
    test_weather_codes_map_to_ascii();
    test_temperature_is_one_decimal();
    test_observed_time_is_trimmed_to_hh_mm();
    test_parse_current_extracts_the_three_fields();
    test_parse_negative_and_rounded_temperatures();
    test_physical_bounds_are_enforced();
    test_parse_rejects_incomplete_or_wrong_typed_payloads();
    test_parse_tolerates_a_missing_observation_time();
    puts("weather source tests: PASS");
    return 0;
}
