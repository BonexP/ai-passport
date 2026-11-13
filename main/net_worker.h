// main/net_worker.h —— 唯一的网络取数任务（多数据源）。
//
// 为什么只有一个任务：TLS 握手峰值占用 20-30 KB 内部 RAM，天气与 AMD 用量共用同一个
// 任务与同一块静态响应缓冲，就能保证"同一时刻只有一条 TLS 连接"。这是硬约束而不是
// 节约：停止预算只有 4 秒（配网要抢射频），两条并发 TLS 会让它直接失效。
//
// 生命周期由 main.c 管：进入天气页或用量页时 start，配网/Audio/BLE/深睡之前 stop。
// 任务内部不持射频，只是 STA 连接上的消费者；是否该取数由 net_schedule 决定，
// 页面不可见时它只是每 250 ms 醒一次看标志，不发请求。
//
// 多源语义（P5.5）：
//   - 每个源有自己的调度、可见性与刷新标志，互不影响；
//   - 每轮循环最多取一个源，按 NET_SOURCE_* 的顺序先到先取，剩下的等下一轮；
//   - 一个源失败只推进它自己的退避，不会碰另一个源。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// 数据源编号。顺序即同一轮里的抢占顺序：天气在前，因为它是既有功能，
// 用量是新增的，没有理由让它抢走天气的刷新时机。
typedef enum {
    NET_SOURCE_WEATHER = 0,
    NET_SOURCE_USAGE,
    NET_SOURCE_COUNT,
} net_source_id_t;

// 建任务。已在运行时返回 ESP_OK（幂等）。失败返回 ESP_ERR_NO_MEM。
esp_err_t net_worker_start(void);

// 停止任务并等待它退出。最坏会等一次在途请求（见 .c 里的预算说明）。
// 未运行时是空操作。必须在 wifi_profile_service_stop() 之前调用。
void net_worker_stop(void);

bool net_worker_running(void);

// 某个源的页面是否可见。不可见的源不取数（电池设备，不为看不到的页面唤醒射频）。
// id 越界是空操作。
void net_worker_set_visible(net_source_id_t id, bool visible);

// 用户按了刷新：该源的下一次门都打开时立即取数。线程安全，只置一个标志。
void net_worker_request_refresh(net_source_id_t id);
