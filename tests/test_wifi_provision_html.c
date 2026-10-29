// tests/test_wifi_provision_html.c —— 表单页与结果页的 host 测试。
//
// 页面是手机上唯一能看到的界面，所以这里既查内容契约（字段名、方法、路径），
// 也查"不含任何预填值"和"用户输入必须转义"这两条安全线。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "wifi_provision_html.h"

static bool is_ascii(const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7E) {
            if (c != '\n' && c != '\r') return false;
        }
    }
    return true;
}

static void test_form_page_matches_the_parser_contract(void) {
    size_t len = 0;
    const char *page = wifi_provision_form_page(&len);
    assert(page != NULL);
    assert(len > 0);
    assert(strlen(page) == len);
    assert(len < 2048);

    // 字段名与方法必须和解析器、httpd 注册一致，否则表单提交上去必然 400。
    assert(strstr(page, "method=\"post\"") != NULL);
    assert(strstr(page, "name=\"" WIFI_PROV_FIELD_SSID "\"") != NULL);
    assert(strstr(page, "name=\"" WIFI_PROV_FIELD_PASS "\"") != NULL);
    assert(strstr(page, "action=\"" WIFI_PROV_SAVE_PATH "\"") != NULL);
    assert(strstr(page, "viewport") != NULL);
}

static void test_form_page_is_ascii_and_carries_no_prefilled_values(void) {
    size_t len = 0;
    const char *page = wifi_provision_form_page(&len);
    assert(is_ascii(page, len));
    // 预填的 value= 会把上一次的 SSID 或密码写进静态页面。
    assert(strstr(page, "value=") == NULL);
    assert(strstr(page, "CHANGE-ME") == NULL);
}

static void test_escape_handles_every_dangerous_character(void) {
    char buf[128];
    size_t n = wifi_provision_html_escape(buf, sizeof buf, "<script>&\"'");
    assert(n > 0);
    assert(strcmp(buf, "&lt;script&gt;&amp;&quot;&#39;") == 0);
    assert(strstr(buf, "<script>") == NULL);
}

static void test_escape_leaves_plain_text_alone(void) {
    char buf[64];
    assert(wifi_provision_html_escape(buf, sizeof buf, "home-net") == 8);
    assert(strcmp(buf, "home-net") == 0);
}

static void test_saved_page_names_the_network_and_mentions_the_restart(void) {
    char buf[512];
    size_t n = wifi_provision_html_saved(buf, sizeof buf, "home-net");
    assert(n > 0);
    assert(strlen(buf) == n);
    assert(strstr(buf, "home-net") != NULL);
    assert(strstr(buf, "estart") != NULL);
}

static void test_saved_page_escapes_a_hostile_ssid(void) {
    char buf[512];
    assert(wifi_provision_html_saved(buf, sizeof buf, "<img src=x>") > 0);
    assert(strstr(buf, "<img") == NULL);
    assert(strstr(buf, "&lt;img") != NULL);
}

static void test_error_page_differs_per_reason(void) {
    char auth[512];
    char no_ap[512];
    char malformed[512];

    assert(wifi_provision_html_error(auth, sizeof auth, WIFI_FORM_ERR_TOO_LONG, NULL) > 0);
    assert(wifi_provision_html_error(no_ap, sizeof no_ap, WIFI_FORM_ERR_NUL, NULL) > 0);
    assert(wifi_provision_html_error(malformed, sizeof malformed, WIFI_FORM_ERR_BAD_ESCAPE, NULL) >
           0);

    assert(strstr(auth, wifi_provision_form_error_text(WIFI_FORM_ERR_TOO_LONG)) != NULL);
    assert(strstr(no_ap, wifi_provision_form_error_text(WIFI_FORM_ERR_NUL)) != NULL);
    assert(strstr(malformed, wifi_provision_form_error_text(WIFI_FORM_ERR_BAD_ESCAPE)) != NULL);
    assert(strcmp(auth, no_ap) != 0);
    assert(strcmp(no_ap, malformed) != 0);
}

static void test_error_page_reassures_that_nothing_changed(void) {
    char buf[512];
    assert(wifi_provision_html_error(buf, sizeof buf, WIFI_FORM_ERR_EMPTY_SSID, "home-net") > 0);
    assert(strstr(buf, "Nothing was changed") != NULL);
    assert(strstr(buf, "home-net") != NULL);
}

static void test_error_page_maps_ok_to_a_real_message(void) {
    char buf[512];
    assert(wifi_provision_html_error(buf, sizeof buf, WIFI_FORM_OK, NULL) > 0);
    // 不该出现"空原因"的错误页。
    assert(strstr(buf, "<p></p>") == NULL);
}

static void test_output_buffer_is_all_or_nothing(void) {
    char buf[16];
    memset(buf, 0x7E, sizeof buf);
    assert(wifi_provision_html_saved(buf, sizeof buf, "home-net") == 0);
    assert(buf[0] == '\0'); // 半截 HTML 会让手机白屏，必须整体放弃

    assert(wifi_provision_html_saved(NULL, 64, "home-net") == 0);
    assert(wifi_provision_html_saved(buf, 0, "home-net") == 0);

    assert(wifi_provision_html_escape(buf, sizeof buf, "a-very-long-string-too-big") == 0);
}

int main(void) {
    test_form_page_matches_the_parser_contract();
    test_form_page_is_ascii_and_carries_no_prefilled_values();
    test_escape_handles_every_dangerous_character();
    test_escape_leaves_plain_text_alone();
    test_saved_page_names_the_network_and_mentions_the_restart();
    test_saved_page_escapes_a_hostile_ssid();
    test_error_page_differs_per_reason();
    test_error_page_reassures_that_nothing_changed();
    test_error_page_maps_ok_to_a_real_message();
    test_output_buffer_is_all_or_nothing();
    puts("wifi provision html tests: PASS");
    return 0;
}
