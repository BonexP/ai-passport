// main/page_router.h —— 页面路由的决策表（纯逻辑）。
//
// 开机首屏是状态页，菜单退到它后面。于是"当前在哪一页"变成三态，而原来的
// demo_navigation 只有"菜单 / 页面"两态。把决策抽到这里，是为了让
// "菜单 ↔ 状态页怎么切换"能在主机上测，而不是埋在 main.c 的 if-else 里。
//
// 输入来自 demo_navigation 的归一化结果（UP/DOWN 单击、OK 单击、OK 长按、其他），
// 输出是"该做什么"。main.c 只负责执行，不再判断。
#pragma once

#include "demo_navigation.h"

typedef enum {
    PAGE_STATUS = 0, // 开机首屏
    PAGE_MENU,       // 菜单
    PAGE_DEMO,       // 某个演示页
} page_id_t;

typedef enum {
    ROUTE_IGNORE = 0,  // 丢弃（状态页上的绝大多数按键）
    ROUTE_TO_STATUS,   // 菜单 → 状态页
    ROUTE_TO_MENU,     // 状态页 → 菜单
    ROUTE_MENU_MOVE,   // 菜单光标移动（交给 demo_navigation）
    ROUTE_MENU_ENTER,  // 进入选中项（交给 demo_navigation 判定可用性）
    ROUTE_DEMO_KEY,    // 转发给当前页
    ROUTE_DEMO_EXIT,   // 退出当前页回菜单
} route_action_t;

route_action_t page_router_resolve(page_id_t page, demo_nav_input_t input);
