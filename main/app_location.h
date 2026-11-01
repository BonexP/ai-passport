// main/app_location.h —— 地点模型：是否配置、微度坐标、地点名。
//
// 纯逻辑：不 include 任何 ESP-IDF / LVGL 头文件，可在主机上把解析与格式化的边界
// 全部走一遍。profile、配网表单、天气 URL 组装共用这一份坐标规则，避免三处各写一遍
// "多少度算合法"。
//
// 为什么坐标用整数微度而不是 double：
//   1. 固件里的 URL 拼装不需要浮点格式化，省掉 newlib 浮点支持的依赖与体积；
//   2. 记录编解码是字节布局，整数没有端序之外的坑；
//   3. 微度精度约 0.11 米，对天气与"哪个城市"完全够用。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 地点名上限（含结尾 NUL）。24 字节够放 "Beijing Chaoyang" 这类英文名；
// 中文地点名按 UTF-8 存（每字 3 字节），本阶段 UI 仍只保证 ASCII 字形。
#define APP_LOCATION_PLACE_MAX 24

// 合法范围（微度）。纬度 ±90°，经度 ±180°。
#define APP_LOCATION_LAT_UDEG_MAX 90000000
#define APP_LOCATION_LON_UDEG_MAX 180000000

// 一条地点。has_location == false 时坐标与地点名都无意义：
// 天气页显示 Location not set，worker 不发请求。
typedef struct {
    bool    has_location;
    int32_t lat_udeg; // -90000000..90000000
    int32_t lon_udeg; // -180000000..180000000
    char    place[APP_LOCATION_PLACE_MAX]; // 从未配置时为空串，永不为 NULL
} app_location_t;

// 清零。loc 为 NULL 时无操作。
void app_location_clear(app_location_t *loc);

// 填充并校验。place 可为 NULL 或空串。
// 返回 false 时 loc 已被清空（保持"未配置"），不会留下半条地点。
// 失败原因：坐标越界、place 超长、place 含控制字符。
bool app_location_set(app_location_t *loc, int32_t lat_udeg, int32_t lon_udeg, const char *place);

// 结构体是否自洽：未配置时 place 必须为空；已配置时坐标在范围内、place 合法。
// 编解码与 store 的入口都用它兜底，防止内存里被写坏的记录落盘。
bool app_location_is_valid(const app_location_t *loc);

// 解析十进制经纬度文本（如 "39.9042"、"-73.935242"），输出微度。
// 规则：允许首尾空白与正负号；整数部分最多 9 位；小数最多取前 6 位，多余位忽略；
// 必须有整数位；不允许指数、逗号、内部空白。"12." 与 ".5" 都算非法。
// limit_udeg 由调用方给（纬度 90000000、经度 180000000），越界返回 false。
// 成功返回 true 并写 *out_udeg；失败不改写 *out_udeg。
bool app_location_parse_coord(const char *text, int32_t limit_udeg, int32_t *out_udeg);

// 把微度格式化为 4 位小数的十进制度文本（URL 与屏幕共用），如 39904200 → "39.9042"。
// 超出的精度截断（不四舍五入）：天气坐标不需要更高精度。
// 缓冲不足返回 false 并把 buf 置为空串。
bool app_location_format_4dp(int32_t udeg, char *buf, size_t cap);
