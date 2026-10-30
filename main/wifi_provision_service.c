// main/wifi_provision_service.c —— SoftAP 配网：热点 + 最小网页表单 + 保存后重启。
//
// 两条不变量，改动前先读：
//
//   1. **同一时刻只有一个服务持有射频**（wifi_radio_owner）。所以状态快照只有一个写者，
//      本文件发布时不需要与联网服务互锁。
//
//   2. **HTTP handler 只碰 socket，worker 只碰 NVS 与驱动**。handler 把表单解析结果
//      放进 s_pending_form 后入队并等 s_reply_sem；worker 收到后落盘，把结论写进
//      s_reply 再 give。两者之间没有别的共享可变状态。stop() 的第一步是 httpd_stop()，
//      它会 join httpd 任务，因此队列与信号量不可能在 handler 还在用时被删。
//
// 重启一定要从 worker 发出：handler 里直接 esp_restart() 会丢掉响应，主机测试也无法
// 建模（桩用 longjmp 模拟 noreturn，只能在 test_run_worker 的上下文里跳）。
#include "wifi_provision_service.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "wifi_profile_nvs.h"
#include "wifi_profile_service.h"
#include "wifi_provision_flow.h"
#include "wifi_provision_form.h"
#include "wifi_provision_html.h"
#include "wifi_radio_owner.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

static const char *TAG = "wifi_prov";

#define PROV_TASK_STACK       4096
#define PROV_TASK_PRIO        4
#define PROV_QUEUE_DEPTH      4
#define PROV_TICK_MS          1000u
#define PROV_RESTART_MS       1500u
#define PROV_STOP_TIMEOUT_MS  5000u
#define PROV_SAVE_WAIT_MS     3000u
#define PROV_RECV_RETRIES     2u
#define PROV_PAGE_MAX         1024u
#define PROV_CONTENT_LEN_MAX  20u /* 19 位数字足够；再长必然是超上限 */

typedef enum {
    PROV_MSG_SUBMIT = 0,  // handler → worker
    PROV_MSG_RESP_SENT,   // handler → worker：响应已经发出去，可以重启了
    PROV_MSG_STOP,        // stop() → worker
    PROV_MSG_CLIENT_JOINED,
    PROV_MSG_CLIENT_LEFT,
} prov_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t form_result; // wifi_form_result_t，仅 SUBMIT 有效
} prov_msg_t;

typedef enum {
    PROV_REPLY_NONE = 0,
    PROV_REPLY_SAVED,
    PROV_REPLY_SAVE_FAILED,
    PROV_REPLY_REJECT,
} prov_reply_t;

typedef struct {
    uint8_t kind;
    uint8_t error; // wifi_form_result_t，仅 REJECT 有效
} prov_reply_msg_t;

static bool                s_active;
static bool                s_session_open;
static bool                s_closing;      // 超时已自关，只等 STOP
static volatile bool       s_worker_alive;

static esp_netif_t        *s_netif;
static httpd_handle_t      s_httpd;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static QueueHandle_t       s_queue;
static SemaphoreHandle_t   s_reply_sem;
static TaskHandle_t        s_task;

static wifi_profile_nvs_t    s_nvs;
static wifi_profile_store_t  s_store;
static wifi_provision_flow_t s_flow;
static prov_reply_msg_t      s_reply;
static wifi_provision_form_t s_pending_form; // handler 写、worker 读（handler 全程阻塞等回复）
static int64_t               s_restart_at_ms; // 0 = 未武装
static uint8_t               s_clients;
static char                  s_ap_ssid[WIFI_PROFILE_SSID_MAX];
static char                  s_ap_ip[WIFI_STATUS_IP_MAX];

/* 分层守卫：拆栈是无条件的，但"没起来的那一层"不该调 stop/deinit。 */
static bool s_netif_inited;
static bool s_wifi_inited;
static bool s_wifi_started;

/* handler 串行执行，可以安全复用；放静态是为了不占 4 KB 任务栈。 */
static char s_body[WIFI_PROV_BODY_MAX + 1];
static char s_page[PROV_PAGE_MAX];

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    size_t len = (src != NULL) ? strlen(src) : 0;
    if (len >= cap) len = cap - 1;
    if (len > 0) memcpy(dst, src, len);
    dst[len] = '\0';
}

static void publish_prov(void) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.phase = WIFI_PHASE_PROVISIONING;
    s.active = -1;
    s.trying = -1;
    s.rssi = -128;
    s.ap_clients = s_clients;
    s.profile_count = wifi_profile_store_count(&s_store);
    copy_str(s.ssid, sizeof s.ssid, s_ap_ssid);
    copy_str(s.ip, sizeof s.ip, s_ap_ip);
    wifi_status_publish(wifi_status_shared(), &s);
}

static void publish_idle(void) {
    wifi_status_snapshot_t s;
    memset(&s, 0, sizeof s);
    s.phase = WIFI_PHASE_IDLE;
    s.active = -1;
    s.trying = -1;
    s.rssi = -128;
    wifi_status_publish(wifi_status_shared(), &s);
}

// AP 名取 SoftAP MAC 的后两字节。失败时退回固定后缀：宁可两台设备撞名，
// 也不要让用户面对一个连不上的"Passport-"。
static void make_ap_ssid(void) {
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) {
        snprintf(s_ap_ssid, sizeof s_ap_ssid, "Passport-%02X%02X", mac[4], mac[5]);
    } else {
        copy_str(s_ap_ssid, sizeof s_ap_ssid, "Passport-0000");
        ESP_LOGW(TAG, "读 MAC 失败，热点名退回固定后缀");
    }
}

static void http_stop(void) {
    if (s_httpd != NULL) {
        (void)httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

// 幂等的"关掉这一轮"。超时自关与 stop() 都走这里。
static void session_close(void) {
    if (!s_session_open) return;
    s_session_open = false;

    // 顺序不能改：HTTP 先停（之后没有 handler 会碰队列/信号量），
    // 再注销事件处理器，最后拆驱动与 netif。
    http_stop();
    if (s_wifi_handler != NULL) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler = NULL;
    }
    if (s_ip_handler != NULL) {
        (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED,
                                                    s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_wifi_started) {
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_inited) {
        (void)esp_wifi_deinit();
        s_wifi_inited = false;
    }
    if (s_netif != NULL) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
    wifi_profile_nvs_close(&s_nvs);
    (void)wifi_radio_release(wifi_radio_shared(), WIFI_RADIO_PROVISION);
}

/* ---- HTTP ---- */

static esp_err_t send_html(httpd_req_t *req, const char *status, const char *body, size_t len) {
    if (status != NULL) (void)httpd_resp_set_status(req, status);
    (void)httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (body == NULL || len == 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "render failed");
    }
    return httpd_resp_send(req, body, (ssize_t)len);
}

static esp_err_t send_error_page(httpd_req_t *req, const char *status, wifi_form_result_t err,
                                 const char *ssid) {
    size_t n = wifi_provision_html_error(s_page, sizeof s_page, err, ssid);
    return send_html(req, status, s_page, n);
}

static esp_err_t handle_get(httpd_req_t *req) {
    size_t len = 0;
    const char *page = wifi_provision_form_page(&len);
    if (page == NULL || len == 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no page");
    }
    (void)httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, (ssize_t)len);
}

static esp_err_t handle_save(httpd_req_t *req) {
    size_t hlen = httpd_req_get_hdr_value_len(req, "Content-Length");
    if (hlen == 0) {
        return send_error_page(req, "411 Length Required", WIFI_FORM_ERR_NO_FIELD, NULL);
    }
    if (hlen >= PROV_CONTENT_LEN_MAX) {
        // 位数就已经超了，不必去解析；也绝不读正文。
        return send_error_page(req, "413 Payload Too Large", WIFI_FORM_ERR_TOO_LONG, NULL);
    }

    char cl[24];
    if (httpd_req_get_hdr_value_str(req, "Content-Length", cl, sizeof cl) != ESP_OK) {
        return send_error_page(req, "400 Bad Request", WIFI_FORM_ERR_NO_FIELD, NULL);
    }

    size_t body_len = 0;
    wifi_content_length_t clr = wifi_provision_content_length(cl, WIFI_PROV_BODY_MAX, &body_len);
    if (clr == WIFI_CL_TOO_LARGE) {
        return send_error_page(req, "413 Payload Too Large", WIFI_FORM_ERR_TOO_LONG, NULL);
    }
    if (clr != WIFI_CL_OK || body_len == 0) {
        return send_error_page(req, "400 Bad Request", WIFI_FORM_ERR_MISSING_SSID, NULL);
    }

    // 有界接收：收满声明长度才往下走；零 / 断开 / 超时都算明确失败。
    size_t got = 0;
    unsigned retries = 0;
    while (got < body_len) {
        int r = httpd_req_recv(req, s_body + got, body_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++retries > PROV_RECV_RETRIES) {
                return send_error_page(req, "408 Request Timeout", WIFI_FORM_ERR_NO_FIELD, NULL);
            }
            continue;
        }
        if (r <= 0) {
            return send_error_page(req, "400 Bad Request", WIFI_FORM_ERR_NO_FIELD, NULL);
        }
        got += (size_t)r;
        retries = 0;
    }
    s_body[got] = '\0';

    wifi_form_result_t fr = wifi_provision_form_parse(s_body, got, &s_pending_form);

    prov_msg_t msg = { .kind = (uint8_t)PROV_MSG_SUBMIT, .form_result = (uint8_t)fr };
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        return send_error_page(req, "503 Service Unavailable", WIFI_FORM_ERR_NO_FIELD, NULL);
    }
    if (xSemaphoreTake(s_reply_sem, pdMS_TO_TICKS(PROV_SAVE_WAIT_MS)) != pdTRUE) {
        return send_error_page(req, "503 Service Unavailable", WIFI_FORM_ERR_NO_FIELD, NULL);
    }

    switch ((prov_reply_t)s_reply.kind) {
    case PROV_REPLY_REJECT:
        return send_error_page(req, "400 Bad Request", (wifi_form_result_t)s_reply.error, NULL);
    case PROV_REPLY_SAVE_FAILED:
        // 具体失败原因（槽位满 / NVS 写失败）对用户是同一句话：这次没存上，可以重试。
        return send_error_page(req, "500 Internal Server Error", WIFI_FORM_ERR_TOO_LONG,
                               s_pending_form.profile.ssid);
    case PROV_REPLY_SAVED: {
        size_t n = wifi_provision_html_saved(s_page, sizeof s_page, s_pending_form.profile.ssid);
        esp_err_t e = send_html(req, NULL, s_page, n); // NULL = 默认 200
        // 无论 send 是否成功都要重启：数据已经落盘，重启才是用户的预期动作。
        prov_msg_t ack = { .kind = (uint8_t)PROV_MSG_RESP_SENT, .form_result = 0 };
        (void)xQueueSend(s_queue, &ack, 0);
        return e;
    }
    case PROV_REPLY_NONE:
    default:
        return send_error_page(req, "500 Internal Server Error", WIFI_FORM_ERR_NO_FIELD, NULL);
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    prov_msg_t msg = { .kind = 0, .form_result = 0 };
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        msg.kind = (uint8_t)PROV_MSG_CLIENT_JOINED;
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        msg.kind = (uint8_t)PROV_MSG_CLIENT_LEFT;
    } else {
        return;
    }
    if (s_queue != NULL) (void)xQueueSend(s_queue, &msg, 0);
}

// 只有"拿到 DHCP"才说明手机真的能打开网页；单纯的关联成功可能还没分配地址。
static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id != IP_EVENT_AP_STAIPASSIGNED) return;
    prov_msg_t msg = { .kind = (uint8_t)PROV_MSG_CLIENT_JOINED, .form_result = 0 };
    if (s_queue != NULL) (void)xQueueSend(s_queue, &msg, 0);
}

/* ---- 落盘 ---- */

// 同 SSID 走 update，避免反复配同一个网络把 8 个槽位耗光；槽位满时返回失败，
// **不删任何既有记录**——删掉一条能用的网络比"这次没存上"更糟。
static bool save_profile(const wifi_profile_t *profile) {
    if (!s_nvs.opened) return false;

    int existing = -1;
    uint8_t count = wifi_profile_store_count(&s_store);
    for (uint8_t i = 0; i < count; i++) {
        const wifi_profile_t *q = wifi_profile_store_at(&s_store, i);
        if (q != NULL && strcmp(q->ssid, profile->ssid) == 0) {
            existing = (int)i;
            break;
        }
    }

    int8_t index;
    if (existing >= 0) {
        if (wifi_profile_store_update(&s_store, (uint8_t)existing, profile) != WIFI_PROFILE_OK) {
            return false;
        }
        index = (int8_t)existing;
    } else {
        if (wifi_profile_store_add(&s_store, profile) != WIFI_PROFILE_OK) return false;
        index = (int8_t)(wifi_profile_store_count(&s_store) - 1);
    }

    // 必须设为 active：遍历顺序是"先 active 再 0..N"，不设的话重启后可能先连旧网络，
    // 用户会以为配网根本没生效。
    if (wifi_profile_store_set_active(&s_store, index) != WIFI_PROFILE_OK) return false;
    return wifi_profile_store_save(&s_store) == WIFI_PROFILE_OK;
}

/* ---- worker ---- */

static void worker_submit(const prov_msg_t *msg) {
    wifi_prov_step_t step = wifi_provision_flow_handle(
        &s_flow, WIFI_PROV_EV_SUBMIT, (wifi_form_result_t)msg->form_result, &s_pending_form,
        now_ms());

    if (step.action == WIFI_PROV_ACT_REJECT) {
        s_reply.kind = (uint8_t)PROV_REPLY_REJECT;
        s_reply.error = (uint8_t)step.error;
        (void)xSemaphoreGive(s_reply_sem);
        return;
    }
    if (step.action != WIFI_PROV_ACT_SAVE) return; // 重复提交等：忽略

    if (save_profile(&step.profile)) {
        wifi_prov_step_t ok = wifi_provision_flow_handle(&s_flow, WIFI_PROV_EV_SAVE_OK,
                                                         WIFI_FORM_OK, NULL, now_ms());
        if (ok.action == WIFI_PROV_ACT_SAVED_RESTART) {
            s_reply.kind = (uint8_t)PROV_REPLY_SAVED;
            s_reply.error = (uint8_t)WIFI_FORM_OK;
            s_restart_at_ms = now_ms() + (int64_t)PROV_RESTART_MS;
        }
    } else {
        (void)wifi_provision_flow_handle(&s_flow, WIFI_PROV_EV_SAVE_FAIL, WIFI_FORM_OK, NULL,
                                         now_ms());
        s_reply.kind = (uint8_t)PROV_REPLY_SAVE_FAILED;
        s_reply.error = (uint8_t)WIFI_FORM_OK;
    }
    (void)xSemaphoreGive(s_reply_sem);
}

static void service_task(void *arg) {
    (void)arg;
    s_worker_alive = true;

    for (;;) {
        prov_msg_t msg;
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(PROV_TICK_MS)) == pdTRUE) {
            if (msg.kind == (uint8_t)PROV_MSG_STOP) break;
            if (s_closing) continue; // 已经关了，只等 STOP

            switch ((prov_kind_t)msg.kind) {
            case PROV_MSG_SUBMIT:
                worker_submit(&msg);
                break;
            case PROV_MSG_RESP_SENT:
                if (s_restart_at_ms > 0) {
                    session_close();
                    esp_restart(); // noreturn
                }
                break;
            case PROV_MSG_CLIENT_JOINED:
                if (s_clients < UINT8_MAX) s_clients++;
                publish_prov();
                break;
            case PROV_MSG_CLIENT_LEFT:
                if (s_clients > 0) s_clients--;
                publish_prov();
                break;
            case PROV_MSG_STOP:
            default:
                break;
            }
            continue;
        }

        if (s_closing) continue;

        // 超时推进：5 分钟没人提交就关掉热点，把射频还给常驻服务。
        wifi_prov_step_t step = wifi_provision_flow_handle(&s_flow, WIFI_PROV_EV_TICK, WIFI_FORM_OK,
                                                           NULL, now_ms());
        if (step.action == WIFI_PROV_ACT_CLOSE) {
            session_close();
            s_closing = true;
            (void)wifi_profile_service_start(); // 射频已释放，可以恢复联网
            continue;
        }

        // 响应丢了也必须重启：数据已经落盘，留在配网态只会让用户重复提交。
        if (s_restart_at_ms > 0 && now_ms() >= s_restart_at_ms) {
            session_close();
            esp_restart(); // noreturn
        }
    }

    // 由 stop() 回收，与 profile service 的 owner 契约一致。
    s_worker_alive = false;
    for (;;) vTaskSuspend(NULL);
}

/* ---- 对外 ---- */

// 对外的"配网还在吗"看的是**AP 是否还开着**，不是 worker 是否还活着：
// 5 分钟超时会先关热点、再等 stop() 回收 worker，这段时间用户已经回到可用状态。
bool wifi_provision_service_active(void) { return s_session_open; }

esp_err_t wifi_provision_service_start(void) {
    if (s_active) return ESP_ERR_INVALID_STATE;
    if (wifi_profile_service_running()) {
        ESP_LOGE(TAG, "联网服务仍在运行，配网不启动");
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_radio_claim(wifi_radio_shared(), WIFI_RADIO_PROVISION)) {
        ESP_LOGE(TAG, "射频已被占用，配网不启动");
        return ESP_ERR_INVALID_STATE;
    }

    s_active = true;
    s_session_open = true;
    s_closing = false;
    s_clients = 0;
    s_restart_at_ms = 0;
    s_reply.kind = (uint8_t)PROV_REPLY_NONE;

    make_ap_ssid();
    copy_str(s_ap_ip, sizeof s_ap_ip, "192.168.4.1");

    // NVS 打不开不阻止起热点：用户仍能打开表单，提交时会收到 500 并可以重试。
    esp_err_t err = wifi_profile_nvs_open(&s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 打开失败(%s)，配网可显示但无法保存", esp_err_to_name(err));
    } else {
        wifi_kv_t kv = wifi_profile_nvs_kv(&s_nvs);
        if (wifi_profile_store_load(&s_store, &kv) != WIFI_PROFILE_OK) {
            ESP_LOGW(TAG, "profile 读取失败，按空库处理");
        }
    }

    if (!s_netif_inited) {
        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
        s_netif_inited = true;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_WIFI_AP();
    s_netif = esp_netif_new(&netif_cfg);
    if (s_netif == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    err = esp_netif_attach_wifi_ap(s_netif);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_default_wifi_ap_handlers();
    if (err != ESP_OK) goto fail;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) goto fail;
    s_wifi_inited = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) goto fail;

    wifi_config_t ap_cfg;
    memset(&ap_cfg, 0, sizeof ap_cfg);
    copy_str((char *)ap_cfg.ap.ssid, sizeof ap_cfg.ap.ssid, s_ap_ssid);
    ap_cfg.ap.ssid_len = (uint8_t)strlen((const char *)ap_cfg.ap.ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 1;
    ap_cfg.ap.ssid_hidden = 0;
    ap_cfg.ap.beacon_interval = 100;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) goto fail;

    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL,
                                              &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, on_ip_event,
                                              NULL, &s_ip_handler);
    if (err != ESP_OK) goto fail;

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 2;
    http_cfg.max_open_sockets = 3; // 一两个客户端足够，省 socket 与 DHCP 槽
    http_cfg.lru_purge_enable = true;
    http_cfg.recv_wait_timeout = 2;
    http_cfg.send_wait_timeout = 5;
    http_cfg.stack_size = 5120; // 1 KB 正文 + 1 KB 页面 + snprintf，4096 偏紧
    err = httpd_start(&s_httpd, &http_cfg);
    if (err != ESP_OK) goto fail;

    httpd_uri_t get_uri = { .uri = "/", .method = HTTP_GET, .handler = handle_get, .user_ctx = NULL };
    err = httpd_register_uri_handler(s_httpd, &get_uri);
    if (err != ESP_OK) goto fail;
    httpd_uri_t post_uri = { .uri = WIFI_PROV_SAVE_PATH,
                             .method = HTTP_POST,
                             .handler = handle_save,
                             .user_ctx = NULL };
    err = httpd_register_uri_handler(s_httpd, &post_uri);
    if (err != ESP_OK) goto fail;

    s_queue = xQueueCreate(PROV_QUEUE_DEPTH, sizeof(prov_msg_t));
    if (s_queue == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_reply_sem = xSemaphoreCreateBinary();
    if (s_reply_sem == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    wifi_provision_flow_init(&s_flow, WIFI_PROV_TIMEOUT_MS);
    (void)wifi_provision_flow_handle(&s_flow, WIFI_PROV_EV_START, WIFI_FORM_OK, NULL, now_ms());
    publish_prov();

    if (xTaskCreate(service_task, "wifi_prov", PROV_TASK_STACK, NULL, PROV_TASK_PRIO, &s_task) !=
        pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    ESP_LOGI(TAG, "配网已启动：热点 %s，地址 %s", s_ap_ssid, s_ap_ip);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "配网启动失败: %s", esp_err_to_name(err));
    // 先关 HTTP 与栈，再删队列/信号量：反过来的话 httpd 任务可能还在用它们。
    session_close();
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (s_reply_sem != NULL) {
        vSemaphoreDelete(s_reply_sem);
        s_reply_sem = NULL;
    }
    s_active = false;
    publish_idle();
    return err;
}

void wifi_provision_service_stop(void) {
    if (!s_active) return;

    // 先停 HTTP：httpd_stop 会 join httpd 任务，返回后就不会再有 handler 碰队列或信号量。
    http_stop();

    if (s_task != NULL) {
        prov_msg_t stop = { .kind = (uint8_t)PROV_MSG_STOP, .form_result = 0 };
        if (s_queue != NULL) (void)xQueueSend(s_queue, &stop, 0);
        for (int waited = 0; waited < (int)(PROV_STOP_TIMEOUT_MS / 10) && s_worker_alive; waited++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_worker_alive) {
            ESP_LOGW(TAG, "worker 未在超时内退出，强制回收");
            vTaskDelete(s_task);
        }
        s_task = NULL;
    }

    session_close();

    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (s_reply_sem != NULL) {
        vSemaphoreDelete(s_reply_sem);
        s_reply_sem = NULL;
    }

    s_active = false;
    // 超时自关时联网服务已经自己起来了，别把它刚发的 CONNECTING 覆盖成 IDLE。
    if (!wifi_profile_service_running()) publish_idle();
}
