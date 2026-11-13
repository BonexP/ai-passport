// main/page_usage.h —— AMD 用量页：剩余点数、已用额度、重置时刻、今日计数。
//
// 三个入口都要求调用方持有 LVGL 锁；内部的定时器跑在 LVGL 任务里，本身已持锁，
// 因此回调里不再加锁。页面只读 app_data 快照，从不发起网络请求——刷新由 main.c
// 转给 net_worker，按键路径保持非阻塞。
//
// 与天气页的关键差异：这一页所有文案都由 usage_view（纯逻辑、有主机测试）算出来，
// 本文件只负责把它们贴到 label 上，不做任何判断。判断散在页面里是这类"空卡片到底
// 该说什么"最容易出错的地方。
#pragma once

void page_usage_build(void);   // 建页并立刻画一帧
void page_usage_destroy(void); // 幂等：先删定时器再删对象树
void page_usage_refresh(void); // 强制重画一次
