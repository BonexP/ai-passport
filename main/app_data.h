// main/app_data.h —— 网络数据的快照总线（纯逻辑）。
//
// 单写者 + 整体拷贝，与 wifi_status 同一套模式：net worker 是唯一写者，UI 只读
// 一份拷贝。锁以 vtable 注入，主机测试用计数器验证"每次访问恰好锁一次"，
// 设备上由 app_data_shared.c 注入 portMUX 实现。
//
// 为什么天气与（将来的）行情放在同一个结构里：它们都由同一个 worker 串行更新，
// 一个 seq 就能让 UI 判断"这一帧要不要重画"，也避免 N 个模块各造一套锁协议。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_location.h"

// 接口返回的本地观测时间串，如 "2026-11-01T14:00"。原样显示，不解析。
#define APP_DATA_OBSERVED_MAX 20

// 一次成功的天气观测。所有字段都是 POD，可整体拷贝。
typedef struct {
    uint16_t seq;               // 由 bus 维护：每次发布 +1
    bool     has_data;          // false = 从未成功取过数
    bool     stale;             // true = 来自 NVS 的旧快照
    int16_t  temp_dc;           // 十分之一摄氏度：123 = 12.3
    int16_t  weather_code;      // Open-Meteo 的 WMO 码
    int32_t  fetched_epoch;     // SNTP 时间；0 = 未知
    int64_t  fetched_uptime_ms; // 同一次开机内的单调时间；-1 = 未知（用于算"多久前"）
    char     place[APP_LOCATION_PLACE_MAX];
    char     observed[APP_DATA_OBSERVED_MAX];
} weather_state_t;

typedef struct {
    weather_state_t weather;
} app_data_t;

typedef struct {
    void *ctx;
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} app_data_lock_t;

typedef struct {
    app_data_t               data;
    const app_data_lock_t   *lock; // 可为 NULL，等价于不加锁（只在单线程场景用）
} app_data_bus_t;

// 清零并绑定临界区。bus 不可为 NULL。
void app_data_bus_init(app_data_bus_t *bus, const app_data_lock_t *lock);

// 整体覆盖天气并 seq++，全程持锁。next->seq 被忽略，由 bus 自己维护。
// 写者不变量：只允许 net worker 调用。
void app_data_publish_weather(app_data_bus_t *bus, const weather_state_t *next);

// 整体拷贝出来，全程持锁。读侧通常是 LVGL 定时器，200-500 ms 一次。
void app_data_read(const app_data_bus_t *bus, app_data_t *out);
