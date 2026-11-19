// main/quote_view.h —— 行情页该显示什么（纯逻辑）。
//
// 页面只负责把这里算出来的文字贴到 label 上。与 usage_view / weather_view 同一套理由：
// "卡片为什么是空的"本身就是这个页面的主要用途之一，状态机决定用户看到的是
// "没联网"还是"等校时"，而这些分支都能在主机上逐格断言，不需要屏幕。
//
// 三条规则，都写进了测试：
//   1. 失败不清空数据。有数据时 quote_view_state 永远返回 DATA；最近一次请求
//      失败只由状态行补充说明，已取到的行情照常显示（同 usage_view 第 1 条规则）。
//   2. 所有函数不 include ESP-IDF / LVGL，可在主机编译（同 quote_source）。
//   3. 文案全 ASCII：默认字体 Montserrat 没有中文字形，中文只出现在源码注释里
//      （见 docs/development/engineering/coding-conventions.md 的字体清单）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "quote_source.h"

typedef enum {
    QUOTE_VIEW_DATA = 0,    // 有数据
    QUOTE_VIEW_NO_NET,      // 没联网
    QUOTE_VIEW_WAIT_TIME,   // 有网但还没校时
    QUOTE_VIEW_UPDATING,    // 首次请求还没回来
    QUOTE_VIEW_RETRYING,    // 等太久仍无数据
    QUOTE_VIEW_BAD_BODY,    // 报文解析失败
} quote_view_state_t;

// 判断优先级顺序：有数据→DATA；没网→NO_NET；没校时→WAIT_TIME；等太久→RETRYING；
// 否则 UPDATING。
// connected: Wi-Fi 已连接；time_valid: SNTP 已同步；has_data: 总线上有无成功观测；
// waited_long: 等待超阈值。
// 注意这里没有 status 参数：BAD_BODY 由调用方在解析失败时直接传给
// quote_view_message / quote_view_status_line。
quote_view_state_t quote_view_state(bool connected, bool time_valid, bool has_data,
                                    bool waited_long);

// 无数据时的大字占位文案；DATA 返回 NULL。文案用英文 ASCII：
// "No network" / "Waiting for time" / "Updating..." / "No data - retrying" / "Bad data"。
const char *quote_view_message(quote_view_state_t state);

// 格式化一行行情，输出形如 "NVDA  156.1400  -4.12%  -6.7100  $52.6B" 的可读串。
// item 必须 item->valid，否则返回 false。价格、涨跌幅、成交额分别交给
// quote_format_price / quote_format_pct / quote_format_amount；涨跌额本身就是
// 带符号的定点数，用 quote_format_price 格式化。code 是接口代码（如 "usNVDA" /
// "sh000001"），传给 quote_format_amount 判断单位；label 用 item->label。
// 缓冲不足返回 false 并置 buf 为空串。
bool quote_view_row(const quote_item_t *item, const char *code, char *buf, size_t cap);

// 状态行：有数据时报告"多久前更新"（age_ms<0 只报前缀不编数字；分钟/小时格式
// 模仿 weather_view_updated），没有数据时就是 quote_view_message(state) 的文案。
// fetched_epoch 目前可忽略（行情不做持久化）；status 保留在签名里供页面统一调用。
bool quote_view_status_line(quote_view_state_t state, quote_status_t status,
                            int64_t age_ms, int32_t fetched_epoch, char *buf, size_t cap);
