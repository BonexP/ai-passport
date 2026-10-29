// main/wifi_provision_form.h —— 配网表单的解析与校验（纯逻辑）。
//
// 不 include 任何 ESP-IDF 头文件：HTTP 胶水只负责把 Content-Length 与正文喂进来，
// 其余判断都在这里，因此可以在主机上把畸形输入一条条试过去。
//
// 输入纪律（与 docs/reference/phoenixzhc/softap-provisioning-and-resource-budget
// 的要求一致）：先判声明长度是否超上限，收够了才解码，解码后再按字段上限二次校验。
// 任何一步不满足都返回明确的错误码，而不是"尽力而为地截断"——截断会把一个写错的
// 密码变成一条看起来合法的记录。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "wifi_profile.h"

// 正文上限。表单只有两个字段，1024 字节足够放下 32 字节 SSID 与 64 字节密码的
// 任何百分号编码形式（最坏 3 倍膨胀）。
#define WIFI_PROV_BODY_MAX 1024u

// 表单字段名。页面与解析器共用这两个常量，避免两边写错。
#define WIFI_PROV_FIELD_SSID "ssid"
#define WIFI_PROV_FIELD_PASS "pass"

typedef enum {
    WIFI_FORM_OK = 0,
    WIFI_FORM_ERR_MISSING_SSID, // 没有 ssid 这个键
    WIFI_FORM_ERR_EMPTY_SSID,   // 有键但值为空
    WIFI_FORM_ERR_TOO_LONG,     // 解码后超出 SSID 32 / 密码 64
    WIFI_FORM_ERR_BAD_ESCAPE,   // %XX 不完整或不是十六进制
    WIFI_FORM_ERR_NUL,          // 解码出 '\0'（C 字符串装不下）
    WIFI_FORM_ERR_DUPLICATE,    // 同一个键出现两次
    WIFI_FORM_ERR_NO_FIELD,     // 某一段没有 '='
} wifi_form_result_t;

typedef struct {
    wifi_profile_t profile;      // 已通过 wifi_profile_set_fields 校验
    bool           has_password; // false = 开放网络
} wifi_provision_form_t;

typedef enum {
    WIFI_CL_OK = 0,
    WIFI_CL_MISSING,    // 头不存在或为空串
    WIFI_CL_NOT_NUMBER, // 含符号、空格或字母
    WIFI_CL_TOO_LARGE,  // 超出上限
} wifi_content_length_t;

// 解析 application/x-www-form-urlencoded 正文（不是以 NUL 结尾的，长度单独给）。
// 只认 ssid 与 pass 两个键；其他键连同其值一起忽略（值也不解码）。
// 失败时不保证 out 被清零以外的任何状态——调用方应只看返回值。
wifi_form_result_t wifi_provision_form_parse(const char *body, size_t len,
                                             wifi_provision_form_t *out);

// 解析 Content-Length 头的值。max 是允许的最大正文长度（通常 WIFI_PROV_BODY_MAX）。
// 手写累乘而不是 atoi/strtoul：前者会把 "-1" 当 -1、后者会在超长数字上溢出。
wifi_content_length_t wifi_provision_content_length(const char *text, size_t max, size_t *out);

// 错误码 → 页面上的一句话（ASCII）。未知码返回通用文案，永不为 NULL。
const char *wifi_provision_form_error_text(wifi_form_result_t result);
