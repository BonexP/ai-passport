// main/diag_repl_logic.c —— REPL 参数解析与状态名（纯逻辑）。
#include "diag_repl_logic.h"

#include <stddef.h>
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
