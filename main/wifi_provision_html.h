// main/wifi_provision_html.h —— 配网网页的静态表单页与结果页生成（纯逻辑）。
//
// 表单页是 .rodata 里的常量，零 RAM；结果页按需生成到调用方的缓冲里，且**要么整页
// 写进去、要么一个字节都不写**——半截的 HTML 在手机上会渲染成一片空白，比错误页更糟。
#pragma once

#include <stddef.h>

#include "wifi_provision_form.h"

// 静态表单页。返回指针，长度（不含 NUL）写进 *len（可为 NULL）。
// 内容是纯 ASCII、< 2048 字节、不含任何凭据。
const char *wifi_provision_form_page(size_t *len);

// 提交表单的目标路径，页面与 httpd 注册必须一致。
#define WIFI_PROV_SAVE_PATH "/save"

// 成功页：告诉用户"已保存，设备将重启"。
size_t wifi_provision_html_saved(char *buf, size_t cap, const char *ssid);

// 失败页：headline + 具体原因 + （可选）回填的 SSID + 返回链接。
// result 为 WIFI_FORM_OK 时按通用错误处理。
size_t wifi_provision_html_error(char *buf, size_t cap, wifi_form_result_t result,
                                 const char *ssid);

// 把字符串里的 & < > " ' 换成实体，供上面两个函数内部使用；暴露出来便于单独测试。
size_t wifi_provision_html_escape(char *buf, size_t cap, const char *text);
