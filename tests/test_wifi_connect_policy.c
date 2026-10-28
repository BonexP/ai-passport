// tests/test_wifi_connect_policy.c —— 遍历/重连状态机的 host 测试。
//
// 时钟由测试直接构造（毫秒），不依赖任何真实时间；store 只在 RAM 里构造，
// 不需要 kv 后端，因为这个状态机只读 count / active。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_connect_policy.h"
#include "wifi_profile.h"

static void make_store(wifi_profile_store_t *store, unsigned count, int8_t active) {
    memset(store, 0, sizeof *store);
    store->active = -1;
    for (unsigned i = 0; i < count; i++) {
        char ssid[16];
        snprintf(ssid, sizeof ssid, "net-%u", i);
        wifi_profile_t p;
        assert(wifi_profile_set_fields(&p, "p", ssid, "pw"));
        assert(wifi_profile_store_add(store, &p) == WIFI_PROFILE_OK);
    }
    assert(wifi_profile_store_set_active(store, active) == WIFI_PROFILE_OK);
}

static void test_empty_store_goes_straight_to_provisioning(void) {
    wifi_profile_store_t store;
    make_store(&store, 0, -1);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0);
    assert(s.action == WIFI_CONN_ACT_PROVISION);
    assert(s.index == -1);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_WAIT_PROVISION);
}

static void test_start_tries_the_active_profile_first(void) {
    wifi_profile_store_t store;
    make_store(&store, 3, 1);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0);
    assert(s.action == WIFI_CONN_ACT_CONNECT);
    assert(s.index == 1);
    assert(s.timeout_ms == WIFI_CONNECT_ATTEMPT_TIMEOUT_MS);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_TRYING);
}

static void test_failure_walks_from_zero_after_active(void) {
    wifi_profile_store_t store;
    make_store(&store, 3, 1);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 1);
    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 100);
    assert(s.action == WIFI_CONN_ACT_CONNECT);
    assert(s.index == 0);
}

static void test_middle_success_lands_on_connected_and_reports_index(void) {
    wifi_profile_store_t store;
    make_store(&store, 3, 1);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 1);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 10).index == 0);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 20).index == 2);

    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_OK, 30);
    assert(s.action == WIFI_CONN_ACT_NONE);
    assert(s.index == 2); // 调用方据此把第 2 条晋升为 active
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_CONNECTED);
}

static void test_all_failures_end_in_provisioning(void) {
    wifi_profile_store_t store;
    make_store(&store, 3, 0);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 0);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 10).index == 1);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 20).index == 2);

    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 30);
    assert(s.action == WIFI_CONN_ACT_PROVISION);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_WAIT_PROVISION);
}

static void test_timeout_advances_only_when_deadline_is_reached(void) {
    wifi_profile_store_t store;
    make_store(&store, 2, 0);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 0);

    assert(wifi_connect_policy_tick(&policy, 9999).action == WIFI_CONN_ACT_NONE);
    wifi_conn_step_t s = wifi_connect_policy_tick(&policy, 10000);
    assert(s.action == WIFI_CONN_ACT_CONNECT);
    assert(s.index == 1);
}

static void test_disconnect_restarts_from_active_not_from_last_attempt(void) {
    wifi_profile_store_t store;
    make_store(&store, 3, 1);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 1);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_OK, 10).index == 1);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_CONNECTED);

    // 用户/自动漫游把 active 换成了第 2 条；掉线后必须从新的 active 开始，
    // 而不是从上一次尝试过的第 1 条开始。
    assert(wifi_profile_store_set_active(&store, 2) == WIFI_PROFILE_OK);
    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_DISCONNECT, 20);
    assert(s.action == WIFI_CONN_ACT_CONNECT);
    assert(s.index == 2);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_TRYING);
}

static void test_disconnect_while_trying_is_ignored(void) {
    wifi_profile_store_t store;
    make_store(&store, 3, 0);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 0);
    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_DISCONNECT, 5);
    assert(s.action == WIFI_CONN_ACT_NONE);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_TRYING);
}

static void test_stop_is_honoured_from_every_live_state(void) {
    wifi_profile_store_t store;
    make_store(&store, 2, 0);
    wifi_connect_policy_t policy;

    // TRYING
    wifi_connect_policy_init(&policy, &store);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).action == WIFI_CONN_ACT_CONNECT);
    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_STOP, 10);
    assert(s.action == WIFI_CONN_ACT_STOPPED);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_IDLE);

    // CONNECTED
    wifi_connect_policy_init(&policy, &store);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).action == WIFI_CONN_ACT_CONNECT);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_OK, 10).index == 0);
    s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_STOP, 20);
    assert(s.action == WIFI_CONN_ACT_STOPPED);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_IDLE);

    // WAIT_PROVISION
    wifi_profile_store_t empty;
    make_store(&empty, 0, -1);
    wifi_connect_policy_init(&policy, &empty);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).action == WIFI_CONN_ACT_PROVISION);
    s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_STOP, 10);
    assert(s.action == WIFI_CONN_ACT_STOPPED);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_IDLE);
}

static void test_start_after_provisioning_retries_the_sweep(void) {
    wifi_profile_store_t store;
    make_store(&store, 2, 0);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 0);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 10).index == 1);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 20).action ==
           WIFI_CONN_ACT_PROVISION);

    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 30);
    assert(s.action == WIFI_CONN_ACT_CONNECT);
    assert(s.index == 0);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_TRYING);
}

static void test_single_profile_failure_goes_to_provisioning_at_once(void) {
    wifi_profile_store_t store;
    make_store(&store, 1, 0);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 0);
    wifi_conn_step_t s = wifi_connect_policy_event(&policy, WIFI_CONN_EV_ATTEMPT_FAIL, 10);
    assert(s.action == WIFI_CONN_ACT_PROVISION);
    assert(wifi_connect_policy_state(&policy) == WIFI_CONN_WAIT_PROVISION);
}

static void test_repeated_start_does_not_extend_the_timeout(void) {
    wifi_profile_store_t store;
    make_store(&store, 2, 0);
    wifi_connect_policy_t policy;
    wifi_connect_policy_init(&policy, &store);

    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 0).index == 0);
    assert(wifi_connect_policy_event(&policy, WIFI_CONN_EV_START, 5000).action ==
           WIFI_CONN_ACT_NONE);
    // 若重复 START 重置了 deadline，9999 就不会触发推进。
    assert(wifi_connect_policy_tick(&policy, 9999).action == WIFI_CONN_ACT_NONE);
    assert(wifi_connect_policy_tick(&policy, 10000).action == WIFI_CONN_ACT_CONNECT);
}

int main(void) {
    test_empty_store_goes_straight_to_provisioning();
    test_start_tries_the_active_profile_first();
    test_failure_walks_from_zero_after_active();
    test_middle_success_lands_on_connected_and_reports_index();
    test_all_failures_end_in_provisioning();
    test_timeout_advances_only_when_deadline_is_reached();
    test_disconnect_restarts_from_active_not_from_last_attempt();
    test_disconnect_while_trying_is_ignored();
    test_stop_is_honoured_from_every_live_state();
    test_start_after_provisioning_retries_the_sweep();
    test_single_profile_failure_goes_to_provisioning_at_once();
    test_repeated_start_does_not_extend_the_timeout();
    puts("wifi connect policy tests: PASS");
    return 0;
}
