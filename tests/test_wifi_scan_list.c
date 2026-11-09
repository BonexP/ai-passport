// tests/test_wifi_scan_list.c —— 扫描结果的去重、排序与截断（纯逻辑）。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wifi_scan_list.h"

static void test_clear_empties_the_list(void) {
    wifi_scan_list_t list;
    memset(&list, 0xff, sizeof list);

    wifi_scan_list_clear(&list);
    assert(list.count == 0);

    wifi_scan_list_clear(NULL); // 不该崩
}

static void test_add_keeps_the_first_entry(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    assert(wifi_scan_list_add(&list, "HomeNet", -48));
    assert(list.count == 1);
    assert(strcmp(list.items[0].ssid, "HomeNet") == 0);
    assert(list.items[0].rssi == -48);
}

static void test_same_ssid_keeps_the_strongest(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    assert(wifi_scan_list_add(&list, "HomeNet", -80));
    // 同一个网络的两台设备：弱的这次不该覆盖，也不该多出一行。
    assert(!wifi_scan_list_add(&list, "HomeNet", -85));
    assert(list.count == 1);
    assert(list.items[0].rssi == -80);

    // 遇到更强的一次广播就更新。
    assert(wifi_scan_list_add(&list, "HomeNet", -40));
    assert(list.count == 1);
    assert(list.items[0].rssi == -40);
}

static void test_sorted_by_signal(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    assert(wifi_scan_list_add(&list, "weak", -88));
    assert(wifi_scan_list_add(&list, "strong", -30));
    assert(wifi_scan_list_add(&list, "middle", -60));

    assert(list.count == 3);
    assert(strcmp(list.items[0].ssid, "strong") == 0);
    assert(strcmp(list.items[1].ssid, "middle") == 0);
    assert(strcmp(list.items[2].ssid, "weak") == 0);
}

static void test_rejects_empty_and_missing_ssid(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    // 隐藏网络的广播里没有名字：列出来也选不中。
    assert(!wifi_scan_list_add(&list, "", -50));
    assert(!wifi_scan_list_add(&list, NULL, -50));
    assert(list.count == 0);
    assert(!wifi_scan_list_add(NULL, "HomeNet", -50));
}

static void test_ssid_longer_than_the_limit_is_truncated(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    const char *long_ssid = "0123456789abcdef0123456789abcdefEXTRA";
    assert(wifi_scan_list_add(&list, long_ssid, -50));
    assert(list.count == 1);
    assert(strlen(list.items[0].ssid) == WIFI_PROFILE_SSID_MAX - 1);
    assert(strncmp(list.items[0].ssid, long_ssid, WIFI_PROFILE_SSID_MAX - 1) == 0);
}

static void test_rssi_is_clamped(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    assert(wifi_scan_list_add(&list, "odd", 12)); // dBm 不会为正
    assert(list.items[0].rssi == 0);

    assert(wifi_scan_list_add(&list, "far", -300));
    assert(list.items[1].rssi == -128);
}

static void test_full_list_drops_the_weakest(void) {
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    char name[8];
    for (int i = 0; i < WIFI_SCAN_LIST_MAX; i++) {
        snprintf(name, sizeof name, "ap%d", i);
        assert(wifi_scan_list_add(&list, name, -10 - i));
    }
    assert(list.count == WIFI_SCAN_LIST_MAX);

    // 比最弱的还弱：直接丢掉，列表不变。
    assert(!wifi_scan_list_add(&list, "useless", -100));
    assert(list.count == WIFI_SCAN_LIST_MAX);
    assert(strstr(list.items[WIFI_SCAN_LIST_MAX - 1].ssid, "useless") == NULL);

    // 够强就能挤掉最弱的那条（ap15，-25）。
    assert(wifi_scan_list_add(&list, "newcomer", -5));
    assert(list.count == WIFI_SCAN_LIST_MAX);
    assert(strcmp(list.items[0].ssid, "newcomer") == 0);
    for (size_t i = 0; i < list.count; i++) {
        assert(strcmp(list.items[i].ssid, "ap15") != 0);
    }
}

int main(void) {
    test_clear_empties_the_list();
    test_add_keeps_the_first_entry();
    test_same_ssid_keeps_the_strongest();
    test_sorted_by_signal();
    test_rejects_empty_and_missing_ssid();
    test_ssid_longer_than_the_limit_is_truncated();
    test_rssi_is_clamped();
    test_full_list_drops_the_weakest();
    puts("wifi scan list tests: PASS");
    return 0;
}
