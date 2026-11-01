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
    app_location_t loc;
    assert(app_location_set(&loc, 39904200, 116407400, "Beijing"));
    assert(wifi_profile_set_location(&p, &loc));
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
    assert(got->location.has_location);
    assert(got->location.lat_udeg == 39904200);
    assert(got->location.lon_udeg == 116407400);
    assert(strcmp(got->location.place, "Beijing") == 0);
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

// 手工造一条 v2 记录：模拟"升级前的老固件写下的网络"。
static size_t make_v2_blob(const char *name, const char *ssid, const char *pass,
                           uint8_t *buf, size_t cap) {
    size_t name_len = strlen(name);
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(pass);
    size_t total = WIFI_PROFILE_RECORD_HEADER_V2 + name_len + ssid_len + pass_len;

    assert(total <= cap);
    assert(name_len < WIFI_PROFILE_NAME_MAX);
    assert(ssid_len < WIFI_PROFILE_SSID_MAX);
    assert(pass_len < WIFI_PROFILE_PASS_MAX);

    buf[0] = (uint8_t)WIFI_PROFILE_RECORD_VERSION_V2;
    buf[1] = (uint8_t)name_len;
    buf[2] = (uint8_t)ssid_len;
    buf[3] = (uint8_t)pass_len;
    memcpy(buf + WIFI_PROFILE_RECORD_HEADER_V2, name, name_len);
    memcpy(buf + WIFI_PROFILE_RECORD_HEADER_V2 + name_len, ssid, ssid_len);
    memcpy(buf + WIFI_PROFILE_RECORD_HEADER_V2 + name_len + ssid_len, pass, pass_len);
    return total;
}

static void test_location_setter_validates_and_clears(void) {
    wifi_profile_t p = make_profile("home", "home-net", "pw");
    app_location_t loc;

    assert(!p.location.has_location); // set_fields 之后默认没有地点

    assert(app_location_set(&loc, 39904200, 116407400, "Beijing"));
    assert(wifi_profile_set_location(&p, &loc));
    assert(p.location.has_location);
    assert(strcmp(p.location.place, "Beijing") == 0);

    // NULL 与 has_location == false 都表示"清除这个网络的地点"。
    assert(wifi_profile_set_location(&p, NULL));
    assert(!p.location.has_location);
    app_location_clear(&loc);
    assert(wifi_profile_set_location(&p, &loc));
    assert(!p.location.has_location);

    // 非法地点不得破坏已经写好的那一份。
    assert(app_location_set(&loc, 1000000, 2000000, "Office"));
    assert(wifi_profile_set_location(&p, &loc));

    app_location_t bad;
    memset(&bad, 0, sizeof bad);
    bad.has_location = true;
    bad.lat_udeg = APP_LOCATION_LAT_UDEG_MAX + 1;
    assert(!wifi_profile_set_location(&p, &bad));
    assert(strcmp(p.location.place, "Office") == 0);
    assert(p.location.lat_udeg == 1000000);

    // place 缓冲区内没有 NUL：必须拒绝，而不是 strlen 读飞。
    memset(&bad, 0, sizeof bad);
    bad.has_location = true;
    memset(bad.place, 'A', sizeof bad.place);
    assert(!wifi_profile_set_location(&p, &bad));
}

static void test_v2_record_is_still_readable(void) {
    uint8_t blob[64];
    size_t len = make_v2_blob("home", "home-net", "pw", blob, sizeof blob);
    assert(len > 0);

    wifi_profile_t decoded;
    assert(wifi_profile_decode(blob, len, &decoded));
    assert(strcmp(decoded.name, "home") == 0);
    assert(strcmp(decoded.ssid, "home-net") == 0);
    assert(strcmp(decoded.password, "pw") == 0);
    assert(!decoded.location.has_location); // 老记录没有坐标
    assert(decoded.location.place[0] == '\0');

    // store 也必须接受 v2，并且下次 save 时升级成 v3。
    stub_store_t st;
    stub_init(&st);
    uint8_t v2 = (uint8_t)WIFI_PROFILE_RECORD_VERSION_V2;
    stub_put_raw(&st, "ver", &v2, sizeof v2);
    uint8_t count = 1;
    stub_put_raw(&st, "n", &count, sizeof count);
    stub_put_raw(&st, "p0", blob, len);

    wifi_kv_t kv = make_kv(&st);
    wifi_profile_store_t store;
    assert(wifi_profile_store_load(&store, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&store) == 1);
    assert(strcmp(wifi_profile_store_at(&store, 0)->ssid, "home-net") == 0);
    assert(!wifi_profile_store_at(&store, 0)->location.has_location);

    assert(wifi_profile_store_save(&store) == WIFI_PROFILE_OK);
    uint8_t stored_version = 0;
    size_t version_len = sizeof stored_version;
    assert(stub_get(&st, "ver", &stored_version, &version_len) == WIFI_KV_OK);
    assert(stored_version == (uint8_t)WIFI_PROFILE_RECORD_VERSION);

    wifi_profile_store_t reloaded;
    assert(wifi_profile_store_load(&reloaded, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&reloaded) == 1);
    assert(strcmp(wifi_profile_store_at(&reloaded, 0)->ssid, "home-net") == 0);
}

static void test_decode_rejects_inconsistent_location(void) {
    wifi_profile_t p = make_profile("home", "home-net", "pw");
    app_location_t loc;
    assert(app_location_set(&loc, 39904200, 116407400, "Beijing"));
    assert(wifi_profile_set_location(&p, &loc));

    uint8_t encoded[WIFI_PROFILE_RECORD_MAX];
    size_t encoded_len = wifi_profile_encode(&p, encoded, sizeof encoded);
    assert(encoded_len > 0);

    wifi_profile_t decoded;
    assert(wifi_profile_decode(encoded, encoded_len, &decoded));
    assert(decoded.location.has_location);

    // 未知 flag 位：版本号没升，就不该出现新语义。
    uint8_t with_unknown_flag[WIFI_PROFILE_RECORD_MAX];
    memcpy(with_unknown_flag, encoded, encoded_len);
    with_unknown_flag[1] |= 0x02u;
    assert(!wifi_profile_decode(with_unknown_flag, encoded_len, &decoded));

    // 纬度越界：解码入口必须拦住，不能让坏值进 store。
    uint8_t bad_lat[WIFI_PROFILE_RECORD_MAX];
    memcpy(bad_lat, encoded, encoded_len);
    uint32_t raw = (uint32_t)(APP_LOCATION_LAT_UDEG_MAX + 1);
    bad_lat[2] = (uint8_t)(raw & 0xFFu);
    bad_lat[3] = (uint8_t)((raw >> 8) & 0xFFu);
    bad_lat[4] = (uint8_t)((raw >> 16) & 0xFFu);
    bad_lat[5] = (uint8_t)((raw >> 24) & 0xFFu);
    assert(!wifi_profile_decode(bad_lat, encoded_len, &decoded));

    // 没有坐标却带地点名：写侧逻辑坏了，按畸形记录处理。
    uint8_t place_without_location[32] = {0};
    place_without_location[0] = (uint8_t)WIFI_PROFILE_RECORD_VERSION;
    place_without_location[1] = 0x00u; // has_location = false
    place_without_location[11] = 3;    // ssid_len
    place_without_location[13] = 1;    // place_len
    memcpy(place_without_location + WIFI_PROFILE_RECORD_HEADER, "net", 3);
    place_without_location[WIFI_PROFILE_RECORD_HEADER + 3] = 'x';
    assert(!wifi_profile_decode(place_without_location,
                                WIFI_PROFILE_RECORD_HEADER + 4, &decoded));
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
    test_location_setter_validates_and_clears();
    test_v2_record_is_still_readable();
    test_decode_rejects_inconsistent_location();
    puts("wifi profile store tests: PASS");
    return 0;
}
