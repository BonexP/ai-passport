// tests/test_diag_repl_logic.c —— 诊断 REPL 参数解析的主机测试。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "diag_repl_logic.h"

static void test_page_arguments(void) {
    page_id_t page = PAGE_MENU;

    assert(diag_page_arg_parse("0", &page));
    assert(page == PAGE_STATUS);
    assert(diag_page_arg_parse("1", &page));
    assert(page == PAGE_WEATHER);
    assert(diag_page_arg_parse("2", &page));
    assert(page == PAGE_USAGE);

    assert(!diag_page_arg_parse(NULL, &page));
    assert(!diag_page_arg_parse("", &page));
    assert(!diag_page_arg_parse("3", &page));
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

int main(void) {
    test_page_arguments();
    test_net_arguments();
    test_usage_status_text();
    puts("diag REPL logic tests: PASS");
    return 0;
}
