// tests/test_http_body.c —— 有界响应体缓冲的 host 测试。
//
// 重点不是"能存字符串"，而是三条边界：写满但不越界、溢出后丢弃而不是中断、
// 以及状态码与截断同时出现时先报哪个。缓冲区两侧放哨兵字节，任何越界都会失败。
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "http_body.h"

static void test_init_clears_the_buffer(void) {
    char raw[8];
    memset(raw, 0x7E, sizeof raw);

    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    assert(body.len == 0);
    assert(!body.truncated);
    assert(!body.closed);
    assert(raw[0] == '\0');
    assert(raw[1] == (char)0x7E);
    assert(strcmp(http_body_text(&body), "") == 0);
    assert(http_body_len(&body) == 0);
    assert(!http_body_truncated(&body));
}

static void test_exact_fit_is_complete(void) {
    // cap 为 8 时只有 7 个字节能放内容，最后一个字节必须留给 NUL。
    char raw[8];
    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    http_body_write(&body, "0123456", 7);

    assert(body.len == 7);
    assert(!body.truncated);
    assert(strcmp(http_body_text(&body), "0123456") == 0);
    assert(http_body_finish(&body));
}

static void test_one_byte_past_the_limit_truncates_without_overrun(void) {
    // 缓冲之外再放两个哨兵：写满时不能碰到它们。
    char raw[10];
    memset(raw, 0x7E, sizeof raw);

    http_body_t body;
    http_body_init(&body, raw, 8);
    http_body_write(&body, "0123456789", 10);

    assert(body.len == 7);
    assert(body.truncated);
    assert(strcmp(raw, "0123456") == 0);
    assert(raw[7] == '\0');
    assert(raw[8] == (char)0x7E);
    assert(raw[9] == (char)0x7E);
    assert(!http_body_finish(&body));
    assert(strcmp(http_body_text(&body), "0123456") == 0);
}

static void test_chunks_across_the_limit_keep_only_what_fits(void) {
    char raw[8];
    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    http_body_write(&body, "abc", 3); // 3 字节
    http_body_write(&body, "def", 3); // 6 字节
    assert(!body.truncated);

    http_body_write(&body, "gh", 2); // 只能收下 'g'
    assert(body.len == 7);
    assert(body.truncated);
    assert(strcmp(http_body_text(&body), "abcdefg") == 0);
}

static void test_writes_after_truncation_are_dropped(void) {
    char raw[8];
    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    http_body_write(&body, "012345678", 9);
    size_t len_after_overflow = body.len;
    assert(body.truncated);

    http_body_write(&body, "xyz", 3);
    assert(body.len == len_after_overflow);
    assert(strcmp(http_body_text(&body), "0123456") == 0);
}

static void test_writes_after_finish_are_ignored(void) {
    char raw[8];
    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    http_body_write(&body, "abc", 3);
    assert(http_body_finish(&body));
    assert(body.closed);

    http_body_write(&body, "def", 3);
    assert(body.len == 3);
    assert(strcmp(http_body_text(&body), "abc") == 0);
}

static void test_empty_body_is_complete(void) {
    // 204 与 304 之外，某些接口也会回空体。完整性不等于非空。
    char raw[4];
    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    assert(http_body_finish(&body));
    assert(body.len == 0);
    assert(strcmp(http_body_text(&body), "") == 0);
}

static void test_zero_capacity_and_null_storage_are_degenerate(void) {
    char raw[4];
    memset(raw, 0x7E, sizeof raw);

    http_body_t body;
    http_body_init(&body, raw, 0);
    assert(body.truncated);
    assert(strcmp(http_body_text(&body), "") == 0);
    assert(!http_body_finish(&body));
    assert(raw[0] == (char)0x7E); // 没有写入任何字节

    http_body_init(&body, NULL, 32);
    assert(body.truncated);
    assert(strcmp(http_body_text(&body), "") == 0);
    http_body_write(&body, "abc", 3); // 不崩、不写
    assert(http_body_len(&body) == 0);
}

static void test_zero_and_null_writes_are_noops(void) {
    char raw[8];
    http_body_t body;
    http_body_init(&body, raw, sizeof raw);

    http_body_write(&body, NULL, 4);
    http_body_write(&body, "abc", 0);
    assert(body.len == 0);
    assert(!body.truncated);

    http_body_write(NULL, "abc", 3); // body 为 NULL 时同样静默
    assert(!http_body_truncated(&body));
    assert(http_body_truncated(NULL));
    assert(http_body_len(NULL) == 0);
    assert(strcmp(http_body_text(NULL), "") == 0);
}

static void test_status_mapping(void) {
    static const struct {
        int           status;
        bool          truncated;
        http_result_t want;
    } cases[] = {
        {200, false, HTTP_RESULT_OK},
        {204, false, HTTP_RESULT_OK},
        {299, false, HTTP_RESULT_OK},
        {200, true, HTTP_RESULT_TRUNCATED},
        {301, false, HTTP_RESULT_HTTP_STATUS},
        {404, false, HTTP_RESULT_HTTP_STATUS},
        // 服务器已经明确报错时，先报状态码，别把"内容不完整"当成主因。
        {500, true, HTTP_RESULT_HTTP_STATUS},
        {0, false, HTTP_RESULT_TRANSPORT},
        {-1, false, HTTP_RESULT_TRANSPORT},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        assert(http_result_from_status(cases[i].status, cases[i].truncated) == cases[i].want);
    }
}

int main(void) {
    test_init_clears_the_buffer();
    test_exact_fit_is_complete();
    test_one_byte_past_the_limit_truncates_without_overrun();
    test_chunks_across_the_limit_keep_only_what_fits();
    test_writes_after_truncation_are_dropped();
    test_writes_after_finish_are_ignored();
    test_empty_body_is_complete();
    test_zero_capacity_and_null_storage_are_degenerate();
    test_zero_and_null_writes_are_noops();
    test_status_mapping();
    puts("http body tests: PASS");
    return 0;
}
