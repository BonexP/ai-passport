// tests/test_page_router.c —— 四页模型的按键决策表（纯逻辑）。
//
// 这张表是"状态页 ↔ 菜单 ↔ 配网"的唯一真相，所以逐格断言，不做抽样。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "page_router.h"

static void test_status_page_routes_both_long_presses(void) {
    assert(page_router_resolve(PAGE_STATUS, DEMO_NAV_INPUT_OK_LONG) == ROUTE_TO_MENU);
    assert(page_router_resolve(PAGE_STATUS, DEMO_NAV_INPUT_UP_LONG) == ROUTE_TO_PROVISION);
    assert(page_router_resolve(PAGE_STATUS, DEMO_NAV_INPUT_OK_CLICK) == ROUTE_IGNORE);
    assert(page_router_resolve(PAGE_STATUS, DEMO_NAV_INPUT_UP_CLICK) == ROUTE_IGNORE);
    assert(page_router_resolve(PAGE_STATUS, DEMO_NAV_INPUT_DOWN_CLICK) == ROUTE_IGNORE);
    assert(page_router_resolve(PAGE_STATUS, DEMO_NAV_INPUT_OTHER) == ROUTE_IGNORE);
}

static void test_provision_page_only_exits_on_ok_long(void) {
    assert(page_router_resolve(PAGE_PROVISION, DEMO_NAV_INPUT_OK_LONG) == ROUTE_PROVISION_EXIT);
    assert(page_router_resolve(PAGE_PROVISION, DEMO_NAV_INPUT_OK_CLICK) == ROUTE_IGNORE);
    assert(page_router_resolve(PAGE_PROVISION, DEMO_NAV_INPUT_UP_CLICK) == ROUTE_IGNORE);
    assert(page_router_resolve(PAGE_PROVISION, DEMO_NAV_INPUT_DOWN_CLICK) == ROUTE_IGNORE);
    assert(page_router_resolve(PAGE_PROVISION, DEMO_NAV_INPUT_OTHER) == ROUTE_IGNORE);
    // UP 长按在配网态不能再进一次配网。
    assert(page_router_resolve(PAGE_PROVISION, DEMO_NAV_INPUT_UP_LONG) == ROUTE_IGNORE);
}

static void test_menu_ok_long_returns_to_status(void) {
    assert(page_router_resolve(PAGE_MENU, DEMO_NAV_INPUT_OK_LONG) == ROUTE_TO_STATUS);
}

static void test_menu_cursor_and_enter(void) {
    assert(page_router_resolve(PAGE_MENU, DEMO_NAV_INPUT_UP_CLICK) == ROUTE_MENU_MOVE);
    assert(page_router_resolve(PAGE_MENU, DEMO_NAV_INPUT_DOWN_CLICK) == ROUTE_MENU_MOVE);
    assert(page_router_resolve(PAGE_MENU, DEMO_NAV_INPUT_OK_CLICK) == ROUTE_MENU_ENTER);
    assert(page_router_resolve(PAGE_MENU, DEMO_NAV_INPUT_OTHER) == ROUTE_IGNORE);
    // 菜单里长按 UP 不该跳到配网：入口只在状态页。
    assert(page_router_resolve(PAGE_MENU, DEMO_NAV_INPUT_UP_LONG) == ROUTE_IGNORE);
}

static void test_demo_page_forwards_and_exits(void) {
    assert(page_router_resolve(PAGE_DEMO, DEMO_NAV_INPUT_OK_LONG) == ROUTE_DEMO_EXIT);
    assert(page_router_resolve(PAGE_DEMO, DEMO_NAV_INPUT_OK_CLICK) == ROUTE_DEMO_KEY);
    assert(page_router_resolve(PAGE_DEMO, DEMO_NAV_INPUT_UP_CLICK) == ROUTE_DEMO_KEY);
    assert(page_router_resolve(PAGE_DEMO, DEMO_NAV_INPUT_DOWN_CLICK) == ROUTE_DEMO_KEY);
    // 其他事件（PRESS/RELEASE/DOUBLE 以及 UP 长按）也要转发给页面，
    // 否则页面收不到长按与松手。
    assert(page_router_resolve(PAGE_DEMO, DEMO_NAV_INPUT_OTHER) == ROUTE_DEMO_KEY);
    assert(page_router_resolve(PAGE_DEMO, DEMO_NAV_INPUT_UP_LONG) == ROUTE_DEMO_KEY);
}

static void test_table_is_total(void) {
    const page_id_t pages[] = { PAGE_STATUS, PAGE_MENU, PAGE_DEMO, PAGE_PROVISION };
    const demo_nav_input_t inputs[] = { DEMO_NAV_INPUT_OTHER, DEMO_NAV_INPUT_UP_CLICK,
                                        DEMO_NAV_INPUT_DOWN_CLICK, DEMO_NAV_INPUT_OK_CLICK,
                                        DEMO_NAV_INPUT_OK_LONG, DEMO_NAV_INPUT_UP_LONG };
    // 每一格都必须落到一个明确动作。状态页与配网态允许 IGNORE（它们只认长按）；
    // 菜单对非单击事件静默忽略是合理的；演示页绝不允许吃掉按键。
    for (size_t p = 0; p < sizeof pages / sizeof pages[0]; p++) {
        for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
            route_action_t route = page_router_resolve(pages[p], inputs[i]);
            if (pages[p] == PAGE_STATUS) {
                assert(route == ROUTE_IGNORE || route == ROUTE_TO_MENU ||
                       route == ROUTE_TO_PROVISION);
            } else if (pages[p] == PAGE_PROVISION) {
                assert(route == ROUTE_IGNORE || route == ROUTE_PROVISION_EXIT);
            } else if (pages[p] == PAGE_MENU) {
                if (inputs[i] == DEMO_NAV_INPUT_OTHER || inputs[i] == DEMO_NAV_INPUT_UP_LONG) {
                    assert(route == ROUTE_IGNORE);
                } else {
                    assert(route != ROUTE_IGNORE);
                }
            } else {
                assert(route != ROUTE_IGNORE);
            }
        }
    }
}

int main(void) {
    test_status_page_routes_both_long_presses();
    test_provision_page_only_exits_on_ok_long();
    test_menu_ok_long_returns_to_status();
    test_menu_cursor_and_enter();
    test_demo_page_forwards_and_exits();
    test_table_is_total();
    puts("page router tests: PASS");
    return 0;
}
