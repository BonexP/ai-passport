#pragma once

// Minimal typed platform boundary for executing the actual demo C lifecycle code.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h> /* ssize_t：httpd 假件的签名要与 IDF 一致 */

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_FOUND 0x105
const char *esp_err_to_name(esp_err_t error);
void test_log(const char *tag, const char *format, ...);
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)

typedef int BaseType_t;
typedef unsigned TickType_t;
typedef struct test_task *TaskHandle_t;
typedef struct test_sem *SemaphoreHandle_t;
typedef void (*TaskFunction_t)(void *);
#define pdTRUE 1
#define pdPASS 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define configMAX_PRIORITIES 25
#define NIMBLE_HS_STACK_SIZE 4096
#define NIMBLE_CORE 0
#define eSetValueWithOverwrite 1
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *task);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                 unsigned stack, void *arg, unsigned priority,
                                 TaskHandle_t *task, int core);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, int action);
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit,
                          uint32_t *value, TickType_t ticks);
void vTaskSuspend(TaskHandle_t task);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
void vSemaphoreDelete(SemaphoreHandle_t sem);

typedef enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK } bsp_btn_t;
typedef enum { BSP_BTN_PRESS, BSP_BTN_CLICK, BSP_BTN_DOUBLE, BSP_BTN_LONG, BSP_BTN_RELEASE } bsp_btn_ev_t;
int bsp_button_read_mv(void);
esp_err_t bsp_button_prepare_deep_sleep(int *level);
// Voltage the key pad reports in host tests; set it to a pressed window to drive
// the "refuse to sleep" path.
extern int test_btn_mv;
// Deep-sleep hand-off result a test can force via bsp_button_prepare_deep_sleep().
extern esp_err_t test_prepare_deep_sleep_result;
// Number of times esp_restart() was reached in the demo runtime host test.
extern unsigned test_restarts;
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t channels);
void bsp_audio_set_volume(uint8_t percent);
esp_err_t bsp_audio_read(void *pcm, size_t bytes);
esp_err_t bsp_audio_write(const void *pcm, size_t bytes);
esp_err_t bsp_audio_sleep(void);
esp_err_t bsp_audio_wake(void);
esp_err_t bsp_audio_prepare_deep_sleep(void);
esp_err_t bsp_battery_sleep(void);
esp_err_t bsp_i2c_prepare_deep_sleep(void);
esp_err_t bsp_display_prepare_deep_sleep(void);
void bsp_display_backlight(uint8_t percent);
bool bsp_lvgl_lock(int timeout);
void bsp_lvgl_unlock(void);

#define RTC_DATA_ATTR
#define ESP_SLEEP_WAKEUP_TIMER 4
#define ESP_SLEEP_WAKEUP_GPIO  7
typedef enum { ESP_GPIO_WAKEUP_GPIO_LOW = 0, ESP_GPIO_WAKEUP_GPIO_HIGH } esp_deepsleep_gpio_wake_up_mode_t;
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t gpio_pin_mask,
                                           esp_deepsleep_gpio_wake_up_mode_t mode);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us);
esp_err_t esp_sleep_disable_wakeup_source(int source);
esp_err_t esp_light_sleep_start(void);
int esp_sleep_get_wakeup_cause(void);
void esp_deep_sleep_start(void);
void esp_restart(void);
int64_t esp_timer_get_time(void);

typedef struct { int unused; } lv_obj_t;
typedef struct { int unused; } lv_timer_t;
typedef struct { int unused; } lv_font_t;
extern const lv_font_t lv_font_montserrat_14;
#define LV_RADIUS_CIRCLE 1000
#define LV_TEXT_ALIGN_CENTER 0
#define LV_LABEL_LONG_WRAP 0
#define LV_ALIGN_BOTTOM_MID 0
#define LV_ALIGN_TOP_MID 1
#define LV_ALIGN_TOP_LEFT 2
uint32_t lv_color_hex(uint32_t color);
lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
void lv_obj_set_size(lv_obj_t *object, int width, int height);
void lv_obj_set_width(lv_obj_t *object, int width);
void lv_obj_set_style_radius(lv_obj_t *object, int radius, int selector);
void lv_obj_set_style_bg_color(lv_obj_t *object, uint32_t color, int selector);
void lv_obj_set_style_border_width(lv_obj_t *object, int width, int selector);
void lv_obj_set_style_text_color(lv_obj_t *object, uint32_t color, int selector);
void lv_obj_set_style_text_align(lv_obj_t *object, int align, int selector);
void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font, int selector);
void lv_obj_center(lv_obj_t *object);
void lv_obj_align(lv_obj_t *object, int align, int x, int y);
void lv_obj_delete(lv_obj_t *object);
void lv_label_set_text(lv_obj_t *object, const char *text);
void lv_label_set_text_fmt(lv_obj_t *object, const char *format, ...);
void lv_label_set_long_mode(lv_obj_t *object, int mode);
void lv_screen_load(lv_obj_t *screen);
lv_timer_t *lv_timer_create(void (*callback)(lv_timer_t *), unsigned period, void *data);
void lv_timer_delete(lv_timer_t *timer);

typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() { 0 }
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
#define WIFI_EVENT "wifi"
#define WIFI_EVENT_SCAN_DONE 1
/* IDF 真值：flash=0 / ram=1。原先写成 0 是错的，只是没人读过它。 */
#define WIFI_STORAGE_RAM 1
/* IDF 真值：NULL=0 / STA=1 / AP=2 / APSTA=3。 */
#define WIFI_MODE_STA 1
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() { 0 }
typedef struct { int rssi; uint8_t ssid[33]; uint8_t primary; } wifi_ap_record_t;
esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif);
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);
void esp_netif_destroy_default_wifi(void *netif);
esp_err_t esp_wifi_init(const wifi_init_config_t *cfg);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_scan_start(const void *cfg, bool blocking);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg,
    esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
    esp_event_handler_instance_t instance);

struct ble_gap_event { int type; };
struct ble_hs_adv_fields { int flags; const uint8_t *name; size_t name_len; int name_is_complete; };
struct ble_gap_adv_params { int conn_mode; int disc_mode; };
struct test_ble_hs_cfg { void (*reset_cb)(int); void (*sync_cb)(void); };
extern struct test_ble_hs_cfg ble_hs_cfg;
#define BLE_HS_ADV_F_DISC_GEN 1
#define BLE_HS_ADV_F_BREDR_UNSUP 2
#define BLE_GAP_CONN_MODE_NON 0
#define BLE_GAP_DISC_MODE_GEN 0
#define BLE_HS_FOREVER -1
#define BLE_GAP_EVENT_ADV_COMPLETE 1
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields);
int ble_gap_adv_start(uint8_t address_type, const void *address, int duration,
    const struct ble_gap_adv_params *params,
    int (*callback)(struct ble_gap_event *, void *), void *arg);
int ble_gap_adv_stop(void);
int ble_hs_util_ensure_addr(int privacy);
int ble_hs_id_infer_auto(int privacy, uint8_t *type);
void ble_svc_gap_init(void);
void ble_svc_gatt_init(void);
int ble_svc_gap_device_name_set(const char *name);
esp_err_t nimble_port_init(void);
esp_err_t nimble_port_deinit(void);
int nimble_port_stop(void);
void nimble_port_run(void);

/* ==== S2 追加：队列、临界区、NVS、netif/事件补充、LVGL 补充、观测 ==== */

/* 错误码：IDF 真实值，wifi_profile_nvs.c 已经在用 INVALID_ARG。 */
#define ESP_ERR_INVALID_ARG        0x102
#define ESP_ERR_NVS_BASE           0x1100
#define ESP_ERR_NVS_NOT_FOUND      (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x06)

/* ---- 队列 ---- */
typedef struct test_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned depth, unsigned item_size);
BaseType_t    xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t    xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
void          vQueueDelete(QueueHandle_t queue);
extern bool test_queue_fails, test_queue_send_fails, test_queue_expire;
/* worker 每次准备取下一个事件时回调一次：此时上一个事件已处理完、快照已发布。 */
extern void (*test_queue_poll_hook)(void);

/* ---- 临界区：用函数而非空宏，主机上才能断言成对 ---- */
typedef struct { int unused; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
void test_port_enter(portMUX_TYPE *mux);
void test_port_exit(portMUX_TYPE *mux);
#define portENTER_CRITICAL(mux) test_port_enter(mux)
#define portEXIT_CRITICAL(mux)  test_port_exit(mux)
extern unsigned test_port_enters, test_port_exits;

/* ---- NVS ---- */
typedef uint32_t nvs_handle_t;
#define NVS_READWRITE 0
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *buf, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *buf, size_t len);
esp_err_t nvs_commit(nvs_handle_t handle);
void      nvs_close(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
extern esp_err_t test_nvs_flash_result, test_nvs_open_result;
extern bool test_nvs_write_fails;
extern unsigned test_nvs_opens, test_nvs_closes, test_nvs_commits;
void test_nvs_clear(void);
void test_nvs_put(const char *key, const void *buf, size_t len);

/* ---- netif / 事件 / wifi 补充 ---- */
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
extern unsigned test_netif_inits, test_event_loop_creates;

typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
char     *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int len);

#define ESP_EVENT_ANY_ID            (-1)
#define WIFI_EVENT_STA_CONNECTED    4
#define WIFI_EVENT_STA_DISCONNECTED 5
#define IP_EVENT                    "ip"
#define IP_EVENT_STA_GOT_IP         0
#define WIFI_IF_STA                 0

typedef struct { uint8_t ssid[32]; uint8_t password[64]; } wifi_sta_config_t;
typedef struct {
    uint8_t  ssid[32];
    uint8_t  password[64];
    uint8_t  ssid_len;
    uint8_t  channel;
    int      authmode; // wifi_auth_mode_t
    uint8_t  ssid_hidden;
    uint8_t  max_connection;
    uint16_t beacon_interval;
} wifi_ap_config_t;
typedef union { wifi_sta_config_t sta; wifi_ap_config_t ap; } wifi_config_t;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *cfg);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);

/* ---- LVGL 补充（ui_product 需要；其余 20 项已在上面声明） ---- */
#define LV_ALIGN_TOP_RIGHT 3
#define LV_OBJ_FLAG_SCROLLABLE 0x0001
extern const lv_font_t lv_font_montserrat_20;
void lv_obj_set_pos(lv_obj_t *object, int x, int y);
void lv_obj_remove_flag(lv_obj_t *object, int flag);
void lv_obj_set_style_pad_all(lv_obj_t *object, int pad, int selector);
void lv_obj_set_style_border_color(lv_obj_t *object, uint32_t color, int selector);

/* ---- 测试观测 ---- */
extern int64_t test_time_us;
#define TEST_LABEL_LOG_DEPTH 16
#define TEST_LABEL_LOG_WIDTH 64
extern char test_label_log[TEST_LABEL_LOG_DEPTH][TEST_LABEL_LOG_WIDTH];
extern unsigned test_label_writes;

/* ---- STA 驱动假件的控制口（实现见 demo_wifi_stack.c） ---- */
extern unsigned test_wifi_step, test_wifi_fail_at, test_wifi_destroys;
extern esp_err_t test_wifi_fail_err, test_wifi_set_config_result, test_wifi_connect_result;
extern wifi_config_t test_wifi_last_cfg;
extern int8_t test_wifi_rssi;
void test_wifi_stack_reset(void);
bool test_wifi_stack_clean(void);
void test_wifi_fire_connected(void);
void test_wifi_fire_got_ip(void);
void test_wifi_fire_disconnected(uint8_t reason);

/* ---- 电量：状态页要读，host 测试可注入 ---- */
extern int test_battery_soc;
int bsp_battery_soc(void);
int bsp_battery_mv(void);

/* 队列槽位池跨用例会耗尽，测试之间必须显式清零。 */
void test_queue_reset(void);

/* ==== S3：SoftAP 配网所需的桩面 ==== */

#define WIFI_IF_AP      1
#define WIFI_MODE_AP    2
#define WIFI_MODE_APSTA 3

#define WIFI_AUTH_OPEN          0
#define WIFI_AUTH_WPA_PSK       2
#define WIFI_AUTH_WPA2_PSK      3
#define WIFI_AUTH_WPA_WPA2_PSK  4

#define ESP_NETIF_DEFAULT_WIFI_AP() { 0 }
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *netif);
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void);

/* WIFI_EVENT_AP_* 的数值未在固件侧核对过；构建通过即视为核对完成。 */
#define WIFI_EVENT_AP_STACONNECTED    14
#define WIFI_EVENT_AP_STADISCONNECTED 15
#define IP_EVENT_AP_STAIPASSIGNED     2
typedef struct { uint8_t mac[6]; uint8_t aid; } wifi_event_ap_staconnected_t;
typedef struct { uint8_t mac[6]; uint8_t aid; } wifi_event_ap_stadisconnected_t;
typedef struct { esp_ip4_addr_t ip; uint8_t mac[6]; } ip_event_ap_staipassigned_t;

#define ESP_MAC_WIFI_SOFTAP 1
esp_err_t esp_read_mac(uint8_t *mac, int type);

/* 会话选择器：STA 沿用 1..9 的旧矩阵，AP 用独立计数器，互不干扰。 */
typedef enum { TEST_WIFI_SESSION_STA = 0, TEST_WIFI_SESSION_AP } test_wifi_session_t;
void     test_wifi_session(test_wifi_session_t session);
unsigned test_ap_next_step(void); /* httpd 假件也要参与编号，故导出 */
extern unsigned test_ap_step, test_ap_fail_at, test_ap_destroys;
int      test_wifi_mode(void);
extern wifi_config_t test_wifi_cfg_sta, test_wifi_cfg_ap;

/* 事件注入：按 base + id 分发（覆盖 ANY_ID）。 */
void test_wifi_fire_event(esp_event_base_t base, int32_t id, void *data);
void test_wifi_fire_ap_staconnected(void);
void test_wifi_fire_ap_stadisconnected(void);
void test_wifi_fire_ap_ipassigned(void);

/* 任务/信号量池跨用例只增不减，必须显式清零。 */
void test_task_reset(void);
void test_sem_reset(void);

/* ==== S3：esp_http_server 桩面（IDF 名逐字一致；值待固件构建核对） ==== */

typedef void *httpd_handle_t;
typedef enum { HTTP_ANY = 1, HTTP_GET = 2, HTTP_POST = 4 } httpd_method_t;

typedef enum {
    HTTPD_400_BAD_REQUEST = 0,
    HTTPD_408_REQ_TIMEOUT,
    HTTPD_411_LENGTH_REQUIRED,
    HTTPD_413_CONTENT_TOO_LARGE,
    HTTPD_500_INTERNAL_SERVER_ERROR,
} httpd_err_code_t;

typedef struct httpd_req {
    httpd_handle_t handle;
    httpd_method_t method;
    void          *user_ctx;
    size_t         content_len; /* 名字与类型都必须与 IDF 一致 */
} httpd_req_t;

typedef struct {
    const char      *uri;
    httpd_method_t   method;
    esp_err_t      (*handler)(httpd_req_t *r);
    void            *user_ctx;
} httpd_uri_t;

typedef struct {
    unsigned task_priority;
    size_t   stack_size;
    int      core_id;
    uint16_t server_port;
    uint16_t ctrl_port;
    uint16_t max_open_sockets;
    uint16_t max_uri_handlers;
    uint16_t max_resp_headers;
    uint16_t backlog_conn;
    bool     lru_purge_enable;
    uint16_t recv_wait_timeout;
    uint16_t send_wait_timeout;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG()                     \
    {                                              \
        .task_priority = 5, .stack_size = 4096,    \
        .core_id = 0, .server_port = 80,           \
        .ctrl_port = 32768, .max_open_sockets = 7, \
        .max_uri_handlers = 8, .max_resp_headers = 8, \
        .backlog_conn = 5, .lru_purge_enable = false, \
        .recv_wait_timeout = 5, .send_wait_timeout = 5, \
    }

#define HTTPD_RESP_USE_STRLEN (-1)
#define HTTPD_SOCK_ERR_FAIL (-1)
#define HTTPD_SOCK_ERR_INVALID (-2)
#define HTTPD_SOCK_ERR_TIMEOUT (-3)

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri);
esp_err_t httpd_unregister_uri_handler(httpd_handle_t handle, const char *uri,
                                       httpd_method_t method);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *msg);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
int       httpd_req_recv(httpd_req_t *req, char *buf, size_t buf_len);
size_t    httpd_req_get_hdr_value_len(httpd_req_t *req, const char *field);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val,
                                      size_t val_size);

/* ---- 观测与注入 ---- */
#define TEST_HTTPD_URI_SLOTS 4
#define TEST_HTTPD_BODY_MAX  1024
extern httpd_uri_t test_httpd_uris[TEST_HTTPD_URI_SLOTS];
extern unsigned    test_httpd_uri_count;
extern unsigned    test_httpd_starts, test_httpd_stops, test_httpd_sends,
                   test_httpd_send_errs, test_httpd_recv_calls, test_httpd_recv_timeout_at;
extern char        test_httpd_last_status[32];
extern char        test_httpd_last_type[48];
extern char        test_httpd_last_body[TEST_HTTPD_BODY_MAX];
extern size_t      test_httpd_last_len;
void        test_httpd_reset(void);
/* content_length 传 NULL 表示请求里没有 Content-Length 头。 */
void        test_httpd_set_request(const char *body, size_t len, const char *content_length);
httpd_req_t *test_httpd_req(void);
