// main/weather_source.h —— Open-Meteo 请求与响应解析（纯逻辑）。
//
// 只认识一种报文形状：/v1/forecast?current=temperature_2m,weather_code&timezone=auto。
// 不做通用 JSON 树，不猜字段，不重试——重试由 net_schedule 决定。
//
// 所有函数都不 include ESP-IDF / LVGL，可在主机上用抓取到的真实报文测试。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_data.h"
#include "app_location.h"

// 最长 URL 的实际长度约 130 字节；256 给坐标与未来参数留余量。
#define WEATHER_URL_MAX 256

// 取值边界。超出这些范围说明报文不是天气、单位变了，或者字段被别的接口复用：
// 宁可当作解析失败保留旧数据，也不要把 999 度画到屏幕上。
#define WEATHER_TEMP_LIMIT_DC 1000 // ±100.0 °C
#define WEATHER_CODE_MAX      99   // WMO 4677 在本接口用到的最大码

// 组装当前天气请求。loc 必须 has_location，否则返回 false（天气页显示未配置）。
// 成功的 URL 不含密钥，可以安全写日志。缓冲不足返回 false 并把 buf 置空。
bool weather_url_build(const app_location_t *loc, char *buf, size_t cap);

// WMO 天气码 → ASCII 短语。未知码返回 "Unknown"，永不为 NULL。
const char *weather_code_text(int code);

// 十分之一摄氏度 → "12.3" / "-0.5"。缓冲不足返回 false 并把 buf 置空。
bool weather_format_temp(int16_t temp_dc, char *buf, size_t cap);

// 接口返回的 ISO 本地时间 → 只保留 "HH:MM"，如 "2026-11-01T14:00" → "14:00"。
// 找不到 'T'、时:分不是数字或缓冲不足都返回 false（buf 清空）。
bool weather_format_observed(const char *iso, char *buf, size_t cap);

// 解析 Open-Meteo 的响应到 out。成功时 out->has_data = true、stale = false，
// place 清空（由调用方按当前 profile 填）。缺温度或天气码、类型不符、报文损坏
// 都返回 false，且 out 内容未定义（调用方应只在 true 时使用）。
//
// 观测时间取接口返回的 current.time（已按 timezone=auto 本地化），原样显示；
// 缺失不算失败，只是不显示观测时间。
bool weather_parse_current(const char *json, weather_state_t *out);
