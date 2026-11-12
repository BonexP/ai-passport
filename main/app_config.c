// main/app_config.c —— 编译期配置的唯一取值点。
//
// 这是全程序唯一 include app_config.local.h 的编译单元，因此凭据在固件里只存在
// 一份，也不会被任何主机测试的桩意外看到。
#include "app_config.h"

// __has_include 到 C23 才进标准，GCC 5+ / Clang 早就作为扩展提供。用
// `defined(__has_include)` 包一层：在没有这个运算符的编译器上退化成"没有本地配置"，
// 而不是语法错误。两个 #if 必须嵌套，不能拼成一个条件：部分预处理器在解析 && 右侧
// 时仍会把未知的类函数宏当错误。
//
// 用引号而不是尖括号：引号形式按"包含者所在目录"查找，正好命中 main/ 下的本地文件；
// 尖括号要走 -I，CI 与本地可能走出不同结果。
#if defined(__has_include)
#if __has_include("app_config.local.h")
#include "app_config.local.h"
#define APP_CONFIG_LOCAL_PRESENT 1
#endif
#endif

#ifndef APP_CONFIG_LOCAL_PRESENT
#define APP_CONFIG_LOCAL_PRESENT 0
#endif

// ---- Wi-Fi 预设 ----
//
// 空列表不能写成 `= {}`：C11 不允许零长数组。所以"没有预设"用"不定义数组 + 返回
// 常量"表达，两个分支都是标准 C11，CI 上不会因为文件缺失而编译失败。
#ifdef APP_CONFIG_WIFI
static const app_config_wifi_t s_wifi[] = {APP_CONFIG_WIFI};
#define APP_CONFIG_WIFI_TABLE s_wifi
#define APP_CONFIG_WIFI_COUNT (sizeof s_wifi / sizeof s_wifi[0])
#else
#define APP_CONFIG_WIFI_TABLE NULL
#define APP_CONFIG_WIFI_COUNT 0u
#endif

// ---- 自选表 ----
//
// 内置表是产品默认值，不是个人数据：全新 checkout 与 CI 构建都能得到一份可用的
// 行情页。本地头文件一旦提供 APP_CONFIG_QUOTES，这份默认表就不参与编译，
// 避免 -Wunused-variable。
#ifndef APP_CONFIG_QUOTES
static const app_config_quote_t s_default_quotes[] = {
    {.symbol = "930713", .label = "CSI AI"},
    {.symbol = "159819", .label = "AI ETF"},
    {.symbol = "QQQ", .label = "NASDAQ 100"},
    {.symbol = "SPY", .label = "S&P 500"},
    {.symbol = "NVDA", .label = "NVIDIA"},
    {.symbol = "TSLA", .label = "TESLA"},
    {.symbol = "XAU", .label = "GOLD"},
    {.symbol = "MU", .label = "MICRON"},
};
#define APP_CONFIG_QUOTE_TABLE s_default_quotes
#else
static const app_config_quote_t s_quotes[] = {APP_CONFIG_QUOTES};
#define APP_CONFIG_QUOTE_TABLE s_quotes
#endif

#define APP_CONFIG_QUOTE_COUNT (sizeof APP_CONFIG_QUOTE_TABLE / sizeof APP_CONFIG_QUOTE_TABLE[0])

// ---- AMD key ----
//
// 三种形态在编译期就已决定：本地头文件没定义 APP_CONFIG_AMD_KEY 时退回 NULL，
// 与 Wi-Fi 预设的"缺段退默认值"一致；定义为空串时原样返回，让自检把它报成
// 问题而不是在访问器里悄悄改写成 NULL——访问器只负责取值，不做裁决。
#ifdef APP_CONFIG_AMD_KEY
#define APP_CONFIG_AMD_KEY_VALUE APP_CONFIG_AMD_KEY
#else
#define APP_CONFIG_AMD_KEY_VALUE NULL
#endif

bool app_config_local_present(void) {
    return APP_CONFIG_LOCAL_PRESENT != 0;
}

size_t app_config_wifi_count(void) {
    return (size_t)APP_CONFIG_WIFI_COUNT;
}

const app_config_wifi_t *app_config_wifi_table(void) {
    return APP_CONFIG_WIFI_TABLE; // count 为 0 时为 NULL，测试据此断言两者一致
}

size_t app_config_quote_count(void) {
    return (size_t)APP_CONFIG_QUOTE_COUNT;
}

const app_config_quote_t *app_config_quote_table(void) {
    return APP_CONFIG_QUOTE_TABLE;
}

const char *app_config_amd_key(void) {
    return APP_CONFIG_AMD_KEY_VALUE;
}
