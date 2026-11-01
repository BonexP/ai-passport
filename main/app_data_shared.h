// main/app_data_shared.h —— 设备上唯一的网络数据快照总线。
//
// worker 发布、天气页读取，中间只隔一个 portMUX 临界区。放在单独文件里是为了让
// 纯逻辑的 app_data.c 完全不认识 portMUX —— 主机测试用计数器 vtable，设备上用
// 这里的中断临界区。
#pragma once

#include "app_data.h"

// 永不为 NULL。静态初始化，不做惰性初始化：发布者与读取者来自不同任务，
// "第一次调用顺便初始化"会引入一次竞态。
app_data_bus_t *app_data_shared(void);
