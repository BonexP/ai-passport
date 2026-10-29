// main/page_status.h —— 开机首屏：Wi-Fi 连接状态。
//
// 三个入口都要求调用方持有 LVGL 锁；内部的定时器跑在 LVGL 任务里，本身已持锁，
// 因此回调里不再加锁。
#pragma once

#include "lvgl.h"

void status_page_build(void);   // 建页并立刻画一帧
void status_page_destroy(void); // 幂等：先删定时器再删对象树
void status_page_refresh(void); // 强制重画一次
