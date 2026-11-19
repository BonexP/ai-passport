// main/page_quote.h —— 行情页：8 行自选标的的现价、涨跌幅、涨跌额、成交额。
//
// 与 page_usage 同构：三个入口都要求调用方持有 LVGL 锁；页面只读 app_data 快照，
// 不发起网络请求，刷新由 main.c 转给 net_worker，按键路径保持非阻塞。
// 每个标的占两行：上行「名称 现价 涨跌幅」，下行「涨跌额 成交额」，
// 这样在 240x320 的窄屏上 5 个字段都能用可读字号显示。
#pragma once

void page_quote_build(void);   // 建页并立刻画一帧
void page_quote_destroy(void); // 幂等：先删定时器再删对象树
void page_quote_refresh(void); // 强制重画一次
