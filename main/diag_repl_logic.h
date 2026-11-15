// main/diag_repl_logic.h —— REPL 命令参数的纯逻辑解析。
//
// 不 include ESP-IDF/LVGL，主机测试可以直接链接本文件。真正的命令实现和
// USB Serial/JTAG 接线在 diag_repl.c。
#pragma once

#include <stdbool.h>

#include "page_router.h"
#include "usage_source.h"

// 解析 page 命令的单个参数。只接受 "0"、"1"、"2"（Status/Weather/Usage）。
bool diag_page_arg_parse(const char *arg, page_id_t *out);

// 解析 net refresh 的可选参数：
//   NULL / "all" -> weather 和 usage 都为 true
//   "weather"    -> 只选天气
//   "usage"      -> 只选用量
// 其他值返回 false。weather/usage 不得为 NULL。
bool diag_net_arg_parse(const char *arg, bool *weather, bool *usage);

// usage_status_t 的 ASCII 诊断名。未知值返回 "unknown"。
const char *diag_usage_status_text(usage_status_t status);
