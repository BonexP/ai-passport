// main/page_router.c —— 三页模型的按键决策。
#include "page_router.h"

route_action_t page_router_resolve(page_id_t page, demo_nav_input_t input) {
    switch (page) {
    case PAGE_STATUS:
        // 状态页只认一种操作。UP 长按留给配网（S3 接），其余一律丢弃，
        // 免得上电时误触把用户弹到别处。
        return input == DEMO_NAV_INPUT_OK_LONG ? ROUTE_TO_MENU : ROUTE_IGNORE;

    case PAGE_MENU:
        switch (input) {
        case DEMO_NAV_INPUT_OK_LONG:
            return ROUTE_TO_STATUS;
        case DEMO_NAV_INPUT_UP_CLICK:
        case DEMO_NAV_INPUT_DOWN_CLICK:
            return ROUTE_MENU_MOVE;
        case DEMO_NAV_INPUT_OK_CLICK:
            return ROUTE_MENU_ENTER;
        case DEMO_NAV_INPUT_OTHER:
            return ROUTE_IGNORE;
        }
        return ROUTE_IGNORE;

    case PAGE_DEMO:
        // 页面里的 OK 长按仍然是"返回菜单"，与既有约定一致。
        return input == DEMO_NAV_INPUT_OK_LONG ? ROUTE_DEMO_EXIT : ROUTE_DEMO_KEY;
    }
    return ROUTE_IGNORE;
}
