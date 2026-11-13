// tests/test_page_ring.c —— 产品页环的主机测试。
//
// 只关心"上下键在状态页与天气页之间怎么走"，以及菜单/配网这些非产品页不会被
// 环带走（那会让长按进菜单的路径莫名其妙地变成切页）。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "page_ring.h"

static void test_three_product_pages_form_a_ring(void) {
    // 状态 → 天气 → 用量 → 状态，两端相接。
    assert(page_ring_next(PAGE_STATUS) == PAGE_WEATHER);
    assert(page_ring_next(PAGE_WEATHER) == PAGE_USAGE);
    assert(page_ring_next(PAGE_USAGE) == PAGE_STATUS);

    assert(page_ring_prev(PAGE_STATUS) == PAGE_USAGE);
    assert(page_ring_prev(PAGE_WEATHER) == PAGE_STATUS);
    assert(page_ring_prev(PAGE_USAGE) == PAGE_WEATHER);
}

static void test_the_ring_is_stable_under_repeated_steps(void) {
    // 走三圈回到原点：环里绝不能有两个页互相卡住或吃掉一步。
    page_id_t page = PAGE_STATUS;
    for (int i = 0; i < 3 * 3; i++) {
        page = page_ring_next(page);
    }
    assert(page == PAGE_STATUS);

    for (int i = 0; i < 3 * 3; i++) {
        page = page_ring_prev(page);
    }
    assert(page == PAGE_STATUS);

    // 前进再后退必须回到原页，三页时最容易把取模写错。
    assert(page_ring_prev(page_ring_next(PAGE_USAGE)) == PAGE_USAGE);
    assert(page_ring_next(page_ring_prev(PAGE_STATUS)) == PAGE_STATUS);
}

static void test_non_product_pages_are_not_in_the_ring(void) {
    static const page_id_t outside[] = {PAGE_MENU, PAGE_DEMO, PAGE_PROVISION};

    for (size_t i = 0; i < sizeof outside / sizeof outside[0]; i++) {
        assert(!page_ring_contains(outside[i]));
        assert(page_ring_next(outside[i]) == outside[i]);
        assert(page_ring_prev(outside[i]) == outside[i]);
    }

    assert(page_ring_contains(PAGE_STATUS));
    assert(page_ring_contains(PAGE_WEATHER));
    assert(page_ring_contains(PAGE_USAGE));
}

int main(void) {
    test_three_product_pages_form_a_ring();
    test_the_ring_is_stable_under_repeated_steps();
    test_non_product_pages_are_not_in_the_ring();
    puts("page ring tests: PASS");
    return 0;
}
