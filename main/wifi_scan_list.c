// main/wifi_scan_list.c —— 扫描结果的去重、排序与截断。契约见 wifi_scan_list.h。
#include "wifi_scan_list.h"

#include <string.h>

void wifi_scan_list_clear(wifi_scan_list_t *list) {
    if (list == NULL) return;
    list->count = 0;
}

static size_t bounded_len(const char *s, size_t cap) {
    size_t n = 0;

    if (s == NULL) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

// 插入排序，强的在前。列表最多 16 条，够用且没有额外内存。
static void sort_desc(wifi_scan_list_t *list) {
    for (size_t i = 1; i < list->count; i++) {
        wifi_scan_entry_t key = list->items[i];
        size_t j = i;
        while (j > 0 && list->items[j - 1].rssi < key.rssi) {
            list->items[j] = list->items[j - 1];
            j--;
        }
        list->items[j] = key;
    }
}

static int8_t clamp_rssi(int rssi) {
    if (rssi > 0) return 0;      // dBm 不会为正，异常值按未知处理
    if (rssi < -128) return -128;
    return (int8_t)rssi;
}

static void fill_entry(wifi_scan_entry_t *entry, const char *ssid, size_t len, int rssi) {
    memset(entry, 0, sizeof *entry);
    memcpy(entry->ssid, ssid, len);
    entry->ssid[len] = '\0';
    entry->rssi = clamp_rssi(rssi);
}

bool wifi_scan_list_add(wifi_scan_list_t *list, const char *ssid, int rssi) {
    if (list == NULL) return false;

    const size_t len = bounded_len(ssid, WIFI_PROFILE_SSID_MAX - 1);
    if (len == 0) return false;

    const int8_t dbm = clamp_rssi(rssi);

    for (size_t i = 0; i < list->count; i++) {
        // 按截断后的长度比，避免超过 32 字节的输入与已存条目永远匹配不上。
        if (bounded_len(list->items[i].ssid, WIFI_PROFILE_SSID_MAX - 1) != len) continue;
        if (memcmp(list->items[i].ssid, ssid, len) != 0) continue;
        // 同名网络：只留最强的一次广播。
        if (dbm > list->items[i].rssi) {
            list->items[i].rssi = dbm;
            sort_desc(list);
            return true;
        }
        return false;
    }

    if (list->count == WIFI_SCAN_LIST_MAX) {
        size_t weakest = 0;
        for (size_t i = 1; i < list->count; i++) {
            if (list->items[i].rssi < list->items[weakest].rssi) weakest = i;
        }
        if (dbm <= list->items[weakest].rssi) return false;
        fill_entry(&list->items[weakest], ssid, len, dbm);
        sort_desc(list);
        return true;
    }

    fill_entry(&list->items[list->count], ssid, len, dbm);
    list->count++;
    sort_desc(list);
    return true;
}
