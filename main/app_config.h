// main/app_config.h —— 编译期自定义配置的只读契约。
//
// 真实数据放在 main/app_config.local.h（被 .gitignore 忽略），本文件只声明类型与
// 访问器，因此这份头文件不含任何凭据，可以安全入库。
//
// 为什么配置是编译期而不是 NVS：
//   设备只有三个按键和一块 240x320 屏，要做"在设备上编辑地点/自选表"就得再写一套
//   列表编辑 UI。把这些个人偏好在编译期定下来，换来零 UI、零迁移、零额外测试。
//   将来若要运行期覆盖，只需换掉 app_config.c 的实现，访问器签名不用动。
//
// 本地头文件的写法见 app_config.local.h.example：每个 section 是一个"初始化列表宏"，
// 用 #define 提供，缺失的 section 自动退回默认值。用宏而不是数组，是因为只有宏才能
// 被 #ifdef 检测到——变量名不是预处理器符号。
//
// 安全边界：Wi-Fi 口令等字符串会以明文进入固件 .rodata，拿到设备就能提取。
// 这不是保密方案，只是"免配网"的便利。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 一条预设 Wi-Fi。字段用 const char * 让本地文件就是几行字符串字面量；
// 长度与合法性由 wifi_profile_set_fields() 统一校验，规则只维护一份。
typedef struct {
    const char *name;     // 显示名；NULL 表示界面上退化为 SSID；须 < 24 字节
    const char *ssid;     // 必填，1..32 字节
    const char *password; // NULL 表示开放网络；非 NULL 时 <= 64 字节

    // 这个网络对应的地点。has_location == false 时后面四项都被忽略。
    bool        has_location;
    int32_t     lat_udeg; // 微度，-90000000..90000000
    int32_t     lon_udeg; // 微度，-180000000..180000000
    const char *place;    // 地点名，可为 NULL/空；须 < 24 字节
} app_config_wifi_t;

// 一个自选标的。symbol 用于请求，label 用于显示；本阶段 label 必须是 ASCII。
typedef struct {
    const char *symbol;
    const char *label;
} app_config_quote_t;

// 本次构建是否真的带上了本地配置。开机日志用它区分"配置为空"与"文件没被编进来"。
bool app_config_local_present(void);

// 预设表。没有本地文件时 count 为 0、table 为 NULL；有本地文件时两者一致。
// 返回的指针生命周期与固件相同，不拥有、不需要释放。
size_t                  app_config_wifi_count(void);
const app_config_wifi_t *app_config_wifi_table(void);

// 自选表。没有本地文件时返回内置的 8 个产品默认标的（不是个人数据），
// 因此全新 checkout 也能得到一个可用的行情页。
size_t                   app_config_quote_count(void);
const app_config_quote_t *app_config_quote_table(void);
