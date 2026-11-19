// main/wifi_connect_policy.h —— 开机遍历与断线重连的状态机。
//
// 纯逻辑：不碰 ESP-IDF、不碰 Wi-Fi 驱动、不持有时间。所有输入都由调用方注入，
// 输出是一步"该做什么"的指令，由 wifi_profile_service.c 翻译成真实 API 调用。
// 因此 tests/test_wifi_connect_policy.c 能在主机上把每条路径都走一遍。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wifi_profile.h"

// 单条 profile 的尝试预算。plan 文档取 10 s：关联 + DHCP 在常见家用路由器上够用，
// 又不至于让人从家门口走到客厅的整段时间都卡在第一个网络上。真机若遇到 DHCP 慢的
// 路由器需要上调，这一项仍属未验证。
#define WIFI_CONNECT_ATTEMPT_TIMEOUT_MS 10000u

typedef enum {
    WIFI_CONN_IDLE = 0,       // 未开始（或已停止）
    WIFI_CONN_TRYING,         // 正在尝试某个 profile
    WIFI_CONN_CONNECTED,      // 已连上
    WIFI_CONN_WAIT_PROVISION, // 一轮全败，等配网
} wifi_conn_state_t;

typedef enum {
    WIFI_CONN_EV_START = 0,  // 要求开始/恢复联网
    WIFI_CONN_EV_ATTEMPT_OK, // 当前 profile 关联成功并拿到 IP
    WIFI_CONN_EV_ATTEMPT_FAIL, // 当前 profile 立即失败，不必等超时
    WIFI_CONN_EV_TIMEOUT,    // 单条超时到期
    WIFI_CONN_EV_DISCONNECT, // 连上之后掉线
    WIFI_CONN_EV_STOP,       // 外部要求停止（休眠、页面退出）
} wifi_conn_event_t;

typedef enum {
    WIFI_CONN_ACT_NONE = 0,
    WIFI_CONN_ACT_CONNECT,   // 去连 step.index 指定的 profile
    WIFI_CONN_ACT_PROVISION, // 没有可用的 profile，进配网态
    WIFI_CONN_ACT_STOPPED,
} wifi_conn_action_t;

typedef struct {
    wifi_conn_action_t action;
    int8_t             index;      // 仅 CONNECT 有效
    uint32_t           timeout_ms; // >0 时调用方应启动单条超时
} wifi_conn_step_t;

typedef struct {
    const wifi_profile_store_t *store; // 不拥有，调用方保证生命周期；active 可被外部改写
    wifi_conn_state_t           state;
    int8_t                      first;     // 本轮起点：active 合法则用它，否则 0
    int8_t                      candidate; // 正在尝试的索引
    int8_t                      attempted; // 本轮已下发过的尝试次数
    int64_t                     deadline_ms;
    uint32_t                    attempt_timeout_ms;
} wifi_connect_policy_t;

void wifi_connect_policy_init(wifi_connect_policy_t *policy,
                              const wifi_profile_store_t *store);

// 喂一个事件，拿到下一步动作。时间由调用方给，便于测试直接构造时钟。
wifi_conn_step_t wifi_connect_policy_event(wifi_connect_policy_t *policy,
                                           wifi_conn_event_t event, int64_t now_ms);

// 调用方在等待事件超时后调用；到点则等价于一次 WIFI_CONN_EV_TIMEOUT。
wifi_conn_step_t wifi_connect_policy_tick(wifi_connect_policy_t *policy, int64_t now_ms);

// 驱动的掉线原因是否属于"我们自己换 profile 时主动断开"，而不是一次真正的失败。
//
// connect_index() 在连接下一条 profile 之前会调用 esp_wifi_disconnect()，驱动随即
// 发出一个"STA 主动离开"的 STA_DISCONNECTED（实测 IDF 5.5.3 报 reason=36
// WIFI_REASON_STA_LEAVING，老版本报 8 WIFI_REASON_ASSOC_LEAVE）。这个事件是切换
// 动作的回声：如果把它当成新候选的失败，整轮遍历会在 5 ms 内被推完——真机表现
// 就是"先试 active、再试下一条、立刻报没有可用 profile"，而驱动其实已经把最后
// 一条连上了，界面却停在"无网络"。所以这两条原因必须被丢弃。
//
// 其他 reason（201 NO_AP_FOUND、204 握手超时、200 beacon 超时……）都是这次候选
// 真的失败了，仍要推进状态机。
bool wifi_connect_policy_ignores_disconnect(int reason_code);

// 上游 esp_wifi_types_generic.h 里"STA 主动离开"的两个数值。写死而不是 include，
// 与 wifi_status_reason_from_code() 保持同一套做法：映射留在纯逻辑里，可主机测试。
#define WIFI_CONNECT_REASON_ASSOC_LEAVE 8
#define WIFI_CONNECT_REASON_STA_LEAVING 36

wifi_conn_state_t wifi_connect_policy_state(const wifi_connect_policy_t *policy);
