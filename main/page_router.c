// main/page_router.c —— 三页模型的按键决策。
#include "page_router.h"

route_action_t page_router_resolve(page_id_t page, demo_nav_input_t input) {
    switch (page) {
    case PAGE_STATUS:
        // 短按上下在产品页环里切换；长按 UP 进配网、长按 OK 进菜单。
        // 其余一律丢弃，免得上电时误触把用户弹到别处。
        switch (input) {
        case DEMO_NAV_INPUT_UP_CLICK:
            return ROUTE_PAGE_NEXT;
        case DEMO_NAV_INPUT_DOWN_CLICK:
            return ROUTE_PAGE_PREV;
        case DEMO_NAV_INPUT_UP_LONG:
            return ROUTE_TO_PROVISION;
        case DEMO_NAV_INPUT_OK_LONG:
            return ROUTE_TO_MENU;
        case DEMO_NAV_INPUT_OK_CLICK: // 状态页没有"可点"的对象
        case DEMO_NAV_INPUT_OTHER:
            return ROUTE_IGNORE;
        }
        return ROUTE_IGNORE;

    case PAGE_WEATHER:
        switch (input) {
        case DEMO_NAV_INPUT_UP_CLICK:
            return ROUTE_PAGE_NEXT;
        case DEMO_NAV_INPUT_DOWN_CLICK:
            return ROUTE_PAGE_PREV;
        case DEMO_NAV_INPUT_OK_CLICK:
            return ROUTE_REFRESH_WEATHER;
        case DEMO_NAV_INPUT_OK_LONG:
            return ROUTE_TO_STATUS;
        case DEMO_NAV_INPUT_UP_LONG: // 配网入口只在状态页：天气页长按 UP 不开热点
        case DEMO_NAV_INPUT_OTHER:
            return ROUTE_IGNORE;
        }
        return ROUTE_IGNORE;

    case PAGE_PROVISION:
        // 配网态只有一条出路：OK 长按退出。热点开着时其他键一概不响应。
        return input == DEMO_NAV_INPUT_OK_LONG ? ROUTE_PROVISION_EXIT : ROUTE_IGNORE;

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
        case DEMO_NAV_INPUT_UP_LONG: // 配网入口只在状态页，菜单里长按 UP 不算数
            return ROUTE_IGNORE;
        }
        return ROUTE_IGNORE;

    case PAGE_DEMO:
        // 页面里的 OK 长按仍然是"返回菜单"，与既有约定一致。
        return input == DEMO_NAV_INPUT_OK_LONG ? ROUTE_DEMO_EXIT : ROUTE_DEMO_KEY;
    }
    return ROUTE_IGNORE;
}
