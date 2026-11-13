// tests/test_app_clock.c —— 系统时间是否可信的边界测试。
//
// 这条判断是 SNTP 与 TLS 之间的唯一契约：时间落在窗口内才允许发起 HTTPS，
// 因为 mbedTLS 会用 notBefore/notAfter 校验证书。窗口必须有下界（1970 是"未同步"）
// 与上界（离谱的未来时间同样会让证书校验失败）。
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_clock.h"

static void test_epoch_before_2024_is_not_plausible(void) {
    // 设备刚上电时 time() 从 1970 开始，必须判为"未同步"。
    assert(!app_clock_epoch_is_plausible(0));
    assert(!app_clock_epoch_is_plausible(1));
    assert(!app_clock_epoch_is_plausible(-1));
    assert(!app_clock_epoch_is_plausible(1704067199LL)); // 2023-12-31T23:59:59Z
}

static void test_epoch_from_2024_is_plausible(void) {
    assert(app_clock_epoch_is_plausible(1704067200LL)); // 2024-01-01T00:00:00Z
    assert(app_clock_epoch_is_plausible(1767225600LL)); // 2026-01-01T00:00:00Z
}

static void test_absurd_future_is_not_plausible(void) {
    // 上界挡的是写坏/伪造的时间，不是真实使用期限。
    assert(app_clock_epoch_is_plausible(4102444799LL)); // 2099-12-31T23:59:59Z
    assert(!app_clock_epoch_is_plausible(4102444800LL)); // 2100-01-01T00:00:00Z
    assert(!app_clock_epoch_is_plausible(INT64_MAX));
}

static void test_utc_formatting_is_stable(void) {
    char buf[32];

    assert(app_clock_format_utc(1704067200, buf, sizeof buf)); // 2024-01-01T00:00:00Z
    assert(strcmp(buf, "2024-01-01 00:00Z") == 0);

    assert(app_clock_format_utc(1767225600, buf, sizeof buf)); // 2026-01-01T00:00:00Z
    assert(strcmp(buf, "2026-01-01 00:00Z") == 0);

    assert(app_clock_format_utc(1767225600 + 14 * 3600 + 5 * 60, buf, sizeof buf));
    assert(strcmp(buf, "2026-01-01 14:05Z") == 0);

    // 闰日：2024-02-29。
    assert(app_clock_format_utc(1709164800, buf, sizeof buf));
    assert(strcmp(buf, "2024-02-29 00:00Z") == 0);

    // 年末最后一秒：不进位到下一天。
    assert(app_clock_format_utc(1767225599, buf, sizeof buf));
    assert(strcmp(buf, "2025-12-31 23:59Z") == 0);
}

static void test_utc_rejects_unusable_input(void) {
    char buf[32];

    memset(buf, 'Z', sizeof buf);
    assert(!app_clock_format_utc(0, buf, sizeof buf));
    assert(buf[0] == '\0');
    assert(!app_clock_format_utc(-5, buf, sizeof buf));
    assert(buf[0] == '\0');

    memset(buf, 'Z', sizeof buf);
    assert(!app_clock_format_utc(1767225600, buf, 8)); // 放不下 "2026-01-01 00:00Z"
    assert(buf[0] == '\0');

    assert(!app_clock_format_utc(1767225600, NULL, 32));
    buf[0] = 'x';
    assert(!app_clock_format_utc(1767225600, buf, 0));
    assert(buf[0] == 'x'); // cap == 0 时连清空都不做
}

int main(void) {
    test_epoch_before_2024_is_not_plausible();
    test_epoch_from_2024_is_plausible();
    test_absurd_future_is_not_plausible();
    test_utc_formatting_is_stable();
    test_utc_rejects_unusable_input();
    puts("app clock tests: PASS");
    return 0;
}
