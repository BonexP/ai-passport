// main/net_schedule.c —— 取数调度的实现。契约见 net_schedule.h。
#include "net_schedule.h"

#include <stddef.h>

void net_schedule_init(net_schedule_t *s) {
    if (s == NULL) {
        return;
    }
    s->connected = false;
    s->time_valid = false;
    s->visible = false;
    s->force = false;
    s->failures = 0;
    s->last_attempt_ms = -1;
}

void net_schedule_set_context(net_schedule_t *s, bool connected, bool time_valid, bool visible) {
    if (s == NULL) {
        return;
    }
    s->connected = connected;
    s->time_valid = time_valid;
    s->visible = visible;
}

uint32_t net_schedule_interval_ms(const net_schedule_t *s) {
    if (s == NULL || s->failures == 0) {
        return NET_SCHEDULE_INTERVAL_MS;
    }

    // 30 s << (failures-1)，移位上限取到封顶值即可，避免对 8 位计数做无意义的移位。
    uint32_t interval = NET_SCHEDULE_BACKOFF_BASE_MS;
    for (uint8_t i = 1; i < s->failures && interval < NET_SCHEDULE_BACKOFF_MAX_MS; i++) {
        interval *= 2u;
    }
    return interval > NET_SCHEDULE_BACKOFF_MAX_MS ? NET_SCHEDULE_BACKOFF_MAX_MS : interval;
}

bool net_schedule_due(const net_schedule_t *s, int64_t now_ms) {
    if (s == NULL) {
        return false;
    }
    // 门没开时不取数：没有网、时间不可信（证书会失败）、页面不可见都不该唤醒射频。
    if (!s->connected || !s->time_valid || !s->visible) {
        return false;
    }
    if (s->force) {
        return true;
    }
    if (s->last_attempt_ms < 0) {
        return true;
    }
    return now_ms - s->last_attempt_ms >= (int64_t)net_schedule_interval_ms(s);
}

void net_schedule_begin(net_schedule_t *s, int64_t now_ms) {
    if (s == NULL) {
        return;
    }
    s->force = false;
    s->last_attempt_ms = now_ms;
}

void net_schedule_on_success(net_schedule_t *s, int64_t now_ms) {
    if (s == NULL) {
        return;
    }
    s->failures = 0;
    s->last_attempt_ms = now_ms;
}

void net_schedule_on_failure(net_schedule_t *s, int64_t now_ms) {
    if (s == NULL) {
        return;
    }
    if (s->failures < UINT8_MAX) {
        s->failures++;
    }
    s->last_attempt_ms = now_ms;
}

void net_schedule_request_refresh(net_schedule_t *s) {
    if (s != NULL) {
        s->force = true;
    }
}
