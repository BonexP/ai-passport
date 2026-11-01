// main/net_snapshot.c —— 离线快照编解码的实现。契约见 net_snapshot.h。
#include "net_snapshot.h"

#include <string.h>

// 布局（全部小端）：
//   0  u8  version
//   1  u8  flags      bit0 = has_data
//   2  i16 temp_dc
//   4  i16 weather_code
//   6  i32 fetched_epoch
//   10 u8  place_len
//   11 u8  observed_len
//   12 ... place | observed   （无结尾 NUL）
#define SNAPSHOT_HEADER 12u

static size_t bounded_strlen(const char *s, size_t cap) {
    size_t n = 0;

    if (s == NULL) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

static void put_i16le(uint8_t *p, int16_t value) {
    uint16_t raw = (uint16_t)value;
    p[0] = (uint8_t)(raw & 0xFFu);
    p[1] = (uint8_t)((raw >> 8) & 0xFFu);
}

static int16_t get_i16le(const uint8_t *p) {
    uint16_t raw = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return (int16_t)raw;
}

static void put_i32le(uint8_t *p, int32_t value) {
    uint32_t raw = (uint32_t)value;
    p[0] = (uint8_t)(raw & 0xFFu);
    p[1] = (uint8_t)((raw >> 8) & 0xFFu);
    p[2] = (uint8_t)((raw >> 16) & 0xFFu);
    p[3] = (uint8_t)((raw >> 24) & 0xFFu);
}

static int32_t get_i32le(const uint8_t *p) {
    uint32_t raw = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[3] << 24);
    return (int32_t)raw;
}

bool net_snapshot_save_due(bool saved_valid, bool changed, int64_t last_save_ms, int64_t now_ms) {
    if (!saved_valid) {
        return true; // 本次开机还没有任何快照
    }
    if (changed) {
        return true; // 内容变了就写，不必等窗口
    }
    if (last_save_ms < 0) {
        return true; // 本次开机还没写过（例如刚从 NVS 载入）
    }
    return now_ms - last_save_ms >= NET_SNAPSHOT_SAVE_INTERVAL_MS;
}

size_t net_snapshot_encode(const weather_state_t *w, uint8_t *buf, size_t cap) {
    if (w == NULL || buf == NULL || !w->has_data) {
        return 0;
    }

    size_t place_len = bounded_strlen(w->place, sizeof w->place);
    size_t observed_len = bounded_strlen(w->observed, sizeof w->observed);
    if (place_len >= sizeof w->place || observed_len >= sizeof w->observed) {
        return 0; // 缓冲区内没有结尾
    }

    size_t total = SNAPSHOT_HEADER + place_len + observed_len;
    if (total > cap || total > NET_SNAPSHOT_MAX) {
        return 0;
    }

    buf[0] = (uint8_t)NET_SNAPSHOT_VERSION;
    buf[1] = 0x01u; // has_data
    put_i16le(buf + 2, w->temp_dc);
    put_i16le(buf + 4, w->weather_code);
    put_i32le(buf + 6, w->fetched_epoch);
    buf[10] = (uint8_t)place_len;
    buf[11] = (uint8_t)observed_len;

    uint8_t *cursor = buf + SNAPSHOT_HEADER;
    memcpy(cursor, w->place, place_len);
    cursor += place_len;
    memcpy(cursor, w->observed, observed_len);
    return total;
}

bool net_snapshot_decode(const uint8_t *buf, size_t len, weather_state_t *out) {
    if (buf == NULL || out == NULL || len < SNAPSHOT_HEADER) {
        return false;
    }
    if (buf[0] != (uint8_t)NET_SNAPSHOT_VERSION) {
        return false;
    }
    // 只认识 bit0；新 flag 必须同时升版本号。
    if ((buf[1] & 0xFEu) != 0) {
        return false;
    }

    size_t place_len = buf[10];
    size_t observed_len = buf[11];
    if (place_len >= APP_LOCATION_PLACE_MAX || observed_len >= APP_DATA_OBSERVED_MAX) {
        return false;
    }
    if (len != SNAPSHOT_HEADER + place_len + observed_len) {
        return false; // 多一个字节都当作损坏
    }

    memset(out, 0, sizeof *out);
    out->has_data = (buf[1] & 0x01u) != 0;
    // 磁盘上的东西一律是旧数据：页面据此显示 Offline - saved。
    out->stale = true;
    out->fetched_uptime_ms = -1; // 跨重启的单调时间没有意义
    out->temp_dc = get_i16le(buf + 2);
    out->weather_code = get_i16le(buf + 4);
    out->fetched_epoch = get_i32le(buf + 6);

    const uint8_t *cursor = buf + SNAPSHOT_HEADER;
    if (place_len > 0) {
        memcpy(out->place, cursor, place_len);
    }
    cursor += place_len;
    if (observed_len > 0) {
        memcpy(out->observed, cursor, observed_len);
    }
    return true;
}
