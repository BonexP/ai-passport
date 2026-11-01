// tests/test_app_location.c —— 地点模型的主机测试。
//
// 重点覆盖三处容易出错的地方：微度换算的截断/越界、失败时不能留下半条地点、
// 以及格式化必须与 URL 拼装使用的 4 位小数完全一致。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_location.h"

static void test_clear_zeroes_every_field(void) {
    app_location_t loc;

    memset(&loc, 0xFF, sizeof loc);
    app_location_clear(&loc);

    assert(!loc.has_location);
    assert(loc.lat_udeg == 0);
    assert(loc.lon_udeg == 0);
    assert(loc.place[0] == '\0');
}

static void test_set_accepts_valid_location(void) {
    app_location_t loc;

    assert(app_location_set(&loc, 39904200, 116407400, "Beijing"));
    assert(loc.has_location);
    assert(loc.lat_udeg == 39904200);
    assert(loc.lon_udeg == 116407400);
    assert(strcmp(loc.place, "Beijing") == 0);
    assert(app_location_is_valid(&loc));
}

static void test_set_allows_empty_place(void) {
    app_location_t loc;

    assert(app_location_set(&loc, -33500000, 151200000, NULL));
    assert(loc.has_location);
    assert(loc.place[0] == '\0');
    assert(app_location_is_valid(&loc));

    assert(app_location_set(&loc, -33500000, 151200000, ""));
    assert(loc.place[0] == '\0');
}

static void test_set_rejects_out_of_range_and_clears(void) {
    app_location_t loc;

    assert(app_location_set(&loc, 10000000, 20000000, "Home"));

    // 纬度 90.000001°，必须整体拒绝，并把上一次的地点清空。
    assert(!app_location_set(&loc, 90000001, 0, "Home"));
    assert(!loc.has_location);
    assert(loc.place[0] == '\0');

    assert(!app_location_set(&loc, 0, 180000001, "Home"));
    assert(!loc.has_location);
    assert(!app_location_set(&loc, -90000001, 0, "Home"));
    assert(!app_location_set(&loc, 0, -180000001, "Home"));
}

static void test_set_rejects_bad_place(void) {
    app_location_t loc;
    char too_long[APP_LOCATION_PLACE_MAX + 1];

    memset(too_long, 'A', sizeof too_long);
    too_long[sizeof too_long - 1] = '\0'; // 恰好 24 字节：超出上限

    assert(!app_location_set(&loc, 0, 0, too_long));
    assert(!loc.has_location);

    assert(!app_location_set(&loc, 0, 0, "bad\nname")); // 控制字符
    assert(!app_location_set(&loc, 0, 0, "bad\x7fname"));

    too_long[APP_LOCATION_PLACE_MAX - 1] = '\0'; // 23 字节：上限内
    assert(app_location_set(&loc, 0, 0, too_long));
    assert(strlen(loc.place) == APP_LOCATION_PLACE_MAX - 1);
}

static void test_validity_checks_consistency(void) {
    app_location_t loc;

    app_location_clear(&loc);
    assert(app_location_is_valid(&loc));

    // 未配置却带地点名：某处只改了一半，必须判为不自洽。
    strcpy(loc.place, "Home");
    assert(!app_location_is_valid(&loc));

    app_location_clear(&loc);
    loc.has_location = true;
    loc.lat_udeg = 0;
    loc.lon_udeg = 0;
    assert(app_location_is_valid(&loc));

    loc.lat_udeg = APP_LOCATION_LAT_UDEG_MAX + 1;
    assert(!app_location_is_valid(&loc));

    // place 缓冲区内没有 NUL：越界风险，判非法。
    memset(&loc, 0, sizeof loc);
    loc.has_location = true;
    memset(loc.place, 'A', sizeof loc.place);
    assert(!app_location_is_valid(&loc));

    assert(!app_location_is_valid(NULL));
}

static void test_parse_decimal_degrees(void) {
    int32_t udeg = 0;

    assert(app_location_parse_coord("39.9042", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 39904200);

    assert(app_location_parse_coord("-73.935242", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == -73935242);

    assert(app_location_parse_coord("+12.5", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 12500000);

    assert(app_location_parse_coord("0", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 0);

    assert(app_location_parse_coord(" 39.9\t", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 39900000);

    assert(app_location_parse_coord("0039.9", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 39900000);

    assert(app_location_parse_coord("90", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 90000000);

    assert(app_location_parse_coord("-90", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == -90000000);

    assert(app_location_parse_coord("180", APP_LOCATION_LON_UDEG_MAX, &udeg));
    assert(udeg == 180000000);

    // 超过 6 位的小数被截断，不做四舍五入。
    assert(app_location_parse_coord("39.1234567", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(udeg == 39123456);
}

static void test_parse_rejects_bad_text(void) {
    int32_t udeg = 12345;

    static const char *bad[] = {
        "", "   ", "+", "-", ".", ".5", "12.", "12..3", "12,5", "1e3", "abc",
        "39 9", "39.9.9", "--39", "39-9", "1234567890",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        assert(!app_location_parse_coord(bad[i], APP_LOCATION_LAT_UDEG_MAX, &udeg));
        assert(udeg == 12345); // 失败不改写输出
    }

    assert(!app_location_parse_coord(NULL, APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(!app_location_parse_coord("39.9", 0, &udeg));
    assert(!app_location_parse_coord("39.9", APP_LOCATION_LAT_UDEG_MAX, NULL));

    // 范围按调用方给的 limit 判定，而不是写死 90/180。
    assert(!app_location_parse_coord("90.000001", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(!app_location_parse_coord("-90.000001", APP_LOCATION_LAT_UDEG_MAX, &udeg));
    assert(!app_location_parse_coord("180.0001", APP_LOCATION_LON_UDEG_MAX, &udeg));
    assert(!app_location_parse_coord("170", APP_LOCATION_LAT_UDEG_MAX, &udeg));
}

static void test_format_fixed_four_decimals(void) {
    char buf[24];

    assert(app_location_format_4dp(39904200, buf, sizeof buf));
    assert(strcmp(buf, "39.9042") == 0);

    assert(app_location_format_4dp(-73935242, buf, sizeof buf));
    assert(strcmp(buf, "-73.9352") == 0); // 截断，不四舍五入

    assert(app_location_format_4dp(0, buf, sizeof buf));
    assert(strcmp(buf, "0.0000") == 0);

    assert(app_location_format_4dp(APP_LOCATION_LAT_UDEG_MAX, buf, sizeof buf));
    assert(strcmp(buf, "90.0000") == 0);

    assert(app_location_format_4dp(-APP_LOCATION_LON_UDEG_MAX, buf, sizeof buf));
    assert(strcmp(buf, "-180.0000") == 0);

    // "39.9042" 需要 8 字节含 NUL。
    assert(app_location_format_4dp(39904200, buf, 8));
    assert(strcmp(buf, "39.9042") == 0);

    memset(buf, 'Z', sizeof buf);
    assert(!app_location_format_4dp(39904200, buf, 7));
    assert(buf[0] == '\0');

    assert(!app_location_format_4dp(0, NULL, 8));
    assert(!app_location_format_4dp(0, buf, 0));
}

int main(void) {
    test_clear_zeroes_every_field();
    test_set_accepts_valid_location();
    test_set_allows_empty_place();
    test_set_rejects_out_of_range_and_clears();
    test_set_rejects_bad_place();
    test_validity_checks_consistency();
    test_parse_decimal_degrees();
    test_parse_rejects_bad_text();
    test_format_fixed_four_decimals();
    puts("app location tests: PASS");
    return 0;
}
