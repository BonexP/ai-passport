// main/wifi_provision_form.c —— 表单正文与 Content-Length 的解析。
#include "wifi_provision_form.h"

#include <stdint.h>
#include <string.h>

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 就地解码一段值：'+' 变空格，%XX 变字节。
// dst_cap 是缓冲总容量（含结尾 NUL），因此最多写 dst_cap-1 个字符。
static wifi_form_result_t decode_value(const char *src, size_t len, char *dst, size_t dst_cap) {
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        char ch;
        if (src[i] == '+') {
            ch = ' ';
        } else if (src[i] == '%') {
            // 需要两个字符的余量；i+2 必须落在 [0, len-1]。
            if (i + 2 >= len) return WIFI_FORM_ERR_BAD_ESCAPE;
            int hi = hex_digit(src[i + 1]);
            int lo = hex_digit(src[i + 2]);
            if (hi < 0 || lo < 0) return WIFI_FORM_ERR_BAD_ESCAPE;
            unsigned value = (unsigned)(hi * 16 + lo);
            // C 字符串装不下 NUL；静默截断会让"密码里带 %00"变成另一条密码。
            if (value == 0) return WIFI_FORM_ERR_NUL;
            ch = (char)value;
            i += 2;
        } else {
            if (src[i] == '\0') return WIFI_FORM_ERR_NUL;
            ch = src[i];
        }
        if (o + 1 >= dst_cap) return WIFI_FORM_ERR_TOO_LONG;
        dst[o] = ch;
        o++;
    }
    dst[o] = '\0';
    return WIFI_FORM_OK;
}

wifi_form_result_t wifi_provision_form_parse(const char *body, size_t len,
                                             wifi_provision_form_t *out) {
    if (out == NULL) return WIFI_FORM_ERR_NO_FIELD;
    memset(out, 0, sizeof *out);
    if (body == NULL || len == 0) return WIFI_FORM_ERR_MISSING_SSID;

    char ssid[WIFI_PROFILE_SSID_MAX];
    char pass[WIFI_PROFILE_PASS_MAX];
    ssid[0] = '\0';
    pass[0] = '\0';
    bool seen_ssid = false;
    bool seen_pass = false;

    size_t pos = 0;
    for (;;) {
        size_t end = pos;
        while (end < len && body[end] != '&') end++;

        size_t pair_len = end - pos;
        if (pair_len > 0) {
            size_t eq = pos;
            while (eq < end && body[eq] != '=') eq++;
            if (eq >= end) return WIFI_FORM_ERR_NO_FIELD; // 没有 '=' 的裸片段

            size_t key_len = eq - pos;
            const char *value = body + eq + 1;
            size_t value_len = end - eq - 1;

            if (key_len == 4 && memcmp(body + pos, WIFI_PROV_FIELD_SSID, 4) == 0) {
                if (seen_ssid) return WIFI_FORM_ERR_DUPLICATE;
                wifi_form_result_t r = decode_value(value, value_len, ssid, sizeof ssid);
                if (r != WIFI_FORM_OK) return r;
                seen_ssid = true;
            } else if (key_len == 4 && memcmp(body + pos, WIFI_PROV_FIELD_PASS, 4) == 0) {
                if (seen_pass) return WIFI_FORM_ERR_DUPLICATE;
                wifi_form_result_t r = decode_value(value, value_len, pass, sizeof pass);
                if (r != WIFI_FORM_OK) return r;
                seen_pass = true;
            }
            // 其他键：连值都不解码，直接跳过。前端多带一个字段不该让整次配网失败。
        }

        if (end >= len) break;
        pos = end + 1;
    }

    if (!seen_ssid) return WIFI_FORM_ERR_MISSING_SSID;
    if (ssid[0] == '\0') return WIFI_FORM_ERR_EMPTY_SSID;

    // 复用 store 的字段校验，规则只维护一份（SSID 1..32、密码 <=64）。
    if (!wifi_profile_set_fields(&out->profile, NULL, ssid, seen_pass ? pass : NULL)) {
        return WIFI_FORM_ERR_TOO_LONG;
    }
    out->has_password = seen_pass && pass[0] != '\0';
    return WIFI_FORM_OK;
}

wifi_content_length_t wifi_provision_content_length(const char *text, size_t max, size_t *out) {
    if (text == NULL || text[0] == '\0') return WIFI_CL_MISSING;

    size_t value = 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        // 只接受纯数字：'-'、'+'、空格、前导 'x' 一律拒绝，不做宽松解析。
        if (c < '0' || c > '9') return WIFI_CL_NOT_NUMBER;
        if (value > (SIZE_MAX - 9u) / 10u) return WIFI_CL_TOO_LARGE; // 溢出门槛
        value = value * 10u + (size_t)(c - '0');
        if (value > max) return WIFI_CL_TOO_LARGE;
    }
    if (out != NULL) *out = value;
    return WIFI_CL_OK;
}

const char *wifi_provision_form_error_text(wifi_form_result_t result) {
    switch (result) {
    case WIFI_FORM_OK:               return "";
    case WIFI_FORM_ERR_MISSING_SSID: return "Network name is required";
    case WIFI_FORM_ERR_EMPTY_SSID:   return "Network name cannot be empty";
    case WIFI_FORM_ERR_TOO_LONG:     return "Name or password is too long";
    case WIFI_FORM_ERR_BAD_ESCAPE:   return "Malformed form data";
    case WIFI_FORM_ERR_NUL:          return "Invalid character in form data";
    case WIFI_FORM_ERR_DUPLICATE:    return "A field was sent twice";
    case WIFI_FORM_ERR_NO_FIELD:     return "Malformed form data";
    }
    return "Malformed form data";
}
