// main/app_data.c —— 网络数据快照总线的实现。契约见 app_data.h。
#include "app_data.h"

#include <string.h>

static void bus_lock(const app_data_bus_t *bus) {
    if (bus->lock != NULL && bus->lock->lock != NULL) {
        bus->lock->lock(bus->lock->ctx);
    }
}

static void bus_unlock(const app_data_bus_t *bus) {
    if (bus->lock != NULL && bus->lock->unlock != NULL) {
        bus->lock->unlock(bus->lock->ctx);
    }
}

void app_data_bus_init(app_data_bus_t *bus, const app_data_lock_t *lock) {
    if (bus == NULL) {
        return;
    }
    memset(&bus->data, 0, sizeof bus->data);
    // -1 表示"本次开机还没取过数"，与 0（开机瞬间）区分开。
    bus->data.weather.fetched_uptime_ms = -1;
    bus->lock = lock;
}

void app_data_publish_weather(app_data_bus_t *bus, const weather_state_t *next) {
    if (bus == NULL || next == NULL) {
        return;
    }

    bus_lock(bus);
    // 序号由总线维护：写者只负责内容，忘了递增也不会让 UI 停在旧帧。
    uint16_t seq = (uint16_t)(bus->data.weather.seq + 1u);
    bus->data.weather = *next;
    bus->data.weather.seq = seq;
    bus_unlock(bus);
}

void app_data_read(const app_data_bus_t *bus, app_data_t *out) {
    if (bus == NULL || out == NULL) {
        return;
    }

    bus_lock(bus);
    *out = bus->data;
    bus_unlock(bus);
}
