// tests/test_wifi_provision_flow.c —— 配网会话状态机的 host 测试。
//
// 时间由测试直接构造（毫秒），超时阈值注入成 1 秒，因此不需要等待任何真实时间。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_provision_flow.h"

#define TEST_TIMEOUT_MS 1000u

static wifi_provision_form_t make_form(void) {
    const char *body = "ssid=Net&pass=pw";
    wifi_provision_form_t f;
    assert(wifi_provision_form_parse(body, strlen(body), &f) == WIFI_FORM_OK);
    return f;
}

static wifi_provision_flow_t started_flow(void) {
    wifi_provision_flow_t flow;
    wifi_provision_flow_init(&flow, TEST_TIMEOUT_MS);
    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_START, WIFI_FORM_OK, NULL, 0);
    assert(step.action == WIFI_PROV_ACT_START_AP);
    return flow;
}

static void test_init_starts_idle(void) {
    wifi_provision_flow_t flow;
    wifi_provision_flow_init(&flow, TEST_TIMEOUT_MS);
    assert(wifi_provision_flow_state(&flow) == WIFI_PROV_IDLE);
    assert(flow.deadline_ms == 0);
}

static void test_start_arms_the_deadline(void) {
    wifi_provision_flow_t flow;
    wifi_provision_flow_init(&flow, TEST_TIMEOUT_MS);
    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_START, WIFI_FORM_OK, NULL, 500);
    assert(step.action == WIFI_PROV_ACT_START_AP);
    assert(flow.state == WIFI_PROV_AP_UP);
    assert(flow.deadline_ms == 500 + (int64_t)TEST_TIMEOUT_MS);
}

static void test_tick_counts_down_then_closes(void) {
    wifi_provision_flow_t flow = started_flow(); // deadline = 1000

    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_TICK, WIFI_FORM_OK, NULL, 400);
    assert(step.action == WIFI_PROV_ACT_NONE);
    assert(step.remaining_s == 1); // 还剩 600 ms，向上取整到 1 秒

    step = wifi_provision_flow_handle(&flow, WIFI_PROV_EV_TICK, WIFI_FORM_OK, NULL, 999);
    assert(step.action == WIFI_PROV_ACT_NONE);

    step = wifi_provision_flow_handle(&flow, WIFI_PROV_EV_TICK, WIFI_FORM_OK, NULL, 1000);
    assert(step.action == WIFI_PROV_ACT_CLOSE);
    assert(flow.state == WIFI_PROV_CLOSED);
}

static void test_client_join_moves_state_and_refreshes_deadline(void) {
    wifi_provision_flow_t flow = started_flow();

    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_CLIENT_JOINED, WIFI_FORM_OK, NULL, 800);
    assert(step.action == WIFI_PROV_ACT_NONE);
    assert(flow.state == WIFI_PROV_HAS_CLIENT);
    assert(flow.deadline_ms == 800 + (int64_t)TEST_TIMEOUT_MS);
}

static void test_valid_submit_asks_for_a_save(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();

    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100);
    assert(step.action == WIFI_PROV_ACT_SAVE);
    assert(strcmp(step.profile.ssid, "Net") == 0);
    assert(strcmp(step.profile.password, "pw") == 0);
    assert(flow.state == WIFI_PROV_SAVING);
}

static void test_invalid_submit_rejects_and_stays_usable(void) {
    wifi_provision_flow_t flow = started_flow();

    wifi_prov_step_t step = wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT,
                                                       WIFI_FORM_ERR_EMPTY_SSID, NULL, 100);
    assert(step.action == WIFI_PROV_ACT_REJECT);
    assert(step.error == WIFI_FORM_ERR_EMPTY_SSID);
    assert(flow.state == WIFI_PROV_AP_UP);
    // 用户还在动，截止时间要往后推。
    assert(flow.deadline_ms == 100 + (int64_t)TEST_TIMEOUT_MS);
}

static void test_submit_without_a_form_is_rejected_with_a_real_error(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, NULL, 100);
    assert(step.action == WIFI_PROV_ACT_REJECT);
    // 不能把 OK 当成错误码发出去。
    assert(step.error != WIFI_FORM_OK);
}

static void test_save_ok_requests_a_restart(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100)
               .action == WIFI_PROV_ACT_SAVE);

    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SAVE_OK, WIFI_FORM_OK, NULL, 200);
    assert(step.action == WIFI_PROV_ACT_SAVED_RESTART);
    assert(flow.state == WIFI_PROV_RESTARTING);
}

static void test_save_failure_returns_to_a_retryable_state(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100)
               .action == WIFI_PROV_ACT_SAVE);

    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SAVE_FAIL, WIFI_FORM_OK, NULL, 200);
    assert(step.action == WIFI_PROV_ACT_SAVE_FAILED);
    assert(flow.state == WIFI_PROV_AP_UP);
    assert(flow.deadline_ms == 200 + (int64_t)TEST_TIMEOUT_MS);
}

static void test_second_submit_while_saving_is_ignored(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100)
               .action == WIFI_PROV_ACT_SAVE);

    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 120);
    assert(step.action == WIFI_PROV_ACT_NONE);
    assert(flow.state == WIFI_PROV_SAVING);
}

static void test_timeout_does_not_interrupt_a_save(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100)
               .action == WIFI_PROV_ACT_SAVE);

    // 保存已经开始，此时不能用超时把栈拆掉。
    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_TICK, WIFI_FORM_OK, NULL, 99999);
    assert(step.action == WIFI_PROV_ACT_NONE);
    assert(flow.state == WIFI_PROV_SAVING);
}

static void test_exit_closes_from_live_states(void) {
    wifi_provision_flow_t flow = started_flow();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_EXIT, WIFI_FORM_OK, NULL, 100).action ==
           WIFI_PROV_ACT_CLOSE);
    assert(flow.state == WIFI_PROV_CLOSED);

    flow = started_flow();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_CLIENT_JOINED, WIFI_FORM_OK, NULL, 50)
               .action == WIFI_PROV_ACT_NONE);
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_EXIT, WIFI_FORM_OK, NULL, 100).action ==
           WIFI_PROV_ACT_CLOSE);
    assert(flow.state == WIFI_PROV_CLOSED);
}

static void test_exit_is_idempotent_after_close(void) {
    wifi_provision_flow_t flow = started_flow();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_EXIT, WIFI_FORM_OK, NULL, 100).action ==
           WIFI_PROV_ACT_CLOSE);
    // 页面的 stop() 可能被重复调用，第二次必须是无害的。
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_EXIT, WIFI_FORM_OK, NULL, 110).action ==
           WIFI_PROV_ACT_NONE);
    assert(flow.state == WIFI_PROV_CLOSED);
}

static void test_exit_during_a_save_is_deferred(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100)
               .action == WIFI_PROV_ACT_SAVE);

    // 保存中途拆栈可能留下半条记录，让这一轮走完更安全。
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_EXIT, WIFI_FORM_OK, NULL, 150).action ==
           WIFI_PROV_ACT_NONE);
    assert(flow.state == WIFI_PROV_SAVING);
}

static void test_repeated_start_is_ignored(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_prov_step_t step =
        wifi_provision_flow_handle(&flow, WIFI_PROV_EV_START, WIFI_FORM_OK, NULL, 100);
    assert(step.action == WIFI_PROV_ACT_NONE);
    // 截止时间不能被重复 START 重置。
    assert(flow.deadline_ms == (int64_t)TEST_TIMEOUT_MS);
}

static void test_nothing_reacts_after_restart_is_requested(void) {
    wifi_provision_flow_t flow = started_flow();
    wifi_provision_form_t form = make_form();
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 100)
               .action == WIFI_PROV_ACT_SAVE);
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SAVE_OK, WIFI_FORM_OK, NULL, 200)
               .action == WIFI_PROV_ACT_SAVED_RESTART);

    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_SUBMIT, WIFI_FORM_OK, &form, 300)
               .action == WIFI_PROV_ACT_NONE);
    assert(wifi_provision_flow_handle(&flow, WIFI_PROV_EV_CLIENT_JOINED, WIFI_FORM_OK, NULL, 300)
               .action == WIFI_PROV_ACT_NONE);
    assert(flow.state == WIFI_PROV_RESTARTING);
}

int main(void) {
    test_init_starts_idle();
    test_start_arms_the_deadline();
    test_tick_counts_down_then_closes();
    test_client_join_moves_state_and_refreshes_deadline();
    test_valid_submit_asks_for_a_save();
    test_invalid_submit_rejects_and_stays_usable();
    test_submit_without_a_form_is_rejected_with_a_real_error();
    test_save_ok_requests_a_restart();
    test_save_failure_returns_to_a_retryable_state();
    test_second_submit_while_saving_is_ignored();
    test_timeout_does_not_interrupt_a_save();
    test_exit_closes_from_live_states();
    test_exit_is_idempotent_after_close();
    test_exit_during_a_save_is_deferred();
    test_repeated_start_is_ignored();
    test_nothing_reacts_after_restart_is_requested();
    puts("wifi provision flow tests: PASS");
    return 0;
}
