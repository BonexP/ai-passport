// main/net_schedule.h —— 什么时候该取一次数（纯逻辑）。
//
// 三个门：连着网、时间可信、页面可见。再加一条节奏：成功后按固定间隔，
// 失败后指数退避。规则全部在这里，worker 只负责问 "due?" 与汇报结果，
// 因此调度可以在主机上把每一条时间线走一遍。
//
// 为什么不后台常驻轮询：这是电池设备，屏幕关掉后没有理由继续唤醒射频。
// 代价是下次进页面要等一次请求，换来的是可预测的功耗。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 成功后到下一次的间隔。天气不需要更实时；行情（P3）会另给更短的间隔。
#define NET_SCHEDULE_INTERVAL_MS (10 * 60 * 1000)

// 失败退避：30 s、60 s、120 s…… 封顶 5 分钟。
#define NET_SCHEDULE_BACKOFF_BASE_MS (30 * 1000)
#define NET_SCHEDULE_BACKOFF_MAX_MS (5 * 60 * 1000)

typedef struct {
    bool    connected;       // Wi-Fi 已拿到 IP
    bool    time_valid;      // SNTP 已同步（TLS 证书校验需要）
    bool    visible;         // 天气页当前可见
    bool    force;           // 用户按了刷新：忽略间隔，只等门打开
    uint8_t failures;        // 连续失败次数，成功清零
    int64_t last_attempt_ms; // 上次开始取数的时刻；-1 = 本次开机还没试过
} net_schedule_t;

// 初始化：三个门都是关的，last_attempt = -1。
void net_schedule_init(net_schedule_t *s);

// 更新外部条件。不触发取数，只改变 due() 的答案。
void net_schedule_set_context(net_schedule_t *s, bool connected, bool time_valid, bool visible);

// 现在是否该取数。纯查询，不改状态。
bool net_schedule_due(const net_schedule_t *s, int64_t now_ms);

// 调用方决定取数时先调它：消费 force 标记并记下本次尝试时刻。
void net_schedule_begin(net_schedule_t *s, int64_t now_ms);

// 取数结果。成功清零退避；失败累加退避。
void net_schedule_on_success(net_schedule_t *s, int64_t now_ms);
void net_schedule_on_failure(net_schedule_t *s, int64_t now_ms);

// 用户主动刷新：下一次 due() 只要门都开着就返回 true。
void net_schedule_request_refresh(net_schedule_t *s);

// 当前生效的间隔（毫秒）：无失败时是 NET_SCHEDULE_INTERVAL_MS，失败后按退避。
uint32_t net_schedule_interval_ms(const net_schedule_t *s);
