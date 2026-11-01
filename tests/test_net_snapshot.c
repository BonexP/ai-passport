// tests/test_net_snapshot.c —— 离线快照编解码的主机测试。
//
// 重点：往返保真、跨重启语义（stale=true、uptime=-1）、以及坏 blob 一律拒绝。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "net_snapshot.h"

static weather_state_t sample(void) {
    weather_state_t w;
    memset(&w, 0, sizeof w);
    w.has_data = true;
    w.temp_dc = -123;
    w.weather_code = 65;
    w.fetched_epoch = 1767225600;
    strcpy(w.place, "Beijing");
    strcpy(w.observed, "2026-11-01T14:00");
    return w;
}

static void test_round_trip_preserves_display_fields(void) {
    weather_state_t in = sample();
    uint8_t blob[NET_SNAPSHOT_MAX];
    size_t len = net_snapshot_encode(&in, blob, sizeof blob);
    assert(len > 0);
    assert(len <= NET_SNAPSHOT_MAX);

    weather_state_t out;
    assert(net_snapshot_decode(blob, len, &out));
    assert(out.has_data);
    assert(out.stale);                    // 从磁盘读回来必然是旧数据
    assert(out.fetched_uptime_ms == -1);  // 跨重启的单调时间无意义
    assert(out.seq == 0);                 // 序号由总线在发布时给
    assert(out.temp_dc == -123);
    assert(out.weather_code == 65);
    assert(out.fetched_epoch == 1767225600);
    assert(strcmp(out.place, "Beijing") == 0);
    assert(strcmp(out.observed, "2026-11-01T14:00") == 0);
}

static void test_empty_strings_are_allowed(void) {
    weather_state_t in = sample();
    in.place[0] = '\0';
    in.observed[0] = '\0';

    uint8_t blob[NET_SNAPSHOT_MAX];
    size_t len = net_snapshot_encode(&in, blob, sizeof blob);
    assert(len > 0);

    weather_state_t out;
    assert(net_snapshot_decode(blob, len, &out));
    assert(out.place[0] == '\0');
    assert(out.observed[0] == '\0');
}

static void test_encode_rejects_missing_data_and_small_buffers(void) {
    weather_state_t in = sample();
    uint8_t blob[NET_SNAPSHOT_MAX];

    weather_state_t empty = in;
    empty.has_data = false;
    assert(net_snapshot_encode(&empty, blob, sizeof blob) == 0);
    assert(net_snapshot_encode(NULL, blob, sizeof blob) == 0);
    assert(net_snapshot_encode(&in, NULL, sizeof blob) == 0);
    assert(net_snapshot_encode(&in, blob, 4) == 0); // 头都放不下

    // 恰好够：先算出真实长度，再要求少一个字节。
    size_t len = net_snapshot_encode(&in, blob, sizeof blob);
    assert(len > 0);
    assert(net_snapshot_encode(&in, blob, len) == len);
    assert(net_snapshot_encode(&in, blob, len - 1) == 0);
}

static void test_decode_rejects_bad_blobs(void) {
    weather_state_t in = sample();
    uint8_t blob[NET_SNAPSHOT_MAX];
    size_t len = net_snapshot_encode(&in, blob, sizeof blob);
    assert(len > 0);

    weather_state_t out;

    // 版本不符：不猜旧布局。
    uint8_t bad_version[NET_SNAPSHOT_MAX];
    memcpy(bad_version, blob, len);
    bad_version[0] = 99;
    assert(!net_snapshot_decode(bad_version, len, &out));

    // 未知 flag：新语义必须升版本号。
    uint8_t bad_flag[NET_SNAPSHOT_MAX];
    memcpy(bad_flag, blob, len);
    bad_flag[1] |= 0x02u;
    assert(!net_snapshot_decode(bad_flag, len, &out));

    // 截断与多挂一个字节。
    assert(!net_snapshot_decode(blob, len - 1, &out));
    uint8_t padded[NET_SNAPSHOT_MAX];
    memcpy(padded, blob, len);
    padded[len] = 0x7F;
    assert(!net_snapshot_decode(padded, len + 1, &out));

    // 长度字段超限。
    uint8_t bad_len[NET_SNAPSHOT_MAX];
    memcpy(bad_len, blob, len);
    bad_len[10] = 200; // place_len >= APP_LOCATION_PLACE_MAX
    assert(!net_snapshot_decode(bad_len, len, &out));

    assert(!net_snapshot_decode(NULL, len, &out));
    assert(!net_snapshot_decode(blob, len, NULL));
    assert(!net_snapshot_decode(blob, 0, &out));
}

static void test_save_due_policy(void) {
    // 本次开机还没有快照：写。
    assert(net_snapshot_save_due(false, false, -1, 0));

    // 内容变化：立刻写，不等窗口。
    assert(net_snapshot_save_due(true, true, 0, 1000));

    // 内容没变，但本次开机还没写过（刚从 NVS 载入）：写一次。
    assert(net_snapshot_save_due(true, false, -1, 1000));

    // 内容没变且刚写过：等满 30 分钟。
    assert(!net_snapshot_save_due(true, false, 0, NET_SNAPSHOT_SAVE_INTERVAL_MS - 1));
    assert(net_snapshot_save_due(true, false, 0, NET_SNAPSHOT_SAVE_INTERVAL_MS));

    // 时间倒退（异常）时不要误判为到期。
    assert(!net_snapshot_save_due(true, false, 5000, 1000));
}

int main(void) {
    test_round_trip_preserves_display_fields();
    test_empty_strings_are_allowed();
    test_encode_rejects_missing_data_and_small_buffers();
    test_decode_rejects_bad_blobs();
    test_save_due_policy();
    puts("net snapshot tests: PASS");
    return 0;
}
