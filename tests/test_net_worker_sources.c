// tests/test_net_worker_sources.c —— 多数据源 worker 的决策与隔离测试（白盒）。
//
// test_net_worker.c 覆盖天气单源的记账（节流、载入、起停），本文件只补多源之后
// 新增的那部分语义，两者不重叠：
//   - 一个源失败不推进另一个源的退避（隔离）
//   - 页面不可见的源不取数
//   - 两个源同时到期时，一轮只取一个，第二个等下一轮
//   - 没有 key 时用量源的门禁始终关着
//   - 失败发布只改 status，不擦掉已有数字
//   - 状态码到 usage_status_t 的每一条映射
//
// 与 test_net_worker.c 同样以 include .c 的方式白盒测试：任务体不跑（桩里的
// vTaskDelay 是空操作，while 循环会原地自旋），只测它做出决定的那几个函数。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "demo_stubs/demo_runtime.c"

#include "../main/net_worker.c"

/* ---- 被测文件依赖外部符号的替身与记录 ---- */

static bool    s_time_valid = true;
static int64_t s_time_epoch = 1767225600;

bool app_time_is_valid(void) { return s_time_valid; }
int64_t app_time_now(void) { return s_time_valid ? s_time_epoch : 0; }

// 编译期 key 的替身：用例通过改它来模拟"配了 key / 没配 key"。
//
// 注意这**不能**写成一条长得像真 key 的字符串：tools/check_repo.py 有一条
// "rc-" + 20 位以上字母数字的机密规则，测试夹具也会被它拦下来（这是好事）。
// 用带连字符的形式，明显不是凭据，规则也不会命中。
#define TEST_KEY_PRESENT "test-key-not-a-credential"

static const char *s_amd_key;

const char *app_config_amd_key(void) { return s_amd_key; }

// 天气与用量两条路径分别记录"是否被调用"，这是"每轮最多取一个"的判据。
static int s_weather_calls;
static int s_usage_calls;

bool weather_url_build(const app_location_t *loc, char *buf, size_t cap) {
    (void)loc;
    if (buf != NULL && cap > 0) {
        snprintf(buf, cap, "https://example.invalid/weather");
    }
    return true;
}

bool weather_parse_current(const char *json, weather_state_t *out) {
    (void)json;
    (void)out;
    return false; // 天气取数一律失败：本文件不关心它的成功路径
}

// 下一次 net_http_get_ex 的行为，由用例设置。
static http_result_t s_ex_result = HTTP_RESULT_OK;
static int           s_ex_status;
static int           s_ex_body_valid; // 1 = 返回一份可解析的用量报文

static const char USAGE_BODY[] =
    "{\"daily_cost_limit_usd\":2.0,"
    "\"daily_cost_used_usd\":0.4512,"
    "\"daily_cost_remaining_usd\":1.5488,"
    "\"daily_reset_at\":\"2026-11-12T00:00:00+08:00\","
    "\"today\":{\"requests\":42,\"errors\":3,\"total_tokens\":180600}}";

http_result_t net_http_get_ex(const net_http_request_t *req) {
    s_usage_calls++;
    assert(req != NULL);
    // 用量路径必须带 Authorization，否则这个功能根本不成立。
    assert(req->header_count >= 1);
    assert(req->status_out != NULL);

    if (req->body_len != NULL) {
        *req->body_len = 0;
    }
    if (req->status_out != NULL) {
        *req->status_out = s_ex_status;
    }
    if (s_ex_result == HTTP_RESULT_OK && s_ex_body_valid && req->body != NULL) {
        const size_t n = sizeof USAGE_BODY - 1;
        if (n < req->body_cap) {
            memcpy(req->body, USAGE_BODY, n);
            req->body[n] = '\0';
            if (req->body_len != NULL) *req->body_len = n;
        }
    }
    return s_ex_result;
}

http_result_t net_http_get(const char *url, char *body, size_t body_cap, size_t *body_len,
                           uint32_t timeout_ms) {
    (void)url;
    (void)body;
    (void)body_cap;
    (void)timeout_ms;
    s_weather_calls++;
    if (body_len != NULL) *body_len = 0;
    return HTTP_RESULT_TRANSPORT; // 天气一律失败，便于观察退避
}

/* ---- 夹具 ---- */

static app_location_t location(void) {
    app_location_t loc;
    memset(&loc, 0, sizeof loc);
    loc.has_location = true;
    loc.lat_udeg = 32123456;
    loc.lon_udeg = 119654321;
    strcpy(loc.place, "Zhenjiang");
    return loc;
}

static void reset_all(void) {
    test_nvs_clear();
    s_saved_valid = false;
    s_last_save_ms = -1;
    memset(&s_last_saved, 0, sizeof s_last_saved);
    app_data_bus_init(app_data_shared(), NULL);

    for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
        net_schedule_init(&s_sources[i].sched);
        s_sources[i].visible = false;
        s_sources[i].refresh = false;
    }

    s_weather_calls = 0;
    s_usage_calls = 0;
    s_ex_result = HTTP_RESULT_OK;
    s_ex_status = 200;
    s_ex_body_valid = 1;
    s_amd_key = TEST_KEY_PRESENT;
    s_time_valid = true;
}

// 让一个源处于"门都开、且已到期"的状态。
static void arm_source(net_source_id_t id) {
    s_sources[id].visible = true;
    net_schedule_set_context(&s_sources[id].sched, true, true, true);
    net_schedule_request_refresh(&s_sources[id].sched);
}

/* ---- 门禁 ---- */

static void test_usage_gate_closed_without_a_key(void) {
    reset_all();
    app_location_t loc = location();
    s_amd_key = NULL;

    assert(!source_gate_open(NET_SOURCE_USAGE, &loc));

    // 空串与 NULL 必须等价：模板里写 "" 也是"没配"。
    s_amd_key = "";
    assert(!source_gate_open(NET_SOURCE_USAGE, &loc));

    s_amd_key = TEST_KEY_PRESENT;
    assert(source_gate_open(NET_SOURCE_USAGE, &loc));
}

static void test_weather_gate_needs_a_location(void) {
    reset_all();
    app_location_t none;
    memset(&none, 0, sizeof none);

    assert(!source_gate_open(NET_SOURCE_WEATHER, &none));
    assert(!source_gate_open(NET_SOURCE_WEATHER, NULL));

    app_location_t loc = location();
    assert(source_gate_open(NET_SOURCE_WEATHER, &loc));
}

// 用量请求与设备地点无关：一个配网时没填地点的网络，仍然应该能查用量。
// 这条曾经是真实缺陷——最初的实现把"连着网"和"有地点"合成一个条件，
// 于是没填地点的网络下用量页永远显示 No network。
static void test_usage_gate_does_not_need_a_location(void) {
    reset_all();
    app_location_t none;
    memset(&none, 0, sizeof none);

    s_amd_key = TEST_KEY_PRESENT;
    assert(source_gate_open(NET_SOURCE_USAGE, &none));
    assert(source_gate_open(NET_SOURCE_USAGE, NULL));
}

/* ---- 隔离：一个源失败不推进另一个源的退避 ---- */

static void test_weather_failure_does_not_touch_usage_backoff(void) {
    reset_all();
    arm_source(NET_SOURCE_USAGE);
    arm_source(NET_SOURCE_WEATHER);

    // 这一轮取天气（抢占顺序在前）。天气在我们桩里一律失败。
    app_location_t loc = location();
    const int64_t now = 1000;
    net_schedule_begin(&s_sources[NET_SOURCE_WEATHER].sched, now);
    assert(!fetch_weather_once(NULL, &loc));
    net_schedule_on_failure(&s_sources[NET_SOURCE_WEATHER].sched, now);

    assert(s_sources[NET_SOURCE_WEATHER].sched.failures == 1);
    // 关键断言：用量的退避一动不动。
    assert(s_sources[NET_SOURCE_USAGE].sched.failures == 0);
}

static void test_usage_failure_does_not_touch_weather_backoff(void) {
    reset_all();
    arm_source(NET_SOURCE_WEATHER);
    arm_source(NET_SOURCE_USAGE);

    s_ex_result = HTTP_RESULT_HTTP_STATUS;
    s_ex_status = 401;
    s_ex_body_valid = 0;

    const int64_t now = 1000;
    net_schedule_begin(&s_sources[NET_SOURCE_USAGE].sched, now);
    assert(!fetch_usage_once());
    net_schedule_on_failure(&s_sources[NET_SOURCE_USAGE].sched, now);

    assert(s_sources[NET_SOURCE_USAGE].sched.failures == 1);
    assert(s_sources[NET_SOURCE_WEATHER].sched.failures == 0);
}

static void test_success_clears_only_its_own_backoff(void) {
    reset_all();
    s_sources[NET_SOURCE_WEATHER].sched.failures = 3;
    s_sources[NET_SOURCE_USAGE].sched.failures = 3;

    net_schedule_on_success(&s_sources[NET_SOURCE_USAGE].sched, 1000);
    assert(s_sources[NET_SOURCE_USAGE].sched.failures == 0);
    assert(s_sources[NET_SOURCE_WEATHER].sched.failures == 3);
}

/* ---- 可见性 ---- */

static void test_hidden_source_is_never_due(void) {
    reset_all();
    app_location_t loc = location();

    // 用量不可见，但其余条件都满足。
    net_schedule_set_context(&s_sources[NET_SOURCE_USAGE].sched, true, true, false);
    net_schedule_request_refresh(&s_sources[NET_SOURCE_USAGE].sched);
    assert(!net_schedule_due(&s_sources[NET_SOURCE_USAGE].sched, 100000));

    // 可见之后立刻就能取（force 还没被消费）。
    net_schedule_set_context(&s_sources[NET_SOURCE_USAGE].sched, true, true, true);
    assert(net_schedule_due(&s_sources[NET_SOURCE_USAGE].sched, 100000));
    (void)loc;
}

/* ---- 每轮最多一个 ---- */

static void test_both_sources_due_but_one_fetch_per_round(void) {
    reset_all();
    arm_source(NET_SOURCE_WEATHER);
    arm_source(NET_SOURCE_USAGE);

    const int64_t now = 5000;
    app_location_t loc = location();

    // 复刻 worker 循环里的那一段：按顺序找第一个到期的，取完就 break。
    int fetched = 0;
    for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
        const net_source_id_t id = (net_source_id_t)i;
        if (!source_gate_open(id, &loc)) continue;
        if (!net_schedule_due(&s_sources[i].sched, now)) continue;

        net_schedule_begin(&s_sources[i].sched, now);
        (void)fetch_source(id, NULL, &loc);
        fetched++;
        break;
    }

    assert(fetched == 1);
    // 天气在前，所以这一轮只可能是天气。
    assert(s_weather_calls == 1);
    assert(s_usage_calls == 0);

    // 用量的 force 还没被消费，下一轮仍然是"到期"的。
    assert(net_schedule_due(&s_sources[NET_SOURCE_USAGE].sched, now + 250));
}

/* ---- 失败发布保留数字 ---- */

static void test_failure_keeps_the_previous_numbers(void) {
    reset_all();

    // 先成功发布一份用量。
    assert(fetch_usage_once());
    app_data_t got;
    app_data_read(app_data_shared(), &got);
    assert(got.usage.usage.has_data);
    assert(got.usage.usage.remaining_1e4 == 15488);
    assert(got.usage.status == USAGE_STATUS_OK);
    const uint16_t seq_after_success = got.usage.seq;

    // 再失败一次：数字必须原样留着，只有 status 和 seq 变。
    s_ex_result = HTTP_RESULT_HTTP_STATUS;
    s_ex_status = 401;
    s_ex_body_valid = 0;
    assert(!fetch_usage_once());

    app_data_read(app_data_shared(), &got);
    assert(got.usage.usage.has_data);              // 仍然有数据
    assert(got.usage.usage.remaining_1e4 == 15488); // 数字没被擦
    assert(got.usage.usage.today_requests == 42);
    assert(got.usage.status == USAGE_STATUS_KEY_REJECTED);
    assert(got.usage.seq == (uint16_t)(seq_after_success + 1));
}

static void test_no_key_publishes_its_own_status(void) {
    reset_all();
    s_amd_key = NULL;
    assert(!fetch_usage_once()); // 不发请求，直接返回 false
    assert(s_usage_calls == 0);  // 关键：一个 socket 都没开

    app_data_t got;
    app_data_read(app_data_shared(), &got);
    assert(got.usage.status == USAGE_STATUS_NO_KEY);
    assert(!got.usage.usage.has_data);
}

/* ---- 状态码映射 ---- */

static void test_status_code_mapping(void) {
    // 2xx 成功
    assert(classify_usage_result(HTTP_RESULT_OK, 200) == USAGE_STATUS_OK);

    // 断 auth：401 / 403 同类。
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 401) == USAGE_STATUS_KEY_REJECTED);
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 403) == USAGE_STATUS_KEY_REJECTED);

    // 限流
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 429) == USAGE_STATUS_RATE_LIMITED);

    // 服务端故障与其余 4xx
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 500) == USAGE_STATUS_UNAVAILABLE);
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 502) == USAGE_STATUS_UNAVAILABLE);
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 503) == USAGE_STATUS_UNAVAILABLE);
    assert(classify_usage_result(HTTP_RESULT_HTTP_STATUS, 404) == USAGE_STATUS_UNAVAILABLE);

    // 截断是"内容不完整"，必须与传输失败分开。
    assert(classify_usage_result(HTTP_RESULT_TRUNCATED, 200) == USAGE_STATUS_TRUNCATED);

    // 200 + 非 JSON（门户页）
    assert(classify_usage_result(HTTP_RESULT_CONTENT_TYPE, 200) == USAGE_STATUS_BAD_BODY);

    // 传输层
    assert(classify_usage_result(HTTP_RESULT_TIMEOUT, 0) == USAGE_STATUS_UNAVAILABLE);
    assert(classify_usage_result(HTTP_RESULT_TLS, 0) == USAGE_STATUS_UNAVAILABLE);
    assert(classify_usage_result(HTTP_RESULT_TRANSPORT, 0) == USAGE_STATUS_UNAVAILABLE);
}

/* ---- 刷新标志 ---- */

static void test_refresh_flags_are_per_source(void) {
    reset_all();

    net_worker_request_refresh(NET_SOURCE_USAGE);
    assert(s_sources[NET_SOURCE_USAGE].refresh);
    assert(!s_sources[NET_SOURCE_WEATHER].refresh);

    net_worker_request_refresh(NET_SOURCE_WEATHER);
    assert(s_sources[NET_SOURCE_WEATHER].refresh);
    assert(s_sources[NET_SOURCE_USAGE].refresh); // 两者独立，不互相清除
}

static void test_visibility_flags_are_per_source(void) {
    reset_all();

    net_worker_set_visible(NET_SOURCE_USAGE, true);
    assert(s_sources[NET_SOURCE_USAGE].visible);
    assert(!s_sources[NET_SOURCE_WEATHER].visible);

    net_worker_set_visible(NET_SOURCE_USAGE, false);
    assert(!s_sources[NET_SOURCE_USAGE].visible);
}

static void test_out_of_range_source_id_is_ignored(void) {
    reset_all();
    // 越界 id 必须是空操作，不能越写数组。
    net_worker_set_visible((net_source_id_t)99, true);
    net_worker_request_refresh((net_source_id_t)99);

    // reset_all 之后所有源都是不可见、不刷新的；越界调用一个都不该改变。
    for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
        assert(!s_sources[i].visible);
        assert(!s_sources[i].refresh);
    }
}

/* ---- 成功路径填齐运行时字段 ---- */

static void test_success_fills_the_runtime_fields(void) {
    reset_all();
    assert(fetch_usage_once());

    app_data_t got;
    app_data_read(app_data_shared(), &got);
    assert(got.usage.status == USAGE_STATUS_OK);
    assert(got.usage.usage.has_limit);
    assert(got.usage.usage.limit_1e4 == 20000);
    assert(got.usage.usage.used_1e4 == 4512);
    // 运行时字段来自发布者，不是报文。
    assert(got.usage.fetched_epoch == (int32_t)s_time_epoch);
    assert(got.usage.fetched_uptime_ms >= 0);
    // seq 由总线维护。
    assert(got.usage.seq == 1);
}

int main(void) {
    test_usage_gate_closed_without_a_key();
    test_weather_gate_needs_a_location();
    test_usage_gate_does_not_need_a_location();
    test_weather_failure_does_not_touch_usage_backoff();
    test_usage_failure_does_not_touch_weather_backoff();
    test_success_clears_only_its_own_backoff();
    test_hidden_source_is_never_due();
    test_both_sources_due_but_one_fetch_per_round();
    test_failure_keeps_the_previous_numbers();
    test_no_key_publishes_its_own_status();
    test_status_code_mapping();
    test_refresh_flags_are_per_source();
    test_visibility_flags_are_per_source();
    test_out_of_range_source_id_is_ignored();
    test_success_fills_the_runtime_fields();
    puts("net worker sources tests: PASS");
    return 0;
}
