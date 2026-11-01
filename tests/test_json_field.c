// tests/test_json_field.c —— 最小 JSON 字段提取的 host 测试。
//
// 覆盖三类输入：真实形状的样本（Open-Meteo 风格的嵌套对象）、必须被拒绝的畸形
// 报文，以及路径语义本身的边界（段数、数组、同名键、不回溯）。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "json_field.h"

// 样本数字都是短的十进制字面量，strtod 与编译器对同一字符串做同样的最近值舍入，
// 所以直接比较即可。不用 fabs，避免主机测试额外依赖 libm。
static bool near(double got, double want) {
    return got == want;
}

static double number_or_fail(const char *doc, const char *path) {
    double value = -12345.0;

    assert(json_field_number(doc, path, &value));
    return value;
}

static void test_flat_object(void) {
    const char *doc = "{\"temperature\":12.5,\"name\":\"Beijing\",\"ok\":true}";
    char text[32];

    assert(near(number_or_fail(doc, "temperature"), 12.5));
    assert(json_field_string(doc, "name", text, sizeof text));
    assert(strcmp(text, "Beijing") == 0);
}

static void test_nested_object(void) {
    // 与 Open-Meteo 的 current / current_units 形状同构：同名字段在两层里各有一份。
    const char *doc =
        "{\"latitude\":52.52,"
        "\"current_units\":{\"temperature_2m\":\"\xC2\xB0\x43\"},"
        "\"current\":{\"time\":\"2026-10-31T10:00\",\"temperature_2m\":12.34,"
        "\"weather_code\":3}}";
    char text[32];

    assert(near(number_or_fail(doc, "current.temperature_2m"), 12.34));
    assert(near(number_or_fail(doc, "current.weather_code"), 3.0));
    assert(json_field_string(doc, "current.time", text, sizeof text));
    assert(strcmp(text, "2026-10-31T10:00") == 0);
    assert(json_field_string(doc, "current_units.temperature_2m", text, sizeof text));
    assert(strcmp(text, "\xC2\xB0\x43") == 0); // "°C"
}

static void test_string_escapes_are_decoded(void) {
    const char *doc =
        "{\"text\":\"a\\\"b\\\\c\\nd\","
        "\"cn\":\"\\u4e2d\\u6587\","
        "\"emoji\":\"\\ud83d\\ude00\"}";
    char text[64];

    assert(json_field_string(doc, "text", text, sizeof text));
    assert(strcmp(text, "a\"b\\c\nd") == 0);

    assert(json_field_string(doc, "cn", text, sizeof text));
    assert(strcmp(text, "\xE4\xB8\xAD\xE6\x96\x87") == 0); // "中文"

    assert(json_field_string(doc, "emoji", text, sizeof text));
    assert(strcmp(text, "\xF0\x9F\x98\x80") == 0); // U+1F600
}

static void test_escaped_key_still_matches(void) {
    assert(near(number_or_fail("{\"a\\u0062\":1}", "ab"), 1.0));
}

static void test_arrays_are_skipped_but_not_indexable(void) {
    const char *doc = "{\"list\":[1,{\"x\":2},\"s\"],\"after\":7}";

    // 跳过数组后仍要能找到后面的键。
    assert(near(number_or_fail(doc, "after"), 7.0));
    // 数组本体不是对象，路径不能穿过去。
    assert(!json_field_number(doc, "list.x", &(double){0}));
}

static void test_delimiters_inside_strings_do_not_confuse_the_scanner(void) {
    const char *doc = "{\"k\":\"}{[]\",\"after\":1}";
    char text[16];

    assert(json_field_string(doc, "k", text, sizeof text));
    assert(strcmp(text, "}{[]") == 0);
    assert(near(number_or_fail(doc, "after"), 1.0));
}

static void test_duplicate_keys_take_the_first_occurrence(void) {
    assert(near(number_or_fail("{\"a\":1,\"a\":2}", "a"), 1.0));
}

static void test_no_backtracking_between_duplicate_subtrees(void) {
    const char *doc = "{\"a\":{\"b\":1},\"a\":{\"c\":2}}";

    assert(near(number_or_fail(doc, "a.b"), 1.0));
    // 第一处 a 里没有 c，第二处不会再看一眼。
    assert(!json_field_number(doc, "a.c", &(double){0}));
}

static void test_depth_limit(void) {
    const char *doc = "{\"a\":{\"b\":{\"c\":{\"d\":9}}}}";

    assert(near(number_or_fail(doc, "a.b.c.d"), 9.0));
    // 五段路径直接判非法，不会因为文档里真有第五层就放行。
    assert(!json_field_number(doc, "a.b.c.d.e", &(double){0}));
}

static void test_malformed_paths_are_rejected(void) {
    const char *doc = "{\"a\":1}";
    static const char *paths[] = {"", ".a", "a.", "a..b", "a.b.c.d.e"};

    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        assert(!json_field_number(doc, paths[i], &(double){0}));
    }
    // 超长路径在进入扫描前就被拒绝。
    char long_path[128];
    memset(long_path, 'a', sizeof long_path);
    long_path[sizeof long_path - 1] = '\0';
    assert(!json_field_number(doc, long_path, &(double){0}));
}

static void test_string_output_must_fit_whole(void) {
    const char *doc = "{\"s\":\"abcdef\"}";
    char exact[7];
    char tight[6];
    char one[1];

    assert(json_field_string(doc, "s", exact, sizeof exact));
    assert(strcmp(exact, "abcdef") == 0);

    // 放不下就整体失败，绝不留 "abcde" 这样的半截值。
    memset(tight, 'Z', sizeof tight);
    assert(!json_field_string(doc, "s", tight, sizeof tight));
    assert(tight[0] == '\0');

    assert(!json_field_string(doc, "s", one, sizeof one));
    assert(one[0] == '\0');

    assert(!json_field_string(doc, "s", NULL, 8));
    assert(!json_field_string(doc, "s", exact, 0));
}

static void test_string_type_and_encoding_errors(void) {
    char text[16];

    memset(text, 'Z', sizeof text);
    assert(!json_field_string("{\"s\":123}", "s", text, sizeof text));
    assert(text[0] == '\0');

    assert(!json_field_string("{\"s\":\"abc}", "s", text, sizeof text));
    assert(!json_field_string("{\"s\":\"a\nb\"}", "s", text, sizeof text)); // 裸换行
    assert(!json_field_string("{\"s\":\"\\u0000\"}", "s", text, sizeof text)); // NUL
    assert(!json_field_string("{\"s\":\"\\ud83d\"}", "s", text, sizeof text)); // 孤立高代理
    assert(!json_field_string("{\"s\":\"\\udc00\"}", "s", text, sizeof text)); // 孤立低代理
    assert(!json_field_string("{\"s\":\"\\q\"}", "s", text, sizeof text));    // 未知转义
}

static void test_number_grammar(void) {
    assert(near(number_or_fail("{\"n\":0}", "n"), 0.0));
    assert(near(number_or_fail("{\"n\":-0.5e+2}", "n"), -50.0));
    assert(near(number_or_fail("{\"n\": 42 }", "n"), 42.0));

    static const char *bad[] = {
        "{\"n\":01}",
        "{\"n\":-}",
        "{\"n\":.5}",
        "{\"n\":1.}",
        "{\"n\":1e}",
        "{\"n\":1e+}",
        "{\"n\":1x}",
        "{\"n\":\"12\"}",
        "{\"n\":true}",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        double value = 12345.0;
        assert(!json_field_number(bad[i], "n", &value));
        assert(value == 12345.0); // 失败时不改写调用方的值
    }

    assert(!json_field_number("{\"n\":1}", "n", NULL));
}

static void test_index_overflow_is_rejected(void) {
    double value = 12345.0;

    // 1e999 会被 strtod 解析成 inf；这种值不能进 UI。
    assert(!json_field_number("{\"n\":1e999}", "n", &value));
    assert(value == 12345.0);
}

static void test_missing_keys_and_broken_documents(void) {
    static const char *docs[] = {
        "",
        "[]",
        "null",
        "{\"other\":1}",
        "{\"a\":}",
        "{\"a\" 1}",
        "{\"a\":1 x}",
        "{\"a\":{\"b\":1}",
        "{a:1}",
    };

    for (size_t i = 0; i < sizeof docs / sizeof docs[0]; i++) {
        assert(!json_field_number(docs[i], "a", &(double){0}));
        assert(!json_field_string(docs[i], "a", (char[8]){0}, 8));
    }

    assert(!json_field_number(NULL, "a", &(double){0}));
    assert(!json_field_string(NULL, "a", (char[8]){0}, 8));
}

static void test_lazy_validation_stops_at_the_value(void) {
    // 文档缺右括号，但命中值本身完整。本模块只校验路径与值，不重扫整篇文档；
    // 残缺响应应在 HTTP 层被长度或截断检查拦住（见 json_field.h 的说明）。
    assert(near(number_or_fail("{\"a\":12", "a"), 12.0));
}

static void test_skip_depth_is_bounded(void) {
    // 20 层数组超过跳过上限：不是崩溃，而是"这个值跳不过去"，于是否定整个查找。
    char doc[128];
    size_t at = 0;

    doc[at++] = '{';
    memcpy(doc + at, "\"deep\":", 7);
    at += 7;
    for (int i = 0; i < 20; i++) {
        doc[at++] = '[';
    }
    for (int i = 0; i < 20; i++) {
        doc[at++] = ']';
    }
    memcpy(doc + at, ",\"after\":1}", 12);
    at += 12;
    doc[at] = '\0';

    assert(!json_field_number(doc, "after", &(double){0}));
}

int main(void) {
    test_flat_object();
    test_nested_object();
    test_string_escapes_are_decoded();
    test_escaped_key_still_matches();
    test_arrays_are_skipped_but_not_indexable();
    test_delimiters_inside_strings_do_not_confuse_the_scanner();
    test_duplicate_keys_take_the_first_occurrence();
    test_no_backtracking_between_duplicate_subtrees();
    test_depth_limit();
    test_malformed_paths_are_rejected();
    test_string_output_must_fit_whole();
    test_string_type_and_encoding_errors();
    test_number_grammar();
    test_index_overflow_is_rejected();
    test_missing_keys_and_broken_documents();
    test_lazy_validation_stops_at_the_value();
    test_skip_depth_is_bounded();
    puts("json field tests: PASS");
    return 0;
}
