// main/app_clock.h —— 系统时间是否可信的窗口（纯逻辑）。
//
// 只有一条规则，但它同时决定三件事：能不能发起 HTTPS（mbedTLS 校验证书有效期）、
// 快照时间戳是否有意义、日志里的时间是否可信。规则放在这里，SNTP 服务与 worker
// 共用同一份判断。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 下界 2024-01-01T00:00:00Z：设备上电时 time() 从 1970 开始，任何早于这个点的值
// 都只可能是"尚未同步"。
#define APP_CLOCK_VALID_FROM_EPOCH 1704067200LL

// 上界 2100-01-01T00:00:00Z：挡的是写坏或伪造的时间。真实使用期限远短于此。
#define APP_CLOCK_VALID_UNTIL_EPOCH 4102444800LL

// epoch_seconds 是否落在可信窗口内。窗口按左闭右开处理。
bool app_clock_epoch_is_plausible(int64_t epoch_seconds);

// epoch 秒 → "YYYY-MM-DD HH:MMZ"（UTC）。epoch <= 0 或缓冲不足返回 false 并清空 buf。
//
// 放在这里而不是各页面各写一份：天气页的 "Offline - saved" 与用量页的
// "Offline - 2026-11-11 23:59Z" 说的是同一件事，日历算法只该有一个实现。
// 刻意不用 libc 的 gmtime：主机与固件对时区的处理不同，会让同一份快照在两边
// 显示成不同日期，而这份时间戳的全部意义就是"它是什么时候的"。
bool app_clock_format_utc(int32_t epoch, char *buf, size_t cap);
