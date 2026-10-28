// tests/test_wifi_profile.c —— profile store 的 host 测试。
//
// 不依赖 ESP-IDF：用一个内存里的桩 kv 后端跑完整的"写—读—删"往返，并故意注入
// 坏记录与写失败，验证 store 不会因为一条脏数据而整体失效。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_profile.h"

#define STUB_MAX_ENTRIES 16
#define STUB_VALUE_MAX 160

typedef struct {
    char    key[8];
    uint8_t value[STUB_VALUE_MAX];
    size_t  len;
    bool    used;
} stub_entry_t;

typedef struct {
    stub_entry_t entries[STUB_MAX_ENTRIES];
    bool         fail_set;
} stub_store_t;

static stub_entry_t *find_entry(stub_store_t *st, const char *key) {
    for (size_t i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (st->entries[i].used && strcmp(st->entries[i].key, key) == 0) {
            return &st->entries[i];
        }
    }
    return NULL;
}

static stub_entry_t *alloc_entry(stub_store_t *st, const char *key) {
    stub_entry_t *existing = find_entry(st, key);
    if (existing != NULL) return existing;
    for (size_t i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (!st->entries[i].used) {
            size_t klen = strlen(key);
            if (klen >= sizeof st->entries[i].key) return NULL;
            memcpy(st->entries[i].key, key, klen + 1);
            st->entries[i].used = true;
            st->entries[i].len = 0;
            return &st->entries[i];
        }
    }
    return NULL;
}

static wifi_kv_result_t stub_get(void *ctx, const char *key, void *buf, size_t *len) {
    stub_store_t *st = ctx;
    stub_entry_t *e = find_entry(st, key);
    if (e == NULL) return WIFI_KV_ERR_NOT_FOUND;
    if (*len < e->len) {
        *len = e->len;
        return WIFI_KV_ERR_NO_MEM;
    }
    memcpy(buf, e->value, e->len);
    *len = e->len;
    return WIFI_KV_OK;
}

static wifi_kv_result_t stub_set(void *ctx, const char *key, const void *buf, size_t len) {
    stub_store_t *st = ctx;
    if (st->fail_set) return WIFI_KV_ERR_FAIL;
    if (len > STUB_VALUE_MAX) return WIFI_KV_ERR_FAIL;
    stub_entry_t *e = alloc_entry(st, key);
    if (e == NULL) return WIFI_KV_ERR_FAIL;
    memcpy(e->value, buf, len);
    e->len = len;
    return WIFI_KV_OK;
}

static wifi_kv_result_t stub_erase(void *ctx, const char *key) {
    stub_store_t *st = ctx;
    stub_entry_t *e = find_entry(st, key);
    if (e != NULL) {
        memset(e, 0, sizeof *e);
    }
    return WIFI_KV_OK;
}

static void stub_init(stub_store_t *st) {
    memset(st, 0, sizeof *st);
}

static void stub_put_raw(stub_store_t *st, const char *key, const void *data, size_t len) {
    stub_entry_t *e = alloc_entry(st, key);
    assert(e != NULL);
    assert(len <= STUB_VALUE_MAX);
    memcpy(e->value, data, len);
    e->len = len;
}

static size_t stub_live_keys(stub_store_t *st) {
    size_t live = 0;
    for (size_t i = 0; i < STUB_MAX_ENTRIES; i++) {
        if (st->entries[i].used) live++;
    }
    return live;
}

static wifi_kv_t make_kv(stub_store_t *st) {
    wifi_kv_t kv = {
        .ctx = st,
        .get = stub_get,
        .set = stub_set,
        .erase = stub_erase,
    };
    return kv;
}

static wifi_profile_t make_profile(const char *name, const char *ssid, const char *pass) {
    wifi_profile_t p;
    bool ok = wifi_profile_set_fields(&p, name, ssid, pass);
    assert(ok);
    p.last_ok_seq = 1700000000u;
    p.flags = WIFI_PROFILE_FLAG_ENABLED;
    return p;
}

static void test_empty_load_reads_as_empty_library(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;

    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&store) == 0);
    assert(wifi_profile_store_active(&store) == -1);
    assert(wifi_profile_store_at(&store, 0) == NULL);
}

static void test_round_trip_preserves_every_field(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);

    wifi_profile_store_t writer;
    assert(wifi_profile_store_load(&writer, &kv) == WIFI_PROFILE_OK);
    wifi_profile_t p = make_profile("home", "MyHome-5G", "s3cret-passphrase");
    assert(wifi_profile_store_add(&writer, &p) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_save(&writer) == WIFI_PROFILE_OK);

    wifi_profile_store_t reader;
    assert(wifi_profile_store_load(&reader, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&reader) == 1);
    assert(wifi_profile_store_active(&reader) == 0);

    const wifi_profile_t *got = wifi_profile_store_at(&reader, 0);
    assert(got != NULL);
    assert(strcmp(got->name, p.name) == 0);
    assert(strcmp(got->ssid, p.ssid) == 0);
    assert(strcmp(got->password, p.password) == 0);
    assert(got->last_ok_seq == p.last_ok_seq);
    assert(got->flags == p.flags);
}

static void test_ninth_profile_is_rejected(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);

    for (int i = 0; i < WIFI_PROFILE_MAX; i++) {
        char ssid[16];
        snprintf(ssid, sizeof ssid, "net-%u", (unsigned)i);
        wifi_profile_t p = make_profile("slot", ssid, "pw");
        assert(wifi_profile_store_add(&store, &p) == WIFI_PROFILE_OK);
    }
    assert(wifi_profile_store_count(&store) == WIFI_PROFILE_MAX);

    wifi_profile_t extra = make_profile("slot", "one-too-many", "pw");
    assert(wifi_profile_store_add(&store, &extra) == WIFI_PROFILE_ERR_FULL);
    assert(wifi_profile_store_count(&store) == WIFI_PROFILE_MAX);
}

static void test_update_only_touches_target_slot(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);

    wifi_profile_t a = make_profile("home", "home-net", "pa");
    wifi_profile_t b = make_profile("work", "work-net", "pb");
    wifi_profile_t c = make_profile("spare", "spare-net", "pc");
    assert(wifi_profile_store_add(&store, &a) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_add(&store, &b) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_add(&store, &c) == WIFI_PROFILE_OK);

    wifi_profile_t replacement = make_profile("work-2", "work-net-2", "pb2");
    assert(wifi_profile_store_update(&store, 1, &replacement) == WIFI_PROFILE_OK);

    assert(memcmp(wifi_profile_store_at(&store, 0), &a, sizeof a) == 0);
    assert(memcmp(wifi_profile_store_at(&store, 1), &replacement, sizeof replacement) == 0);
    assert(memcmp(wifi_profile_store_at(&store, 2), &c, sizeof c) == 0);

    assert(wifi_profile_store_update(&store, 3, &replacement) == WIFI_PROFILE_ERR_NOT_FOUND);
}

static void test_remove_compacts_slots_and_fixes_active(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);

    wifi_profile_t a = make_profile("home", "home-net", "pa");
    wifi_profile_t b = make_profile("work", "work-net", "pb");
    wifi_profile_t c = make_profile("spare", "spare-net", "pc");
    assert(wifi_profile_store_add(&store, &a) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_add(&store, &b) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_add(&store, &c) == WIFI_PROFILE_OK);

    // active 指向被删项之后的条目，删除后应前移一位。
    assert(wifi_profile_store_set_active(&store, 2) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_remove(&store, 1) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&store) == 2);
    assert(memcmp(wifi_profile_store_at(&store, 0), &a, sizeof a) == 0);
    assert(memcmp(wifi_profile_store_at(&store, 1), &c, sizeof c) == 0);
    assert(wifi_profile_store_active(&store) == 1);

    // 再删掉当前 active，应归 -1 而不是指向别的网络。
    assert(wifi_profile_store_remove(&store, 1) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&store) == 1);
    assert(wifi_profile_store_active(&store) == -1);
    assert(wifi_profile_store_remove(&store, 5) == WIFI_PROFILE_ERR_NOT_FOUND);
}

static void test_set_active_validation_and_clear(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);

    wifi_profile_t a = make_profile("home", "home-net", "pa");
    assert(wifi_profile_store_add(&store, &a) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_set_active(&store, 0) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_active(&store) == 0);
    assert(wifi_profile_store_set_active(&store, 1) == WIFI_PROFILE_ERR_INVALID);
    assert(wifi_profile_store_set_active(&store, -2) == WIFI_PROFILE_ERR_INVALID);
    assert(wifi_profile_store_set_active(&store, -1) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_active(&store) == -1);
}

static void test_erase_all_leaves_no_keys_behind(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);

    wifi_profile_t a = make_profile("home", "home-net", "pa");
    assert(wifi_profile_store_add(&store, &a) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_save(&store) == WIFI_PROFILE_OK);
    assert(stub_live_keys(&st) > 0);

    assert(wifi_profile_store_erase_all(&store) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&store) == 0);
    assert(wifi_profile_store_active(&store) == -1);
    assert(stub_live_keys(&st) == 0);

    wifi_profile_store_t reloaded;
    assert(wifi_profile_store_load(&reloaded, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&reloaded) == 0);
}

static void test_corrupt_records_are_skipped_not_fatal(void) {
    stub_store_t st;
    stub_init(&st);

    // 槽 0：未知版本。槽 1：合法记录被截断。槽 2：合法记录后面多挂一个字节。
    uint8_t unknown_version[WIFI_PROFILE_RECORD_MAX] = {0};
    unknown_version[0] = 99;
    unknown_version[2] = 3;
    memcpy(&unknown_version[WIFI_PROFILE_RECORD_HEADER], "abc", 3);
    stub_put_raw(&st, "p0", unknown_version, WIFI_PROFILE_RECORD_HEADER + 3);

    wifi_profile_t good = make_profile("home", "home-net", "pw");
    uint8_t encoded[WIFI_PROFILE_RECORD_MAX];
    size_t encoded_len = wifi_profile_encode(&good, encoded, sizeof encoded);
    assert(encoded_len > 0);
    stub_put_raw(&st, "p1", encoded, encoded_len - 1);

    uint8_t padded[WIFI_PROFILE_RECORD_MAX];
    memcpy(padded, encoded, encoded_len);
    padded[encoded_len] = 0x7f;
    stub_put_raw(&st, "p2", padded, encoded_len + 1);

    uint8_t count = 3;
    stub_put_raw(&st, "n", &count, sizeof count);

    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&store) == 0);
    assert(wifi_profile_store_active(&store) == -1);
}

static void test_save_failure_keeps_ram_cache_intact(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);

    wifi_profile_t a = make_profile("home", "home-net", "pa");
    assert(wifi_profile_store_add(&store, &a) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_save(&store) == WIFI_PROFILE_OK);

    wifi_profile_t b = make_profile("work", "work-net", "pb");
    assert(wifi_profile_store_add(&store, &b) == WIFI_PROFILE_OK);
    st.fail_set = true;
    assert(wifi_profile_store_save(&store) == WIFI_PROFILE_ERR_STORAGE);
    st.fail_set = false;

    // 内存里的改动不能因为落盘失败被回滚，否则用户按一次键就白按。
    assert(wifi_profile_store_count(&store) == 2);

    wifi_profile_store_t reloaded;
    assert(wifi_profile_store_load(&reloaded, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&reloaded) == 1);
    assert(strcmp(wifi_profile_store_at(&reloaded, 0)->ssid, "home-net") == 0);
}

static void test_field_length_boundaries(void) {
    char ssid[40];
    char pass[80];

    memset(ssid, 'S', sizeof ssid);
    ssid[32] = '\0';
    memset(pass, 'P', sizeof pass);
    pass[64] = '\0';

    wifi_profile_t p;
    assert(wifi_profile_set_fields(&p, "max", ssid, pass));
    assert(strlen(p.ssid) == 32u);
    assert(strlen(p.password) == 64u);

    // 33 字节：必须把第 32 位恢复成字符，否则字符串仍然是 32 字节。
    ssid[32] = 'S';
    ssid[33] = '\0';
    assert(!wifi_profile_set_fields(&p, "max", ssid, "pw"));
    ssid[32] = '\0';

    // 65 字节同理。
    pass[64] = 'P';
    pass[65] = '\0';
    assert(!wifi_profile_set_fields(&p, "max", ssid, pass));
    pass[64] = '\0';

    assert(!wifi_profile_set_fields(&p, "max", "", "pw"));
    assert(!wifi_profile_set_fields(NULL, "bad", "bad-net", "pw"));
}

int main(void) {
    test_empty_load_reads_as_empty_library();
    test_round_trip_preserves_every_field();
    test_ninth_profile_is_rejected();
    test_update_only_touches_target_slot();
    test_remove_compacts_slots_and_fixes_active();
    test_set_active_validation_and_clear();
    test_erase_all_leaves_no_keys_behind();
    test_corrupt_records_are_skipped_not_fatal();
    test_save_failure_keeps_ram_cache_intact();
    test_field_length_boundaries();
    puts("wifi profile store tests: PASS");
    return 0;
}
