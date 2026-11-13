// tests/test_usage_view.c —— 用量页状态机与文案的主机测试。
//
// 这些分支在屏幕上很难凑齐（尤其 401、429、5xx 与"没配 key"四种空卡片），
// 但每一种都对应一句用户会读到的解释，所以逐格断言。
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usage_view.h"

// 一份典型的好数据：额度 2.0000，已用 0.4512，剩 1.5488。
static usage_state_t good_usage(void) {
    usage_state_t s;
    memset(&s, 0, sizeof s);
    s.has_data = true;
    s.has_limit = true;
    s.limit_1e4 = 20000;
    s.used_1e4 = 4512;
    s.remaining_1e4 = 15488;
    s.today_requests = 42;
    s.today_errors = 3;
    s.today_total_tokens = 180600;
    s.has_days30 = true;
    s.days30_cost_1e4 = 125000; // 12.5000 pts
    snprintf(s.reset_at, sizeof s.reset_at, "%s", "2026-11-12T00:00:00+08:00");
    return s;
}

static void test_data_wins_over_every_status(void) {
    // 有数据时任何失败状态都不能把数字藏起来：这只是"这份数据有多旧"的问题。
    assert(usage_view_state(true, true, true, true, USAGE_STATUS_OK, false) == USAGE_VIEW_DATA);
    assert(usage_view_state(true, true, true, true, USAGE_STATUS_KEY_REJECTED, false) ==
           USAGE_VIEW_DATA);
    assert(usage_view_state(true, true, true, true, USAGE_STATUS_RATE_LIMITED, true) ==
           USAGE_VIEW_DATA);
    // 没网、没校时也一样：那正是 Offline - saved 存在的理由。
    assert(usage_view_state(true, false, false, true, USAGE_STATUS_UNAVAILABLE, true) ==
           USAGE_VIEW_DATA);
}

static void test_no_key_beats_everything(void) {
    // 没配 key 是构建形态，不是故障：哪怕同时没网，也该先说这件事。
    assert(usage_view_state(false, false, false, false, USAGE_STATUS_NONE, false) ==
           USAGE_VIEW_NO_KEY);
    assert(usage_view_state(false, true, true, false, USAGE_STATUS_NO_KEY, false) ==
           USAGE_VIEW_NO_KEY);
    // 没 key 却在别的状态下：仍然报没 key。NO_KEY 不会被"更新中"盖掉。
    assert(usage_view_state(false, true, true, false, USAGE_STATUS_UPDATING, false) ==
           USAGE_VIEW_NO_KEY);
}

static void test_certain_statuses_before_network_state(void) {
    // 服务端已经给了明确结论时，别用"没网"去覆盖它。
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_KEY_REJECTED, false) ==
           USAGE_VIEW_KEY_REJECTED);
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_RATE_LIMITED, false) ==
           USAGE_VIEW_RATE_LIMITED);
    // 报文坏掉：显示 Bad response，而不是含糊的重试中。
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_BAD_BODY, false) ==
           USAGE_VIEW_BAD_BODY);
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_TRUNCATED, false) ==
           USAGE_VIEW_BAD_BODY);
}

static void test_wait_time_is_local_state(void) {
    // 没校时发不出 HTTPS：这是本机的事，不是"服务不可用"。
    assert(usage_view_state(true, true, false, false, USAGE_STATUS_NONE, false) ==
           USAGE_VIEW_WAIT_TIME);
    // 但没网时先说没网：这时候谈校时没有意义。
    assert(usage_view_state(true, false, false, false, USAGE_STATUS_NONE, false) ==
           USAGE_VIEW_UNAVAILABLE);
    assert(usage_view_state(true, false, true, false, USAGE_STATUS_UNAVAILABLE, false) ==
           USAGE_VIEW_UNAVAILABLE);
}

static void test_first_fetch_waits_then_retries(void) {
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_NONE, false) ==
           USAGE_VIEW_UPDATING);
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_NONE, true) ==
           USAGE_VIEW_RETRYING);
    assert(usage_view_state(true, true, true, false, USAGE_STATUS_UNAVAILABLE, false) ==
           USAGE_VIEW_UNAVAILABLE);
}

static void test_messages_cover_every_state(void) {
    assert(usage_view_message(USAGE_VIEW_DATA) == NULL);
    assert(strcmp(usage_view_message(USAGE_VIEW_NO_KEY), "Key not configured") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_KEY_REJECTED), "Key rejected") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_RATE_LIMITED), "Rate limited") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_UNAVAILABLE), "No network") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_WAIT_TIME), "Waiting for time") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_UPDATING), "Updating...") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_RETRYING), "No data - retrying") == 0);
    assert(strcmp(usage_view_message(USAGE_VIEW_BAD_BODY), "Bad response") == 0);
}

static void test_remaining_shows_four_decimals(void) {
    char buf[24];
    usage_state_t u = good_usage();
    assert(usage_view_remaining(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "1.5488") == 0);

    // P5.4 验收点名的四格：8.1588、0.0032、0，以及六位数额度。
    u.remaining_1e4 = 81588;
    assert(usage_view_remaining(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "8.1588") == 0);

    // 小于 1 点时不能读成 0.00：这正是四位小数的理由。
    u.remaining_1e4 = 32;
    assert(usage_view_remaining(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "0.0032") == 0);

    // 零额度剩零：显示 0.0000，而不是空标签。
    u.remaining_1e4 = 0;
    assert(usage_view_remaining(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "0.0000") == 0);

    // 六位数额度：剩下 214748.3647 也不截断。
    u.remaining_1e4 = INT32_MAX;
    assert(usage_view_remaining(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "214748.3647") == 0);

    // 剩下的额度是接口独立字段：即使 limit 缺失也照常显示。
    u = good_usage();
    u.has_limit = false;
    u.remaining_1e4 = 15488;
    assert(usage_view_remaining(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "1.5488") == 0);

    // 没有数据时主卡让位给状态文案。
    assert(!usage_view_remaining(USAGE_VIEW_NO_KEY, &u, buf, sizeof buf));
    assert(buf[0] == '\0');
    assert(!usage_view_remaining(USAGE_VIEW_DATA, NULL, buf, sizeof buf));
}

static void test_used_line_distinguishes_unknown_cap(void) {
    char buf[48];
    usage_state_t u = good_usage();
    assert(usage_view_used_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "0.4512 / 2.0000 used") == 0);

    // 额度未知 ≠ 额度为 0：只说用掉多少，绝不画成 "0.4512 / 0.0000"。
    u = good_usage();
    u.has_limit = false;
    u.limit_1e4 = 0;
    assert(usage_view_used_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "0.4512 used") == 0);
    assert(strstr(buf, "0.0000") == NULL);

    // 六位数额度也要完整显示，不做紧凑化——这是账，不是计数。
    u = good_usage();
    u.limit_1e4 = 12345678; // 1234.5678
    u.used_1e4 = 10000;     // 1.0000
    assert(usage_view_used_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "1.0000 / 1234.5678 used") == 0);

    // 永不出现美元符号：_usd 是 points，不是货币。
    assert(strchr(buf, '$') == NULL);
}

static void test_used_permille_reports_unknown_as_minus_one(void) {
    usage_state_t u = good_usage();
    // 0.4512 / 2.0000 = 22.56% → 226‰（四舍五入）
    assert(usage_view_used_permille(USAGE_VIEW_DATA, &u) == 226);

    u.used_1e4 = 0;
    assert(usage_view_used_permille(USAGE_VIEW_DATA, &u) == 0);

    u.used_1e4 = 20000;
    assert(usage_view_used_permille(USAGE_VIEW_DATA, &u) == 1000);

    // 超额使用夹在满格：进度条不该画到框外。
    u.used_1e4 = 30000;
    assert(usage_view_used_permille(USAGE_VIEW_DATA, &u) == 1000);

    // 额度未知 → 没有比例。这条是"未知不是用满"的守卫。
    u = good_usage();
    u.has_limit = false;
    assert(usage_view_used_permille(USAGE_VIEW_DATA, &u) == -1);

    // 额度为 0 → 也不算比例，除零无意义。
    u = good_usage();
    u.limit_1e4 = 0;
    assert(usage_view_used_permille(USAGE_VIEW_DATA, &u) == -1);

    assert(usage_view_used_permille(USAGE_VIEW_NO_KEY, &u) == -1);
    assert(usage_view_used_permille(USAGE_VIEW_DATA, NULL) == -1);
}

static void test_reset_line_uses_response_clock(void) {
    char buf[24];
    usage_state_t u = good_usage();
    assert(usage_view_reset_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "Reset 00:00") == 0);

    // 时区偏移不是 +08:00 也照取钟点：不硬编码偏移。
    snprintf(u.reset_at, sizeof u.reset_at, "%s", "2026-11-12T23:59:59Z");
    assert(usage_view_reset_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "Reset 23:59") == 0);

    // 接口没给重置时刻：整行不显示，不编一个 00:00。
    u.reset_at[0] = '\0';
    assert(!usage_view_reset_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(buf[0] == '\0');
}

static void test_today_line_abbreviates_tokens(void) {
    char buf[64];
    usage_state_t u = good_usage();
    assert(usage_view_today_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "Today 42 req  180.6k tok  3 err") == 0);

    // 小于 1000 的 token 直接给整数，不做 .0k。
    u.today_total_tokens = 999;
    assert(usage_view_today_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "Today 42 req  999 tok  3 err") == 0);

    u.today_total_tokens = 0;
    assert(usage_view_today_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "Today 42 req  0 tok  3 err") == 0);

    assert(!usage_view_today_line(USAGE_VIEW_NO_KEY, &u, buf, sizeof buf));
}

static void test_30d_line_hidden_when_absent(void) {
    char buf[40];
    usage_state_t u = good_usage();
    assert(usage_view_30d_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));
    assert(strcmp(buf, "30d 12.5000 pts") == 0);

    // 报文没给 last_30_days.cost：隐藏整行，不显示 0。
    u.has_days30 = false;
    assert(!usage_view_30d_line(USAGE_VIEW_DATA, &u, buf, sizeof buf));

    u = good_usage();
    assert(!usage_view_30d_line(USAGE_VIEW_NO_KEY, &u, buf, sizeof buf));
}

static void test_status_line_reports_why_and_when(void) {
    char buf[64];
    usage_state_t u = good_usage();
    (void)u;

    // 没有数据时状态行就是状态文案。
    assert(usage_view_status_line(USAGE_VIEW_NO_KEY, USAGE_STATUS_NO_KEY, false, -1, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Key not configured") == 0);
    assert(usage_view_status_line(USAGE_VIEW_KEY_REJECTED, USAGE_STATUS_KEY_REJECTED, false, -1,
                                  0, buf, sizeof buf));
    assert(strcmp(buf, "Key rejected") == 0);

    // 有数据、请求正常：只报多久前。"just now" 单独成句，不套 "Updated"。
    assert(usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_OK, false, 0, 0, buf, sizeof buf));
    assert(strcmp(buf, "just now") == 0);
    assert(usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_OK, false, 5 * 60 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated 5 min ago") == 0);
    assert(usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_OK, false, 3 * 3600 * 1000, 0, buf,
                                  sizeof buf));
    assert(strcmp(buf, "Updated 3 h ago") == 0);

    // 有数据但最近一次失败：两个都得说。这正是"数字没被擦掉"的证据。
    assert(usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_KEY_REJECTED, false,
                                  5 * 60 * 1000, 0, buf, sizeof buf));
    assert(strcmp(buf, "Key rejected - 5 min ago") == 0);

    // 跨重启：用快照里的绝对时刻。
    assert(usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_UNAVAILABLE, true, -1,
                                  1767225600, buf, sizeof buf));
    assert(strcmp(buf, "Offline - 2026-01-01 00:00Z") == 0);

    // 没有时间戳的旧快照：一句静态说明。
    assert(usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_OK, true, -1, 0, buf, sizeof buf));
    assert(strcmp(buf, "Offline - saved") == 0);

    // 既没有前缀也没有时间：不编一行字。
    assert(!usage_view_status_line(USAGE_VIEW_DATA, USAGE_STATUS_OK, false, -1, 0, buf, sizeof buf));
    assert(buf[0] == '\0');
}

int main(void) {
    test_data_wins_over_every_status();
    test_no_key_beats_everything();
    test_certain_statuses_before_network_state();
    test_wait_time_is_local_state();
    test_first_fetch_waits_then_retries();
    test_messages_cover_every_state();
    test_remaining_shows_four_decimals();
    test_used_line_distinguishes_unknown_cap();
    test_used_permille_reports_unknown_as_minus_one();
    test_reset_line_uses_response_clock();
    test_today_line_abbreviates_tokens();
    test_30d_line_hidden_when_absent();
    test_status_line_reports_why_and_when();
    puts("usage view tests: PASS");
    return 0;
}
