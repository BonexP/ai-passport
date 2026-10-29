// tests/test_wifi_presets.c —— 预设注入的 host 测试。
//
// 不依赖 ESP-IDF，也不依赖本机的 wifi_presets.local.h：所有"有 N 条预设"的场景都通过
// wifi_preset_seed_apply() 直接传数组验证，因此 CI（预设恒为 0 条）与开发者本机的
// 结果一致。builtin 只用来验证"表访问器与注入函数连得上"，断言对 0 条与 N 条都成立。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_presets.h"
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

static void load_empty(wifi_profile_store_t *store, stub_store_t *st) {
    wifi_kv_t kv = make_kv(st);
    assert(wifi_profile_store_load(store, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(store) == 0);
}

static void test_empty_store_seeds_all_presets(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    const wifi_preset_t presets[] = {
        {"home", "home-net", "home-pass"},
        {"work", "work-net", "work-pass"},
    };
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&store, presets, 2, &rep) == WIFI_PRESET_SEED_APPLIED);
    assert(wifi_profile_store_count(&store) == 2);
    assert(rep.added == 2);
    assert(rep.skipped == 0);

    const wifi_profile_t *p0 = wifi_profile_store_at(&store, 0);
    assert(p0 != NULL);
    assert(strcmp(p0->name, "home") == 0);
    assert(strcmp(p0->ssid, "home-net") == 0);
    assert(strcmp(p0->password, "home-pass") == 0);
    // 空库时第一条被自动提升为 active，所以"预设一优先"不需要额外代码。
    assert(wifi_profile_store_active(&store) == 0);

    // 真的落盘了：从同一份 kv 重新加载还能看到两条。
    wifi_profile_store_t reloaded;
    wifi_kv_t kv = make_kv(&st);
    assert(wifi_profile_store_load(&reloaded, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&reloaded) == 2);
    assert(strcmp(wifi_profile_store_at(&reloaded, 1)->ssid, "work-net") == 0);
}

static void test_non_empty_store_is_left_alone(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    wifi_profile_t existing;
    assert(wifi_profile_set_fields(&existing, "mine", "my-net", "my-pass"));
    assert(wifi_profile_store_add(&store, &existing) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_save(&store) == WIFI_PROFILE_OK);

    wifi_profile_store_t loaded;
    wifi_kv_t kv = make_kv(&st);
    assert(wifi_profile_store_load(&loaded, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&loaded) == 1);

    const wifi_preset_t presets[] = {
        {"home", "home-net", "home-pass"},
        {"work", "work-net", "work-pass"},
    };
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&loaded, presets, 2, &rep) ==
           WIFI_PRESET_SEED_LIBRARY_NOT_EMPTY);
    assert(wifi_profile_store_count(&loaded) == 1);
    assert(rep.added == 0);
    assert(strcmp(wifi_profile_store_at(&loaded, 0)->ssid, "my-net") == 0);
    assert(wifi_profile_store_active(&loaded) == 0);
}

static void test_zero_presets_touch_nothing(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&store, NULL, 0, &rep) == WIFI_PRESET_SEED_NOTHING_TO_ADD);
    assert(wifi_profile_store_count(&store) == 0);
    // 一个 key 都没写：证明"没有可加的东西"时不会去碰 flash。
    assert(stub_live_keys(&st) == 0);

    // 指针为空但条数非零，同样不许崩。
    assert(wifi_preset_seed_apply(&store, NULL, 3, &rep) == WIFI_PRESET_SEED_NOTHING_TO_ADD);
    assert(stub_live_keys(&st) == 0);
}

static void test_invalid_field_skips_only_that_entry(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    char long_ssid[40];
    memset(long_ssid, 'S', sizeof long_ssid);
    long_ssid[33] = '\0'; // 33 字节，超过 32 上限

    const wifi_preset_t presets[] = {
        {"bad-empty", "", "pw"},              // SSID 为空
        {"bad-long", long_ssid, "pw"},        // SSID 超长
        {"good", "good-net", "good-pass"},    // 唯一合法的一条
    };
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&store, presets, 3, &rep) == WIFI_PRESET_SEED_APPLIED);
    assert(wifi_profile_store_count(&store) == 1);
    assert(rep.added == 1);
    assert(rep.skipped == 2);
    assert(strcmp(wifi_profile_store_at(&store, 0)->ssid, "good-net") == 0);
}

static void test_all_invalid_presets_write_nothing(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    const wifi_preset_t presets[] = {
        {"a", "", "pw"},
        {"b", "", "pw"},
    };
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&store, presets, 2, &rep) == WIFI_PRESET_SEED_NOTHING_TO_ADD);
    assert(wifi_profile_store_count(&store) == 0);
    assert(rep.added == 0);
    assert(rep.skipped == 2);
    assert(stub_live_keys(&st) == 0);
}

static void test_duplicate_ssid_keeps_the_first(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    const wifi_preset_t presets[] = {
        {"first", "same-net", "first-pass"},
        {"second", "same-net", "second-pass"},
    };
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&store, presets, 2, &rep) == WIFI_PRESET_SEED_APPLIED);
    assert(wifi_profile_store_count(&store) == 1);
    assert(rep.added == 1);
    assert(rep.skipped == 1);
    assert(strcmp(wifi_profile_store_at(&store, 0)->password, "first-pass") == 0);
}

static void test_save_failure_keeps_presets_in_ram_and_recovers(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    const wifi_preset_t presets[] = {
        {"home", "home-net", "home-pass"},
        {"work", "work-net", "work-pass"},
    };
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    st.fail_set = true;
    assert(wifi_preset_seed_apply(&store, presets, 2, &rep) == WIFI_PRESET_SEED_ERR_SAVE);
    st.fail_set = false;

    // 落盘失败不回滚内存：本次开机仍能连，下次开机会重新注入。
    assert(wifi_profile_store_count(&store) == 2);

    wifi_profile_store_t after_reboot;
    wifi_kv_t kv = make_kv(&st);
    assert(wifi_profile_store_load(&after_reboot, &kv) == WIFI_PROFILE_OK);
    assert(wifi_profile_store_count(&after_reboot) == 0);

    // 恢复后再次注入：语义幂等，自愈。
    assert(wifi_preset_seed_apply(&after_reboot, presets, 2, &rep) == WIFI_PRESET_SEED_APPLIED);
    assert(wifi_profile_store_count(&after_reboot) == 2);
}

static void test_library_full_stops_at_max(void) {
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);

    char ssid_pool[10][4];
    wifi_preset_t presets[10];
    for (int i = 0; i < 10; i++) {
        // 手写两位 SSID 而不是 snprintf：%d 的完整取值范围会让 -Wformat-truncation
        // 认为可能截断，而这里开了 -Werror。
        ssid_pool[i][0] = 'n';
        ssid_pool[i][1] = (char)('0' + i);
        ssid_pool[i][2] = '\0';
        presets[i].name = NULL;
        presets[i].ssid = ssid_pool[i];
        presets[i].password = "pw";
    }

    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    assert(wifi_preset_seed_apply(&store, presets, 10, &rep) == WIFI_PRESET_SEED_APPLIED);
    assert(wifi_profile_store_count(&store) == WIFI_PROFILE_MAX);
    assert(rep.added == WIFI_PROFILE_MAX);
    assert(rep.skipped == 2);
}

static void test_builtin_table_matches_contract(void) {
    size_t n = wifi_presets_count();
    if (n == 0) {
        // CI 与本机没有本地头文件时的路径：表为空，且空库注入不写任何 key。
        assert(wifi_presets_table() == NULL);
        stub_store_t st;
        stub_init(&st);
        wifi_profile_store_t store;
        load_empty(&store, &st);
        wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
        assert(wifi_preset_seed_builtin(&store, &rep) == WIFI_PRESET_SEED_NOTHING_TO_ADD);
        assert(wifi_profile_store_count(&store) == 0);
        assert(stub_live_keys(&st) == 0);
        return;
    }
    // 开发者本机有预设时的路径：只断言契约，不假设内容。
    assert(wifi_presets_table() != NULL);
    stub_store_t st;
    stub_init(&st);
    wifi_profile_store_t store;
    load_empty(&store, &st);
    wifi_preset_seed_report_t rep = { .added = 0, .skipped = 0 };
    wifi_preset_seed_result_t result = wifi_preset_seed_builtin(&store, &rep);
    assert(result == WIFI_PRESET_SEED_APPLIED || result == WIFI_PRESET_SEED_NOTHING_TO_ADD);
    assert(wifi_profile_store_count(&store) <= n);
}

int main(void) {
    test_empty_store_seeds_all_presets();
    test_non_empty_store_is_left_alone();
    test_zero_presets_touch_nothing();
    test_invalid_field_skips_only_that_entry();
    test_all_invalid_presets_write_nothing();
    test_duplicate_ssid_keeps_the_first();
    test_save_failure_keeps_presets_in_ram_and_recovers();
    test_library_full_stops_at_max();
    test_builtin_table_matches_contract();
    puts("wifi preset seeding tests: PASS");
    return 0;
}
