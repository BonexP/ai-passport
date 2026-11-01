// tests/test_net_schedule.c —— 取数调度的主机测试。
//
// 时间线全部由调用方注入，所以能精确断言"差一毫秒不算、到点才算"，
// 也能把失败退避的 30/60/120/…/封顶序列完整走一遍。
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "net_schedule.h"

static net_schedule_t make_ready(void) {
    net_schedule_t s;
    net_schedule_init(&s);
    net_schedule_set_context(&s, true, true, true);
    return s;
}

static void test_init_is_blocked_until_context_opens(void) {
    net_schedule_t s;
    net_schedule_init(&s);

    assert(!net_schedule_due(&s, 1000)); // 三个门都关着

    net_schedule_set_context(&s, true, false, true);
    assert(!net_schedule_due(&s, 1000)); // 时间不可信：证书会失败

    net_schedule_set_context(&s, false, true, true);
    assert(!net_schedule_due(&s, 1000)); // 没网

    net_schedule_set_context(&s, true, true, false);
    assert(!net_schedule_due(&s, 1000)); // 页面不可见

    net_schedule_set_context(&s, true, true, true);
    assert(net_schedule_due(&s, 1000)); // 从未尝试过
}

static void test_success_holds_off_for_the_full_interval(void) {
    net_schedule_t s = make_ready();

    net_schedule_begin(&s, 5000);
    assert(!net_schedule_due(&s, 5000)); // 已经开始，不能重入
    net_schedule_on_success(&s, 5000);

    assert(net_schedule_interval_ms(&s) == NET_SCHEDULE_INTERVAL_MS);
    assert(!net_schedule_due(&s, 5000 + NET_SCHEDULE_INTERVAL_MS - 1));
    assert(net_schedule_due(&s, 5000 + NET_SCHEDULE_INTERVAL_MS));
}

static void test_failure_backoff_doubles_until_the_cap(void) {
    net_schedule_t s = make_ready();
    int64_t now = 0;

    static const uint32_t expected[] = {
        30000, 60000, 120000, 240000, 300000, 300000, // 封顶 5 分钟
    };

    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        net_schedule_begin(&s, now);
        net_schedule_on_failure(&s, now);

        assert(net_schedule_interval_ms(&s) == expected[i]);
        assert(!net_schedule_due(&s, now + (int64_t)expected[i] - 1));
        assert(net_schedule_due(&s, now + (int64_t)expected[i]));
        now += (int64_t)expected[i];
    }
}

static void test_success_resets_backoff(void) {
    net_schedule_t s = make_ready();

    net_schedule_begin(&s, 0);
    net_schedule_on_failure(&s, 0);
    net_schedule_begin(&s, 30000);
    net_schedule_on_failure(&s, 30000);
    assert(net_schedule_interval_ms(&s) == 60000);

    net_schedule_begin(&s, 90000);
    net_schedule_on_success(&s, 90000);
    assert(net_schedule_interval_ms(&s) == NET_SCHEDULE_INTERVAL_MS);
    assert(!net_schedule_due(&s, 90000 + NET_SCHEDULE_INTERVAL_MS - 1));
    assert(net_schedule_due(&s, 90000 + NET_SCHEDULE_INTERVAL_MS));
}

static void test_manual_refresh_ignores_the_interval_once(void) {
    net_schedule_t s = make_ready();

    net_schedule_begin(&s, 0);
    net_schedule_on_success(&s, 0);
    assert(!net_schedule_due(&s, 1000));

    net_schedule_request_refresh(&s);
    assert(net_schedule_due(&s, 1000));

    net_schedule_begin(&s, 1000);
    assert(!net_schedule_due(&s, 1000)); // force 已被消费
    net_schedule_on_success(&s, 1000);
    assert(!net_schedule_due(&s, 2000));
}

static void test_visibility_is_a_gate_not_a_trigger(void) {
    net_schedule_t s = make_ready();

    net_schedule_begin(&s, 0);
    net_schedule_on_success(&s, 0);

    // 离开页面：即使间隔已到，也不取数。
    net_schedule_set_context(&s, true, true, false);
    assert(!net_schedule_due(&s, NET_SCHEDULE_INTERVAL_MS));

    // 回到页面：间隔已过，立即取数。
    net_schedule_set_context(&s, true, true, true);
    assert(net_schedule_due(&s, NET_SCHEDULE_INTERVAL_MS));

    // 回来得太早：仍然要等到间隔。
    net_schedule_begin(&s, NET_SCHEDULE_INTERVAL_MS);
    net_schedule_on_success(&s, NET_SCHEDULE_INTERVAL_MS);
    net_schedule_set_context(&s, true, true, false);
    net_schedule_set_context(&s, true, true, true);
    assert(!net_schedule_due(&s, NET_SCHEDULE_INTERVAL_MS + 1000));
}

static void test_null_arguments_are_safe(void) {
    net_schedule_init(NULL);
    net_schedule_set_context(NULL, true, true, true);
    net_schedule_begin(NULL, 0);
    net_schedule_on_success(NULL, 0);
    net_schedule_on_failure(NULL, 0);
    net_schedule_request_refresh(NULL);
    assert(!net_schedule_due(NULL, 0));
    assert(net_schedule_interval_ms(NULL) == NET_SCHEDULE_INTERVAL_MS);
}

int main(void) {
    test_init_is_blocked_until_context_opens();
    test_success_holds_off_for_the_full_interval();
    test_failure_backoff_doubles_until_the_cap();
    test_success_resets_backoff();
    test_manual_refresh_ignores_the_interval_once();
    test_visibility_is_a_gate_not_a_trigger();
    test_null_arguments_are_safe();
    puts("net schedule tests: PASS");
    return 0;
}
