// main/app_time.h —— SNTP 校时服务（设备侧）。
//
// 为什么必须有它：mbedTLS 用系统时间校验证书的 notBefore/notAfter。设备上电时
// time() 从 1970 开始，此时发起 HTTPS 会以 BADCERT_FUTURE 失败；而 HTTP 的 Date
// 响应头救不了第一次握手（鸡生蛋）。所以连上 Wi-Fi 后必须先校时。
//
// 生命周期：netif 存在之后 start，netif 销毁之前 stop。start 不阻塞等待同步——
// worker 自己轮询 app_time_is_valid()，界面显示 "Waiting for time"。
// 时间无效时绝不退化成"跳过证书校验"，那是另一条完全不同的安全路径。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_netif.h"

// 启动 SNTP。netif 由 wifi_profile_service_netif() 提供；重复调用是空操作。
// 返回 ESP_ERR_INVALID_STATE 表示 netif 为 NULL（Wi-Fi 服务不在运行）。
esp_err_t app_time_start(esp_netif_t *netif);

// 停止并释放在跑的 SNTP。未启动时是空操作。
void app_time_stop(void);

// 系统时间是否已经可信（见 app_clock.h 的窗口）。未同步时返回 false。
bool app_time_is_valid(void);

// 当前 epoch 秒；未同步返回 0。调用方不要用它做算术，除非先确认 is_valid。
int64_t app_time_now(void);
