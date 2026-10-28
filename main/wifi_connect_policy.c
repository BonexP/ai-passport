// main/wifi_connect_policy.c —— 遍历与重连的转移实现。
#include "wifi_connect_policy.h"

static wifi_conn_step_t step(wifi_conn_action_t action, int8_t index, uint32_t timeout_ms) {
    wifi_conn_step_t s = { .action = action, .index = index, .timeout_ms = timeout_ms };
    return s;
}

static wifi_conn_step_t step_none(void) {
    return step(WIFI_CONN_ACT_NONE, -1, 0);
}

// 本轮第 position 个候选槽位。顺序是「先 active，再从 0 递增并跳过 active」，
// 这样人换到另一个地点时，上一次成功的网络不会被立刻重复尝试两次。
static int8_t index_for_position(const wifi_connect_policy_t *policy, int8_t position) {
    if (position <= 0) return policy->first;
    if (position <= policy->first) return (int8_t)(position - 1);
    return position;
}

static wifi_conn_step_t begin_sweep(wifi_connect_policy_t *policy, int64_t now_ms) {
    uint8_t count = wifi_profile_store_count(policy->store);
    if (count == 0) {
        policy->state = WIFI_CONN_WAIT_PROVISION;
        policy->candidate = -1;
        policy->attempted = 0;
        policy->deadline_ms = 0;
        return step(WIFI_CONN_ACT_PROVISION, -1, 0);
    }

    int8_t active = wifi_profile_store_active(policy->store);
    policy->first = (active >= 0 && active < (int8_t)count) ? active : 0;
    policy->state = WIFI_CONN_TRYING;
    policy->candidate = index_for_position(policy, 0);
    policy->attempted = 1;
    policy->deadline_ms = now_ms + (int64_t)policy->attempt_timeout_ms;
    return step(WIFI_CONN_ACT_CONNECT, policy->candidate, policy->attempt_timeout_ms);
}

// 放弃当前候选，试下一个；本轮已试满则进配网态。
static wifi_conn_step_t advance(wifi_connect_policy_t *policy, int64_t now_ms) {
    uint8_t count = wifi_profile_store_count(policy->store);
    if (policy->attempted >= (int8_t)count) {
        policy->state = WIFI_CONN_WAIT_PROVISION;
        policy->candidate = -1;
        policy->deadline_ms = 0;
        return step(WIFI_CONN_ACT_PROVISION, -1, 0);
    }

    policy->candidate = index_for_position(policy, policy->attempted);
    policy->attempted++;
    policy->deadline_ms = now_ms + (int64_t)policy->attempt_timeout_ms;
    return step(WIFI_CONN_ACT_CONNECT, policy->candidate, policy->attempt_timeout_ms);
}

void wifi_connect_policy_init(wifi_connect_policy_t *policy,
                              const wifi_profile_store_t *store) {
    if (policy == NULL) return;
    policy->store = store;
    policy->state = WIFI_CONN_IDLE;
    policy->first = 0;
    policy->candidate = -1;
    policy->attempted = 0;
    policy->deadline_ms = 0;
    policy->attempt_timeout_ms = WIFI_CONNECT_ATTEMPT_TIMEOUT_MS;
}

wifi_conn_step_t wifi_connect_policy_event(wifi_connect_policy_t *policy,
                                           wifi_conn_event_t event, int64_t now_ms) {
    if (policy == NULL || policy->store == NULL) return step_none();

    if (event == WIFI_CONN_EV_STOP) {
        policy->state = WIFI_CONN_IDLE;
        policy->candidate = -1;
        policy->attempted = 0;
        policy->deadline_ms = 0;
        return step(WIFI_CONN_ACT_STOPPED, -1, 0);
    }

    switch (policy->state) {
    case WIFI_CONN_IDLE:
    case WIFI_CONN_WAIT_PROVISION:
        if (event == WIFI_CONN_EV_START) return begin_sweep(policy, now_ms);
        return step_none();

    case WIFI_CONN_TRYING:
        switch (event) {
        case WIFI_CONN_EV_ATTEMPT_OK:
            policy->state = WIFI_CONN_CONNECTED;
            policy->deadline_ms = 0;
            // index 回传给调用方，用来把这条晋升为 active 并落盘。
            return step(WIFI_CONN_ACT_NONE, policy->candidate, 0);
        case WIFI_CONN_EV_ATTEMPT_FAIL:
        case WIFI_CONN_EV_TIMEOUT:
            return advance(policy, now_ms);
        case WIFI_CONN_EV_DISCONNECT:
            // 已经在尝试了，驱动的重复掉线通知不应该再触发一轮连接。
            return step_none();
        case WIFI_CONN_EV_START:
            // 重复 START 不重开一轮，也不延长当前这条的超时。
            return step_none();
        case WIFI_CONN_EV_STOP:
            return step_none(); // 已在入口处理
        }
        return step_none();

    case WIFI_CONN_CONNECTED:
        if (event == WIFI_CONN_EV_DISCONNECT) {
            // 重新走完整轮，而不是只重试上一个 SSID：人从家到公司时，上一个 SSID
            // 在这个地点根本不在，重试它没有意义。
            return begin_sweep(policy, now_ms);
        }
        return step_none();
    }
    return step_none();
}

wifi_conn_step_t wifi_connect_policy_tick(wifi_connect_policy_t *policy, int64_t now_ms) {
    if (policy == NULL || policy->store == NULL) return step_none();
    if (policy->state != WIFI_CONN_TRYING) return step_none();
    if (now_ms < policy->deadline_ms) return step_none();
    return advance(policy, now_ms);
}

wifi_conn_state_t wifi_connect_policy_state(const wifi_connect_policy_t *policy) {
    return policy != NULL ? policy->state : WIFI_CONN_IDLE;
}
