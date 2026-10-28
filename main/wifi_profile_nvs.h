// main/wifi_profile_nvs.h —— wifi_kv_t 的 NVS 实现。
//
// 逻辑层（wifi_profile.c）不认识 NVS；这一层负责 namespace、句柄与 esp_err_t 到
// wifi_kv_result_t 的翻译。命名空间取 "wprof"，与 NVS 里其他数据互不干扰。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "nvs.h"

#include "wifi_profile.h"

#define WIFI_PROFILE_NVS_NAMESPACE "wprof"

typedef struct {
    nvs_handle_t handle;
    bool         opened;
} wifi_profile_nvs_t;

// 初始化 NVS 分区并打开 namespace。
//
// 分区处于 ESP_ERR_NVS_NO_FREE_PAGES 或 ESP_ERR_NVS_NEW_VERSION_FOUND 时**不擦除**：
// 擦掉会连带清掉别的组件可能保存的数据（沿用 main/demo_radio.c 的既定策略）。
// 调用方拿到错误后应退化为"没有 profile"，而不是清空分区重来。
esp_err_t wifi_profile_nvs_open(wifi_profile_nvs_t *nv);

// 返回可注入 wifi_profile_store_load() 的 vtable，ctx 指向 nv 本身。
wifi_kv_t wifi_profile_nvs_kv(wifi_profile_nvs_t *nv);

void wifi_profile_nvs_close(wifi_profile_nvs_t *nv);
