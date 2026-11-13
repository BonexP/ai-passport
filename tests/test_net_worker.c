// tests/test_net_worker.c —— net worker 的决策与记帐测试（白盒）。
//
// net_worker.c 的任务体本身不在这里跑：桩里的 vTaskDelay 是空操作，worker 的
// while(!s_stop) 会原地自旋，而"跑到一半被挂起"的模型要靠 setjmp，不适合这个
// 只有"每 250 ms 醒一次"的循环。这里测的是它真正的判断：
//   - 哪些字段变了才算内容变化（决定会不会把 NVS 写穿）
//   - 落盘节流的记帐（s_saved_valid / s_last_save_ms 什么时候推进）
//   - 从 NVS 载入的旧快照怎么发布、为什么立刻允许再写一次
//   - 地点比较、有界拷贝、以及起停的幂等与强删路径
//
// app_time 与网络两层的真实实现都依赖 ESP-IDF（app_time.c 还要 esp_netif_sntp.h，
// 仓库没有这个桩），所以本文件自带最小替身，只保证签名与契约一致。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "demo_stubs/demo_runtime.c"

#include "../main/net_worker.c"

/* ---- 被测文件依赖外部符号的最小替身 ---- */

static bool    s_time_valid;
static int64_t s_time_epoch = 1767225600;

bool app_time_is_valid(void) { return s_time_valid; }
int64_t app_time_now(void) { return s_time_valid ? s_time_epoch : 0; }

http_result_t net_http_get(const char *url, char *body, size_t body_cap, size_t *body_len,
                           uint32_t timeout_ms) {
    (void)url;
    (void)body;
    (void)body_cap;
    (void)timeout_ms;
    if (body_len != NULL) *body_len = 0;
    return HTTP_RESULT_TRANSPORT;
}

// 多源改动之后 net_worker.c 还引用了扩展形态与编译期 key。本文件不测用量路径
// （那是 test_net_worker_sources.c 的事），这里只给最小替身让链接通过。
http_result_t net_http_get_ex(const net_http_request_t *req) {
    (void)req;
    return HTTP_RESULT_TRANSPORT;
}

const char *app_config_amd_key(void) {
    return NULL; // 默认无 key：用量源的门禁始终关着，不影响天气用例
}

bool weather_url_build(const app_location_t *loc, char *buf, size_t cap) {
    (void)loc;
    (void)buf;
    (void)cap;
    return false;
}

bool weather_parse_current(const char *json, weather_state_t *out) {
    (void)json;
    (void)out;
    return false;
}

/* ---- 夹具 ---- */

static weather_state_t weather(int16_t temp_dc, int16_t code, const char *place) {
    weather_state_t w;
    memset(&w, 0, sizeof w);
    w.has_data = true;
    w.temp_dc = temp_dc;
    w.weather_code = code;
    strcpy(w.place, place);
    return w;
}

static app_location_t location(int32_t lat_udeg, int32_t lon_udeg, const char *place) {
    app_location_t loc;
    memset(&loc, 0, sizeof loc);
    loc.has_location = true;
    loc.lat_udeg = lat_udeg;
    loc.lon_udeg = lon_udeg;
    strcpy(loc.place, place);
    return loc;
}

static void reset_tracking(void) {
    s_saved_valid = false;
    s_last_save_ms = -1;
    memset(&s_last_saved, 0, sizeof s_last_saved);
    app_data_bus_init(app_data_shared(), NULL);
}

static net_snapshot_store_t open_store(void) {
    net_snapshot_store_t store;
    memset(&store, 0, sizeof store);
    assert(net_snapshot_store_open(&store) == ESP_OK);
    return store;
}

/* ---- 地点比较 ---- */

static void test_same_location(void) {
    app_location_t a = location(32123456, 119654321, "Zhenjiang");
    app_location_t b = location(32123456, 119654321, "Zhenjiang");
    assert(same_location(&a, &b));

    b.lat_udeg += 1; // 一个微度也算换了地点
    assert(!same_location(&a, &b));

    b = a;
    b.place[0] = 'X';
    assert(!same_location(&a, &b));

    app_location_t none;
    memset(&none, 0, sizeof none);
    app_location_t none2;
    memset(&none2, 0, sizeof none2);
    assert(same_location(&none, &none2));

    assert(!same_location(&a, &none)); // 一边有地点一边没有
    assert(!same_location(&none, &a));
}

/* ---- 内容变化判定 ---- */

static void test_snapshot_changed(void) {
    weather_state_t a = weather(123, 3, "Zhenjiang");
    weather_state_t b = a;
    assert(!snapshot_changed(&a, &b));

    b.temp_dc = 124;
    assert(snapshot_changed(&a, &b));

    b = a;
    b.weather_code = 4;
    assert(snapshot_changed(&a, &b));

    b = a;
    strcpy(b.place, "Nanjing");
    assert(snapshot_changed(&a, &b));

    // observed 与 fetched_* 每次都变，把它们算进"内容变化"等于每次成功都写 flash。
    b = a;
    strcpy(b.observed, "2026-11-01T15:00");
    b.fetched_epoch += 600;
    b.fetched_uptime_ms += 600000;
    assert(!snapshot_changed(&a, &b));
}

/* ---- 落盘节流 ---- */

static void test_persist_skips_a_closed_store(void) {
    test_nvs_clear();
    reset_tracking();

    net_snapshot_store_t store;
    memset(&store, 0, sizeof store); // 未打开
    weather_state_t w = weather(123, 3, "Zhenjiang");
    persist_if_due(&store, &w);

    assert(test_nvs_commits == 0);
    assert(!s_saved_valid);
}

static void test_persist_writes_the_first_snapshot(void) {
    test_nvs_clear();
    reset_tracking();
    net_snapshot_store_t store = open_store();

    test_time_us = 1000 * 1000; // 开机 1 秒
    weather_state_t w = weather(123, 3, "Zhenjiang");
    persist_if_due(&store, &w);

    assert(test_nvs_commits == 1);
    assert(s_saved_valid);
    assert(s_last_save_ms == 1000);
    assert(s_last_saved.temp_dc == 123);
}

static void test_persist_throttles_unchanged_content(void) {
    test_nvs_clear();
    reset_tracking();
    net_snapshot_store_t store = open_store();

    test_time_us = 1000 * 1000;
    weather_state_t w = weather(123, 3, "Zhenjiang");
    persist_if_due(&store, &w);
    assert(test_nvs_commits == 1);

    // 10 分钟后内容没变：不写。
    test_time_us = (10 * 60 * 1000LL) * 1000;
    persist_if_due(&store, &w);
    assert(test_nvs_commits == 1);

    // 内容变了：立刻写，不等窗口。
    weather_state_t warmer = weather(150, 3, "Zhenjiang");
    persist_if_due(&store, &warmer);
    assert(test_nvs_commits == 2);
    assert(s_last_saved.temp_dc == 150);

    // 又过了 30 分钟、内容仍然没变：写一次刷新时间戳。
    test_time_us = (40 * 60 * 1000LL) * 1000;
    persist_if_due(&store, &warmer);
    assert(test_nvs_commits == 3);
}

static void test_persist_keeps_the_bookkeeping_on_failure(void) {
    test_nvs_clear();
    reset_tracking();
    net_snapshot_store_t store = open_store();

    test_time_us = 1000 * 1000;
    weather_state_t w = weather(123, 3, "Zhenjiang");
    persist_if_due(&store, &w);
    assert(test_nvs_commits == 1);
    const int64_t saved_at = s_last_save_ms;

    // 落盘失败：不能把记账推进，否则这次内容就永远不重试了。
    test_nvs_write_fails = true;
    test_time_us = (5 * 60 * 1000LL) * 1000;
    weather_state_t colder = weather(-50, 3, "Zhenjiang");
    persist_if_due(&store, &colder);

    assert(s_last_saved.temp_dc == 123);
    assert(s_last_save_ms == saved_at);
}

/* ---- 载入旧快照 ---- */

static void test_load_saved_publishes_a_stale_snapshot(void) {
    test_nvs_clear();
    reset_tracking();
    net_snapshot_store_t store = open_store();

    weather_state_t w = weather(123, 65, "Zhenjiang");
    assert(net_snapshot_store_save(&store, &w) == ESP_OK);

    load_saved(&store);
    assert(s_saved_valid);
    // 本次开机还没写过：下一次成功取数必须允许立刻落盘。
    assert(s_last_save_ms == -1);

    app_data_t out;
    app_data_read(app_data_shared(), &out);
    assert(out.weather.has_data);
    assert(out.weather.stale); // 页面靠它显示 Offline - saved
    assert(out.weather.temp_dc == 123);
    assert(strcmp(out.weather.place, "Zhenjiang") == 0);

    // 载入之后第一次取数：即使内容完全一样也要写一次。
    test_time_us = 2000 * 1000;
    persist_if_due(&store, &w);
    assert(test_nvs_commits == 2);
    assert(s_last_save_ms == 2000);
}

static void test_load_saved_without_a_snapshot_changes_nothing(void) {
    test_nvs_clear();
    reset_tracking();
    net_snapshot_store_t store = open_store();

    load_saved(&store);
    assert(!s_saved_valid);
    assert(s_last_save_ms == -1);

    app_data_t out;
    app_data_read(app_data_shared(), &out);
    assert(!out.weather.has_data);
}

/* ---- 有界拷贝 ---- */

static void test_copy_text_bounds(void) {
    char buf[6];

    memset(buf, 'x', sizeof buf);
    copy_text(buf, sizeof buf, "abcdefgh");
    assert(strcmp(buf, "abcde") == 0); // cap-1 截断，永远留 NUL

    copy_text(buf, sizeof buf, NULL);
    assert(buf[0] == '\0');

    memset(buf, 'x', sizeof buf);
    copy_text(buf, 0, "abc"); // cap 为 0 时一个字节都不许碰
    assert(buf[0] == 'x');

    assert(bounded_strlen(NULL, 10) == 0);
    assert(bounded_strlen("abc", 10) == 3);
    assert(bounded_strlen("abc", 2) == 2);
}

/* ---- 起停 ---- */

static void test_start_is_idempotent_and_stop_reaps_the_task(void) {
    test_task_reset();
    const unsigned deletes_before = test_task_deletes;

    assert(net_worker_start() == ESP_OK);
    assert(net_worker_running());
    assert(net_worker_start() == ESP_OK); // 幂等，不建第二个任务
    assert(net_worker_running());

    net_worker_stop();
    assert(!net_worker_running());
    // 桩里的 worker 永不消费 STOP，所以走的必然是"超时强删"那条路。
    // 这条断言同时把这个已知限制固化下来：改成优雅退出时它会失败，提醒同步改文档。
    assert(test_task_deletes == deletes_before + 1);

    net_worker_stop(); // 再停一次什么都不该发生
    assert(!net_worker_running());
    assert(test_task_deletes == deletes_before + 1);
}

static void test_start_reports_out_of_memory(void) {
    test_task_reset();
    test_create_fails = true;

    assert(net_worker_start() == ESP_ERR_NO_MEM);
    assert(!net_worker_running());
    test_create_fails = false;
}

int main(void) {
    test_same_location();
    test_snapshot_changed();
    test_persist_skips_a_closed_store();
    test_persist_writes_the_first_snapshot();
    test_persist_throttles_unchanged_content();
    test_persist_keeps_the_bookkeeping_on_failure();
    test_load_saved_publishes_a_stale_snapshot();
    test_load_saved_without_a_snapshot_changes_nothing();
    test_copy_text_bounds();
    test_start_is_idempotent_and_stop_reaps_the_task();
    test_start_reports_out_of_memory();
    puts("net worker tests: PASS");
    return 0;
}
