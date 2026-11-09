// tests/test_net_snapshot_store.c —— 离线快照 NVS 存取的契约测试。
//
// 只测 net_snapshot_store 这一层的边界与错误透传：谁在什么时候该失败、失败后
// 状态是什么、有没有把 NVS 的错误悄悄吞掉。字节编解码本身的正确性由
// test_net_snapshot.c 负责，这里不重复。
//
// 桩：只 include demo_runtime.c（NVS / 日志），被测源文件用命令行编进来。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "demo_stubs/demo_runtime.c"

#include "net_snapshot.h"
#include "net_snapshot_store.h"

static weather_state_t sample(void) {
    weather_state_t w;
    memset(&w, 0, sizeof w);
    w.has_data = true;
    w.stale = false;
    w.temp_dc = 123;
    w.weather_code = 65;
    w.fetched_epoch = 1767225600;
    w.fetched_uptime_ms = 4242;
    strcpy(w.place, "Zhenjiang");
    strcpy(w.observed, "2026-11-01T14:00");
    return w;
}

static void test_open_sets_the_flag(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);

    assert(net_snapshot_store_open(&store) == ESP_OK);
    assert(store.open);
    assert(test_nvs_opens == 1);
}

static void test_open_failure_keeps_it_closed(void) {
    test_nvs_clear();
    test_nvs_open_result = ESP_ERR_NO_MEM; // 空间不足是设备上最常见的一种失败

    net_snapshot_store_t store;
    memset(&store, 0xff, sizeof store); // 先涂脏，确认函数自己把它清干净
    assert(net_snapshot_store_open(&store) == ESP_ERR_NO_MEM);
    assert(!store.open);
}

static void test_close_is_idempotent(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);

    assert(net_snapshot_store_open(&store) == ESP_OK);
    net_snapshot_store_close(&store);
    assert(!store.open);
    assert(test_nvs_closes == 1);

    // 第二次什么都不该发生：重复关闭不能再去碰已经还给驱动的句柄。
    net_snapshot_store_close(&store);
    assert(test_nvs_closes == 1);
}

static void test_save_and_load_refuse_a_closed_store(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store); // 从未 open
    weather_state_t w = sample();

    assert(net_snapshot_store_save(&store, &w) == ESP_ERR_INVALID_STATE);
    assert(test_nvs_commits == 0);

    weather_state_t out;
    assert(net_snapshot_store_load(&store, &out) == ESP_ERR_INVALID_STATE);
}

static void test_save_rejects_null_arguments(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);

    assert(net_snapshot_store_save(&store, NULL) == ESP_ERR_INVALID_STATE);

    weather_state_t w = sample();
    assert(net_snapshot_store_load(&store, NULL) == ESP_ERR_INVALID_STATE);
    (void)w;
}

static void test_save_commits(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);

    weather_state_t w = sample();
    assert(net_snapshot_store_save(&store, &w) == ESP_OK);
    // 写进去不算数，commit 过才算数。
    assert(test_nvs_commits == 1);
}

static void test_save_propagates_a_write_failure(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);

    test_nvs_write_fails = true;
    weather_state_t w = sample();
    assert(net_snapshot_store_save(&store, &w) != ESP_OK);
    assert(test_nvs_commits == 0);
}

static void test_load_without_a_saved_snapshot(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);

    weather_state_t out;
    // NOT_FOUND 必须原样透传：调用方靠它区分"没存过"和"存坏了"。
    assert(net_snapshot_store_load(&store, &out) == ESP_ERR_NVS_NOT_FOUND);
}

static void test_load_rejects_a_corrupt_blob(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);

    const char junk[] = "not a snapshot";
    test_nvs_put("weather", junk, sizeof junk);

    weather_state_t out;
    // 版本不符/长度不自洽一律当作"没有快照"，绝不猜。
    assert(net_snapshot_store_load(&store, &out) == ESP_ERR_INVALID_STATE);
}

static void test_roundtrip_keeps_the_fields_and_marks_it_stale(void) {
    test_nvs_clear();
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);

    weather_state_t w = sample();
    assert(net_snapshot_store_save(&store, &w) == ESP_OK);

    weather_state_t out;
    memset(&out, 0, sizeof out);
    assert(net_snapshot_store_load(&store, &out) == ESP_OK);

    assert(out.has_data);
    assert(out.temp_dc == w.temp_dc);
    assert(out.weather_code == w.weather_code);
    assert(out.fetched_epoch == w.fetched_epoch);
    assert(strcmp(out.place, w.place) == 0);
    assert(strcmp(out.observed, w.observed) == 0);

    // 从磁盘读回来的一律是旧数据：页面靠这两个字段显示 Offline - saved。
    assert(out.stale);
    assert(out.fetched_uptime_ms == -1);
    assert(out.seq == 0);
}

int main(void) {
    test_open_sets_the_flag();
    test_open_failure_keeps_it_closed();
    test_close_is_idempotent();
    test_save_and_load_refuse_a_closed_store();
    test_save_rejects_null_arguments();
    test_save_commits();
    test_save_propagates_a_write_failure();
    test_load_without_a_saved_snapshot();
    test_load_rejects_a_corrupt_blob();
    test_roundtrip_keeps_the_fields_and_marks_it_stale();
    puts("net snapshot store tests: PASS");
    return 0;
}
