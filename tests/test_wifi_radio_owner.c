// tests/test_wifi_radio_owner.c —— 射频持有者的 host 测试（纯逻辑，零桩）。
#include <assert.h>
#include <stdio.h>

#include "wifi_radio_owner.h"

static void test_starts_free(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_FREE);
}

static void test_claim_once_and_hold(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(wifi_radio_claim(&owner, WIFI_RADIO_PROFILE));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_PROFILE);
}

static void test_second_owner_is_rejected(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(wifi_radio_claim(&owner, WIFI_RADIO_PROFILE));
    // 配网想抢：必须被拒，且现状不变。
    assert(!wifi_radio_claim(&owner, WIFI_RADIO_PROVISION));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_PROFILE);
}

static void test_same_owner_cannot_claim_twice(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(wifi_radio_claim(&owner, WIFI_RADIO_PROFILE));
    // 重复 claim 也返回 false：调用方据此判断"我本来就该持有"是错误的，
    // 进而避免在嵌套路径里重复 release。
    assert(!wifi_radio_claim(&owner, WIFI_RADIO_PROFILE));
}

static void test_release_is_owner_only(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(wifi_radio_claim(&owner, WIFI_RADIO_PROFILE));
    // 别人来释放：无效，持有者不受影响。
    assert(!wifi_radio_release(&owner, WIFI_RADIO_PROVISION));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_PROFILE);
    // 自己释放：生效。
    assert(wifi_radio_release(&owner, WIFI_RADIO_PROFILE));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_FREE);
}

static void test_reclaim_after_release(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(wifi_radio_claim(&owner, WIFI_RADIO_PROFILE));
    assert(wifi_radio_release(&owner, WIFI_RADIO_PROFILE));
    assert(wifi_radio_claim(&owner, WIFI_RADIO_PROVISION));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_PROVISION);
}

static void test_release_when_free_is_harmless(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(!wifi_radio_release(&owner, WIFI_RADIO_PROFILE));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_FREE);
}

static void test_invalid_owner_is_rejected(void) {
    wifi_radio_owner_t owner;
    wifi_radio_owner_init(&owner);
    assert(!wifi_radio_claim(&owner, WIFI_RADIO_FREE));
    assert(wifi_radio_holder(&owner) == WIFI_RADIO_FREE);
}

static void test_null_is_survivable(void) {
    // 这两个函数会在初始化早期被调用，容错比断言更合适。
    assert(!wifi_radio_claim(NULL, WIFI_RADIO_PROFILE));
    assert(!wifi_radio_release(NULL, WIFI_RADIO_PROFILE));
    assert(wifi_radio_holder(NULL) == WIFI_RADIO_FREE);
    wifi_radio_owner_init(NULL);
}

int main(void) {
    test_starts_free();
    test_claim_once_and_hold();
    test_second_owner_is_rejected();
    test_same_owner_cannot_claim_twice();
    test_release_is_owner_only();
    test_reclaim_after_release();
    test_release_when_free_is_harmless();
    test_invalid_owner_is_rejected();
    test_null_is_survivable();
    puts("wifi radio owner tests: PASS");
    return 0;
}
