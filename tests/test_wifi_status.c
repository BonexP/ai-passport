// tests/test_wifi_status.c —— 状态快照的 host 测试。
//
// 临界区用计数器 vtable 注入，因此能直接断言"每次访问恰好锁一次、解锁一次"，
// 而不需要真的并发。锁 vtable 与计数器都放在 fixture 里，生命周期覆盖整个用例。
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_status.h"

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
    counter_t           counter;
    wifi_status_lock_t  lock; // bus 只存指针，所以它必须与 bus 同寿命
    wifi_status_bus_t   bus;
} fixture_t;

static void fixture_init(fixture_t *f) {
    f->counter.locks = 0;
    f->counter.unlocks = 0;
    f->lock.ctx = &f->counter;
    f->lock.lock = counter_lock;
    f->lock.unlock = counter_unlock;
    wifi_status_bus_init(&f->bus, &f->lock);
}

static bool all_printable(const char *text) {
    for (const char *p = text; *p != '\0'; p++) {
        if (!isprint((unsigned char)*p)) return false;
    }
    return true;
}

static void test_bus_init_clears_the_snapshot(void) {
    fixture_t f;
    fixture_init(&f);

    assert(f.bus.snapshot.phase == WIFI_PHASE_IDLE);
    assert(f.bus.snapshot.seq == 0);
    assert(f.bus.snapshot.active == -1);
    assert(f.bus.snapshot.trying == -1);
    assert(f.bus.snapshot.rssi == -128);
    assert(f.bus.snapshot.ssid[0] == '\0');
    assert(f.bus.snapshot.ip[0] == '\0');
    assert(f.counter.locks == 0);
}

static void test_publish_copies_every_field(void) {
    fixture_t f;
    fixture_init(&f);

    wifi_status_snapshot_t next;
    memset(&next, 0, sizeof next);
    next.phase = WIFI_PHASE_CONNECTED;
    next.profile_count = 3;
    next.ap_clients = 1;
    next.active = 2;
    next.trying = 1;
    next.rssi = -55;
    strcpy(next.ssid, "home-net");
    strcpy(next.ip, "192.168.1.7");

    wifi_status_publish(&f.bus, &next);

    wifi_status_snapshot_t got;
    memset(&got, 0xFF, sizeof got);
    wifi_status_read(&f.bus, &got);

    assert(got.phase == WIFI_PHASE_CONNECTED);
    assert(got.profile_count == 3);
    assert(got.ap_clients == 1);
    assert(got.active == 2);
    assert(got.trying == 1);
    assert(got.rssi == -55);
    assert(strcmp(got.ssid, "home-net") == 0);
    assert(strcmp(got.ip, "192.168.1.7") == 0);
}

static void test_publish_owns_the_sequence_number(void) {
    fixture_t f;
    fixture_init(&f);

    wifi_status_snapshot_t next;
    memset(&next, 0, sizeof next);
    next.seq = 999; // 调用方写的序号必须被忽略

    wifi_status_publish(&f.bus, &next);
    assert(f.bus.snapshot.seq == 1);
    wifi_status_publish(&f.bus, &next);
    assert(f.bus.snapshot.seq == 2);
    wifi_status_publish(&f.bus, &next);
    assert(f.bus.snapshot.seq == 3);
}

static void test_each_access_takes_the_lock_exactly_once(void) {
    fixture_t f;
    fixture_init(&f);

    wifi_status_snapshot_t next;
    memset(&next, 0, sizeof next);
    wifi_status_publish(&f.bus, &next);
    assert(f.counter.locks == 1);
    assert(f.counter.unlocks == 1);

    wifi_status_snapshot_t got;
    wifi_status_read(&f.bus, &got);
    assert(f.counter.locks == 2);
    assert(f.counter.unlocks == 2);
}

static void test_publish_replaces_wholesale_without_leftovers(void) {
    fixture_t f;
    fixture_init(&f);

    wifi_status_snapshot_t first;
    memset(&first, 0, sizeof first);
    first.phase = WIFI_PHASE_CONNECTED;
    strcpy(first.ssid, "long-network-name");
    strcpy(first.ip, "192.168.1.7");
    wifi_status_publish(&f.bus, &first);

    // 换成更短的值：若实现是"逐字段覆盖"，旧字符串的尾巴会残留。
    wifi_status_snapshot_t second;
    memset(&second, 0, sizeof second);
    second.phase = WIFI_PHASE_IDLE;
    strcpy(second.ssid, "x");
    wifi_status_publish(&f.bus, &second);

    wifi_status_snapshot_t got;
    wifi_status_read(&f.bus, &got);
    assert(strcmp(got.ssid, "x") == 0);
    assert(got.ip[0] == '\0');
    assert(got.phase == WIFI_PHASE_IDLE);
}

static void test_reason_mapping_covers_the_classes_users_can_act_on(void) {
    assert(wifi_status_reason_from_code(0) == WIFI_FAIL_NONE);
    assert(wifi_status_reason_from_code(2) == WIFI_FAIL_AUTH);
    assert(wifi_status_reason_from_code(3) == WIFI_FAIL_AUTH);
    assert(wifi_status_reason_from_code(15) == WIFI_FAIL_AUTH);
    assert(wifi_status_reason_from_code(204) == WIFI_FAIL_AUTH);
    assert(wifi_status_reason_from_code(200) == WIFI_FAIL_NO_AP);
    assert(wifi_status_reason_from_code(201) == WIFI_FAIL_NO_AP);
    assert(wifi_status_reason_from_code(205) == WIFI_FAIL_NO_AP);
    // 不认识的码不编造解释。
    assert(wifi_status_reason_from_code(77) == WIFI_FAIL_OTHER);
}

static void test_auto_provision_only_when_library_is_empty_and_untried(void) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);

    s.phase = WIFI_PHASE_NO_PROFILE;
    assert(wifi_status_should_auto_provision(&s, false) == true);
    assert(wifi_status_should_auto_provision(&s, true) == false);

    s.phase = WIFI_PHASE_ALL_FAILED;
    assert(wifi_status_should_auto_provision(&s, false) == false);

    s.phase = WIFI_PHASE_CONNECTED;
    assert(wifi_status_should_auto_provision(&s, false) == false);

    s.phase = WIFI_PHASE_ERROR;
    assert(wifi_status_should_auto_provision(&s, false) == false);

    assert(wifi_status_should_auto_provision(NULL, false) == false);
}

static void test_headline_names_the_network_when_it_knows_it(void) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);
    char buf[64];

    s.phase = WIFI_PHASE_CONNECTED;
    strcpy(s.ssid, "home-net");
    assert(wifi_status_format_headline(&s, buf, sizeof buf) > 0);
    assert(strcmp(buf, "Connected: home-net") == 0);

    s.phase = WIFI_PHASE_CONNECTING;
    assert(wifi_status_format_headline(&s, buf, sizeof buf) > 0);
    assert(strcmp(buf, "Connecting: home-net") == 0);
}

static void test_headline_covers_every_phase_with_printable_ascii(void) {
    wifi_status_snapshot_t s;
    char buf[64];

    for (int phase = 0; phase <= (int)WIFI_PHASE_ERROR; phase++) {
        memset(&s, 0, sizeof s);
        s.phase = (uint8_t)phase;
        assert(wifi_status_format_headline(&s, buf, sizeof buf) > 0);
        assert(all_printable(buf));
    }
}

static void test_detail_explains_the_failure(void) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);
    char buf[64];

    s.phase = WIFI_PHASE_ALL_FAILED;
    s.fail = WIFI_FAIL_AUTH;
    assert(wifi_status_format_detail(&s, buf, sizeof buf) > 0);
    assert(strstr(buf, "password") != NULL);

    s.fail = WIFI_FAIL_NO_AP;
    assert(wifi_status_format_detail(&s, buf, sizeof buf) > 0);
    assert(strstr(buf, "not found") != NULL);

    s.phase = WIFI_PHASE_NO_PROFILE;
    assert(wifi_status_format_detail(&s, buf, sizeof buf) > 0);
    assert(strstr(buf, "UP") != NULL);
}

static void test_format_refuses_to_write_half_a_field(void) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.phase = WIFI_PHASE_CONNECTED;
    strcpy(s.ssid, "a-very-long-network-name-32chars");

    char buf[8];
    memset(buf, 0x7E, sizeof buf);
    // "Connected: ..." 放不进 8 字节的缓冲：必须整体放弃，不留半截。
    assert(wifi_status_format_headline(&s, buf, sizeof buf) == 0);
    assert(buf[0] == (char)0x7E);

    assert(wifi_status_format_headline(&s, NULL, 64) == 0);
    assert(wifi_status_format_headline(&s, buf, 0) == 0);
}

int main(void) {
    test_bus_init_clears_the_snapshot();
    test_publish_copies_every_field();
    test_publish_owns_the_sequence_number();
    test_each_access_takes_the_lock_exactly_once();
    test_publish_replaces_wholesale_without_leftovers();
    test_reason_mapping_covers_the_classes_users_can_act_on();
    test_auto_provision_only_when_library_is_empty_and_untried();
    test_headline_names_the_network_when_it_knows_it();
    test_headline_covers_every_phase_with_printable_ascii();
    test_detail_explains_the_failure();
    test_format_refuses_to_write_half_a_field();
    puts("wifi status tests: PASS");
    return 0;
}
