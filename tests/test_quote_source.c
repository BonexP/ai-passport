// tests/test_quote_source.c —— 行情请求组装、标的映射与解析的主机测试。
//
// 报文片段取自 2026-11-17 对 https://qt.gtimg.cn/q=... 的实测抓取。
// 只覆盖本模块的取舍：代码映射、URL 组装、字段解析、宽松数值、格式化。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "quote_source.h"

// 来自实测：上证指数、英伟达、AI ETF。保留 '~' 结构，字段位置与真实报文一致。
static const char SAMPLE[] =
    "v_sh000001=\"1~上证指数~000001~3581.73~3602.57~3585.67~353650350~0~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~0.00~0~~20261117140515~-20.84~-0.58~3604.98~3565.59~3581.73/353650350/550463784388~353650350~55046378~0.73~16.34~~3604.98~3565.59~1.09~564782.62~627099.31~0.00~-1~-1~1.01~0~3582.58~~~~~~55046378.4388~0.0000~0~ ~ZS~-9.75~-2.24~~~~4258.86~3565.59~-0.73~-4.11~-10.07~4865887854015~~0.54~-12.26~4865887854015~~~-9.83~-0.25~~CNY~0~~0.00~0~\";\n"
    "v_usNVDA=\"200~英伟达~NVDA.OQ~156.14~162.85~159.02~333822062~0~0~155.58~400~0~0~0~0~0~0~0~0~155.60~5000~0~0~0~0~0~0~0~0~~2026-11-16 16:00:01~-6.71~-4.12~163.20~155.01~USD~333822062~52678055451~1.38~19.74~~31.87~~5.03~36208.38631~37703.12580~Nvidia Corporation~7.91~243.37~148.80~-4600~16.47~0.33~37703.12580~-16.08~-12.61~GP~117.21~83.68~-22.73~-28.08~-27.20~24147000000~23189692783~0.71~16.10~0.52~157.80~~~~~\";\n"
    "v_sz159819=\"51~人工智能ETF易方达~159819~1.306~1.328~1.314~1696758~693529~1003192~1.306~2889~1.305~19060~1.304~59013~1.303~59280~1.302~36725~1.307~100~1.308~43937~1.309~14398~1.310~4099~1.311~1141~~20261117140557~-0.022~-1.66~1.344~1.302~1.306/1696758/224093294~1696758~22409~1.42~~~1.344~1.302~3.16~156.58~156.58~0.00~1.461~1.195~0.82~113292~1.321~~~~~~22409.3294~0.0000~0~ A~ETF~-14.75~-6.31~~~~2.266~1.291~0.46~-11.46~-31.37~11989229669~11989229669~47.08~-18.17~11989229669~-0.05~1.3066~-7.31~-0.38~1.3289~CNY~0~~1.301~34548~\";\n";

static void test_symbol_mapping(void) {
    char out[QUOTE_SYMBOL_MAX];
    assert(quote_symbol_to_code("600519", out, sizeof out) && strcmp(out, "sh600519") == 0);
    assert(quote_symbol_to_code("000001", out, sizeof out) && strcmp(out, "sz000001") == 0);
    assert(quote_symbol_to_code("159819", out, sizeof out) && strcmp(out, "sz159819") == 0);
    assert(quote_symbol_to_code("NVDA", out, sizeof out) && strcmp(out, "usNVDA") == 0);
    assert(quote_symbol_to_code("QQQ", out, sizeof out) && strcmp(out, "usQQQ") == 0);
    // 两个无 `~` 数据的默认值退到可解析的近似标的。
    assert(quote_symbol_to_code("930713", out, sizeof out) && strcmp(out, "sh000001") == 0);
    assert(quote_symbol_to_code("XAU", out, sizeof out) && strcmp(out, "usGLD") == 0);
    assert(!quote_symbol_to_code("", out, sizeof out));
    assert(!quote_symbol_to_code(NULL, out, sizeof out));
    char tiny[4];
    assert(!quote_symbol_to_code("NVDA", tiny, sizeof tiny));
}

static void test_url_build(void) {
    char url[QUOTE_URL_MAX];
    char *codes[] = {"sh000001", "usNVDA", "sz159819"};
    assert(quote_url_build(codes, 3, url, sizeof url));
    assert(strcmp(url, "https://qt.gtimg.cn/q=sh000001,usNVDA,sz159819") == 0);
    assert(!quote_url_build(codes, 0, url, sizeof url));
    assert(url[0] == '\0');
    char small[8];
    assert(!quote_url_build(codes, 3, small, sizeof small));
    assert(small[0] == '\0');
}

static void test_parse_real_sample(void) {
    char *codes[] = {"sh000001", "usNVDA", "sz159819"};
    char *labels[] = {"SSE", "NVDA", "AI ETF"};
    quote_state_t s;
    assert(quote_parse(SAMPLE, codes, labels, 3, &s));
    assert(s.has_data);
    assert(s.count == 3);

    assert(s.items[0].valid);
    assert(strcmp(s.items[0].label, "SSE") == 0);
    assert(s.items[0].price_1e4 == 35817300);
    assert(s.items[0].change_1e4 == -208400);
    assert(s.items[0].pct_1e4 == -5800);
    assert(strcmp(s.items[0].amount, "55046378") == 0);

    assert(s.items[1].valid);
    assert(s.items[1].price_1e4 == 1561400);
    assert(s.items[1].change_1e4 == -67100);
    assert(s.items[1].pct_1e4 == -41200);

    assert(s.items[2].valid);
    assert(s.items[2].price_1e4 == 13060);
    assert(s.items[2].pct_1e4 == -16600);
}

static void test_missing_quote_is_invalid_not_fatal(void) {
    char *codes[] = {"sh000001", "usZZZZ"};
    char *labels[] = {"SSE", "ZZZZ"};
    quote_state_t s;
    assert(quote_parse(SAMPLE, codes, labels, 2, &s));
    assert(s.has_data);
    assert(s.count == 1);
    assert(s.items[0].valid);
    assert(!s.items[1].valid);
}

static void test_parse_rejects_bad_args(void) {
    char *codes[] = {"sh000001"};
    char *labels[] = {"SSE"};
    quote_state_t s;
    assert(!quote_parse(NULL, codes, labels, 1, &s));
    assert(!quote_parse(SAMPLE, NULL, labels, 1, &s));
    assert(!quote_parse(SAMPLE, codes, NULL, 1, &s));
    assert(!quote_parse(SAMPLE, codes, labels, 0, &s));
}

static void test_format_price(void) {
    char buf[32];
    assert(quote_format_price(35817300, buf, sizeof buf) && strcmp(buf, "3581.7300") == 0);
    assert(quote_format_price(13060, buf, sizeof buf) && strcmp(buf, "1.3060") == 0);
    assert(quote_format_price(-41200, buf, sizeof buf) && strcmp(buf, "-4.1200") == 0);
    char tiny[4];
    assert(!quote_format_price(35817300, tiny, sizeof tiny));
    assert(tiny[0] == '\0');
}

static void test_format_pct(void) {
    char buf[24];
    assert(quote_format_pct(41200, buf, sizeof buf) && strcmp(buf, "4.12%") == 0);
    assert(quote_format_pct(-41200, buf, sizeof buf) && strcmp(buf, "-4.12%") == 0);
    assert(quote_format_pct(0, buf, sizeof buf) && strcmp(buf, "0.00%") == 0);
    assert(quote_format_pct(-5800, buf, sizeof buf) && strcmp(buf, "-0.58%") == 0);
}

static void test_format_amount(void) {
    char buf[32];
    // A 股：55046378 万元 = 5504.6 亿元。
    assert(quote_format_amount("sh000001", "55046378", buf, sizeof buf) &&
           strcmp(buf, "5504.6亿") == 0);
    // 美股：52678055451 美元 = 52.6B。
    assert(quote_format_amount("usNVDA", "52678055451", buf, sizeof buf) &&
           strcmp(buf, "$52.6B") == 0);
    assert(!quote_format_amount("usNVDA", "12x", buf, sizeof buf));
}

int main(void) {
    test_symbol_mapping();
    test_url_build();
    test_parse_real_sample();
    test_missing_quote_is_invalid_not_fatal();
    test_parse_rejects_bad_args();
    test_format_price();
    test_format_pct();
    test_format_amount();
    puts("test_quote_source: PASS");
    return 0;
}
