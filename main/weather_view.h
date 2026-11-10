// main/weather_view.h —— 天气页该显示什么（纯逻辑）。
//
// 页面本身只负责把这里算出来的文字贴到 label 上。之所以把判断抽出来：
//   1. "离线时也要显示旧数据"这条语义容易写错——把状态文案当成唯一真相，
//      就会在没网时把温度藏起来；
//   2. 这些分支能在主机上逐格断言，不需要屏幕。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WEATHER_VIEW_DATA = 0,  // 有数据：温度、天气码、观测时间都显示
    WEATHER_VIEW_NO_NETWORK,
    WEATHER_VIEW_NO_LOCATION,
    WEATHER_VIEW_WAIT_TIME,
    WEATHER_VIEW_UPDATING, // 首次请求还没回来
    WEATHER_VIEW_RETRYING, // 等得太久仍无数据
} weather_view_state_t;

// 离线时有旧数据就显示（页面用 "Offline - saved" 标明来源），没网、没校时都不该
// 把已经取到的温度藏起来。但"连上了一个没配地点的网络"是例外：快照不记归属，
// 这种网络的正确终态是 Location not set，不能回放别的网络的数据。
weather_view_state_t weather_view_state(bool connected, bool located, bool time_valid,
                                        bool has_data, bool waited_long);

// 主卡在没有数据时显示的一句话；有数据时返回 NULL。
const char *weather_view_message(weather_view_state_t state);

// 顶部地点行。有数据时优先用快照里的地点（离线时它才是数据的归属），
// 其次当前地点、网络名；没有数据时退回状态文案。
const char *weather_view_place(weather_view_state_t state, const char *saved_place,
                               const char *location_place, const char *ssid);

// 更新行。返回 false 表示这一行不显示。
// stale 用快照时间（UTC）说明"什么时候存的"；否则用本次开机内的相对时间。
bool weather_view_updated(weather_view_state_t state, bool stale, int64_t age_ms,
                          int32_t fetched_epoch, char *buf, size_t cap);

// epoch 秒 → "YYYY-MM-DD HH:MMZ"（UTC）。epoch <= 0 或缓冲不足返回 false。
bool weather_view_format_utc(int32_t epoch, char *buf, size_t cap);
