// main/wifi_presets.h —— 编译期预设 Wi-Fi：类型、表访问器与注入语义。
//
// 预设是"第一次怎么连"的种子，不是安全机制：字符串会明文进入固件 .rodata，拿到设备
// 就能提取。数据本身放在 main/wifi_presets.local.h（被 .gitignore 忽略），本文件只声明
// 契约，因此这份头文件不含任何凭据，可以安全入库。
//
// 为什么这里需要条件编译，而 wifi_profile.h 明确反对 #ifdef：
// 那边拒绝的是"用 #ifdef 区分平台"——平台差异用函数指针 vtable 在运行期就能解决。
// 这里的分歧不在平台，而在构建输入：本地头文件可能存在也可能不存在，而预设必须是
// 编译期常量。运行期的间接层表达不了"这个文件不存在"，所以 __has_include 是必要例外，
// 并且被收敛在唯一一个 .c 文件里，不扩散到任何调用方。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wifi_profile.h"

// 一条预设。字段用 const char * 而不是定长数组，让本地头文件就是几行字符串字面量
// （手写时出错的机会最小）；长度与合法性统一交给 wifi_profile_set_fields() 校验，
// 规则只维护一份。
typedef struct {
    const char *name;     // 显示名。NULL 表示界面上退化为 SSID；非 NULL 时须 < 24 字节
    const char *ssid;     // 必填，1..32 字节
    const char *password; // NULL 表示开放网络；非 NULL 时 <= 64 字节
} wifi_preset_t;

// 编译期预设表的只读视图。没有本地头文件时分别是 0 与 NULL。
// 定义在 wifi_presets.c，所以凭据在固件里只存在这一份。
size_t               wifi_presets_count(void);
const wifi_preset_t *wifi_presets_table(void);

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
} wifi_preset_seed_report_t;

// 把 presets[0..count) 注入 store。只在 store 为空时动手，写库后调 save() 落盘。
//
// 前置：store 已 load 过（s->kv 已就绪）。
// 失败：返回 ERR_SAVE 时 RAM 里的记录保持有效（本次开机可用），磁盘仍为空，
//       下次开机自然重试——整套语义幂等，所以既不回滚，也不需要"已注入"标志位。
// 线程：只由启动路径在 worker 创建之前调用，因此不加锁。
// report 可为 NULL；无论是否传入，入口都会先清零。
wifi_preset_seed_result_t wifi_preset_seed_apply(wifi_profile_store_t *s,
                                                 const wifi_preset_t *presets, size_t count,
                                                 wifi_preset_seed_report_t *report);

// wifi_preset_seed_apply() + 编译期表。服务层只调这个。
wifi_preset_seed_result_t wifi_preset_seed_builtin(wifi_profile_store_t *s,
                                                   wifi_preset_seed_report_t *report);
