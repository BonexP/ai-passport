// main/wifi_scan_list.h —— 配网页里那份周围网络列表（纯逻辑）。
//
// 只做四件事：去重、取强、排序、截断。不碰 ESP-IDF，也不碰 HTML：
// 扫描由 wifi_provision_service 在起 AP 之前完成，渲染由 wifi_provision_html
// 负责，这里只保证"给一批扫描结果，得到一份稳定、可预期的列表"。
//
// 为什么要按 SSID 去重：同一个网络通常有两三个 BSSID（2.4G 的多个信道、mesh
// 节点），对用户来说那就是一个网络，列三遍只会让本来只有 3 寸的屏幕更难选。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "wifi_profile.h"

// 一屏能点中的数量上限；再多也选不过来，还挤占页面缓冲。
#define WIFI_SCAN_LIST_MAX 16

typedef struct {
    char   ssid[WIFI_PROFILE_SSID_MAX];
    int8_t rssi; // dBm，负数；0 表示未知（真实值不会为正）
} wifi_scan_entry_t;

typedef struct {
    wifi_scan_entry_t items[WIFI_SCAN_LIST_MAX];
    size_t            count; // 恒 <= WIFI_SCAN_LIST_MAX
} wifi_scan_list_t;

void wifi_scan_list_clear(wifi_scan_list_t *list);

// 加入一条结果。规则：
//   - ssid 为 NULL 或空串直接拒绝（隐藏网络的广播里没有名字，选了也没用）；
//   - 同名网络只保留最强的那条；
//   - 列表按 RSSI 从强到弱排序；
//   - 满 16 条时，比现有最弱还弱的丢弃，否则顶掉最弱的那条。
// 返回 true 表示这条结果对列表产生了影响（新增或变强）。
bool wifi_scan_list_add(wifi_scan_list_t *list, const char *ssid, int rssi);
