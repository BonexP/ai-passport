// tests/test_usage_source.c —— 用量请求组装、解析与格式化的主机测试。
//
// 报文形状取自 AMD Radeon Cloud 官方文档（2026-11-10 读取）的 /v1/usage 示例。
// 这里覆盖的是本模块的取舍，不是 TLS 与网络本身：
//   - URL 永不包含 key；
//   - 点数以万分位整数落地，绝不按货币解释；
//   - by_model / recent 这类数组不得让解析失败；
//   - 缺字段、null、类型不符、负数、越界一律判失败，不显示半截数字。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "usage_source.h"

// 文档里的代表性报文（压缩成一行）。
static const char DOC_EXAMPLE[] =
    "{"
    "\"daily_cost_limit_usd\":2.0,"
    "\"daily_cost_used_usd\":0.4512,"
    "\"daily_cost_remaining_usd\":1.5488,"
    "\"daily_reset_at\":\"2026-11-12T00:00:00+08:00\","
    "\"daily_reset_timezone\":\"Asia/Shanghai\","
    "\"today\":{\"requests\":42,\"errors\":3,\"total_tokens\":185000,"
    "\"prompt_tokens\":150000,\"completion_tokens\":35000,"
    "\"cost\":0.4512,\"last_request_at\":\"2026-11-11T23:59:01+08:00\"},"
    "\"last_30_days\":{\"requests\":900,\"total_tokens\":4000000,\"cost\":12.5},"
    "\"all_time\":{\"requests\":5000,\"total_tokens\":20000000,\"cost\":60.0},"
    "\"rpm_limit\":30,"
    "\"by_model\":[{\"model\":\"m1\",\"requests\":10}],"
    "\"recent\":[{\"at\":\"2026-11-11T23:59:01+08:00\"}]"
    "}";

static void test_url_never_contains_the_key(void) {
    char url[USAGE_URL_MAX];
    assert(usage_url_build(url, sizeof url));
    assert(strcmp(url, "https://developer.amd.com.cn/radeon/api/v1/usage") == 0);
    assert(strstr(url, "rc-") == NULL);
    assert(strstr(url, "Bearer") == NULL);
    assert(strstr(url, "key") == NULL);
    assert(strstr(url, "include_recent") == NULL);
}

static void test_url_buffer_contract(void) {
    char small[8];
    memset(small, 'x', sizeof small);
    assert(!usage_url_build(small, sizeof small));
    assert(small[0] == '\0');
    assert(!usage_url_build(NULL, 64));
    assert(!usage_url_build(small, 0));
}

static void test_parse_documented_example(void) {
    usage_state_t s;
    assert(usage_parse(DOC_EXAMPLE, &s));
    assert(s.has_data);
    assert(s.has_limit);
    assert(s.limit_1e4 == 20000);
    assert(s.used_1e4 == 4512);
    assert(s.remaining_1e4 == 15488);
    assert(s.today_requests == 42);
    assert(s.today_errors == 3);
    assert(s.today_total_tokens == 185000);
    assert(strcmp(s.reset_at, "2026-11-12T00:00:00+08:00") == 0);
}


static void test_arrays_and_extra_keys_are_ignored(void) {
    // by_model / recent / last_30_days / all_time 都是本模块不碰的东西：
    // 它们存在不得改变结果，更不能让解析失败。
    usage_state_t s;
    assert(usage_parse(DOC_EXAMPLE, &s));
    assert(s.has_limit);
}

// 类型不符（字符串）→ 未知额度但整报文仍可用；数值越界 → 整报文失败（单位可能不是 points）。
static void test_wrong_type_limit_is_unknown_but_bad_range_fails(void) {
    usage_state_t s;
    assert(usage_parse("{\"daily_cost_limit_usd\":\"2.0\",\"daily_cost_used_usd\":1,"
                       "\"daily_cost_remaining_usd\":1,"
                       "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}", &s));
    assert(!s.has_limit);
    assert(s.used_1e4 == 10000);

    // 负数：数值越界 → 整报文失败
    assert(!usage_parse("{\"daily_cost_limit_usd\":-1,\"daily_cost_used_usd\":1,"
                        "\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}", &s));
    // 大于 214748.3647：超出万分位 int32 上限 → 整报文失败
    assert(!usage_parse("{\"daily_cost_limit_usd\":2147483650,\"daily_cost_used_usd\":1,"
                        "\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}", &s));
}

static void test_missing_cap_is_unknown_not_zero(void) {
    const char json[] =
        "{\"daily_cost_used_usd\":1.5,\"daily_cost_remaining_usd\":2.5,"
        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":10}}";
    usage_state_t s;
    assert(usage_parse(json, &s));
    assert(!s.has_limit); // 关键：缺失 ≠ 额度为 0
    assert(s.used_1e4 == 15000);
    assert(s.remaining_1e4 == 25000);
}

static void test_required_fields_missing_fail(void) {
    usage_state_t s;
    const char *bases[] = {
        "{\"daily_cost_remaining_usd\":1,\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}",
        "{\"daily_cost_used_usd\":1,\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}",
        "{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
        "\"today\":{\"errors\":0,\"total_tokens\":1}}",
        "{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
        "\"today\":{\"requests\":1,\"total_tokens\":1}}",
        "{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
        "\"today\":{\"requests\":1,\"errors\":0}}",
        "{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1}",
    };
    for (size_t i = 0; i < sizeof bases / sizeof bases[0]; i++) {
        assert(!usage_parse(bases[i], &s));
    }
}

static void test_null_and_wrong_types_fail(void) {
    usage_state_t s;
    assert(!usage_parse("{\"daily_cost_used_usd\":null,\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}", &s));
    assert(!usage_parse("{\"daily_cost_used_usd\":\"1.5\",\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}", &s));
    assert(!usage_parse("{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":\"1\",\"errors\":0,\"total_tokens\":1}}", &s));
    assert(!usage_parse("{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":true,\"errors\":0,\"total_tokens\":1}}", &s));
}

// 可选字段显式写成 null：整报文仍可接受，但该字段必须是"未知"，不能变成 0。
static void test_explicit_null_optionals_stay_unknown(void) {
    usage_state_t s;
    assert(usage_parse("{\"daily_cost_limit_usd\":null,\"daily_cost_used_usd\":1,"
                       "\"daily_cost_remaining_usd\":1,"
                       "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}",
                       &s));
    assert(!s.has_limit);          // null ≠ 0 额度
    assert(s.used_1e4 == 10000);

    assert(usage_parse("{\"daily_reset_at\":null,\"daily_cost_used_usd\":1,"
                       "\"daily_cost_remaining_usd\":1,"
                       "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}",
                       &s));
    assert(s.has_data);
    assert(s.reset_at[0] == '\0'); // 重置时刻缺失 → 空串，页面据此不显示
}

static void test_negative_and_out_of_range_fail(void) {
    usage_state_t s;
    assert(!usage_parse("{\"daily_cost_used_usd\":-1,\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1}}", &s));
    assert(!usage_parse("{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":-5,\"errors\":0,\"total_tokens\":1}}", &s));
    assert(!usage_parse("{\"daily_cost_used_usd\":1,\"daily_cost_remaining_usd\":1,"
                        "\"today\":{\"requests\":1,\"errors\":0,\"total_tokens\":1000000001}}", &s));
}

static void test_truncated_document_fails(void) {
    usage_state_t s;
    // 截在 today 之后：必需字段缺失。
    assert(!usage_parse("{\"daily_cost_used_usd\":1.5,\"daily_cost_remaining_usd\":2.0,", &s));
    // 数组/标量不是用量报文。
    assert(!usage_parse("[1,2,3]", &s));
    assert(!usage_parse("42", &s));
    assert(!usage_parse("", &s));
    assert(!usage_parse(NULL, &s));
    assert(!usage_parse(DOC_EXAMPLE, NULL));
}

static void test_format_points(void) {
    char buf[32];
    assert(usage_format_points(32500, buf, sizeof buf));
    assert(strcmp(buf, "3.2500") == 0);
    assert(usage_format_points(32, buf, sizeof buf));
    assert(strcmp(buf, "0.0032") == 0);
    assert(usage_format_points(0, buf, sizeof buf));
    assert(strcmp(buf, "0.0000") == 0);
    assert(usage_format_points(12345678, buf, sizeof buf));
    assert(strcmp(buf, "1234.5678") == 0);
    assert(usage_format_points(15488, buf, sizeof buf));
    assert(strcmp(buf, "1.5488") == 0);
    // 负值与 INT32_MIN 不得崩。
    assert(usage_format_points(-15488, buf, sizeof buf));
    assert(strcmp(buf, "-1.5488") == 0);
    assert(usage_format_points(INT32_MIN, buf, sizeof buf));
    assert(strncmp(buf, "-214748.3648", 12) == 0);
    // 缓冲不足：清空并返回 false。
    char tiny[4] = {0};
    assert(!usage_format_points(32500, tiny, sizeof tiny));
    assert(tiny[0] == '\0');
    assert(!usage_format_points(32500, NULL, 8));
    assert(!usage_format_points(32500, buf, 0));
    // 永不出现美元符号。
    assert(strchr(buf, '$') == NULL);
}

static void test_format_compact(void) {
    char buf[16];
    assert(usage_format_compact(2100, buf, sizeof buf));
    assert(strcmp(buf, "2100") == 0);
    assert(usage_format_compact(0, buf, sizeof buf));
    assert(strcmp(buf, "0") == 0);
    assert(usage_format_compact(1234567, buf, sizeof buf));
    assert(strcmp(buf, "1.2M") == 0);
    assert(usage_format_compact(88200123, buf, sizeof buf));
    assert(strcmp(buf, "88.2M") == 0);
    assert(usage_format_compact(999999999, buf, sizeof buf));
    assert(strcmp(buf, "1000.0M") == 0); // 进位边界的诚实结果，不做花哨修正
    assert(usage_format_compact(1000000000, buf, sizeof buf));
    assert(strcmp(buf, "1.0B+") == 0);
    assert(usage_format_compact(-77, buf, sizeof buf));
    assert(strcmp(buf, "0") == 0);
    char tiny[3] = {0};
    assert(!usage_format_compact(88200123, tiny, sizeof tiny));
    assert(tiny[0] == '\0');
}

static void test_format_reset_clock(void) {
    char buf[16];
    assert(usage_format_reset_clock("2026-11-12T00:00:00+08:00", buf, sizeof buf));
    assert(strcmp(buf, "00:00") == 0);
    assert(usage_format_reset_clock("2026-11-12T23:59:59Z", buf, sizeof buf));
    assert(strcmp(buf, "23:59") == 0);
    assert(!usage_format_reset_clock("2026-11-12", buf, sizeof buf));
    assert(!usage_format_reset_clock("2026-11-12Tab:00", buf, sizeof buf));
    assert(!usage_format_reset_clock(NULL, buf, sizeof buf));
    assert(!usage_format_reset_clock("", buf, sizeof buf));
    char tiny[4] = {0};
    assert(!usage_format_reset_clock("2026-11-12T00:00:00Z", tiny, sizeof tiny));
    assert(tiny[0] == '\0');
}

int main(void) {
    test_url_never_contains_the_key();
    test_url_buffer_contract();
    test_parse_documented_example();
    test_arrays_and_extra_keys_are_ignored();
    test_missing_cap_is_unknown_not_zero();
    test_wrong_type_limit_is_unknown_but_bad_range_fails();
    test_required_fields_missing_fail();
    test_null_and_wrong_types_fail();
    test_explicit_null_optionals_stay_unknown();
    test_negative_and_out_of_range_fail();
    test_truncated_document_fails();
    test_format_points();
    test_format_compact();
    test_format_reset_clock();
    puts("usage source tests: PASS");
    return 0;
}
