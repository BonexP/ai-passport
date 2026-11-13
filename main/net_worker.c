// main/net_worker.c —— 网络取数任务的实现。契约见 net_worker.h。
//
// 任务循环只有四步：读快照 → 更新各源的调度条件 → 取一个到期的源 → 睡 250 ms。
// 所有"该不该取"的判断都在 net_schedule（纯逻辑、有主机测试），
// 所有"怎么取"都在 net_http 与 weather_source / usage_source，本文件只做接线。
//
// 多源的关键约束是"每轮最多一次请求"：TLS 会话不可并发，否则 4 秒停止预算不成立。
// 所以这里刻意不写"遍历所有到期源"的循环，而是取第一个到期的就 break，剩下的
// 交给下一轮 250 ms 之后。
#include "net_worker.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
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
#include "usage_source.h"
#include "weather_source.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

static const char *TAG = "net_worker";

// 响应体上限。Open-Meteo 当前天气约 0.5 KB，AMD 用量报文约 0.6-0.8 KB；
// 4 KB 两个源共用一块缓冲，因为它们本来就不会同时读。
#define NET_BODY_MAX 4096

#define NET_TASK_STACK 6144
#define NET_TASK_PRIO 4
#define NET_TICK_MS 250

// 停止时等待在途请求的预算。输入任务会同步等它（配网必须先拿到射频），
// 所以不能照搬 8 s 的 HTTP 超时：取 4 s，覆盖绝大多数请求；
// 超过仍未退出才强制删除，并在日志里说明可能的后果。
#define NET_STOP_WAIT_MS 4000
#define NET_STOP_POLL_MS 50

// Authorization 头的静态大小。"Bearer " 7 字节 + key 上限 128 + NUL，再留些余量。
// key 本身由 app_config_check 限制在 128 字节以内。
#define NET_AUTH_MAX 160

// 一个数据源的运行时状态。三个字段都是"输入"，真正的节奏由 sched 决定。
typedef struct {
    net_schedule_t sched;
    volatile bool  visible;
    volatile bool  refresh;
} net_source_t;

// 静态响应缓冲：不与堆抢碎片，也不随请求次数增长。
static char s_body[NET_BODY_MAX];

// 上次成功落盘的天气快照与时刻，只在 worker 任务里访问。
static weather_state_t s_last_saved;
static bool            s_saved_valid;
static int64_t         s_last_save_ms = -1;

// 按 NET_SOURCE_* 的编号索引，见 net_worker.h 的顺序说明。
static net_source_t s_sources[NET_SOURCE_COUNT];

static TaskHandle_t  s_task;
static volatile bool s_running;
static volatile bool s_stop;

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

// ---- 天气：快照节流与落盘 ----

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

// 取一次天气并发布。返回 true 表示"已成功写入快照"，调度据此清零退避。
static bool fetch_weather_once(net_snapshot_store_t *store, const app_location_t *loc) {
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

// ---- 用量 ----

// HTTP 结果 + 状态码 → 对用户意味着什么。这是 worker 唯一的"翻译"职责，
// 页面只读 usage_status_t，不自己看状态码（它也没有状态码可看）。
//
// 注意 401 与 403 归为同一类：两者对用户都是"这把 key 不行"，区分它们只会
// 多一种没法处置的状态。
static usage_status_t classify_usage_result(http_result_t result, int status) {
    if (result == HTTP_RESULT_OK) {
        return USAGE_STATUS_OK;
    }
    if (result == HTTP_RESULT_TRUNCATED) {
        // 报文超过缓冲：内容不完整，绝不给半截数字。
        return USAGE_STATUS_TRUNCATED;
    }
    if (result == HTTP_RESULT_HTTP_STATUS) {
        if (status == 401 || status == 403) return USAGE_STATUS_KEY_REJECTED;
        if (status == 429) return USAGE_STATUS_RATE_LIMITED;
        // 5xx 与其余 4xx 一律按"服务不可用"：页面能做的动作都是重试。
        return USAGE_STATUS_UNAVAILABLE;
    }
    if (result == HTTP_RESULT_CONTENT_TYPE) {
        // 200 + 非 JSON：多半是网关门户页，不是用量报文。
        return USAGE_STATUS_BAD_BODY;
    }
    // TIMEOUT / TLS / TRANSPORT：连不上就是连不上，页面文案一致。
    return USAGE_STATUS_UNAVAILABLE;
}

// 发布一条"只改状态"的记录：把上一条记录的数字原样带上。
// 这是 usage_view 那条"失败不清空数字"语义的实现侧。
//
// read-then-write 两个临界区之间理论上可以被 UI 插入一次读取，但写者只有本任务，
// 没有任何第二个写者，所以不会丢更新；UI 最多看到一帧"旧数字+旧状态"。
static void publish_usage_status(usage_status_t status) {
    app_data_t current;
    app_data_read(app_data_shared(), &current);

    usage_record_t next = current.usage;
    next.status = status;
    app_data_publish_usage(app_data_shared(), &next);
}

// 取一次用量。返回 true 表示已成功解析并发布，调度据此清零退避。
//
// 失败路径一律走 publish_usage_status：它保留已有数字，只更新 status，
// 因此"key 被吊销的那天仍然能看到昨天剩多少"。
static bool fetch_usage_once(void) {
    const char *key = app_config_amd_key();
    // 没有 key 就不开 socket。调用方本应已经把该源设为不可见，这里是第二道闸：
    // 状态值也要落地，否则页面分不清"没配 key"和"还没取过"。
    if (key == NULL || key[0] == '\0') {
        publish_usage_status(USAGE_STATUS_NO_KEY);
        return false;
    }

    char url[USAGE_URL_MAX];
    if (!usage_url_build(url, sizeof url)) {
        ESP_LOGW(TAG, "用量 URL 组装失败");
        publish_usage_status(USAGE_STATUS_BAD_BODY);
        return false;
    }

    // Authorization 头在栈上组装：key 是编译期常量，这里只做一次拼接，
    // 不分配堆内存，也不在失败路径上留下需要释放的东西。
    char auth[NET_AUTH_MAX];
    const int auth_written = snprintf(auth, sizeof auth, "Bearer %s", key);
    if (auth_written < 0 || (size_t)auth_written >= sizeof auth) {
        ESP_LOGW(TAG, "AMD key 超出 Authorization 缓冲");
        publish_usage_status(USAGE_STATUS_NO_KEY);
        return false;
    }

    const net_http_header_t headers[] = {
        {"Authorization", auth},
        {"Accept", "application/json"},
    };

    size_t len = 0;
    int status = 0;
    const net_http_request_t req = {
        .url = url,
        .headers = headers,
        .header_count = sizeof headers / sizeof headers[0],
        .body = s_body,
        .body_cap = sizeof s_body,
        .body_len = &len,
        .status_out = &status,
        .timeout_ms = NET_HTTP_TIMEOUT_MS,
    };

    const http_result_t result = net_http_get_ex(&req);
    // 日志只报结果与状态码：URL 里没有 key，头里才有，决不能打印 auth。
    if (result != HTTP_RESULT_OK) {
        ESP_LOGW(TAG, "用量请求失败: result=%d status=%d len=%u", (int)result, status,
                 (unsigned)len);
        publish_usage_status(classify_usage_result(result, status));
        return false;
    }

    usage_state_t parsed;
    if (!usage_parse(s_body, &parsed)) {
        ESP_LOGW(TAG, "用量报文无法解析（%u 字节）", (unsigned)len);
        publish_usage_status(USAGE_STATUS_BAD_BODY);
        return false;
    }

    usage_record_t next;
    memset(&next, 0, sizeof next);
    next.usage = parsed;
    next.status = USAGE_STATUS_OK;
    next.fetched_epoch = (int32_t)app_time_now();
    next.fetched_uptime_ms = uptime_ms();
    app_data_publish_usage(app_data_shared(), &next);

    ESP_LOGI(TAG, "用量已更新: 剩 %ld 点(1e-4) 今日 %ld req", (long)next.usage.remaining_1e4,
             (long)next.usage.today_requests);
    return true;
}

// ---- 调度 ----

// 该源此刻能不能取数。除了 net_schedule 的三个门，还有两条本模块自己的规则：
//   1. 用量源没配 key 时永远不取（不发无意义的请求，也不刷 401）；
//   2. 天气源没有地点时永远不取（URL 组不出来）。
// 把这两条放在这里而不是塞进 net_schedule：调度器是通用的，它不认识 key 和地点。
static bool source_gate_open(net_source_id_t id, const app_location_t *loc) {
    if (id == NET_SOURCE_USAGE) {
        const char *key = app_config_amd_key();
        return key != NULL && key[0] != '\0';
    }
    // 天气
    return loc != NULL && loc->has_location;
}

// 取一次某个源。返回值直接喂给 net_schedule_on_success / on_failure。
static bool fetch_source(net_source_id_t id, net_snapshot_store_t *store,
                         const app_location_t *loc) {
    switch (id) {
    case NET_SOURCE_WEATHER: return fetch_weather_once(store, loc);
    case NET_SOURCE_USAGE:   return fetch_usage_once();
    case NET_SOURCE_COUNT:   break;
    }
    return false;
}

static void worker_task(void *arg) {
    (void)arg;

    // NVS 的打开与读取都放在这个任务里：启动路径握着 LVGL 锁，不能碰 flash。
    net_snapshot_store_t store;
    (void)net_snapshot_store_open(&store);
    load_saved(&store);

    for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
        net_schedule_init(&s_sources[i].sched);
    }

    app_location_t last_location;
    app_location_clear(&last_location);

    while (!s_stop) {
        wifi_status_snapshot_t wifi;
        wifi_status_read(wifi_status_shared(), &wifi);

        // 两个不同的事实，别合成一个：用量只需要"连着网"，天气还要"这个网络配了地点"。
        // 合成的话，一个没填地点的网络会让用量页永远显示 No network——而那次请求根本
        // 不需要地点，这是纯粹的功能缺失。
        const bool online = (wifi.phase == WIFI_PHASE_CONNECTED);
        const bool located = online && wifi.location.has_location;

        // 换网等于换地点：立即重新取天气，而不是等下一个 10 分钟。
        // 用量与地点无关，不受这条影响。
        if (located && !same_location(&last_location, &wifi.location)) {
            last_location = wifi.location;
            net_schedule_request_refresh(&s_sources[NET_SOURCE_WEATHER].sched);
            ESP_LOGI(TAG, "地点已切换: %s", wifi.location.place);
        } else if (!located) {
            app_location_clear(&last_location);
        }

        const bool time_valid = app_time_is_valid();
        for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
            // 天气的门是"有地点"，用量的门是"连着网"。
            const bool gate = (i == (size_t)NET_SOURCE_WEATHER) ? located : online;
            net_schedule_set_context(&s_sources[i].sched, gate, time_valid,
                                     s_sources[i].visible);
        }

        // 用户刷新标记：只消费一次，转成调度器的 force。
        for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
            if (s_sources[i].refresh) {
                s_sources[i].refresh = false;
                net_schedule_request_refresh(&s_sources[i].sched);
            }
        }

        // 每轮最多取一个源。顺序即 net_worker.h 里的抢占次序，
        // 到期的那个取完就跳出，剩下的等下一轮 250 ms。
        const int64_t now = uptime_ms();
        for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
            const net_source_id_t id = (net_source_id_t)i;
            // 门没开时不能调 begin()，否则会记下一次没发生的尝试。
            if (!source_gate_open(id, &wifi.location)) {
                continue;
            }
            if (!net_schedule_due(&s_sources[i].sched, now)) {
                continue;
            }

            net_schedule_begin(&s_sources[i].sched, now);
            if (fetch_source(id, &store, &wifi.location)) {
                net_schedule_on_success(&s_sources[i].sched, uptime_ms());
            } else {
                net_schedule_on_failure(&s_sources[i].sched, uptime_ms());
            }
            break; // 严格一次：TLS 不可并发，见文件头。
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
    for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
        s_sources[i].refresh = false;
        s_sources[i].visible = false;
    }
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
    for (size_t i = 0; i < NET_SOURCE_COUNT; i++) {
        s_sources[i].visible = false;
    }
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

void net_worker_set_visible(net_source_id_t id, bool visible) {
    if ((size_t)id >= (size_t)NET_SOURCE_COUNT) {
        return;
    }
    s_sources[(size_t)id].visible = visible;
}

void net_worker_request_refresh(net_source_id_t id) {
    if ((size_t)id >= (size_t)NET_SOURCE_COUNT) {
        return;
    }
    s_sources[(size_t)id].refresh = true;
}
