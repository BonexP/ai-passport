// main/net_worker.h —— 唯一的网络取数任务。
//
// 为什么只有一个任务：TLS 握手峰值占用 20-30 KB 内部 RAM，天气与将来的行情、探针
// 共用同一个任务与同一块静态响应缓冲，就能保证"同一时刻只有一条 TLS 连接"。
//
// 生命周期由 main.c 管：进入天气页时 start，配网/Audio/BLE/深睡之前 stop。
// 任务内部不持射频，只是 STA 连接上的消费者；是否该取数由 net_schedule 决定，
// 页面不可见时它只是每 250 ms 醒一次看标志，不发请求。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// 建任务。已在运行时返回 ESP_OK（幂等）。失败返回 ESP_ERR_NO_MEM。
esp_err_t net_worker_start(void);

// 停止任务并等待它退出。最坏会等一次在途请求（见 .c 里的预算说明）。
// 未运行时是空操作。必须在 wifi_profile_service_stop() 之前调用。
void net_worker_stop(void);

bool net_worker_running(void);

// 天气页是否可见。不可见时不取数（电池设备，不为看不到的页面唤醒射频）。
void net_worker_set_visible(bool visible);

// 用户按了刷新：下一次门都打开时立即取数。线程安全，只置一个标志。
void net_worker_request_refresh(void);
