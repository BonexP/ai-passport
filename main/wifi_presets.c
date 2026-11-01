// main/wifi_presets.c —— 预设表的注入逻辑。
//
// 数据来自 app_config（唯一包含本地头的编译单元），这里只做"校验 + 写入 + 落盘"。
#include "wifi_presets.h"

#include <stdbool.h>
#include <string.h>

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

// 把预设里的地点写进 profile。坐标或地点名写错时只丢地点，不丢网络：
// 网络本身仍然可连，天气退化为"未配置"，这比整条记录被跳过更符合预期。
// 返回 false 表示"这条预设带了地点，但地点非法被丢弃"，供报告计数。
static bool apply_location(wifi_profile_t *p, const app_config_wifi_t *preset) {
    if (!preset->has_location) return true;

    app_location_t loc;
    if (!app_location_set(&loc, preset->lat_udeg, preset->lon_udeg, preset->place)) {
        return false;
    }
    return wifi_profile_set_location(p, &loc);
}

wifi_preset_seed_result_t wifi_preset_seed_apply(wifi_profile_store_t *s,
                                                 const app_config_wifi_t *presets, size_t count,
                                                 wifi_preset_seed_report_t *report) {
    if (report != NULL) {
        report->added = 0;
        report->skipped = 0;
        report->locations_dropped = 0;
    }
    if (s == NULL) return WIFI_PRESET_SEED_NOTHING_TO_ADD;

    // 先判库非空，再判有没有预设。反过来的话，"库非空 + 0 条预设"会报成"没有可加的
    // 东西"，把更重要的事实藏掉，日志也就失去信息。
    if (wifi_profile_store_count(s) > 0) return WIFI_PRESET_SEED_LIBRARY_NOT_EMPTY;
    if (presets == NULL || count == 0) return WIFI_PRESET_SEED_NOTHING_TO_ADD;

    uint8_t added = 0;
    uint8_t skipped = 0;
    uint8_t locations_dropped = 0;
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
        if (!apply_location(&p, &presets[i])) {
            locations_dropped++;
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
        report->locations_dropped = locations_dropped;
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
    return wifi_preset_seed_apply(s, app_config_wifi_table(), app_config_wifi_count(), report);
}
