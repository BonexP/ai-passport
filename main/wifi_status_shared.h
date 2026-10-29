// main/wifi_status_shared.h —— 设备上唯一的 Wi-Fi 状态快照总线。
//
// service 发布、状态页读取，中间只隔一个 portMUX 临界区。放在单独文件里是为了让
// 纯逻辑的 wifi_status.c 完全不认识 portMUX —— 主机测试用计数器 vtable，设备上用
// 这里的中断临界区。
#pragma once

#include "wifi_status.h"

// 永不为 NULL。静态初始化，不做惰性初始化：发布者与读取者来自不同任务，
// "第一次调用顺便初始化"会引入一次竞态。
wifi_status_bus_t *wifi_status_shared(void);
