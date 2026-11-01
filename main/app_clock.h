// main/app_clock.h —— 系统时间是否可信的窗口（纯逻辑）。
//
// 只有一条规则，但它同时决定三件事：能不能发起 HTTPS（mbedTLS 校验证书有效期）、
// 快照时间戳是否有意义、日志里的时间是否可信。规则放在这里，SNTP 服务与 worker
// 共用同一份判断。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 下界 2024-01-01T00:00:00Z：设备上电时 time() 从 1970 开始，任何早于这个点的值
// 都只可能是"尚未同步"。
#define APP_CLOCK_VALID_FROM_EPOCH 1704067200LL

// 上界 2100-01-01T00:00:00Z：挡的是写坏或伪造的时间。真实使用期限远短于此。
#define APP_CLOCK_VALID_UNTIL_EPOCH 4102444800LL

// epoch_seconds 是否落在可信窗口内。窗口按左闭右开处理。
bool app_clock_epoch_is_plausible(int64_t epoch_seconds);
