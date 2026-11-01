// main/net_snapshot.h —— 离线快照的版本化字节编解码（纯逻辑）。
//
// 只序列化"页面上真正会显示的东西"：温度、天气码、观测时间、地点、抓取时刻。
// 不序列化 seq / fetched_uptime_ms —— 那两个是本次开机内的运行时状态，
// 从磁盘读回来的快照一律标记 stale，由页面显示 Offline - saved。
//
// 版本号与长度自洽校验都在 decode 里；坏 blob 只丢快照，不影响配网与连接。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_data.h"

// 单条快照的上界：头 12 字节 + place 23 + observed 19。
#define NET_SNAPSHOT_MAX 64

// 版本。字段布局变化必须递增；旧版本按"没有快照"处理，不猜。
#define NET_SNAPSHOT_VERSION 1u

// 落盘节流窗口：内容没变时最多每 30 分钟写一次。
#define NET_SNAPSHOT_SAVE_INTERVAL_MS (30 * 60 * 1000)

// 现在该不该落盘。抽成纯函数是为了让"内容变化优先、否则等窗口"这条规则
// 能在主机上断言，而不是埋在 worker 的 if 里。
//   saved_valid  本次开机是否已经成功写过
//   changed      与上次落盘的内容相比是否有变化
//   last_save_ms 上次成功落盘的开机时刻；-1 表示本次开机还没写过
bool net_snapshot_save_due(bool saved_valid, bool changed, int64_t last_save_ms, int64_t now_ms);

// 编码。要求 w->has_data 为真；缓冲不足或参数非法返回 0。
size_t net_snapshot_encode(const weather_state_t *w, uint8_t *buf, size_t cap);

// 解码。成功时 has_data 来自 blob，stale = true，fetched_uptime_ms = -1，seq = 0。
// 版本不符、长度不自洽、字段超限都返回 false。
bool net_snapshot_decode(const uint8_t *buf, size_t len, weather_state_t *out);
