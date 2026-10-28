// main/wifi_profile_service.h —— profile 管理器的设备侧入口。
//
// 负责把纯逻辑的 store 与状态机接到真实的 NVS 和 Wi-Fi 驱动上：一个 worker 任务
// 独占状态机，esp_event 回调只往队列里塞事件。页面（后续的 UI 轮次）只读状态，
// 不参与连接决策。
//
// 本轮刻意不从 app_main 调用：还没有 UI 能观察它，而改动开机行为又无法在真机上
// 验证。函数进固件编译即可证明对真实 IDF 头文件的类型正确性。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// 建栈并开始第一轮遍历。可重复调用，已在运行时返回 ESP_ERR_INVALID_STATE。
esp_err_t wifi_profile_service_start(void);

// 停掉 worker、断开并拆掉 Wi-Fi 栈、关闭 NVS 句柄。未运行时是空操作。
void wifi_profile_service_stop(void);

bool wifi_profile_service_running(void);
