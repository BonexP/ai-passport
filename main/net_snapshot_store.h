// main/net_snapshot_store.h —— 离线快照的 NVS 存取（设备侧）。
//
// 与 profile 分开一个 namespace（"net"），避免两类数据的键混在一起。
// 所有函数都会阻塞 flash I/O，只能在 net worker 任务里调用，绝不能在 LVGL 任务里用。
#pragma once

#include <stdbool.h>

#include "app_data.h"
#include "esp_err.h"
#include "nvs.h"

typedef struct {
    nvs_handle_t handle;
    bool         open;
} net_snapshot_store_t;

// 打开 namespace。失败时 s->open = false，调用方应继续运行（只是没有离线快照）。
esp_err_t net_snapshot_store_open(net_snapshot_store_t *s);

// 幂等关闭。
void net_snapshot_store_close(net_snapshot_store_t *s);

// 写入一条快照。未打开或编码失败返回错误；成功后已 commit。
esp_err_t net_snapshot_store_save(net_snapshot_store_t *s, const weather_state_t *w);

// 读取快照。ESP_ERR_NOT_FOUND 表示还没有存过；blob 损坏返回 ESP_ERR_INVALID_*
// 或 ESP_ERR_NVS_INVALID_LENGTH，调用方按"没有快照"处理即可。
esp_err_t net_snapshot_store_load(net_snapshot_store_t *s, weather_state_t *out);
