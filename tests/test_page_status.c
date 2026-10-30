// tests/test_page_status.c —— 状态页的 host 测试。
//
// 页面代码直接 #include 进来，因此能读它的 static 指针、能直接调 tick()，
// 也能从 test_label_log 里按顺序读到最后七次文本写入。
#include "demo_stubs/demo_runtime.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/ui_product.c"
#include "../main/page_status.c"

static const char *label_from_end(unsigned back) {
    unsigned index = (test_label_writes + TEST_LABEL_LOG_DEPTH - back) % TEST_LABEL_LOG_DEPTH;
    return test_label_log[index];
}

static void publish_snapshot(wifi_phase_t phase, int8_t rssi, const char *ssid, const char *ip,
                             uint8_t fail, uint8_t count) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.phase = (uint8_t)phase;
    s.rssi = rssi;
    s.fail = fail;
    s.profile_count = count;
    s.active = count > 0 ? 0 : -1;
    s.trying = count > 0 ? 0 : -1;
    if (ssid != NULL) strcpy(s.ssid, ssid);
    if (ip != NULL) strcpy(s.ip, ip);
    wifi_status_publish(wifi_status_shared(), &s);
}

// 必须最先跑：此时共享总线还没有任何 publish，seq 仍为 0。
static void test_build_shows_the_placeholder_before_any_publish(void) {
    test_battery_soc = 88;
    test_label_writes = 0;

    status_page_build();
    assert(s_scr != NULL);
    assert(s_timer != NULL);
    // build 会先创建控件（页眉 2 次 + 文本 9 次），最后一帧渲染固定 7 次。
    // 这里只保证"渲染发生了"，具体文案由下面按顺序断言。
    assert(test_label_writes >= 8);
    assert(strcmp(label_from_end(7), "Starting Wi-Fi...") == 0);
    assert(strcmp(label_from_end(2), "Hold OK: menu") == 0);
    assert(strcmp(label_from_end(1), "88%") == 0);
}

static void test_connected_snapshot_fills_every_row(void) {
    publish_snapshot(WIFI_PHASE_CONNECTED, -55, "home-net", "192.168.1.7", WIFI_FAIL_NONE, 1);

    unsigned before = test_label_writes;
    status_page_refresh();
    assert(test_label_writes == before + 8);
    assert(strcmp(label_from_end(7), "Connected: home-net") == 0);
    assert(strcmp(label_from_end(6), "IP 192.168.1.7") == 0);
    assert(strcmp(label_from_end(5), "192.168.1.7") == 0);
    assert(strcmp(label_from_end(4), "1 saved") == 0);
    assert(strcmp(label_from_end(3), "-55 dBm") == 0);
    assert(strcmp(label_from_end(2), "Hold OK: menu") == 0);
    assert(strcmp(label_from_end(1), "88%") == 0);
}

static void test_tick_skips_an_unchanged_snapshot(void) {
    unsigned before = test_label_writes;
    tick(NULL); // seq 未变，且还没到电量周期
    assert(test_label_writes == before);
}

static void test_failure_reason_is_named_on_the_detail_row(void) {
    publish_snapshot(WIFI_PHASE_ALL_FAILED, -128, "", "", WIFI_FAIL_AUTH, 2);
    status_page_refresh();
    assert(strcmp(label_from_end(7), "No connection") == 0);
    assert(strcmp(label_from_end(6), "Wrong password") == 0);

    publish_snapshot(WIFI_PHASE_ALL_FAILED, -128, "", "", WIFI_FAIL_NO_AP, 2);
    status_page_refresh();
    assert(strcmp(label_from_end(6), "Network not found") == 0);
}

static void test_long_ssid_is_clamped_to_two_lines(void) {
    char ssid[WIFI_PROFILE_SSID_MAX];
    memset(ssid, 'a', sizeof ssid - 1);
    ssid[sizeof ssid - 1] = '\0'; // 32 个字符

    publish_snapshot(WIFI_PHASE_CONNECTED, -60, ssid, "10.0.0.2", WIFI_FAIL_NONE, 1);
    status_page_refresh();

    const char *headline = label_from_end(7);
    // "Connected: " 11 字符 + 20 字符（含结尾三个点）= 31
    assert(strlen(headline) == 31);
    assert(strstr(headline, "...") != NULL);
}

static void test_unknown_rssi_and_ip_render_as_dash(void) {
    publish_snapshot(WIFI_PHASE_CONNECTING, -128, "shop", "", WIFI_FAIL_NONE, 3);
    status_page_refresh();
    assert(strcmp(label_from_end(7), "Connecting: shop") == 0);
    assert(strcmp(label_from_end(6), "Profile 1 of 3") == 0);
    assert(strcmp(label_from_end(5), "-") == 0); // IP 未知
    assert(strcmp(label_from_end(3), "-") == 0); // RSSI 未知
}

static void test_destroy_stops_everything(void) {
    status_page_destroy();
    assert(s_scr == NULL);
    assert(s_timer == NULL);
    assert(s_headline == NULL);

    unsigned before = test_label_writes;
    publish_snapshot(WIFI_PHASE_CONNECTED, -50, "x", "10.0.0.1", WIFI_FAIL_NONE, 1);
    status_page_refresh(); // 页面已销毁，必须静默返回
    tick(NULL);
    assert(test_label_writes == before);

    // 幂等：再销毁一次不能崩。
    status_page_destroy();
}

static void test_battery_text_degrades_to_dashes(void) {
    test_battery_soc = -1;
    status_page_build();
    assert(strcmp(label_from_end(1), "--%") == 0);
    status_page_destroy();

    test_battery_soc = 5;
    status_page_build();
    assert(strcmp(label_from_end(1), "5%") == 0);
    status_page_destroy();

    test_battery_soc = 100;
    status_page_build();
    assert(strcmp(label_from_end(1), "100%") == 0);
    status_page_destroy();
}

static void test_shared_bus_takes_the_critical_section_in_pairs(void) {
    assert(test_port_enters == test_port_exits);
    assert(test_port_enters >= 2); // build/refresh 至少读过两次快照
}

int main(void) {
    test_build_shows_the_placeholder_before_any_publish();
    test_connected_snapshot_fills_every_row();
    test_tick_skips_an_unchanged_snapshot();
    test_failure_reason_is_named_on_the_detail_row();
    test_long_ssid_is_clamped_to_two_lines();
    test_unknown_rssi_and_ip_render_as_dash();
    test_destroy_stops_everything();
    test_battery_text_degrades_to_dashes();
    test_shared_bus_takes_the_critical_section_in_pairs();
    puts("wifi status page tests: PASS");
    return 0;
}
