// tests/test_app_data.c —— 快照总线的 host 测试。
//
// 与 wifi_status 的测试同构：临界区用计数器 vtable 注入，直接断言"每次访问恰好
// 锁一次、解锁一次"，不需要真的并发。写者不变量（只有 worker 发布）在注释里，
// 主机上无法验证，但可以用"发布覆盖整份数据"来固化。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_data.h"

typedef struct {
    int locks;
    int unlocks;
} counter_t;

static void counter_lock(void *ctx) {
    ((counter_t *)ctx)->locks++;
}

static void counter_unlock(void *ctx) {
    ((counter_t *)ctx)->unlocks++;
}

typedef struct {
    counter_t         counter;
    app_data_lock_t   lock; // bus 只存指针，必须与 bus 同寿命
    app_data_bus_t    bus;
} fixture_t;

static void fixture_init(fixture_t *f) {
    f->counter.locks = 0;
    f->counter.unlocks = 0;
    f->lock.ctx = &f->counter;
    f->lock.lock = counter_lock;
    f->lock.unlock = counter_unlock;
    app_data_bus_init(&f->bus, &f->lock);
}

static weather_state_t sample_weather(void) {
    weather_state_t w;
    memset(&w, 0, sizeof w);
    w.has_data = true;
    w.temp_dc = 123;
    w.weather_code = 3;
    w.fetched_epoch = 1767225600;
    w.fetched_uptime_ms = 5000;
    strcpy(w.place, "Beijing");
    strcpy(w.observed, "2026-11-01T14:00");
    return w;
}

static void test_init_clears_and_marks_uptime_unknown(void) {
    fixture_t f;
    fixture_init(&f);

    assert(f.bus.data.weather.seq == 0);
    assert(!f.bus.data.weather.has_data);
    assert(!f.bus.data.weather.stale);
    assert(f.bus.data.weather.fetched_uptime_ms == -1);
    assert(f.bus.data.weather.place[0] == '\0');
    assert(f.counter.locks == 0); // init 不需要临界区
}

static void test_publish_copies_every_field(void) {
    fixture_t f;
    fixture_init(&f);

    weather_state_t next = sample_weather();
    app_data_publish_weather(&f.bus, &next);

    app_data_t got;
    memset(&got, 0xFF, sizeof got);
    app_data_read(&f.bus, &got);

    assert(got.weather.has_data);
    assert(got.weather.temp_dc == 123);
    assert(got.weather.weather_code == 3);
    assert(got.weather.fetched_epoch == 1767225600);
    assert(got.weather.fetched_uptime_ms == 5000);
    assert(strcmp(got.weather.place, "Beijing") == 0);
    assert(strcmp(got.weather.observed, "2026-11-01T14:00") == 0);
}

static void test_bus_owns_the_sequence_number(void) {
    fixture_t f;
    fixture_init(&f);

    weather_state_t next = sample_weather();
    next.seq = 999; // 调用方写的序号必须被忽略

    app_data_publish_weather(&f.bus, &next);
    assert(f.bus.data.weather.seq == 1);
    app_data_publish_weather(&f.bus, &next);
    assert(f.bus.data.weather.seq == 2);
}

static void test_each_access_takes_the_lock_exactly_once(void) {
    fixture_t f;
    fixture_init(&f);

    weather_state_t next = sample_weather();
    app_data_publish_weather(&f.bus, &next);
    assert(f.counter.locks == 1);
    assert(f.counter.unlocks == 1);

    app_data_t got;
    app_data_read(&f.bus, &got);
    assert(f.counter.locks == 2);
    assert(f.counter.unlocks == 2);
}

static void test_publish_replaces_wholesale(void) {
    fixture_t f;
    fixture_init(&f);

    weather_state_t first = sample_weather();
    app_data_publish_weather(&f.bus, &first);

    weather_state_t second;
    memset(&second, 0, sizeof second);
    second.fetched_uptime_ms = -1;
    app_data_publish_weather(&f.bus, &second);

    // 旧值不能有残留：UI 不该看到"上一处的地点配这一次的温度"。
    assert(!f.bus.data.weather.has_data);
    assert(f.bus.data.weather.temp_dc == 0);
    assert(f.bus.data.weather.place[0] == '\0');
    assert(f.bus.data.weather.seq == 2);
}

static void test_stale_flag_survives_publish(void) {
    fixture_t f;
    fixture_init(&f);

    weather_state_t from_nvs = sample_weather();
    from_nvs.stale = true;
    app_data_publish_weather(&f.bus, &from_nvs);
    assert(f.bus.data.weather.stale);
}

static void test_null_arguments_are_safe(void) {
    fixture_t f;
    fixture_init(&f);

    app_data_publish_weather(NULL, NULL);
    app_data_read(NULL, NULL);

    weather_state_t next = sample_weather();
    app_data_publish_weather(NULL, &next);
    app_data_publish_weather(&f.bus, NULL);
    app_data_read(&f.bus, NULL);
    assert(f.counter.locks == 0); // 非法调用不该碰锁
}

int main(void) {
    test_init_clears_and_marks_uptime_unknown();
    test_publish_copies_every_field();
    test_bus_owns_the_sequence_number();
    test_each_access_takes_the_lock_exactly_once();
    test_publish_replaces_wholesale();
    test_stale_flag_survives_publish();
    test_null_arguments_are_safe();
    puts("app data tests: PASS");
    return 0;
}
