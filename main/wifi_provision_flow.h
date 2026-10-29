// main/wifi_provision_flow.h —— 配网会话状态机（纯逻辑）。
//
// 不碰 ESP-IDF、不碰 LVGL、不持有真实时间：时钟由调用方注入，动作是一步"该做什么"，
// 由 wifi_provision_service.c 翻译成起 AP、落盘、重启。因此超时、重复提交、幂等退出
// 这些容易出错的分支都能在主机上一一覆盖。
//
// 重启的决策也在这里（SAVED_RESTART），执行在胶水层的 worker 里——HTTP 回调里直接
// esp_restart() 既会丢响应，也无法在主机上建模。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wifi_profile.h"
#include "wifi_provision_form.h"

// 5 分钟无提交自动退出。够用户找到热点、打开浏览器、输完密码，又不至于让热点
// 长时间开着耗电。
#define WIFI_PROV_TIMEOUT_MS (5u * 60u * 1000u)

typedef enum {
    WIFI_PROV_IDLE = 0,
    WIFI_PROV_AP_UP,
    WIFI_PROV_HAS_CLIENT,
    WIFI_PROV_SAVING,
    WIFI_PROV_RESTARTING,
    WIFI_PROV_CLOSED,
} wifi_prov_state_t;

typedef enum {
    WIFI_PROV_EV_START = 0,
    WIFI_PROV_EV_AP_READY,
    WIFI_PROV_EV_CLIENT_JOINED,
    WIFI_PROV_EV_SUBMIT,
    WIFI_PROV_EV_SAVE_OK,
    WIFI_PROV_EV_SAVE_FAIL,
    WIFI_PROV_EV_EXIT,
    WIFI_PROV_EV_TICK,
} wifi_prov_event_t;

typedef enum {
    WIFI_PROV_ACT_NONE = 0,
    WIFI_PROV_ACT_START_AP,      // 起 AP + HTTP
    WIFI_PROV_ACT_SAVE,          // 把 step.profile 落盘
    WIFI_PROV_ACT_REJECT,        // 表单非法，回错误页（step.error）
    WIFI_PROV_ACT_SAVED_RESTART, // 先回响应，再请求重启（顺序不能反）
    WIFI_PROV_ACT_SAVE_FAILED,   // 落盘失败，回错误页，可重试
    WIFI_PROV_ACT_CLOSE,         // 停 AP/HTTP，回状态页
} wifi_prov_action_t;

typedef struct {
    wifi_prov_action_t action;
    wifi_form_result_t error;      // 仅 REJECT
    uint32_t           remaining_s;// 仅 TICK：剩余秒数
    wifi_profile_t     profile;    // 仅 SAVE
} wifi_prov_step_t;

typedef struct {
    wifi_prov_state_t     state;
    int64_t               deadline_ms;
    uint32_t              timeout_ms;
    wifi_provision_form_t pending; // SAVE 与 SAVE_OK 之间保存待落盘记录
} wifi_provision_flow_t;

void wifi_provision_flow_init(wifi_provision_flow_t *flow, uint32_t timeout_ms);

// 喂一个事件，拿下一步动作。
// form_result 只在 SUBMIT 时有意义：OK 表示表单合法且 form 已填好。
wifi_prov_step_t wifi_provision_flow_handle(wifi_provision_flow_t *flow, wifi_prov_event_t event,
                                            wifi_form_result_t form_result,
                                            const wifi_provision_form_t *form, int64_t now_ms);

wifi_prov_state_t wifi_provision_flow_state(const wifi_provision_flow_t *flow);
