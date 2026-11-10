// tests/test_weather_view.c —— 天气页状态与文案的主机测试。
//
// 这些分支平时最难在屏幕上看全（没网、没地点、没校时、有旧数据），
// 但每一条都对应一句用户会读到的话，所以逐格断言。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "weather_view.h"

static void test_data_wins_while_offline(void) {
    // 离线时上一份快照仍然要显示：没网不等于把温度藏起来，
    // 页面会用 "Offline - saved" 说明它是旧数据。
    assert(weather_view_state(false, false, false, true, false) == WEATHER_VIEW_DATA);
    assert(weather_view_state(false, true, false, true, false) == WEATHER_VIEW_DATA);
    // 在线、有地点、有数据：正常显示。
    assert(weather_view_state(true, true, true, true, true) == WEATHER_VIEW_DATA);
}

static void test_missing_location_beats_stale_snapshot(void) {
    // 快照不记"属于哪个网络"，换网后旧数据仍然在。已连上但没地点是终态：
    // 这个网络没有天气，就不许拿别的网络的快照顶上去（2026-11-10 设备实证）。
    assert(weather_view_state(true, false, true, true, false) == WEATHER_VIEW_NO_LOCATION);
    assert(weather_view_state(true, false, false, true, true) == WEATHER_VIEW_NO_LOCATION);
}

static void test_state_priority_without_data(void) {
    assert(weather_view_state(false, false, false, false, false) == WEATHER_VIEW_NO_NETWORK);
    assert(weather_view_state(true, false, false, false, false) == WEATHER_VIEW_NO_LOCATION);
    assert(weather_view_state(true, true, false, false, false) == WEATHER_VIEW_WAIT_TIME);
    assert(weather_view_state(true, true, true, false, false) == WEATHER_VIEW_UPDATING);
    assert(weather_view_state(true, true, true, false, true) == WEATHER_VIEW_RETRYING);
}

static void test_messages_cover_every_state(void) {
    assert(weather_view_message(WEATHER_VIEW_DATA) == NULL);
    assert(strcmp(weather_view_message(WEATHER_VIEW_NO_NETWORK), "No network") == 0);
    assert(strcmp(weather_view_message(WEATHER_VIEW_NO_LOCATION), "Location not set") == 0);
    assert(strcmp(weather_view_message(WEATHER_VIEW_WAIT_TIME), "Waiting for time") == 0);
    assert(strcmp(weather_view_message(WEATHER_VIEW_UPDATING), "Updating...") == 0);
    assert(strcmp(weather_view_message(WEATHER_VIEW_RETRYING), "No data - retrying") == 0);
}

static void test_place_falls_back_from_snapshot_to_network_name(void) {
    assert(strcmp(weather_view_place(WEATHER_VIEW_DATA, "Beijing", "Shanghai", "home"), "Beijing") ==
           0);
    assert(strcmp(weather_view_place(WEATHER_VIEW_DATA, "", "Shanghai", "home"), "Shanghai") == 0);
    assert(strcmp(weather_view_place(WEATHER_VIEW_DATA, "", "", "home"), "home") == 0);
    assert(strcmp(weather_view_place(WEATHER_VIEW_DATA, NULL, NULL, NULL), "Weather") == 0);

    // 没有数据时，地点行就是状态文案。
    assert(strcmp(weather_view_place(WEATHER_VIEW_NO_NETWORK, "Beijing", "", "home"), "No network") ==
           0);

    // 没地点时也一样：哪怕快照里残留着上一个网络的地名，也不能贴出来。
    assert(strcmp(weather_view_place(WEATHER_VIEW_NO_LOCATION, "zhenjiang", "", "AH3C_204C"),
                  "Location not set") == 0);
}

static void test_utc_formatting_is_stable(void) {
    char buf[32];

    assert(weather_view_format_utc(1704067200, buf, sizeof buf)); // 2024-01-01T00:00:00Z
    assert(strcmp(buf, "2024-01-01 00:00Z") == 0);

    assert(weather_view_format_utc(1767225600, buf, sizeof buf)); // 2026-01-01T00:00:00Z
    assert(strcmp(buf, "2026-01-01 00:00Z") == 0);

    assert(weather_view_format_utc(1767225600 + 14 * 3600 + 5 * 60, buf, sizeof buf));
    assert(strcmp(buf, "2026-01-01 14:05Z") == 0);

    assert(!weather_view_format_utc(0, buf, sizeof buf));
    assert(buf[0] == '\0');
    assert(!weather_view_format_utc(-5, buf, sizeof buf));

    memset(buf, 'Z', sizeof buf);
    assert(!weather_view_format_utc(1767225600, buf, 8));
    assert(buf[0] == '\0');
}

static void test_updated_line_for_fresh_and_stale_data(void) {
    char buf[40];

    assert(weather_view_updated(WEATHER_VIEW_DATA, false, 0, 0, buf, sizeof buf));
    assert(strcmp(buf, "Updated just now") == 0);

    assert(weather_view_updated(WEATHER_VIEW_DATA, false, 5 * 60 * 1000, 0, buf, sizeof buf));
    assert(strcmp(buf, "Updated 5 min ago") == 0);

    assert(weather_view_updated(WEATHER_VIEW_DATA, false, 3 * 3600 * 1000, 0, buf, sizeof buf));
    assert(strcmp(buf, "Updated 3 h ago") == 0);

    // 相对时间未知：不编数字，也不显示这一行。
    assert(!weather_view_updated(WEATHER_VIEW_DATA, false, -1, 0, buf, sizeof buf));
    assert(buf[0] == '\0');

    // 旧快照用绝对时间；没有时间戳时退回一句静态说明。
    assert(weather_view_updated(WEATHER_VIEW_DATA, true, -1, 1767225600, buf, sizeof buf));
    assert(strcmp(buf, "2026-01-01 00:00Z") == 0);

    assert(weather_view_updated(WEATHER_VIEW_DATA, true, -1, 0, buf, sizeof buf));
    assert(strcmp(buf, "Offline - saved") == 0);

    // 没有数据时这一行不显示。
    assert(!weather_view_updated(WEATHER_VIEW_NO_NETWORK, false, 0, 0, buf, sizeof buf));
}

int main(void) {
    test_data_wins_while_offline();
    test_missing_location_beats_stale_snapshot();
    test_state_priority_without_data();
    test_messages_cover_every_state();
    test_place_falls_back_from_snapshot_to_network_name();
    test_utc_formatting_is_stable();
    test_updated_line_for_fresh_and_stale_data();
    puts("weather view tests: PASS");
    return 0;
}
