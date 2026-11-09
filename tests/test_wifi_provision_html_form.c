// tests/test_wifi_provision_html_form.c —— 动态表单页：把扫到的网络渲染成 <datalist>。
//
// 静态页那三条断言（纯 ASCII、不含 value=、len < 2048）保护的是"静态页不许夹带
// 任何 AP 数据"，不该为了动态页放宽；所以动态页单独一个文件、单独一组断言。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wifi_provision_html.h"

static void test_empty_list_matches_the_static_page(void) {
    char page[4096];
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);

    const size_t n = wifi_provision_html_form(page, sizeof page, &list);
    size_t static_len = 0;
    const char *static_page = wifi_provision_form_page(&static_len);

    // 没扫到网络时必须是同一份字节：降级路径不该有第二种样子。
    assert(n == static_len);
    assert(memcmp(page, static_page, static_len) == 0);

    assert(wifi_provision_html_form(page, sizeof page, NULL) == static_len);
}

static void test_static_page_carries_an_empty_datalist(void) {
    size_t len = 0;
    const char *page = wifi_provision_form_page(&len);

    assert(strstr(page, "list=\"nets\"") != NULL);
    assert(strstr(page, "<datalist id=\"nets\"></datalist>") != NULL);
    // 静态页仍然不许带任何选项。
    assert(strstr(page, "<option") == NULL);
    assert(strstr(page, "value=") == NULL);
}

static void test_options_follow_the_list_order(void) {
    char page[4096];
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);
    assert(wifi_scan_list_add(&list, "OfficeNet", -78));
    assert(wifi_scan_list_add(&list, "HomeNet", -41));
    assert(wifi_scan_list_add(&list, "Cafe", -66));

    const size_t n = wifi_provision_html_form(page, sizeof page, &list);
    assert(n > 0);

    const char *home = strstr(page, "<option value=\"HomeNet\">");
    const char *cafe = strstr(page, "<option value=\"Cafe\">");
    const char *office = strstr(page, "<option value=\"OfficeNet\">");
    assert(home != NULL && cafe != NULL && office != NULL);
    assert(home < cafe && cafe < office); // 列表已经按信号排好，渲染照抄

    assert(strstr(page, "HomeNet (-41 dBm)</option>") != NULL);

    // 表单本身还在：datalist 只是插在中间，不能把后半页吃掉。
    assert(strstr(page, "name=\"pass\"") != NULL);
    assert(strstr(page, "type=\"submit\"") != NULL);
    assert(strstr(page, "</html>") != NULL);
}

static void test_ssid_is_html_escaped(void) {
    char page[4096];
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);
    assert(wifi_scan_list_add(&list, "a&b<c>\"d'e", -50));

    assert(wifi_provision_html_form(page, sizeof page, &list) > 0);

    // 属性值与文本各转义一次；任何一处漏转义都是配网页上的反射型 XSS。
    assert(strstr(page, "value=\"a&amp;b&lt;c&gt;&quot;d&#39;e\"") != NULL);
    assert(strstr(page, "a&amp;b&lt;c&gt;&quot;d&#39;e (-50 dBm)") != NULL);
    assert(strstr(page, "<c>") == NULL);
}

static void test_short_buffer_writes_nothing(void) {
    char page[64];
    wifi_scan_list_t list;
    wifi_scan_list_clear(&list);
    assert(wifi_scan_list_add(&list, "HomeNet", -41));

    memset(page, 'x', sizeof page);
    // 放不下就整页放弃：半截 HTML 在手机上会渲染成一片空白，比错误页更糟。
    assert(wifi_provision_html_form(page, sizeof page, &list) == 0);
    assert(page[0] == '\0');
}

int main(void) {
    test_empty_list_matches_the_static_page();
    test_static_page_carries_an_empty_datalist();
    test_options_follow_the_list_order();
    test_ssid_is_html_escaped();
    test_short_buffer_writes_nothing();
    puts("wifi provision html form tests: PASS");
    return 0;
}
