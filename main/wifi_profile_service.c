// main/wifi_profile_service.c —— store + 状态机与真实 Wi-Fi 驱动的接线。
//
// 线程模型：只有一个 worker 任务会碰 s_policy / s_store / s_deadline_ms。
// esp_event 回调（跑在系统事件任务上）只做 xQueueSend，不碰 LVGL、不读 NVS、
// 不阻塞；连接成功后的"晋升 active + 落盘"也在 worker 里做，不在回调里做。
#include "wifi_profile_service.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wifi_connect_policy.h"
#include "wifi_presets.h"
#include "wifi_profile.h"
#include "wifi_profile_nvs.h"

static const char *TAG = "wifi_profile";

// 栈与优先级对齐仓库既有约定（main.c 的 input task 是 5 / 4096）。网络任务刻意低
// 一级，保证按键分发永远先拿到 CPU。
#define SERVICE_TASK_STACK 4096
#define SERVICE_TASK_PRIO 4
#define SERVICE_QUEUE_DEPTH 8
// stop() 等 worker 退出的上限，与 demo_ble 的 NimBLE 停止等待取同一量级。
#define SERVICE_STOP_TIMEOUT_MS 2000

typedef enum {
    SVC_EV_GOT_IP = 0,
    SVC_EV_DISCONNECTED,
    SVC_EV_STOP,
} svc_event_t;

static wifi_profile_nvs_t     s_nvs;
static wifi_profile_store_t   s_store;
static wifi_connect_policy_t  s_policy;
static QueueHandle_t          s_queue;
static TaskHandle_t           s_task;
static esp_netif_t           *s_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static volatile bool          s_running;
static int64_t                s_deadline_ms; // 仅 worker 读写

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void send_event(svc_event_t ev) {
    if (s_queue == NULL) return;
    (void)xQueueSend(s_queue, &ev, 0);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id != WIFI_EVENT_STA_DISCONNECTED) return;
    send_event(SVC_EV_DISCONNECTED);
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id != IP_EVENT_STA_GOT_IP) return;
    send_event(SVC_EV_GOT_IP);
}

// 连上之后把这条记为 active 并落盘，下次开机先试它。
//
// 只记"哪个连上了"，不记"什么时候连上的"：状态机唯一需要的输入就是 active，
// 而设备没有 RTC，时间戳跨重启没有意义。多存一个没人读的字段只会让人误以为
// 存在按新鲜度排序的逻辑。
static void promote_active(int8_t index) {
    if (wifi_profile_store_at(&s_store, (uint8_t)index) == NULL) return;
    if (wifi_profile_store_set_active(&s_store, index) != WIFI_PROFILE_OK) return;
    if (wifi_profile_store_save(&s_store) != WIFI_PROFILE_OK) {
        ESP_LOGW(TAG, "profile 写入 NVS 失败，本次选择只在内存里生效");
    }
}

static void connect_index(int8_t index) {
    const wifi_profile_t *p = wifi_profile_store_at(&s_store, (uint8_t)index);
    if (p == NULL) return;

    size_t ssid_len = strlen(p->ssid);
    size_t pass_len = strlen(p->password);
    if (ssid_len == 0 || ssid_len > sizeof(((wifi_sta_config_t *)0)->ssid)) return;
    if (pass_len > sizeof(((wifi_sta_config_t *)0)->password)) return;

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.sta.ssid, p->ssid, ssid_len);
    memcpy(cfg.sta.password, p->password, pass_len);
    // threshold.authmode 保持 0（WIFI_AUTH_OPEN）= 不设安全门槛，让 WPA/WPA2/WPA3
    // 都能协商；设成 WPA2_PSK 反而会挡掉只支持 WPA3 的路由器。

    // 日志只写序号和 SSID，绝不打印密码。
    ESP_LOGI(TAG, "尝试 profile %d (%s)", (int)index, p->ssid);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "profile %d 配置失败: %s", (int)index, esp_err_to_name(err));
        send_event(SVC_EV_DISCONNECTED); // 让状态机当作本次尝试失败继续往后走
        return;
    }

    (void)esp_wifi_disconnect(); // 上一轮可能还连着；未连接时返回错误无所谓
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "profile %d 发起连接失败: %s", (int)index, esp_err_to_name(err));
        send_event(SVC_EV_DISCONNECTED);
    }
}

static void apply_step(wifi_conn_step_t step) {
    switch (step.action) {
    case WIFI_CONN_ACT_CONNECT:
        s_deadline_ms = step.timeout_ms > 0 ? now_ms() + (int64_t)step.timeout_ms : 0;
        connect_index(step.index);
        break;
    case WIFI_CONN_ACT_PROVISION:
        s_deadline_ms = 0;
        ESP_LOGW(TAG, "没有可用的 Wi-Fi profile，等待配网");
        break;
    case WIFI_CONN_ACT_STOPPED:
        s_deadline_ms = 0;
        break;
    case WIFI_CONN_ACT_NONE:
        s_deadline_ms = 0;
        // 只有 ATTEMPT_OK 会带回非负 index；其余 NONE 都是"忽略该事件"。
        if (step.index >= 0) promote_active(step.index);
        break;
    }
}

static void service_task(void *arg) {
    (void)arg;

    apply_step(wifi_connect_policy_event(&s_policy, WIFI_CONN_EV_START, now_ms()));

    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (s_deadline_ms > 0) {
            int64_t remaining = s_deadline_ms - now_ms();
            if (remaining < 0) remaining = 0;
            wait = pdMS_TO_TICKS((uint32_t)remaining) + 1; // +1 避免 0 tick 的忙等
        }

        svc_event_t ev;
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            if (ev == SVC_EV_STOP) break;
            wifi_conn_event_t policy_ev;
            if (ev == SVC_EV_GOT_IP) {
                policy_ev = WIFI_CONN_EV_ATTEMPT_OK;
            } else {
                // 已经连上时掉线要整轮重跑，还在尝试时只算这一次失败。
                policy_ev = (wifi_connect_policy_state(&s_policy) == WIFI_CONN_CONNECTED)
                                ? WIFI_CONN_EV_DISCONNECT
                                : WIFI_CONN_EV_ATTEMPT_FAIL;
            }
            apply_step(wifi_connect_policy_event(&s_policy, policy_ev, now_ms()));
        } else {
            apply_step(wifi_connect_policy_tick(&s_policy, now_ms()));
        }
    }

    s_running = false;
    vTaskDelete(NULL);
}

static void teardown_stack(void) {
    if (s_wifi_handler != NULL || s_ip_handler != NULL) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                    s_wifi_handler);
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_wifi_handler = NULL;
        s_ip_handler = NULL;
    }
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    if (s_netif != NULL) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
}

bool wifi_profile_service_running(void) {
    return s_running;
}

esp_err_t wifi_profile_service_start(void) {
    if (s_running) return ESP_ERR_INVALID_STATE;

    esp_err_t err = wifi_profile_nvs_open(&s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s；本次按无 profile 处理", esp_err_to_name(err));
        return err;
    }
    wifi_kv_t kv = wifi_profile_nvs_kv(&s_nvs);
    if (wifi_profile_store_load(&s_store, &kv) != WIFI_PROFILE_OK) {
        ESP_LOGW(TAG, "profile 读取失败，按空库处理");
    }

    // 预设注入放在 load 之后、netif 与驱动初始化之前：worker 在第一次
    // WIFI_CONN_EV_START 时就会读 store，这里必须先让 store 定型。
    // 不写 `if (count == 0)` 守卫：策略属于已被主机测试覆盖的 wifi_presets.c，
    // 这一层只负责把四种结果翻成日志。
    wifi_preset_seed_report_t seed = { .added = 0, .skipped = 0 };
    switch (wifi_preset_seed_builtin(&s_store, &seed)) {
    case WIFI_PRESET_SEED_APPLIED:
        ESP_LOGI(TAG, "注入预设 %u 条（跳过 %u 条）", (unsigned)seed.added,
                 (unsigned)seed.skipped);
        break;
    case WIFI_PRESET_SEED_ERR_SAVE:
        ESP_LOGW(TAG, "预设已进内存但未落盘：本次开机可用，下次开机重试");
        break;
    case WIFI_PRESET_SEED_LIBRARY_NOT_EMPTY:
    case WIFI_PRESET_SEED_NOTHING_TO_ADD:
        break; // 正常路径，不刷日志
    }

    // 默认事件循环通常已由 demo_radio 建好；重复创建返回 INVALID_STATE 属正常。
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "事件循环创建失败: %s", esp_err_to_name(err));
        goto fail;
    }

    // 不用 esp_netif_create_default_wifi_sta()：它失败时会 assert，而这条路径允许失败。
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    s_netif = esp_netif_new(&netif_cfg);
    if (s_netif == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    err = esp_netif_attach_wifi_station(s_netif);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK) goto fail;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) goto fail;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM); // 凭据在我们自己的 namespace 里
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;

    err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                              on_wifi_event, NULL, &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL,
                                              &s_ip_handler);
    if (err != ESP_OK) goto fail;

    s_queue = xQueueCreate(SERVICE_QUEUE_DEPTH, sizeof(svc_event_t));
    if (s_queue == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    wifi_connect_policy_init(&s_policy, &s_store);
    s_deadline_ms = 0;
    s_running = true;

    if (xTaskCreate(service_task, "wifi_profile", SERVICE_TASK_STACK, NULL, SERVICE_TASK_PRIO,
                    &s_task) != pdPASS) {
        s_running = false;
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    ESP_LOGI(TAG, "Wi-Fi profile 管理器已启动，共 %u 条",
             (unsigned)wifi_profile_store_count(&s_store));
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "Wi-Fi profile 管理器启动失败: %s", esp_err_to_name(err));
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    teardown_stack();
    wifi_profile_nvs_close(&s_nvs);
    s_running = false;
    return err;
}

void wifi_profile_service_stop(void) {
    if (!s_running) return;

    svc_event_t stop = SVC_EV_STOP;
    if (s_queue != NULL) {
        (void)xQueueSend(s_queue, &stop, pdMS_TO_TICKS(SERVICE_STOP_TIMEOUT_MS));
    }

    // worker 会自己 vTaskDelete；这里只等到它交出 s_running，超时再强删。
    for (int waited = 0; waited < SERVICE_STOP_TIMEOUT_MS / 10 && s_running; waited++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_running && s_task != NULL) {
        vTaskDelete(s_task);
        s_running = false;
    }
    s_task = NULL;

    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }

    teardown_stack();
    wifi_profile_nvs_close(&s_nvs);
    s_deadline_ms = 0;
}
