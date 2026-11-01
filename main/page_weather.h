// main/page_weather.h —— 天气页：地点、温度、天气状况、观测与更新时刻。
//
// 三个入口都要求调用方持有 LVGL 锁；内部的定时器跑在 LVGL 任务里，本身已持锁，
// 因此回调里不再加锁。页面只读快照，从不发起网络请求——刷新由 main.c 转给
// net_worker，按键路径保持非阻塞。
#pragma once

void page_weather_build(void);   // 建页并立刻画一帧
void page_weather_destroy(void); // 幂等：先删定时器再删对象树
void page_weather_refresh(void); // 强制重画一次
