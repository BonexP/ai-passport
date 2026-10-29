// main/wifi_presets.c —— 编译期预设表的取值与注入逻辑。
//
// 这是全程序唯一 include 本地预设头文件的编译单元，因此凭据在固件里只存在一份，
// 也不会被任何主机测试的桩意外看到。
#include "wifi_presets.h"

#include <stdbool.h>
#include <string.h>

// __has_include 到 C23 才进标准，GCC 5+ / Clang 早就作为扩展提供。用
// `defined(__has_include)` 包一层：在没有这个运算符的编译器上退化成"没有预设"，
// 而不是语法错误。
//
// 两个 #if 必须嵌套，不能拼成 `#if defined(__has_include) && __has_include(...)`：
// 部分预处理器在解析 && 右侧时仍会把未知的类函数宏当错误。
//
// 用引号而不是尖括号：引号形式按"包含者所在目录"查找，正好命中 main/ 下的本地文件；
// 尖括号要走 -I，CI 与本地可能走出不同结果。
#if defined(__has_include)
#if __has_include("wifi_presets.local.h")
#include "wifi_presets.local.h" // 定义 static const wifi_preset_t WIFI_PRESETS[]
#define WIFI_PRESETS_PRESENT 1
#endif
#endif

#ifndef WIFI_PRESETS_PRESENT
#define WIFI_PRESETS_PRESENT 0
#endif

// 空列表绝不写成 `= {}`：C11 不允许零长数组。所以"没有预设"用"不定义数组 + 返回
// 常量"表达，两个分支都是标准 C11，且 CI 上不会因为文件缺失而编译失败。
#if WIFI_PRESETS_PRESENT
size_t wifi_presets_count(void) {
    // 条数由 sizeof 推出，本地头文件不写（也不可能写错）条数宏。
    return sizeof WIFI_PRESETS / sizeof WIFI_PRESETS[0];
}

const wifi_preset_t *wifi_presets_table(void) {
    return WIFI_PRESETS;
}
#else
size_t wifi_presets_count(void) {
    return 0;
}

const wifi_preset_t *wifi_presets_table(void) {
    return NULL; // count 为 0 时指针无意义；测试据此断言两者一致
}
#endif

// 库里是否已有同 SSID 的记录。调用点保证进来时库为空，所以这里的比较对象只可能是
// 本次刚加进去的预设——用来挡住"同一份本地文件里两行写了同一个网络"。
static bool ssid_already_present(const wifi_profile_store_t *s, const char *ssid) {
    uint8_t count = wifi_profile_store_count(s);
    for (uint8_t i = 0; i < count; i++) {
        const wifi_profile_t *p = wifi_profile_store_at(s, i);
        if (p != NULL && strcmp(p->ssid, ssid) == 0) return true;
    }
    return false;
}

wifi_preset_seed_result_t wifi_preset_seed_apply(wifi_profile_store_t *s,
                                                 const wifi_preset_t *presets, size_t count,
                                                 wifi_preset_seed_report_t *report) {
    if (report != NULL) {
        report->added = 0;
        report->skipped = 0;
    }
    if (s == NULL) return WIFI_PRESET_SEED_NOTHING_TO_ADD;

    // 先判库非空，再判有没有预设。反过来的话，"库非空 + 0 条预设"会报成"没有可加的
    // 东西"，把更重要的事实藏掉，日志也就失去信息。
    if (wifi_profile_store_count(s) > 0) return WIFI_PRESET_SEED_LIBRARY_NOT_EMPTY;
    if (presets == NULL || count == 0) return WIFI_PRESET_SEED_NOTHING_TO_ADD;

    uint8_t added = 0;
    uint8_t skipped = 0;
    for (size_t i = 0; i < count; i++) {
        wifi_profile_t p;
        // 长度与空值规则复用 store 的校验：SSID 空或超 32、密码超 64、显示名 >= 24
        // 都在这里被拒。手写文件的一个笔误只丢这一条，不该让整台设备变成零网络。
        if (!wifi_profile_set_fields(&p, presets[i].name, presets[i].ssid, presets[i].password)) {
            skipped++;
            continue;
        }
        if (ssid_already_present(s, p.ssid)) {
            skipped++;
            continue;
        }
        // 槽位满（WIFI_PROFILE_MAX）时 add 返回 FULL，同样只跳过这一条。
        if (wifi_profile_store_add(s, &p) != WIFI_PROFILE_OK) {
            skipped++;
            continue;
        }
        added++;
    }

    if (report != NULL) {
        report->added = added;
        report->skipped = skipped;
    }
    // 一条都没写成就不碰 NVS：这让"CI 固件（0 条预设）开机不写 flash"成为结构上的
    // 事实，而不是一条需要人记住的约定。
    if (added == 0) return WIFI_PRESET_SEED_NOTHING_TO_ADD;

    // 落盘失败也不回滚 RAM：本次开机照样能连，磁盘仍为空，下次开机会重新注入。
    if (wifi_profile_store_save(s) != WIFI_PROFILE_OK) return WIFI_PRESET_SEED_ERR_SAVE;
    return WIFI_PRESET_SEED_APPLIED;
}

wifi_preset_seed_result_t wifi_preset_seed_builtin(wifi_profile_store_t *s,
                                                   wifi_preset_seed_report_t *report) {
    return wifi_preset_seed_apply(s, wifi_presets_table(), wifi_presets_count(), report);
}
