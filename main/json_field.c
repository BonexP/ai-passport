// main/json_field.c —— 最小 JSON 字段提取的实现。对外契约见 json_field.h。
//
// 结构：扫描器自始至终只有"当前指针 + 深度计数"，对象成员逐个跳过，命中路径的
// 那一个分支才继续下降。跳过未知值时用有界递归（JSON_SKIP_DEPTH_MAX 层封顶），
// 每层只占几个指针，因此最坏栈用量是常数级。
#include "json_field.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 跳过未知值时允许的最大嵌套层数。正常天气/行情报文不超过五层；这里的上限是
// 防恶意或损坏报文把栈打穿，不是功能边界。
#define JSON_SKIP_DEPTH_MAX 16

// JSON 的四种空白。刻意不用 isspace：那会把垂直制表符等非 JSON 空白也吃进去，
// 掩盖畸形报文。
static bool is_json_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *skip_ws(const char *p) {
    while (is_json_space(*p)) {
        p++;
    }
    return p;
}

static bool parse_hex4(const char *s, uint32_t *out) {
    uint32_t value = 0;

    for (int i = 0; i < 4; i++) {
        char c = s[i];
        uint32_t digit;

        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a') + 10u;
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A') + 10u;
        } else {
            return false; // 含提前遇到的 NUL
        }
        value = (value << 4) | digit;
    }

    *out = value;
    return true;
}

// 扫描一个字符串 token。p 必须指向开引号；成功时 start/len 是引号之间（未解码）
// 的范围，end 指向闭引号之后。只做语法检查，不解码。
static bool scan_string(const char *p, const char **start, size_t *len, const char **end) {
    if (*p != '"') {
        return false;
    }

    const char *s = p + 1;
    const char *q = s;

    while (*q != '\0') {
        unsigned char c = (unsigned char)*q;

        if (c == '"') {
            *start = s;
            *len = (size_t)(q - s);
            *end = q + 1;
            return true;
        }
        if (c == '\\') {
            q++;
            if (*q == '\0') {
                return false;
            }
            if (*q == 'u') {
                // q 指向 'u'，后面必须恰好跟四个十六进制位；q[1..4] 读到 NUL
                // 时 parse_hex4 会失败，不会越界。
                uint32_t unused;

                if (!parse_hex4(q + 1, &unused)) {
                    return false;
                }
                q += 5;
            } else if (strchr("\"\\/bfnrt", *q) != NULL) {
                q++;
            } else {
                return false; // 未知转义，按畸形报文处理
            }
        } else if (c < 0x20) {
            return false; // JSON 不允许裸控制字符
        } else {
            q++;
        }
    }

    return false; // 没有闭引号
}

// 解码一个字符串单元（一个字节或一条转义序列）。返回消耗的输入字节数，
// 失败返回 0。代理对在这里合并成一个码点。
static size_t decode_unit(const char *s, size_t len, uint32_t *cp) {
    if (len == 0) {
        return 0;
    }

    unsigned char c = (unsigned char)s[0];
    if (c != '\\') {
        *cp = (uint32_t)c;
        return 1;
    }
    if (len < 2) {
        return 0;
    }

    switch (s[1]) {
    case '"': *cp = (uint32_t)'"';  return 2;
    case '\\': *cp = (uint32_t)'\\'; return 2;
    case '/': *cp = (uint32_t)'/';  return 2;
    case 'b': *cp = 0x08u;          return 2;
    case 'f': *cp = 0x0Cu;          return 2;
    case 'n': *cp = 0x0Au;          return 2;
    case 'r': *cp = 0x0Du;          return 2;
    case 't': *cp = 0x09u;          return 2;
    case 'u': {
        uint32_t hi;

        if (len < 6 || !parse_hex4(s + 2, &hi)) {
            return 0;
        }
        if (hi >= 0xD800u && hi <= 0xDBFFu) {
            uint32_t lo;

            // 高代理必须紧跟一个低代理，否则是非法序列。
            if (len < 12 || s[6] != '\\' || s[7] != 'u' || !parse_hex4(s + 8, &lo)) {
                return 0;
            }
            if (lo < 0xDC00u || lo > 0xDFFFu) {
                return 0;
            }
            *cp = 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
            return 12;
        }
        if (hi >= 0xDC00u && hi <= 0xDFFFu) {
            return 0; // 孤立的低代理
        }
        *cp = hi;
        return 6;
    }
    default:
        return 0;
    }
}

// 把码点编码成 UTF-8。返回字节数；码点为 0 或超出 Unicode 范围时返回 0。
static size_t utf8_encode(uint32_t cp, char *out, size_t cap) {
    if (cp == 0 || cp > 0x10FFFFu) {
        return 0; // NUL 会截断 C 字符串，直接判失败
    }
    if (cp < 0x80u) {
        if (cap < 1) {
            return 0;
        }
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        if (cap < 2) {
            return 0;
        }
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        if (cap < 3) {
            return 0;
        }
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    if (cap < 4) {
        return 0;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

// 比较 JSON 键（未解码）与调用方给的 ASCII 路径段。
// 键里出现非 ASCII 或非法转义时直接不匹配：本模块只服务已知的英文键名。
static bool key_matches(const char *s, size_t len, const char *want) {
    size_t i = 0;
    size_t w = 0;

    while (i < len) {
        uint32_t cp;
        size_t used = decode_unit(s + i, len - i, &cp);

        if (used == 0 || cp > 0x7Fu) {
            return false;
        }
        if (want[w] == '\0' || (unsigned char)want[w] != (unsigned char)cp) {
            return false;
        }
        i += used;
        w++;
    }

    return want[w] == '\0';
}

// 值之后必须紧跟逗号、右括号或结尾（允许空白）。用来挡住 `1 x` 这类畸形：
// 只校验 token 本身不够，token 后面挂着垃圾同样说明报文不可信。
static bool value_terminated(const char *p) {
    p = skip_ws(p);
    return *p == ',' || *p == '}' || *p == ']' || *p == '\0';
}

// 校验 JSON 数字字面量。成功时 end 指向 token 之后。
static bool scan_number(const char *p, const char **end) {
    const char *q = p;

    if (*q == '-') {
        q++;
    }
    if (*q == '0') {
        q++; // 前导零只允许单独一个 0
    } else if (*q >= '1' && *q <= '9') {
        do {
            q++;
        } while (*q >= '0' && *q <= '9');
    } else {
        return false;
    }

    if (*q == '.') {
        q++;
        if (!(*q >= '0' && *q <= '9')) {
            return false;
        }
        do {
            q++;
        } while (*q >= '0' && *q <= '9');
    }

    if (*q == 'e' || *q == 'E') {
        q++;
        if (*q == '+' || *q == '-') {
            q++;
        }
        if (!(*q >= '0' && *q <= '9')) {
            return false;
        }
        do {
            q++;
        } while (*q >= '0' && *q <= '9');
    }

    *end = q;
    return true;
}

// 跳过任意一个值（含容器）。递归层数由 depth 封顶，因此不存在无界递归。
static bool skip_value(const char **pp, unsigned depth) {
    if (depth > JSON_SKIP_DEPTH_MAX) {
        return false;
    }

    const char *p = skip_ws(*pp);

    switch (*p) {
    case '"': {
        const char *s;
        const char *e;
        size_t n;

        if (!scan_string(p, &s, &n, &e)) {
            return false;
        }
        *pp = e;
        return true;
    }
    case '{': {
        p = skip_ws(p + 1);
        if (*p == '}') {
            *pp = p + 1;
            return true;
        }
        for (;;) {
            const char *ks;
            const char *ke;
            size_t kl;

            if (!scan_string(p, &ks, &kl, &ke)) {
                return false;
            }
            p = skip_ws(ke);
            if (*p != ':') {
                return false;
            }
            p = skip_ws(p + 1);
            if (!skip_value(&p, depth + 1)) {
                return false;
            }
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                continue;
            }
            if (*p == '}') {
                *pp = p + 1;
                return true;
            }
            return false;
        }
    }
    case '[': {
        p = skip_ws(p + 1);
        if (*p == ']') {
            *pp = p + 1;
            return true;
        }
        for (;;) {
            if (!skip_value(&p, depth + 1)) {
                return false;
            }
            p = skip_ws(p);
            if (*p == ',') {
                p = skip_ws(p + 1);
                continue;
            }
            if (*p == ']') {
                *pp = p + 1;
                return true;
            }
            return false;
        }
    }
    case 't':
        if (strncmp(p, "true", 4) == 0) {
            *pp = p + 4;
            return true;
        }
        return false;
    case 'f':
        if (strncmp(p, "false", 5) == 0) {
            *pp = p + 5;
            return true;
        }
        return false;
    case 'n':
        if (strncmp(p, "null", 4) == 0) {
            *pp = p + 4;
            return true;
        }
        return false;
    default: {
        const char *e;

        if (!scan_number(p, &e)) {
            return false;
        }
        *pp = e;
        return true;
    }
    }
}

// 路径合法性：非空、无空段、不超长、段数不超上限。
static bool path_is_valid(const char *path) {
    size_t len = strlen(path);

    if (len == 0 || len >= JSON_FIELD_PATH_MAX) {
        return false;
    }

    unsigned depth = 1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] != '.') {
            continue;
        }
        if (i == 0 || i + 1 == len || path[i + 1] == '.') {
            return false;
        }
        depth++;
        if (depth > JSON_FIELD_DEPTH_MAX) {
            return false;
        }
    }

    return true;
}

// 沿 path 下降，返回命中值的首字符。任何不符合"对象套对象"的形状都返回 NULL。
// 命中后不回溯：这是路径语义的一部分，不是实现偷懒（见头文件）。
static const char *find_value(const char *doc, const char *path) {
    if (doc == NULL || path == NULL || !path_is_valid(path)) {
        return NULL;
    }

    const char *p = skip_ws(doc);
    if (*p != '{') {
        return NULL;
    }

    const char *seg = path;

    for (;;) {
        size_t seg_len = strcspn(seg, ".");
        if (seg_len == 0 || seg_len >= JSON_FIELD_PATH_MAX) {
            return NULL;
        }

        char want[JSON_FIELD_PATH_MAX];
        memcpy(want, seg, seg_len);
        want[seg_len] = '\0';

        p = skip_ws(p + 1); // 进入当前对象，p 原来指向 '{'
        if (*p == '}') {
            return NULL; // 空对象，不可能再命中
        }

        for (;;) {
            const char *ks;
            const char *ke;
            size_t kl;

            if (!scan_string(p, &ks, &kl, &ke)) {
                return NULL;
            }
            p = skip_ws(ke);
            if (*p != ':') {
                return NULL;
            }
            p = skip_ws(p + 1);

            if (key_matches(ks, kl, want)) {
                break; // p 停在命中值的首字符
            }
            if (!skip_value(&p, 1)) {
                return NULL;
            }
            p = skip_ws(p);
            if (*p != ',') {
                return NULL; // 既不是逗号也不是结尾：本层没有这个键
            }
            p = skip_ws(p + 1);
        }

        const char *tail = seg + seg_len;
        if (*tail == '\0') {
            return p;
        }
        if (*p != '{') {
            return NULL; // 中间段必须是对象；数组与标量都不支持
        }
        seg = tail + 1;
    }
}

// 按 JSON 数字字面量解析。token 长度上限 31 字节：天气与行情数字远小于它，
// 更长的 token 只可能来自畸形或恶意报文。
static bool parse_number(const char *p, double *out) {
    const char *end;

    if (!scan_number(p, &end)) {
        return false;
    }

    if (!value_terminated(end)) {
        return false; // "1x" 或 "1 x" 这类 token 后面必须紧跟分隔符
    }

    size_t len = (size_t)(end - p);
    if (len == 0 || len >= 32) {
        return false;
    }

    char tmp[32];
    memcpy(tmp, p, len);
    tmp[len] = '\0';

    char *parsed_end = NULL;
    double value = strtod(tmp, &parsed_end);
    if (parsed_end != tmp + len) {
        return false;
    }
    if (!isfinite(value)) {
        return false; // 溢出成 inf 的值不能进 UI
    }

    *out = value;
    return true;
}

bool json_field_string(const char *doc, const char *path, char *out, size_t cap) {
    if (out == NULL || cap == 0) {
        return false;
    }
    out[0] = '\0';

    const char *p = find_value(doc, path);
    if (p == NULL) {
        return false;
    }

    const char *s;
    const char *e;
    size_t len;

    if (!scan_string(p, &s, &len, &e) || !value_terminated(e)) {
        return false;
    }

    // 原样字节直接搬运：报文里的中文与符号已经是 UTF-8，若当成码点再编码一次，
    // "°" 会变成 "Ã°"。只有反斜杠转义才需要先解码、再编码。
    // 任何一步放不下都清空输出，失败时不留下半截值。
    size_t o = 0;
    size_t i = 0;
    while (i < len) {
        if (s[i] != '\\') {
            if (o + 1 + 1 > cap) {
                out[0] = '\0';
                return false;
            }
            out[o++] = s[i++];
            continue;
        }

        uint32_t cp;
        size_t used = decode_unit(s + i, len - i, &cp);
        char enc[4];
        size_t n;

        if (used == 0) {
            out[0] = '\0';
            return false;
        }
        n = utf8_encode(cp, enc, sizeof enc);
        if (n == 0 || o + n + 1 > cap) {
            out[0] = '\0';
            return false;
        }
        memcpy(out + o, enc, n);
        o += n;
        i += used;
    }
    out[o] = '\0';
    return true;
}

bool json_field_number(const char *doc, const char *path, double *out) {
    if (out == NULL) {
        return false;
    }

    const char *p = find_value(doc, path);
    if (p == NULL) {
        return false;
    }

    return parse_number(p, out);
}
