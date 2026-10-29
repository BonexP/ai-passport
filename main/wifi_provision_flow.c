// main/wifi_provision_flow.c —— 会话转移的实现。
#include "wifi_provision_flow.h"

static wifi_prov_step_t step_none(void) {
    wifi_prov_step_t s = { .action = WIFI_PROV_ACT_NONE, .error = WIFI_FORM_OK };
    return s;
}

static wifi_prov_step_t step_action(wifi_prov_action_t action) {
    wifi_prov_step_t s = { .action = action, .error = WIFI_FORM_OK };
    return s;
}

// 任何"用户还在动"的事件都把截止时间往后推：判断"没人用了"应该看最后一次交互，
// 而不是看开机时刻。
static void refresh_deadline(wifi_provision_flow_t *flow, int64_t now_ms) {
    flow->deadline_ms = now_ms + (int64_t)flow->timeout_ms;
}

void wifi_provision_flow_init(wifi_provision_flow_t *flow, uint32_t timeout_ms) {
    if (flow == NULL) return;
    flow->state = WIFI_PROV_IDLE;
    flow->deadline_ms = 0;
    flow->timeout_ms = timeout_ms;
}

wifi_prov_step_t wifi_provision_flow_handle(wifi_provision_flow_t *flow, wifi_prov_event_t event,
                                            wifi_form_result_t form_result,
                                            const wifi_provision_form_t *form, int64_t now_ms) {
    if (flow == NULL) return step_none();

    switch (flow->state) {
    case WIFI_PROV_IDLE:
        if (event != WIFI_PROV_EV_START) return step_none();
        refresh_deadline(flow, now_ms);
        flow->state = WIFI_PROV_AP_UP;
        return step_action(WIFI_PROV_ACT_START_AP);

    case WIFI_PROV_AP_UP:
    case WIFI_PROV_HAS_CLIENT:
        switch (event) {
        case WIFI_PROV_EV_AP_READY:
            return step_none(); // 起 AP 的确认，状态已在 START 时确定
        case WIFI_PROV_EV_CLIENT_JOINED:
            refresh_deadline(flow, now_ms);
            flow->state = WIFI_PROV_HAS_CLIENT;
            return step_none();
        case WIFI_PROV_EV_SUBMIT:
            refresh_deadline(flow, now_ms);
            if (form_result != WIFI_FORM_OK || form == NULL) {
                // form 为 NULL 却没带错误码时，按"格式错误"处理，别把 OK 当成错误码发出去。
                wifi_prov_step_t s = {
                    .action = WIFI_PROV_ACT_REJECT,
                    .error = (form_result != WIFI_FORM_OK) ? form_result : WIFI_FORM_ERR_NO_FIELD,
                };
                return s;
            }
            flow->pending = *form;
            flow->state = WIFI_PROV_SAVING;
            {
                wifi_prov_step_t s = { .action = WIFI_PROV_ACT_SAVE, .profile = form->profile };
                return s;
            }
        case WIFI_PROV_EV_EXIT:
            flow->state = WIFI_PROV_CLOSED;
            return step_action(WIFI_PROV_ACT_CLOSE);
        case WIFI_PROV_EV_TICK: {
            if (now_ms < flow->deadline_ms) {
                int64_t remaining = flow->deadline_ms - now_ms;
                wifi_prov_step_t s = { .action = WIFI_PROV_ACT_NONE, .error = WIFI_FORM_OK };
                s.remaining_s = (uint32_t)((remaining + 999) / 1000);
                return s;
            }
            flow->state = WIFI_PROV_CLOSED;
            return step_action(WIFI_PROV_ACT_CLOSE);
        }
        case WIFI_PROV_EV_START:
            return step_none(); // 已经在配网了，重复 START 不重开一轮
        case WIFI_PROV_EV_SAVE_OK:
        case WIFI_PROV_EV_SAVE_FAIL:
            return step_none(); // 没在保存，忽略
        }
        return step_none();

    case WIFI_PROV_SAVING:
        switch (event) {
        case WIFI_PROV_EV_SAVE_OK:
            // 把"回响应"和"请求重启"合成一个动作，顺序由胶水保证：先 send 再 restart。
            flow->state = WIFI_PROV_RESTARTING;
            return step_action(WIFI_PROV_ACT_SAVED_RESTART);
        case WIFI_PROV_EV_SAVE_FAIL:
            // 落盘失败退回可提交状态，用户可以重试；截止时间重新计时。
            refresh_deadline(flow, now_ms);
            flow->state = WIFI_PROV_AP_UP;
            return step_action(WIFI_PROV_ACT_SAVE_FAILED);
        case WIFI_PROV_EV_EXIT:
            // 保存进行中不接受退出：此时半条记录可能已经写进 NVS，
            // 让这一轮走完（失败会退回 AP_UP，成功则重启）比中途拆栈安全。
            return step_none();
        case WIFI_PROV_EV_TICK:
            return step_none(); // 保存不应该被超时打断
        case WIFI_PROV_EV_SUBMIT:
            return step_none(); // 重复提交
        case WIFI_PROV_EV_START:
        case WIFI_PROV_EV_AP_READY:
        case WIFI_PROV_EV_CLIENT_JOINED:
            return step_none();
        }
        return step_none();

    case WIFI_PROV_RESTARTING:
    case WIFI_PROV_CLOSED:
        // 幂等区：页面 stop() 可能被重复调用；重启中更不该再有任何动作。
        return step_none();
    }
    return step_none();
}

wifi_prov_state_t wifi_provision_flow_state(const wifi_provision_flow_t *flow) {
    return flow != NULL ? flow->state : WIFI_PROV_IDLE;
}
