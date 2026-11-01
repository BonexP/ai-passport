// tests/test_app_clock.c —— 系统时间是否可信的边界测试。
//
// 这条判断是 SNTP 与 TLS 之间的唯一契约：时间落在窗口内才允许发起 HTTPS，
// 因为 mbedTLS 会用 notBefore/notAfter 校验证书。窗口必须有下界（1970 是"未同步"）
// 与上界（离谱的未来时间同样会让证书校验失败）。
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void) {
    test_epoch_before_2024_is_not_plausible();
    test_epoch_from_2024_is_plausible();
    test_absurd_future_is_not_plausible();
    puts("app clock tests: PASS");
    return 0;
}
