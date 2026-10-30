// main/wifi_provision_service.h —— SoftAP 配网的设备侧入口。
//
// 只暴露三个函数，**不 include wifi_profile_service.h**：两个服务互相不感知对方的存在，
// 互斥由 wifi_radio_owner 表达。main.c 负责"先停联网服务、再起配网"的顺序。
//
// 两条不变量（实现见 .c 文件头）：
//   1. 同一时刻只有一个服务持有射频，因此 wifi_status 只有一个写者。
//   2. HTTP handler 只碰 socket；worker 只碰 NVS 与驱动；两者只通过一个信号量交接。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// 起 AP + HTTP。会先抢射频；被联网服务占着时返回 ESP_ERR_INVALID_STATE。
// 失败时栈已完整回滚并释放抢占。
esp_err_t wifi_provision_service_start(void);

// 幂等。会先停 HTTP（join httpd 任务），再回收 worker，最后拆栈。
void wifi_provision_service_stop(void);

bool wifi_provision_service_active(void);
