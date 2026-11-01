// main/json_field.h —— 已知报文形状的最小 JSON 字段提取。
//
// 只做一件事：给一个 JSON 文档和一条点分路径（如 "current.temperature_2m"），取出
// 字符串或数字。不建树、不 malloc、不做无界递归，因此可以在 worker 任务里对一条
// 有界响应体反复调用，而不会在失败路径上留下需要释放的东西。
//
// 路径规则：
//   - 点分对象路径，最多 JSON_FIELD_DEPTH_MAX 段，总长度小于 JSON_FIELD_PATH_MAX；
//   - 只沿对象成员下降，不支持数组下标：数组整体视为不透明值被跳过；
//   - 同名键重复时取第一处，且不回溯——第一处子树里没有更深的键，就算失败；
//   - 路径段是字面量，不支持通配，也不支持转义的点号。
//
// 校验范围（刻意的取舍）：只校验"路径经过的对象"与"命中的那个值"，不校验整个文档。
// 因此 `{"a":1` 这种缺右括号的报文仍能取出 a；残缺报文应在 HTTP 层就被长度或解析
// 前置检查拦住，这里不为它重复付一遍扫描成本。
//
// 失败语义：返回 false，字符串输出缓冲被清空（cap > 0 时绝不留半截值），
// 数字输出保持调用方原值。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 路径最大段数。天气与行情报文最深不过三层，四段留一层余量；再多说明这个接口
// 不适合本模块，应该换端而不是加深解析器。
#define JSON_FIELD_DEPTH_MAX 4

// 路径字符串上限（含 NUL）。超出直接判失败，避免调用方传进来一条异常长的路径。
#define JSON_FIELD_PATH_MAX 64

// 取字符串字段，按 JSON 转义规则解码后写入 out（UTF-8，含结尾 NUL）。
// 返回 true 表示找到且放得下；放不下按失败处理，不做截断——半截 SSID 或城市名
// 比没有更糟。doc/path/out 任一为 NULL 或 cap == 0 时返回 false 并保持安全。
bool json_field_string(const char *doc, const char *path, char *out, size_t cap);

// 取数字字段。只接受 JSON 数字字面量（不接受 "12" 这样的字符串数字），
// 转换用 strtod，因此 12.34 会按 double 的最接近值返回。
// 返回 false 时不改写 *out；out 为 NULL 时返回 false。
bool json_field_number(const char *doc, const char *path, double *out);
