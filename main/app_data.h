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
#include "usage_source.h"

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

// 用量记录在总线上的一层"运行时外壳"：把 usage_source 解析出来的纯数据（usage_state_t）
// 与它在这条总线上的记账字段拼在一起。
//
// 为什么不像天气那样把运行时字段直接塞进 usage_state_t：那个类型是解析器的输出，
// 应该只描述报文；seq / 抓取时刻 / 最近一次结局都是总线的事。天气当初把两者混在
// 一起是历史包袱，这里不复制。
//
// 没有 stale：用量不做持久化（见 project-plan.md §4.5 与 §9 第 13 条）。额度每天
// 北京时间 0 点归零，隔夜副本是"错"而不是"旧"，所以这里根本不产生这种数据。
// 页面靠 status 判断卡片该说什么。
typedef struct {
    uint16_t       seq;               // 由 bus 维护：每次发布 +1
    int32_t        fetched_epoch;     // SNTP 时间；0 = 未知
    int64_t        fetched_uptime_ms; // 单调时钟；-1 = 未知
    usage_status_t status;            // 最近一次请求的结局
    usage_state_t  usage;             // 报文本身；has_data 为 false 时其余字段无意义
} usage_record_t;

typedef struct {
    weather_state_t weather;
    usage_record_t  usage;
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

// 整体覆盖用量记录并 seq++，全程持锁。next->seq 被忽略。
//
// 关键约定：失败不应该把已有数字擦掉。worker 在取数失败时要把上一条记录的数字字段
// 原样带上，只改 status —— 这正是"key 被拒那天，昨天剩下的点数仍然有意义"的实现路径。
// bus 只做整体覆盖，没有"只改状态"的魔法；保住数字是写者的责任。
void app_data_publish_usage(app_data_bus_t *bus, const usage_record_t *next);

// 整体拷贝出来，全程持锁。读侧通常是 LVGL 定时器，200-500 ms 一次。
void app_data_read(const app_data_bus_t *bus, app_data_t *out);
