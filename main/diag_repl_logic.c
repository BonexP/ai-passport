// main/diag_repl_logic.c —— REPL 参数解析、状态名与导出格式（纯逻辑）。
#include "diag_repl_logic.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

bool diag_page_arg_parse(const char *arg, page_id_t *out) {
    if (arg == NULL || out == NULL || arg[0] == '\0' || arg[1] != '\0') {
        return false;
    }

    switch (arg[0]) {
    case '0':
        *out = PAGE_STATUS;
        return true;
    case '1':
        *out = PAGE_WEATHER;
        return true;
    case '2':
        *out = PAGE_USAGE;
        return true;
    case '3':
        *out = PAGE_QUOTE;
        return true;
    default:
        return false;
    }
}

bool diag_net_arg_parse(const char *arg, bool *weather, bool *usage) {
    if (weather == NULL || usage == NULL) {
        return false;
    }

    if (arg == NULL || strcmp(arg, "all") == 0) {
        *weather = true;
        *usage = true;
        return true;
    }
    if (strcmp(arg, "weather") == 0) {
        *weather = true;
        *usage = false;
        return true;
    }
    if (strcmp(arg, "usage") == 0) {
        *weather = false;
        *usage = true;
        return true;
    }
    return false;
}

const char *diag_usage_status_text(usage_status_t status) {
    switch (status) {
    case USAGE_STATUS_NONE:
        return "none";
    case USAGE_STATUS_OK:
        return "ok";
    case USAGE_STATUS_NO_KEY:
        return "no_key";
    case USAGE_STATUS_KEY_REJECTED:
        return "key_rejected";
    case USAGE_STATUS_RATE_LIMITED:
        return "rate_limited";
    case USAGE_STATUS_UNAVAILABLE:
        return "unavailable";
    case USAGE_STATUS_TRUNCATED:
        return "truncated";
    case USAGE_STATUS_BAD_BODY:
        return "bad_body";
    default:
        return "unknown";
    }
}

// ---- export 命令 ----

bool diag_export_arg_parse(int argc, char **argv, bool *reveal_secrets) {
    if (reveal_secrets == NULL) {
        return false;
    }
    *reveal_secrets = false;

    if (argc == 1) { // 只有命令名
        return true;
    }
    if (argc == 2 && argv != NULL && argv[1] != NULL &&
        strcmp(argv[1], "--secrets") == 0) {
        *reveal_secrets = true;
        return true;
    }
    return false;
}

// 把一段文本追加到 buf，必要时转义。返回是否成功；成功时 *cursor 前移。
// 转义规则：'\\' -> "\\\\"，'|' -> "\\|"，'\r' -> "\\r"，'\n' -> "\\n"，
// 其它控制字节（< 0x20 或 0x7f）-> "\\xHH"（两位大写十六进制）。
// 普通可打印 ASCII（0x20..0x7e）原样写入。
static bool append_escaped(char *buf, size_t cap, size_t *cursor, const char *text) {
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        const unsigned char c = *p;
        char seq[5];
        size_t n;
        if (c == '\\' || c == '|') {
            seq[0] = '\\';
            seq[1] = (char)c;
            n = 2;
        } else if (c == '\r') {
            seq[0] = '\\';
            seq[1] = 'r';
            n = 2;
        } else if (c == '\n') {
            seq[0] = '\\';
            seq[1] = 'n';
            n = 2;
        } else if (c < 0x20 || c == 0x7f) {
            seq[0] = '\\';
            seq[1] = 'x';
            seq[2] = hex[(c >> 4) & 0x0f];
            seq[3] = hex[c & 0x0f];
            n = 4;
        } else {
            seq[0] = (char)c;
            n = 1;
        }
        if (*cursor + n + 1 > cap) { // +1 留给结尾 NUL
            return false;
        }
        memcpy(buf + *cursor, seq, n);
        *cursor += n;
    }
    buf[*cursor] = '\0';
    return true;
}

// 写一个片段并更新游标；失败返回 false。
static bool append_literal(char *buf, size_t cap, size_t *cursor, const char *text) {
    const size_t n = strlen(text);
    if (*cursor + n + 1 > cap) {
        return false;
    }
    memcpy(buf + *cursor, text, n);
    *cursor += n;
    buf[*cursor] = '\0';
    return true;
}

size_t diag_export_line(const wifi_profile_t *p, uint8_t index, bool reveal_secrets,
                        char *buf, size_t cap) {
    if (p == NULL || buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    size_t cursor = 0;

    char field[64];
    if (snprintf(field, sizeof field, "WIFIPROF,%u|", (unsigned)index) < 0) {
        return 0;
    }
    if (!append_literal(buf, cap, &cursor, field)) {
        return 0;
    }
    if (!append_escaped(buf, cap, &cursor, p->name)) {
        return 0;
    }
    if (!append_literal(buf, cap, &cursor, "|")) {
        return 0;
    }
    if (!append_escaped(buf, cap, &cursor, p->ssid)) {
        return 0;
    }
    if (!append_literal(buf, cap, &cursor, "|")) {
        return 0;
    }
    if (!append_escaped(buf, cap, &cursor,
                        reveal_secrets ? p->password : DIAG_EXPORT_REDACTED)) {
        return 0;
    }
    if (!append_literal(buf, cap, &cursor, "|")) {
        return 0;
    }

    const bool located = p->location.has_location;
    if (!append_literal(buf, cap, &cursor, located ? "1|" : "0|")) {
        return 0;
    }
    if (located) {
        char lat[24];
        char lon[24];
        if (!app_location_format_4dp(p->location.lat_udeg, lat, sizeof lat) ||
            !app_location_format_4dp(p->location.lon_udeg, lon, sizeof lon)) {
            return 0;
        }
        if (!append_escaped(buf, cap, &cursor, lat) ||
            !append_literal(buf, cap, &cursor, "|") ||
            !append_escaped(buf, cap, &cursor, lon) ||
            !append_literal(buf, cap, &cursor, "|") ||
            !append_escaped(buf, cap, &cursor, p->location.place)) {
            return 0;
        }
    } else {
        if (!append_literal(buf, cap, &cursor, "||")) {
            return 0;
        }
    }
    return cursor;
}

size_t diag_export_header(uint8_t count, char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    size_t cursor = 0;
    char field[48];
    if (snprintf(field, sizeof field, "WIFIPROF_BEGIN,%u", (unsigned)count) < 0) {
        return 0;
    }
    if (!append_literal(buf, cap, &cursor, field)) {
        return 0;
    }
    return cursor;
}

size_t diag_export_footer(char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    size_t cursor = 0;
    if (!append_literal(buf, cap, &cursor, "WIFIPROF_END")) {
        return 0;
    }
    return cursor;
}

size_t diag_export_warning(char *buf, size_t cap) {
    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    size_t cursor = 0;
    if (!append_literal(buf, cap, &cursor, "WIFIPROF_WARNING,secrets=revealed")) {
        return 0;
    }
    return cursor;
}
