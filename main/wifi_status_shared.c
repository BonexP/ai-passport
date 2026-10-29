// main/wifi_status_shared.c —— 静态初始化的共享快照总线。
#include "wifi_status_shared.h"

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void shared_lock(void *ctx) {
    (void)ctx;
    portENTER_CRITICAL(&s_mux);
}

static void shared_unlock(void *ctx) {
    (void)ctx;
    portEXIT_CRITICAL(&s_mux);
}

static const wifi_status_lock_t s_lock = {
    .ctx = NULL,
    .lock = shared_lock,
    .unlock = shared_unlock,
};

// 静态初始化而不是首次调用时调用 wifi_status_bus_init()：
// 状态页（LVGL 任务）与服务（worker）可能同时第一次访问。
static wifi_status_bus_t s_bus = {
    .snapshot = { .phase = WIFI_PHASE_IDLE, .active = -1, .trying = -1, .rssi = -128 },
    .lock = &s_lock,
};

wifi_status_bus_t *wifi_status_shared(void) { return &s_bus; }
