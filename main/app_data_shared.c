// main/app_data_shared.c —— 快照总线的设备侧单例。契约见 app_data_shared.h。
#include "app_data_shared.h"

#include "freertos/FreeRTOS.h"

// 临界区只保护"拷贝整份结构体"这一小段；发布与读取都是微秒级。
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void shared_lock(void *ctx) {
    (void)ctx;
    portENTER_CRITICAL(&s_mux);
}

static void shared_unlock(void *ctx) {
    (void)ctx;
    portEXIT_CRITICAL(&s_mux);
}

static const app_data_lock_t s_ops = {
    .ctx = NULL,
    .lock = shared_lock,
    .unlock = shared_unlock,
};

// 静态初始化，与 app_data_bus_init() 的结果等价：全零 + fetched_uptime_ms = -1 + 绑定锁。
// 写成初始化器而不是调用 init，是为了让"发布者先跑"这条路径不需要任何启动顺序保证。
static app_data_bus_t s_bus = {
    .data =
        {
            .weather =
                {
                    .fetched_uptime_ms = -1,
                },
            .usage =
                {
                    .fetched_uptime_ms = -1,
                },
        },
    .lock = &s_ops,
};

app_data_bus_t *app_data_shared(void) {
    return &s_bus;
}
