// main/net_worker.c —— 网络取数任务的实现。契约见 net_worker.h。
//
// 任务循环只有四步：读快照 → 更新调度条件 → 到点就取数 → 睡 250 ms。
// 所有"该不该取"的判断都在 net_schedule（纯逻辑、有主机测试），
// 所有"怎么取"都在 net_http 与 weather_source，本文件只做接线。
#include "net_worker.h"

#include <stdio.h>
#include <string.h>

#include "app_data_shared.h"
#include "app_time.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_http.h"
#include "net_schedule.h"
#include "net_snapshot.h"
#include "net_snapshot_store.h"
#include "weather_source.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

static const char *TAG = "net_worker";

// 响应体上限。Open-Meteo 当前天气约 0.5 KB；4 KB 留出冗余，P3 行情再评估。
#define NET_BODY_MAX 4096

#define NET_TASK_STACK 6144
#define NET_TASK_PRIO 4
#define NET_TICK_MS 250

// 停止时等待在途请求的预算。输入任务会同步等它（配网必须先拿到射频），
// 所以不能照搬 8 s 的 HTTP 超时：取 4 s，覆盖绝大多数请求；
// 超过仍未退出才强制删除，并在日志里说明可能的后果。
#define NET_STOP_WAIT_MS 4000
#define NET_STOP_POLL_MS 50

// 静态响应缓冲：不与堆抢碎片，也不随请求次数增长。
static char s_body[NET_BODY_MAX];

// 上次成功落盘的快照与时刻，只在 worker 任务里访问。
static weather_state_t s_last_saved;
static bool            s_saved_valid;
static int64_t         s_last_save_ms = -1;

static TaskHandle_t  s_task;
static volatile bool s_running;
static volatile bool s_stop;
static volatile bool s_visible;
static volatile bool s_refresh;

// strnlen 是 POSIX 而非 ISO C11，固件与主机工具的声明条件不同，自己实现更稳。
static size_t bounded_strlen(const char *s, size_t cap) {
    size_t n = 0;

    if (s == NULL) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

static void copy_text(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = bounded_strlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool same_location(const app_location_t *a, const app_location_t *b) {
    if (a->has_location != b->has_location) return false;
    if (!a->has_location) return true;
    return a->lat_udeg == b->lat_udeg && a->lon_udeg == b->lon_udeg &&
           strcmp(a->place, b->place) == 0;
}

static int64_t uptime_ms(void) {
    return esp_timer_get_time() / 1000;
}

// 只比较页面上真正会变的三个字段。observed 每次取数都会变，把它算进"内容变化"
// 等于每次成功都写 flash，正是节流要避免的。
static bool snapshot_changed(const weather_state_t *a, const weather_state_t *b) {
    return a->temp_dc != b->temp_dc || a->weather_code != b->weather_code ||
           strcmp(a->place, b->place) != 0;
}

// 按节流策略决定要不要落盘。失败不重试到死：下一次成功取数会再走一遍这里。
static void persist_if_due(net_snapshot_store_t *store, const weather_state_t *w) {
    if (store == NULL || !store->open) {
        return;
    }

    const int64_t now = uptime_ms();
    const bool changed = s_saved_valid && snapshot_changed(w, &s_last_saved);
    if (!net_snapshot_save_due(s_saved_valid, changed, s_last_save_ms, now)) {
        return;
    }

    if (net_snapshot_store_save(store, w) == ESP_OK) {
        s_last_saved = *w;
        s_saved_valid = true;
        s_last_save_ms = now;
    } else {
        ESP_LOGW(TAG, "快照落盘失败，下次成功取数再试");
    }
}

// 开机先把上次的快照发布出去：没网时页面也能显示 Offline - saved，
// 而不是一片空白。解码时已把 stale 置为 true、uptime 置为 -1。
static void load_saved(net_snapshot_store_t *store) {
    weather_state_t saved;
    if (net_snapshot_store_load(store, &saved) != ESP_OK) {
        return;
    }
    app_data_publish_weather(app_data_shared(), &saved);
    s_last_saved = saved;
    s_saved_valid = true;
    s_last_save_ms = -1; // 本次开机还没写过
    ESP_LOGI(TAG, "已载入离线快照: %s", saved.place);
}

// 取一次数并发布。返回 true 表示"已成功写入快照"，调度据此清零退避。
static bool fetch_once(net_snapshot_store_t *store, const app_location_t *loc) {
    char url[WEATHER_URL_MAX];
    if (!weather_url_build(loc, url, sizeof url)) {
        ESP_LOGW(TAG, "地点不完整，跳过取数");
        return false;
    }

    size_t len = 0;
    http_result_t result = net_http_get(url, s_body, sizeof s_body, &len, NET_HTTP_TIMEOUT_MS);
    if (result != HTTP_RESULT_OK) {
        ESP_LOGW(TAG, "天气请求失败: result=%d len=%u", (int)result, (unsigned)len);
        return false;
    }

    weather_state_t next;
    if (!weather_parse_current(s_body, &next)) {
        ESP_LOGW(TAG, "天气报文无法解析（%u 字节）", (unsigned)len);
        return false;
    }

    next.fetched_epoch = (int32_t)app_time_now();
    next.fetched_uptime_ms = uptime_ms();
    copy_text(next.place, sizeof next.place, loc->place);
    app_data_publish_weather(app_data_shared(), &next);
    persist_if_due(store, &next);
    ESP_LOGI(TAG, "天气已更新: %d.%d C code=%d", (int)(next.temp_dc / 10),
             (int)(next.temp_dc % 10), (int)next.weather_code);
    return true;
}

static void worker_task(void *arg) {
    (void)arg;

    // NVS 的打开与读取都放在这个任务里：启动路径握着 LVGL 锁，不能碰 flash。
    net_snapshot_store_t store;
    (void)net_snapshot_store_open(&store);
    load_saved(&store);

    net_schedule_t sched;
    net_schedule_init(&sched);

    app_location_t last_location;
    app_location_clear(&last_location);

    while (!s_stop) {
        wifi_status_snapshot_t wifi;
        wifi_status_read(wifi_status_shared(), &wifi);

        const bool connected =
            (wifi.phase == WIFI_PHASE_CONNECTED) && wifi.location.has_location;

        // 换网等于换地点：立即重新取数，而不是等下一个 10 分钟。
        if (connected && !same_location(&last_location, &wifi.location)) {
            last_location = wifi.location;
            net_schedule_request_refresh(&sched);
            ESP_LOGI(TAG, "地点已切换: %s", wifi.location.place);
        } else if (!connected) {
            app_location_clear(&last_location);
        }

        net_schedule_set_context(&sched, connected, app_time_is_valid(), s_visible);

        if (s_refresh) {
            s_refresh = false;
            net_schedule_request_refresh(&sched);
        }

        if (net_schedule_due(&sched, uptime_ms())) {
            net_schedule_begin(&sched, uptime_ms());
            if (fetch_once(&store, &wifi.location)) {
                net_schedule_on_success(&sched, uptime_ms());
            } else {
                net_schedule_on_failure(&sched, uptime_ms());
            }
        }

        vTaskDelay(pdMS_TO_TICKS(NET_TICK_MS));
    }

    net_snapshot_store_close(&store);
    s_running = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t net_worker_start(void) {
    if (s_running) {
        return ESP_OK;
    }

    s_stop = false;
    s_refresh = false;
    if (xTaskCreate(worker_task, "net_worker", NET_TASK_STACK, NULL, NET_TASK_PRIO, &s_task) !=
        pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    return ESP_OK;
}

void net_worker_stop(void) {
    if (!s_running) {
        return;
    }

    // 先关门再停任务：任务不会在停止过程中因为"页面可见"又发起新请求。
    s_visible = false;
    s_stop = true;

    for (int waited = 0; waited < NET_STOP_WAIT_MS && s_running; waited += NET_STOP_POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(NET_STOP_POLL_MS));
    }

    if (s_running && s_task != NULL) {
        // 走到这里说明在途请求超过预算。强删会泄漏该次 TLS 会话，但射频必须交还，
        // 否则配网根本起不来；把这句日志当作需要复现的信号。
        ESP_LOGE(TAG, "worker 未在预算内退出，强制回收（本次 TLS 会话可能泄漏）");
        vTaskDelete(s_task);
        s_task = NULL;
        s_running = false;
    }
}

bool net_worker_running(void) {
    return s_running;
}

void net_worker_set_visible(bool visible) {
    s_visible = visible;
}

void net_worker_request_refresh(void) {
    s_refresh = true;
}
