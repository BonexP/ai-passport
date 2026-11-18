// tests/test_diag_repl_logic.c —— 诊断 REPL 参数解析与导出格式的主机测试。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "diag_repl_logic.h"

static void test_page_arguments(void) {
    page_id_t page = PAGE_MENU;

    assert(diag_page_arg_parse("0", &page));
    assert(page == PAGE_STATUS);
    assert(diag_page_arg_parse("1", &page));
    assert(page == PAGE_WEATHER);
    assert(diag_page_arg_parse("2", &page));
    assert(page == PAGE_USAGE);
    assert(diag_page_arg_parse("3", &page));
    assert(page == PAGE_QUOTE);

    assert(!diag_page_arg_parse(NULL, &page));
    assert(!diag_page_arg_parse("", &page));
    assert(!diag_page_arg_parse("4", &page));
    assert(!diag_page_arg_parse("00", &page));
    assert(!diag_page_arg_parse("status", &page));
    assert(!diag_page_arg_parse("0", NULL));
}

static void test_net_arguments(void) {
    bool weather = false;
    bool usage = false;

    assert(diag_net_arg_parse(NULL, &weather, &usage));
    assert(weather && usage);

    assert(diag_net_arg_parse("all", &weather, &usage));
    assert(weather && usage);

    assert(diag_net_arg_parse("weather", &weather, &usage));
    assert(weather && !usage);

    assert(diag_net_arg_parse("usage", &weather, &usage));
    assert(!weather && usage);

    assert(!diag_net_arg_parse("", &weather, &usage));
    assert(!diag_net_arg_parse("both", &weather, &usage));
    assert(!diag_net_arg_parse("weather", NULL, &usage));
    assert(!diag_net_arg_parse("weather", &weather, NULL));
}

static void test_usage_status_text(void) {
    assert(diag_usage_status_text(USAGE_STATUS_NONE) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_OK) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_NO_KEY) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_KEY_REJECTED) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_RATE_LIMITED) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_UNAVAILABLE) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_TRUNCATED) != NULL);
    assert(diag_usage_status_text(USAGE_STATUS_BAD_BODY) != NULL);
    assert(diag_usage_status_text((usage_status_t)99) != NULL);
}

static wifi_profile_t make_profile(const char *name, const char *ssid,
                                   const char *pass, int32_t lat, int32_t lon,
                                   const char *place) {
    wifi_profile_t p;
    memset(&p, 0, sizeof p);
    assert(wifi_profile_set_fields(&p, name, ssid, pass));
    if (place != NULL) {
        app_location_t loc;
        assert(app_location_set(&loc, lat, lon, place));
        assert(wifi_profile_set_location(&p, &loc));
    }
    return p;
}

static void test_export_arguments(void) {
    bool reveal = true;

    char cmd[] = "export";
    char *argv1[] = {cmd};
    assert(diag_export_arg_parse(1, argv1, &reveal));
    assert(!reveal);

    char *argv2[] = {cmd, "--secrets"};
    assert(diag_export_arg_parse(2, argv2, &reveal));
    assert(reveal);

    char *argv_bad[] = {cmd, "--other"};
    assert(!diag_export_arg_parse(2, argv_bad, &reveal));

    char *argv_extra[] = {cmd, "--secrets", "--secrets"};
    assert(!diag_export_arg_parse(3, argv_extra, &reveal));

    assert(!diag_export_arg_parse(1, argv1, NULL));
}

static void test_export_line_redacted(void) {
    char buf[DIAG_EXPORT_LINE_MAX];

    wifi_profile_t p = make_profile("Home", "HomeNet", "home-secret",
                                    39904200, 116407400, "Beijing");
    size_t n = diag_export_line(&p, 0, false, buf, sizeof buf);
    assert(n > 0);
    assert(strcmp(buf,
                  "WIFIPROF,0|Home|HomeNet|REDACTED|1|39.9042|116.4074|Beijing") == 0);
    assert(strstr(buf, "home-secret") == NULL); // 默认绝不打印真实口令

    n = diag_export_line(&p, 0, true, buf, sizeof buf);
    assert(n > 0);
    assert(strcmp(buf,
                  "WIFIPROF,0|Home|HomeNet|home-secret|1|39.9042|116.4074|Beijing") == 0);
}

static void test_export_line_no_location(void) {
    char buf[DIAG_EXPORT_LINE_MAX];
    wifi_profile_t p = make_profile("Bakery", "CafeWiFi", "pw", 0, 0, NULL);
    assert(!p.location.has_location);

    size_t n = diag_export_line(&p, 2, false, buf, sizeof buf);
    assert(n > 0);
    assert(strcmp(buf, "WIFIPROF,2|Bakery|CafeWiFi|REDACTED|0|||") == 0);
}

static void test_export_line_negative_coords(void) {
    char buf[DIAG_EXPORT_LINE_MAX];
    wifi_profile_t p = make_profile("US", "Lab-5G", "s3cr3t",
                                    -37774900, -122419400, "San Francisco");

    size_t n = diag_export_line(&p, 1, true, buf, sizeof buf);
    assert(n > 0);
    assert(strcmp(buf,
                  "WIFIPROF,1|US|Lab-5G|s3cr3t|1|-37.7749|-122.4194|San Francisco") == 0);
}

static void test_export_line_escaping(void) {
    char buf[DIAG_EXPORT_LINE_MAX];
    // 名字含 '|' 与 '\'，SSID 含竖线，口令含换行/制表符/CR/DEL。
    // place 由 app_location_set 校验，不能含控制字符，因此用普通文本。
    wifi_profile_t p = make_profile("A|B\\C", "SSID|X", "p\nw\t\r\x7f",
                                    1000000, 2000000, "ZQ");

    size_t n = diag_export_line(&p, 3, true, buf, sizeof buf);
    assert(n > 0);
    // 反斜杠与竖线各自转义；换行 -> \n，制表符(0x09) -> \x09，
    // CR -> \r，DEL(0x7f) -> \x7F（十六进制两位大写）。
    assert(strcmp(buf,
                  "WIFIPROF,3|A\\|B\\\\C|SSID\\|X|p\\nw\\x09\\r\\x7F|1|1.0000|2.0000|ZQ") == 0);
    // 输出必须是单行 ASCII：不含真实换行/制表/回车/DEL。
    assert(strchr(buf, '\n') == NULL);
    assert(strchr(buf, '\t') == NULL);
    assert(strchr(buf, '\r') == NULL);
    assert(strchr(buf, '\x7f') == NULL);
}

static void test_export_line_buffer_too_small(void) {
    char buf[8];
    memset(buf, 0x7f, sizeof buf);
    wifi_profile_t p = make_profile("Home", "HomeNet", "home-secret",
                                    39904200, 116407400, "Beijing");

    size_t n = diag_export_line(&p, 0, true, buf, sizeof buf);
    assert(n == 0);
    assert(buf[0] == '\0'); // 失败时缓冲保持空串，不写半个字段

    assert(diag_export_line(NULL, 0, false, buf, sizeof buf) == 0);
    assert(diag_export_line(&p, 0, false, NULL, sizeof buf) == 0);
    assert(diag_export_line(&p, 0, false, buf, 0) == 0);
}

static void test_export_markers(void) {
    char buf[64];
    size_t n = diag_export_header(8, buf, sizeof buf);
    assert(n > 0 && strcmp(buf, "WIFIPROF_BEGIN,8") == 0);
    n = diag_export_header(0, buf, sizeof buf);
    assert(n > 0 && strcmp(buf, "WIFIPROF_BEGIN,0") == 0);

    n = diag_export_footer(buf, sizeof buf);
    assert(n > 0 && strcmp(buf, "WIFIPROF_END") == 0);

    n = diag_export_warning(buf, sizeof buf);
    assert(n > 0 && strcmp(buf, "WIFIPROF_WARNING,secrets=revealed") == 0);

    assert(diag_export_header(1, buf, 4) == 0);
    assert(diag_export_footer(buf, 4) == 0);
    assert(diag_export_warning(buf, 4) == 0);
}

int main(void) {
    test_page_arguments();
    test_net_arguments();
    test_usage_status_text();
    test_export_arguments();
    test_export_line_redacted();
    test_export_line_no_location();
    test_export_line_negative_coords();
    test_export_line_escaping();
    test_export_line_buffer_too_small();
    test_export_markers();
    puts("diag REPL logic tests: PASS");
    return 0;
}
