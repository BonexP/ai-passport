// main/app_config_check.h —— 编译期配置的自检（纯逻辑）。
//
// 为什么需要它：本地头文件里的值在 C 里都"合法"，编译器管不了纬度写反、SSID 写空、
// 自选表符号重复。这些错误过去只会在使用点被静默跳过（预设少一条、地点被丢掉），
// 而设备照常开机，没人知道为什么。
//
// 这里把所有规则集中成一次检查，返回可逐条打日志的问题清单：
//   - 设备开机时调用一次，把问题写进串口日志；
//   - 主机测试可以用构造的坏表逐条断言，不需要真的改本地配置。
//
// 校验规则与真正消费这些数据的模块保持一致：Wi-Fi 字段复用
// wifi_profile_set_fields()，地点复用 app_location_set()，不另写一套。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"

// 自检一次最多记多少条问题。配置本身很小，16 条足够；超出的只计数。
#define APP_CONFIG_ISSUE_MAX 16

// 自选表上界：240x320 一屏最多放 8 行。
#define APP_CONFIG_QUOTE_MAX 8

typedef enum {
    APP_CONFIG_ISSUE_WIFI_FIELDS = 0, // SSID/密码/显示名非法：该条会被注入跳过
    APP_CONFIG_ISSUE_WIFI_LOCATION,   // 网络合法但坐标/地点名非法：只丢地点
    APP_CONFIG_ISSUE_WIFI_DUPLICATE,  // SSID 与前面某条重复：后面这条会被跳过
    APP_CONFIG_ISSUE_WIFI_OVERFLOW,   // 条数超过 WIFI_PROFILE_MAX
    APP_CONFIG_ISSUE_QUOTE_FIELDS,    // symbol/label 为空、超长或含非 ASCII
    APP_CONFIG_ISSUE_QUOTE_DUPLICATE, // symbol 与前面某条重复
    APP_CONFIG_ISSUE_QUOTE_OVERFLOW,  // 条数超过 APP_CONFIG_QUOTE_MAX
    APP_CONFIG_ISSUE_AMD_KEY_INVALID, // key 非空但含不可打印字符或超出长度上限
} app_config_issue_code_t;

typedef struct {
    app_config_issue_code_t code;
    size_t                  index; // 出问题的表项下标；OVERFLOW 时是第一个越界项
} app_config_issue_t;

// 检查两张表。issues 可为 NULL（只计数），cap 为 0 时同样只计数。
// 返回发现的问题总数，可能大于 cap；数组只填前 cap 条。
// 入参为 NULL 时按空表处理。
size_t app_config_check(const app_config_wifi_t *wifi, size_t wifi_count,
                        const app_config_quote_t *quotes, size_t quote_count,
                        app_config_issue_t *issues, size_t cap);

// 单独校验 AMD key。与 app_config_check 分开，因为 key 不属于任何一张表：
// 没配（NULL）是合法且最常见的形态，页面自己会显示 Key not configured。
// 空串或含不可打印字符才是问题。key 可为 NULL。
bool app_config_amd_key_valid(const char *key);

// 问题码 → 一句 ASCII 说明（日志用）。未知码返回 "unknown"。
const char *app_config_issue_text(app_config_issue_code_t code);
