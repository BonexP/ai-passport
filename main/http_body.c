// main/http_body.c —— 有界响应体缓冲的实现。逻辑见 http_body.h 的契约说明。
#include "http_body.h"

#include <string.h>

void http_body_init(http_body_t *body, char *buf, size_t cap) {
    if (body == NULL) {
        return;
    }

    body->buf = buf;
    body->cap = cap;
    body->len = 0;
    body->closed = false;
    // 没有存储就没有"完整响应"可言，直接把退化配置标成截断，省得调用方再判一次。
    body->truncated = (buf == NULL || cap == 0);

    if (buf != NULL && cap > 0) {
        buf[0] = '\0';
    }
}

void http_body_write(http_body_t *body, const char *data, size_t len) {
    if (body == NULL || data == NULL || len == 0) {
        return;
    }
    // 已结束或已截断：只排空、不写入。截断标记是终态，不做"后来居上"。
    if (body->closed || body->truncated) {
        return;
    }

    // cap - 1 是真正能放内容的上限，最后一个字节留给 NUL。
    // 这里 len 恒小于 cap，room 不会下溢。
    size_t room = body->cap - 1 - body->len;
    size_t take = (len < room) ? len : room;

    if (take > 0) {
        memcpy(body->buf + body->len, data, take);
        body->len += take;
        body->buf[body->len] = '\0';
    }

    if (take < len) {
        body->truncated = true;
    }
}

bool http_body_finish(http_body_t *body) {
    if (body == NULL) {
        return false;
    }

    body->closed = true;
    return !body->truncated;
}

const char *http_body_text(const http_body_t *body) {
    if (body == NULL || body->buf == NULL || body->cap == 0) {
        return "";
    }
    return body->buf;
}

size_t http_body_len(const http_body_t *body) {
    return (body == NULL) ? 0 : body->len;
}

bool http_body_truncated(const http_body_t *body) {
    // NULL 等同于"没有可用内容"，对调用方来说与截断同样不可解析。
    return (body == NULL) ? true : body->truncated;
}

http_result_t http_result_from_status(int http_status, bool truncated) {
    if (http_status <= 0) {
        return HTTP_RESULT_TRANSPORT;
    }
    if (http_status < 200 || http_status > 299) {
        return HTTP_RESULT_HTTP_STATUS;
    }
    return truncated ? HTTP_RESULT_TRUNCATED : HTTP_RESULT_OK;
}
