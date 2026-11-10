// tests/demo_stubs/demo_app_config.c —— app_config 的主机测试替身。
//
// main/app_config.c 会通过 __has_include 吃进开发机的 main/app_config.local.h：
// 本地写了 Wi-Fi 预设的机器上，wifi_profile_service_start() 的预设注入会把
// "空库"测试前提打破（2026-11-10 实证：test_wifi_service 在带本地头的机器上
// 必挂，CI 因没有该文件而常绿——本地红 CI 绿的假回归）。链接服务类主机测试
// 时用本文件替换 main/app_config.c（见 tools/validate.sh），让测试与开发机
// 解耦——这正是 test_wifi_presets.c 注释里约定的卫生目标。
//
// 表内容 = 全新 checkout 的默认形态：无本地配置、无 Wi-Fi 预设；行情用内置
// 产品默认表（与 main/app_config.c 保持一致，不是个人数据）。带 --gc-sections
// 链接时，未被引用的部分会被裁掉，这里保持全量定义只是求稳。
#include "app_config.h"

#include <stddef.h>

bool app_config_local_present(void) { return false; }

size_t app_config_wifi_count(void) { return 0; }

const app_config_wifi_t *app_config_wifi_table(void) { return NULL; }

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

size_t app_config_quote_count(void) {
    return sizeof s_default_quotes / sizeof s_default_quotes[0];
}

const app_config_quote_t *app_config_quote_table(void) {
    return s_default_quotes;
}
