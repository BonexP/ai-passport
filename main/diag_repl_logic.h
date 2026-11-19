// main/diag_repl_logic.h —— REPL 命令参数的纯逻辑解析与输出格式化。
//
// 不 include ESP-IDF/LVGL，主机测试可以直接链接本文件。真正的命令实现和
// USB Serial/JTAG 接线在 diag_repl.c。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "page_router.h"
#include "usage_source.h"
#include "wifi_profile.h"

// 解析 page 命令的单个参数。只接受 "0"、"1"、"2"、"3"
// （Status/Weather/Usage/Quote）。
bool diag_page_arg_parse(const char *arg, page_id_t *out);

// 解析 net refresh 的可选参数：
//   NULL / "all" -> weather 和 usage 都为 true
//   "weather"    -> 只选天气
//   "usage"      -> 只选用量
// 其他值返回 false。weather/usage 不得为 NULL。
bool diag_net_arg_parse(const char *arg, bool *weather, bool *usage);

// usage_status_t 的 ASCII 诊断名。未知值返回 "unknown"。
const char *diag_usage_status_text(usage_status_t status);

// ---- export 命令（从 NVS 导出已保存的 Wi-Fi profile，机器可解析文本）----

// 单行 export 输出的上界。最坏情况每个字段字节都被转义成 4 字节：
// name(23) + ssid(32) + password(64) + place(23) 各 4 倍 ≈ 568，再加固定标签，
// 768 留出充足余量。用于调用方开栈上缓冲。
#define DIAG_EXPORT_LINE_MAX 768u

// 密码被隐藏时的哨兵值。只在非 --secrets 输出中出现，脚本据此区分。
#define DIAG_EXPORT_REDACTED "REDACTED"

// 解析 export 的参数：
//   "export"           -> reveal_secrets = false
//   "export --secrets" -> reveal_secrets = true
// 其它/多余参数返回 false。argv[0] 约定为命令名 "export"，不参与判断；
// reveal_secrets 不得为 NULL。
bool diag_export_arg_parse(int argc, char **argv, bool *reveal_secrets);

// 把一条 profile 格式化成单行 CSV 文本（不含结尾换行）：
//   WIFIPROF,<index>|<name>|<ssid>|<password>|<has_location>|<lat>|<lon>|<place>
// password 默认写 DIAG_EXPORT_REDACTED，reveal_secrets 为真时写真口令。
// 无地点时 has_location=0，lat/lon/place 写空字段；否则 lat/lon 为 4 位小数度。
// '\\'、'|'、CR/LF 与其它控制字节用反斜杠转义，控制字节写成 \xHH，保证单行 ASCII。
// 返回写入字节数（不含 NUL）；缓冲不足返回 0 且缓冲保持空串（不写半个字段）。
size_t diag_export_line(const wifi_profile_t *p, uint8_t index, bool reveal_secrets,
                        char *buf, size_t cap);

// 起始标记 WIFIPROF_BEGIN,<count>，返回写入字节数；缓冲不足返回 0。
size_t diag_export_header(uint8_t count, char *buf, size_t cap);

// 结束标记 WIFIPROF_END，返回写入字节数；缓冲不足返回 0。
size_t diag_export_footer(char *buf, size_t cap);

// --secrets 模式的警示行 WIFIPROF_WARNING,secrets=revealed；缓冲不足返回 0。
size_t diag_export_warning(char *buf, size_t cap);
