// main/wifi_presets.h —— 预设 Wi-Fi 的注入语义（数据来自 app_config）。
//
// 预设是"第一次怎么连"的种子，不是安全机制。数据本身住在 app_config.local.h，
// 本模块只负责按规则把 app_config 提供的表写进 profile store，因此这里不再
// include 任何本地头文件——凭据只在 app_config.c 一个编译单元里出现。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "wifi_profile.h"

// 注入结果。四个值各自对应服务层里一条不同的日志，调用方不要合并处理。
typedef enum {
    WIFI_PRESET_SEED_APPLIED = 0,       // 至少一条写进库并已落盘
    WIFI_PRESET_SEED_LIBRARY_NOT_EMPTY, // 库非空：按语义什么都不做（正常路径，不是错误）
    WIFI_PRESET_SEED_NOTHING_TO_ADD,    // 库为空但没有一条合法预设：未写盘
    WIFI_PRESET_SEED_ERR_SAVE,          // 已写进 RAM，落盘失败
} wifi_preset_seed_result_t;

// 注入统计，让"本地头文件写错了"这件事在日志里可见，而不是静默少一条。
typedef struct {
    uint8_t added;   // 成功写入 RAM 的条数
    uint8_t skipped; // 字段非法、与已加入项撞 SSID、或槽位已满而跳过的条数
    // 网络写进去了，但这条预设的坐标/地点名非法，地点被丢弃。网络仍可连，
    // 天气页会显示 Location not set；单独计数是为了让日志说清"为什么没天气"。
    uint8_t locations_dropped;
} wifi_preset_seed_report_t;

// 把 presets[0..count) 注入 store。只在 store 为空时动手，写库后调 save() 落盘。
//
// 前置：store 已 load 过（s->kv 已就绪）。
// 失败：返回 ERR_SAVE 时 RAM 里的记录保持有效（本次开机可用），磁盘仍为空，
//       下次开机自然重试——整套语义幂等，所以既不回滚，也不需要"已注入"标志位。
// 线程：只由启动路径在 worker 创建之前调用，因此不加锁。
// report 可为 NULL；无论是否传入，入口都会先清零。
wifi_preset_seed_result_t wifi_preset_seed_apply(wifi_profile_store_t *s,
                                                 const app_config_wifi_t *presets, size_t count,
                                                 wifi_preset_seed_report_t *report);

// wifi_preset_seed_apply() + app_config 里的预设表。服务层只调这个。
wifi_preset_seed_result_t wifi_preset_seed_builtin(wifi_profile_store_t *s,
                                                   wifi_preset_seed_report_t *report);
