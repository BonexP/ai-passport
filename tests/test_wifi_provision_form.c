// tests/test_wifi_provision_form.c —— 表单正文与 Content-Length 的 host 测试。
//
// 这里全是对着畸形输入写的用例：配网页的输入来自局域网里任何一个能做 HTTP POST 的
// 东西，不能假设它是我们的页面发出来的。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_provision_form.h"

static wifi_form_result_t parse(const char *body, wifi_provision_form_t *out) {
    return wifi_provision_form_parse(body, strlen(body), out);
}

static void test_normal_submit(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=HomeNet&pass=secret123", &f) == WIFI_FORM_OK);
    assert(strcmp(f.profile.ssid, "HomeNet") == 0);
    assert(strcmp(f.profile.password, "secret123") == 0);
    assert(f.has_password == true);
}

static void test_missing_ssid(void) {
    wifi_provision_form_t f;
    assert(parse("", &f) == WIFI_FORM_ERR_MISSING_SSID);
    assert(parse("pass=secret", &f) == WIFI_FORM_ERR_MISSING_SSID);
}

static void test_empty_ssid(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=&pass=secret", &f) == WIFI_FORM_ERR_EMPTY_SSID);
}

static void test_password_is_optional(void) {
    wifi_provision_form_t f;
    // 完全不带 pass 键 = 开放网络
    assert(parse("ssid=OpenNet", &f) == WIFI_FORM_OK);
    assert(f.has_password == false);
    assert(f.profile.password[0] == '\0');

    // 带空值同样按开放网络处理
    assert(parse("ssid=OpenNet&pass=", &f) == WIFI_FORM_OK);
    assert(f.has_password == false);
}

static void test_plus_decodes_to_space(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=My+Home+Net&pass=a+b", &f) == WIFI_FORM_OK);
    assert(strcmp(f.profile.ssid, "My Home Net") == 0);
    assert(strcmp(f.profile.password, "a b") == 0);
}

static void test_percent_decoding(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=%41%42%43&pass=%2b%2B%20", &f) == WIFI_FORM_OK);
    assert(strcmp(f.profile.ssid, "ABC") == 0);
    // 小写与大写的十六进制都要认；%20 是空格。
    assert(strcmp(f.profile.password, "++ ") == 0);
}

static void test_bad_escapes_are_rejected(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=a%2", &f) == WIFI_FORM_ERR_BAD_ESCAPE);   // 少一位
    assert(parse("ssid=a%", &f) == WIFI_FORM_ERR_BAD_ESCAPE);    // 结尾孤立
    assert(parse("ssid=a%ZZ", &f) == WIFI_FORM_ERR_BAD_ESCAPE);  // 非十六进制
}

static void test_encoded_nul_is_rejected_not_truncated(void) {
    wifi_provision_form_t f;
    // 静默截断会把"密码里带 %00"变成另一条密码，等于替用户改了密码。
    assert(parse("ssid=a%00b", &f) == WIFI_FORM_ERR_NUL);
}

// 手工拼正文而不是 snprintf：后者的 %s 会按数组最大宽度估算，
// 在 -Wformat-truncation 下容易把 -Werror 打红。
static size_t build(char *buf, size_t cap, const char *prefix, char fill, size_t fill_len) {
    size_t n = 0;
    size_t plen = strlen(prefix);
    assert(plen + fill_len + 1 <= cap);
    memcpy(buf, prefix, plen);
    n = plen;
    memset(buf + n, fill, fill_len);
    n += fill_len;
    buf[n] = '\0';
    return n;
}

static void test_ssid_length_limit(void) {
    wifi_provision_form_t f;
    char body[160];

    // 32 字节合法
    build(body, sizeof body, "ssid=", 'A', 32);
    assert(parse(body, &f) == WIFI_FORM_OK);
    assert(strlen(f.profile.ssid) == 32);

    // 33 字节拒绝（原文形式）
    build(body, sizeof body, "ssid=", 'A', 33);
    assert(parse(body, &f) == WIFI_FORM_ERR_TOO_LONG);

    // 33 字节拒绝（百分号编码形式：解码后才超限）
    size_t n = 0;
    memcpy(body, "ssid=", 5);
    n = 5;
    for (int i = 0; i < 33; i++) {
        memcpy(body + n, "%41", 3);
        n += 3;
    }
    body[n] = '\0';
    assert(parse(body, &f) == WIFI_FORM_ERR_TOO_LONG);
}

static void test_password_length_limit(void) {
    wifi_provision_form_t f;
    char body[192];

    // 64 字节合法
    size_t n = 0;
    memcpy(body, "ssid=Net&pass=", 14);
    n = 14;
    memset(body + n, 'p', 64);
    n += 64;
    body[n] = '\0';
    assert(parse(body, &f) == WIFI_FORM_OK);
    assert(strlen(f.profile.password) == 64);

    // 65 字节拒绝
    n = 0;
    memcpy(body, "ssid=Net&pass=", 14);
    n = 14;
    memset(body + n, 'p', 65);
    n += 65;
    body[n] = '\0';
    assert(parse(body, &f) == WIFI_FORM_ERR_TOO_LONG);
}

static void test_duplicate_key_is_rejected(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=a&ssid=b", &f) == WIFI_FORM_ERR_DUPLICATE);
    assert(parse("ssid=a&pass=x&pass=y", &f) == WIFI_FORM_ERR_DUPLICATE);
}

static void test_unknown_keys_are_ignored(void) {
    wifi_provision_form_t f;
    assert(parse("foo=1&ssid=Net&bar=baz&pass=pw", &f) == WIFI_FORM_OK);
    assert(strcmp(f.profile.ssid, "Net") == 0);
    assert(strcmp(f.profile.password, "pw") == 0);
}

static void test_pair_without_equals_is_rejected(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=Net&broken", &f) == WIFI_FORM_ERR_NO_FIELD);
}

static void test_field_names_are_case_sensitive(void) {
    wifi_provision_form_t f;
    assert(parse("SSID=Net&pass=pw", &f) == WIFI_FORM_ERR_MISSING_SSID);
}

static void test_trailing_ampersand_is_tolerated(void) {
    wifi_provision_form_t f;
    assert(parse("ssid=Net&pass=pw&", &f) == WIFI_FORM_OK);
    assert(strcmp(f.profile.ssid, "Net") == 0);
}

static void test_content_length_accepts_plain_numbers(void) {
    size_t value = 0;
    assert(wifi_provision_content_length("12", 1024, &value) == WIFI_CL_OK);
    assert(value == 12);
    // 前导零是合法数字，不是八进制。
    assert(wifi_provision_content_length("0012", 1024, &value) == WIFI_CL_OK);
    assert(value == 12);
    assert(wifi_provision_content_length("0", 1024, &value) == WIFI_CL_OK);
    assert(value == 0);
}

static void test_content_length_rejects_non_numbers(void) {
    size_t value = 0;
    assert(wifi_provision_content_length("", 1024, &value) == WIFI_CL_MISSING);
    assert(wifi_provision_content_length(NULL, 1024, &value) == WIFI_CL_MISSING);
    assert(wifi_provision_content_length("abc", 1024, &value) == WIFI_CL_NOT_NUMBER);
    assert(wifi_provision_content_length("1a", 1024, &value) == WIFI_CL_NOT_NUMBER);
    // 符号与空白一律拒绝，不做宽松解析。
    assert(wifi_provision_content_length("-1", 1024, &value) == WIFI_CL_NOT_NUMBER);
    assert(wifi_provision_content_length("+1", 1024, &value) == WIFI_CL_NOT_NUMBER);
    assert(wifi_provision_content_length(" 12", 1024, &value) == WIFI_CL_NOT_NUMBER);
}

static void test_content_length_respects_the_cap(void) {
    size_t value = 0;
    assert(wifi_provision_content_length("1024", WIFI_PROV_BODY_MAX, &value) == WIFI_CL_OK);
    assert(value == WIFI_PROV_BODY_MAX);
    assert(wifi_provision_content_length("1025", WIFI_PROV_BODY_MAX, &value) ==
           WIFI_CL_TOO_LARGE);
    // 手写累乘后不会溢出，直接判超限。
    assert(wifi_provision_content_length("99999999999999999999", WIFI_PROV_BODY_MAX, &value) ==
           WIFI_CL_TOO_LARGE);
}

static void test_error_text_is_present_for_every_code(void) {
    for (int code = WIFI_FORM_OK; code <= WIFI_FORM_ERR_NO_FIELD; code++) {
        const char *text = wifi_provision_form_error_text((wifi_form_result_t)code);
        assert(text != NULL);
        if (code != WIFI_FORM_OK) assert(text[0] != '\0');
    }
}

int main(void) {
    test_normal_submit();
    test_missing_ssid();
    test_empty_ssid();
    test_password_is_optional();
    test_plus_decodes_to_space();
    test_percent_decoding();
    test_bad_escapes_are_rejected();
    test_encoded_nul_is_rejected_not_truncated();
    test_ssid_length_limit();
    test_password_length_limit();
    test_duplicate_key_is_rejected();
    test_unknown_keys_are_ignored();
    test_pair_without_equals_is_rejected();
    test_field_names_are_case_sensitive();
    test_trailing_ampersand_is_tolerated();
    test_content_length_accepts_plain_numbers();
    test_content_length_rejects_non_numbers();
    test_content_length_respects_the_cap();
    test_error_text_is_present_for_every_code();
    puts("wifi provision form tests: PASS");
    return 0;
}
