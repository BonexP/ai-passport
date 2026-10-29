// Deterministic RTOS boundary: workers execute until they park at vTaskSuspend.
// Tests control acknowledgement timing; notifying or deleting stale handles fails.
#include "demo_test_stubs.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

struct test_task { TaskFunction_t entry; void *arg; bool deleted; uint32_t notification; };
struct test_sem { bool available; bool deleted; };
struct test_task test_tasks[16];
struct test_sem test_sems[16];
unsigned test_task_count, test_sem_count, test_task_deletes, test_notifications;
bool test_create_fails, test_sem_fails, test_auto_ack;
TaskHandle_t test_current_task, test_latest_task;
jmp_buf test_worker_park;
char test_status[160];
unsigned test_read_calls, test_write_calls, test_read_fail_at, test_write_fail_at;
void (*test_read_hook)(void);
struct test_ble_hs_cfg ble_hs_cfg;
int test_nimble_deinit_result, test_nimble_stop_result, test_nimble_name_result;
unsigned test_nimble_deinits, test_nimble_stops;
const lv_font_t lv_font_montserrat_14 = { 0 };
const lv_font_t lv_font_montserrat_20 = { 0 };

// 页面测试要按顺序断言"最后 N 次写入"，单条 test_status 不够用。
char test_label_log[TEST_LABEL_LOG_DEPTH][TEST_LABEL_LOG_WIDTH];
unsigned test_label_writes;
int64_t test_time_us;

void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
bool bsp_lvgl_lock(int timeout) { (void)timeout; return true; }
void bsp_lvgl_unlock(void) {}
void lv_label_set_text(lv_obj_t *object, const char *text) {
    (void)object;
    snprintf(test_status, sizeof(test_status), "%s", text);
    // 页面测试要看"最后 N 次写入的顺序"，单条 test_status 不够用。
    char *slot = test_label_log[test_label_writes % TEST_LABEL_LOG_DEPTH];
    size_t i = 0;
    if (text != NULL) {
        for (; text[i] != '\0' && i + 1 < TEST_LABEL_LOG_WIDTH; i++) slot[i] = text[i];
    }
    slot[i] = '\0';
    test_label_writes++;
}
void bsp_display_backlight(uint8_t percent) { (void)percent; }

// Baseline-demo UI helpers (main/ui_pixel.c) and the LVGL calls they issue:
// host tests never render, so these are link-only no-ops returning a shared
// stub object. Upstream stubs lagged the demo's move onto ui_pixel_*; added
// here so tests/demo_stubs keeps the demo runtime tests linkable.
static lv_obj_t s_stub_obj;
lv_obj_t *ui_pixel_screen_create(const char *title) { (void)title; return &s_stub_obj; }
lv_obj_t *ui_pixel_panel_create(lv_obj_t *parent, int x, int y, int w, int h,
                                uint32_t color) {
    (void)parent; (void)x; (void)y; (void)w; (void)h; (void)color;
    return &s_stub_obj;
}
lv_obj_t *ui_pixel_label(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color) {
    (void)parent; (void)text; (void)font; (void)color; return &s_stub_obj;
}
lv_obj_t *ui_pixel_mascot_create(lv_obj_t *parent, int x, int y) {
    (void)parent; (void)x; (void)y; return &s_stub_obj;
}
void ui_pixel_mascot_jump(lv_obj_t *mascot) { (void)mascot; }
void ui_pixel_set_selected(lv_obj_t *panel, bool selected, bool enabled) {
    (void)panel; (void)selected; (void)enabled;
}
uint32_t lv_color_hex(uint32_t color) { return color; }
lv_obj_t *lv_obj_create(lv_obj_t *parent) { (void)parent; return &s_stub_obj; }
lv_obj_t *lv_label_create(lv_obj_t *parent) { (void)parent; return &s_stub_obj; }
void lv_obj_set_size(lv_obj_t *o, int w, int h) { (void)o; (void)w; (void)h; }
void lv_obj_set_width(lv_obj_t *o, int w) { (void)o; (void)w; }
void lv_obj_set_style_radius(lv_obj_t *o, int r, int s) { (void)o; (void)r; (void)s; }
void lv_obj_set_style_bg_color(lv_obj_t *o, uint32_t c, int s) { (void)o; (void)c; (void)s; }
void lv_obj_set_style_border_width(lv_obj_t *o, int w, int s) { (void)o; (void)w; (void)s; }
void lv_obj_set_style_text_color(lv_obj_t *o, uint32_t c, int s) { (void)o; (void)c; (void)s; }
void lv_obj_set_style_text_align(lv_obj_t *o, int a, int s) { (void)o; (void)a; (void)s; }
void lv_obj_center(lv_obj_t *o) { (void)o; }
void lv_obj_align(lv_obj_t *o, int a, int x, int y) { (void)o; (void)a; (void)x; (void)y; }
void lv_obj_delete(lv_obj_t *o) { (void)o; }
void lv_label_set_long_mode(lv_obj_t *o, int m) { (void)o; (void)m; }
void lv_screen_load(lv_obj_t *o) { (void)o; }
void lv_label_set_text_fmt(lv_obj_t *o, const char *fmt, ...) { (void)o; (void)fmt; }
void lv_obj_set_style_text_font(lv_obj_t *o, const lv_font_t *f, int s) {
    (void)o; (void)f; (void)s;
}
int esp_sleep_get_wakeup_cause(void) { return 0; }   // 0 = unknown cause
static lv_timer_t s_stub_timer;
lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), unsigned period, void *data) {
    (void)cb; (void)period; (void)data; return &s_stub_timer;
}
void lv_timer_delete(lv_timer_t *t) { (void)t; }

// WiFi scan result getters: report one deterministic fake network so the
// demo's result screen renders identically on every host run.
static const wifi_ap_record_t s_stub_ap = { .rssi = -48,
    .ssid = {'A', 'I', '-', 'P', 'a', 's', 's', 'p', 'o', 'r', 't'},
    .primary = 6 };
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count) { *count = 1; return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records) {
    if (*count < 1) { *count = 0; return ESP_OK; }
    *count = 1;
    records[0] = s_stub_ap;
    return ESP_OK;
}
esp_err_t esp_sleep_disable_wakeup_source(int source) { (void)source; return ESP_OK; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { (void)us; return ESP_OK; }
esp_err_t esp_light_sleep_start(void) { return ESP_OK; }
void esp_deep_sleep_start(void) { assert(!"unexpected deep sleep in host test"); }
unsigned test_restarts;
void esp_restart(void) {
    test_restarts++;
    // A reboot ends this run; park the worker instead of returning into the
    // deep-sleep routine, matching esp_restart()'s noreturn contract on device.
    vTaskSuspend(NULL);
}
int64_t esp_timer_get_time(void) { return test_time_us; }
esp_err_t bsp_audio_sleep(void) { return ESP_OK; }
esp_err_t bsp_audio_wake(void) { return ESP_OK; }
esp_err_t bsp_audio_prepare_deep_sleep(void) { return ESP_OK; }
esp_err_t bsp_battery_sleep(void) { return ESP_OK; }

// Key pad: tests set test_btn_mv to the voltage a held key would produce
// (see the windows in bsp_pins.h), or leave it released.
int test_btn_mv = 3300;
// Hand-off failure a test wants bsp_button_prepare_deep_sleep() to report.
esp_err_t test_prepare_deep_sleep_result = ESP_OK;
int bsp_button_read_mv(void) { return test_btn_mv; }
esp_err_t bsp_button_prepare_deep_sleep(int *level) {
    if (level) *level = 1;
    return test_prepare_deep_sleep_result;
}
esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t gpio_pin_mask,
                                           esp_deepsleep_gpio_wake_up_mode_t mode) {
    (void)gpio_pin_mask; (void)mode;
    return ESP_OK;
}
esp_err_t bsp_i2c_prepare_deep_sleep(void) { return ESP_OK; }
esp_err_t bsp_display_prepare_deep_sleep(void) { return ESP_OK; }
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t channels) {
    (void)hz; (void)bits; (void)channels; return ESP_OK;
}
void bsp_audio_set_volume(uint8_t percent) { (void)percent; }
esp_err_t bsp_audio_read(void *pcm, size_t bytes) {
    test_read_calls++;
    if (test_read_hook) test_read_hook();
    if (test_read_calls == test_read_fail_at) return ESP_FAIL;
    memset(pcm, 0, bytes);
    return ESP_OK;
}
esp_err_t bsp_audio_write(const void *pcm, size_t bytes) {
    (void)pcm; (void)bytes;
    return ++test_write_calls == test_write_fail_at ? ESP_FAIL : ESP_OK;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *task) {
    (void)name; (void)stack; (void)priority;
    if (test_create_fails) return pdFALSE;
    assert(test_task_count < 16);
    *task = &test_tasks[test_task_count++];
    **task = (struct test_task){ .entry = entry, .arg = arg };
    test_latest_task = *task;
    return pdPASS;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                 unsigned stack, void *arg, unsigned priority,
                                 TaskHandle_t *task, int core) {
    (void)core;
    return xTaskCreate(entry, name, stack, arg, priority, task);
}
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, int action) {
    (void)action;
    assert(task && !task->deleted);
    task->notification = value;
    test_notifications++;
    return pdTRUE;
}
BaseType_t xTaskNotifyWait(uint32_t clear_entry, uint32_t clear_exit,
                          uint32_t *value, TickType_t ticks) {
    (void)clear_entry; (void)clear_exit; (void)ticks;
    assert(test_current_task && test_current_task->notification);
    *value = test_current_task->notification;
    test_current_task->notification = 0;
    return pdTRUE;
}
void test_run_worker(TaskHandle_t task) {
    assert(task && !task->deleted);
    test_current_task = task;
    if (setjmp(test_worker_park) == 0) {
        task->entry(task->arg);
        assert(!"worker unexpectedly returned");
    }
    test_current_task = NULL;
}
void vTaskSuspend(TaskHandle_t task) {
    assert(task == NULL && test_current_task);
    longjmp(test_worker_park, 1);
}
void vTaskDelete(TaskHandle_t task) {
    // Workers must not delete themselves; only the owner may reap an acked task.
    assert(task && !task->deleted && !test_current_task);
    task->deleted = true;
    test_task_deletes++;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    if (test_sem_fails) return NULL;
    assert(test_sem_count < 16);
    SemaphoreHandle_t sem = &test_sems[test_sem_count++];
    *sem = (struct test_sem){ 0 };
    return sem;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    assert(sem && !sem->deleted);
    sem->available = true;
    return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    (void)ticks;
    assert(sem && !sem->deleted);
    if (!sem->available && test_auto_ack) test_run_worker(test_latest_task);
    if (!sem->available) return pdFALSE;
    sem->available = false;
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t sem) {
    assert(sem && !sem->deleted);
    sem->deleted = true;
}

esp_err_t demo_radio_nvs_prepare(void) { return ESP_OK; }
esp_err_t demo_radio_network_prepare(void) { return ESP_OK; }
esp_err_t nimble_port_init(void) { return ESP_OK; }
esp_err_t nimble_port_deinit(void) {
    test_nimble_deinits++;
    assert(!test_current_task);
    return test_nimble_deinit_result;
}
int nimble_port_stop(void) { test_nimble_stops++; return test_nimble_stop_result; }
void nimble_port_run(void) {}
void ble_svc_gap_init(void) {}
void ble_svc_gatt_init(void) {}
int ble_svc_gap_device_name_set(const char *name) { (void)name; return test_nimble_name_result; }
int ble_hs_util_ensure_addr(int privacy) { (void)privacy; return 0; }
int ble_hs_id_infer_auto(int privacy, uint8_t *type) { (void)privacy; *type = 0; return 0; }
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *fields) { (void)fields; return 0; }
int ble_gap_adv_start(uint8_t address_type, const void *address, int duration,
    const struct ble_gap_adv_params *params,
    int (*callback)(struct ble_gap_event *, void *), void *arg) {
    (void)address_type; (void)address; (void)duration; (void)params; (void)callback; (void)arg;
    return 0;
}
int ble_gap_adv_stop(void) { return 0; }

/* ==== S2 追加：队列 / 临界区 / NVS / netif 初始化 / LVGL 补充 ==== */

/* ---- 队列 ---- */
#define TEST_QUEUE_SLOTS 4
#define TEST_QUEUE_DEPTH 8
#define TEST_QUEUE_ITEM  8

struct test_queue {
    unsigned      depth;
    unsigned      item_size;
    unsigned      head;
    unsigned      count;
    bool          deleted;
    unsigned char items[TEST_QUEUE_DEPTH][TEST_QUEUE_ITEM];
};

static struct test_queue test_queues[TEST_QUEUE_SLOTS];
static unsigned          test_queue_count;
bool                     test_queue_fails;
bool                     test_queue_send_fails;
bool                     test_queue_expire;
void (*test_queue_poll_hook)(void);

QueueHandle_t xQueueCreate(unsigned depth, unsigned item_size) {
    if (test_queue_fails) return NULL;
    assert(test_queue_count < TEST_QUEUE_SLOTS);
    assert(depth <= TEST_QUEUE_DEPTH && item_size <= TEST_QUEUE_ITEM);
    struct test_queue *q = &test_queues[test_queue_count++];
    memset(q, 0, sizeof *q);
    q->depth = depth;
    q->item_size = item_size;
    return q;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks) {
    (void)ticks;
    assert(queue && !queue->deleted);
    // 与 send_event() 的 0 超时语义一致：满就是丢，不能让测试出现"入队成功但真机会丢"。
    if (test_queue_send_fails || queue->count >= queue->depth) return pdFALSE;
    unsigned slot = (queue->head + queue->count) % queue->depth;
    memcpy(queue->items[slot], item, queue->item_size);
    queue->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks) {
    assert(queue && !queue->deleted);
    // 主页面的定时器动作：此时上一条事件已处理完、快照已发布，测试可在此抓取。
    if (test_current_task != NULL && test_queue_poll_hook != NULL) test_queue_poll_hook();

    if (queue->count == 0) {
        // 测试侧（没有停泊上下文）取空只能返回 FALSE。
        if (test_current_task == NULL) return pdFALSE;
        if (test_queue_expire && ticks != portMAX_DELAY) {
            test_queue_expire = false;
            return pdFALSE; // 模拟"等到超时"
        }
        // worker 取空 → 停泊，等价于 portMAX_DELAY 阻塞。
        longjmp(test_worker_park, 1);
    }

    memcpy(item, queue->items[queue->head], queue->item_size);
    queue->head = (queue->head + 1) % queue->depth;
    queue->count--;
    return pdTRUE;
}

void vQueueDelete(QueueHandle_t queue) {
    assert(queue && !queue->deleted);
    queue->deleted = true;
}

/* ---- 临界区 ---- */
unsigned test_port_enters;
unsigned test_port_exits;

void test_port_enter(portMUX_TYPE *mux) {
    (void)mux;
    test_port_enters++;
}

void test_port_exit(portMUX_TYPE *mux) {
    (void)mux;
    test_port_exits++;
}

/* ---- NVS ---- */
#define TEST_NVS_ENTRIES  16
#define TEST_NVS_BLOB_MAX 160
#define TEST_NVS_KEY_MAX  12

struct test_nvs_entry {
    bool          used;
    char          key[TEST_NVS_KEY_MAX];
    size_t        len;
    unsigned char data[TEST_NVS_BLOB_MAX];
};

static struct test_nvs_entry test_nvs[TEST_NVS_ENTRIES];
esp_err_t                    test_nvs_flash_result = ESP_OK;
esp_err_t                    test_nvs_open_result = ESP_OK;
bool                         test_nvs_write_fails;
unsigned                     test_nvs_opens;
unsigned                     test_nvs_closes;
unsigned                     test_nvs_commits;

static struct test_nvs_entry *test_nvs_find(const char *key) {
    for (int i = 0; i < TEST_NVS_ENTRIES; i++) {
        if (test_nvs[i].used && strcmp(test_nvs[i].key, key) == 0) return &test_nvs[i];
    }
    return NULL;
}

void test_nvs_clear(void) {
    memset(test_nvs, 0, sizeof test_nvs);
    test_nvs_flash_result = ESP_OK;
    test_nvs_open_result = ESP_OK;
    test_nvs_write_fails = false;
    test_nvs_opens = test_nvs_closes = test_nvs_commits = 0;
}

void test_nvs_put(const char *key, const void *buf, size_t len) {
    assert(strlen(key) < TEST_NVS_KEY_MAX);
    assert(len <= TEST_NVS_BLOB_MAX);
    struct test_nvs_entry *e = test_nvs_find(key);
    if (e == NULL) {
        for (int i = 0; i < TEST_NVS_ENTRIES; i++) {
            if (!test_nvs[i].used) { e = &test_nvs[i]; break; }
        }
    }
    assert(e != NULL);
    e->used = true;
    memcpy(e->key, key, strlen(key) + 1);
    memcpy(e->data, buf, len);
    e->len = len;
}

esp_err_t nvs_flash_init(void) { return test_nvs_flash_result; }

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    (void)name; (void)mode;
    test_nvs_opens++;
    if (test_nvs_open_result != ESP_OK) return test_nvs_open_result;
    if (out != NULL) *out = 1;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *buf, size_t *len) {
    (void)handle;
    assert(len != NULL);
    struct test_nvs_entry *e = test_nvs_find(key);
    if (e == NULL) return ESP_ERR_NVS_NOT_FOUND;
    if (*len < e->len) {
        *len = e->len; // 真 NVS 也是先回填所需长度再报错
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (buf != NULL) memcpy(buf, e->data, e->len);
    *len = e->len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *buf, size_t len) {
    (void)handle;
    if (test_nvs_write_fails) return ESP_FAIL;
    assert(len <= TEST_NVS_BLOB_MAX && strlen(key) < TEST_NVS_KEY_MAX);
    struct test_nvs_entry *e = test_nvs_find(key);
    if (e == NULL) {
        for (int i = 0; i < TEST_NVS_ENTRIES; i++) {
            if (!test_nvs[i].used) { e = &test_nvs[i]; break; }
        }
    }
    if (e == NULL) return ESP_FAIL;
    e->used = true;
    memcpy(e->key, key, strlen(key) + 1);
    memcpy(e->data, buf, len);
    e->len = len;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    test_nvs_commits++;
    return test_nvs_write_fails ? ESP_FAIL : ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    (void)handle;
    test_nvs_closes++;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle;
    struct test_nvs_entry *e = test_nvs_find(key);
    if (e == NULL) return ESP_ERR_NVS_NOT_FOUND;
    memset(e, 0, sizeof *e);
    return ESP_OK;
}

/* ---- netif / 事件循环：进程级单例，第二次返回 INVALID_STATE ---- */
unsigned test_netif_inits;
unsigned test_event_loop_creates;

esp_err_t esp_netif_init(void) {
    test_netif_inits++;
    return test_netif_inits == 1 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t esp_event_loop_create_default(void) {
    test_event_loop_creates++;
    return test_event_loop_creates == 1 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// 按 addr 真实格式化，不再恒返 192.168.1.7：AP 需要 192.168.4.1。
// 约定与桩里的 esp_netif_get_ip_info 一致（小端：低字节是第一段）。
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int len) {
    if (buf == NULL || len <= 0) return buf;
    unsigned value = (addr != NULL) ? addr->addr : 0;
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%u.%u.%u.%u", value & 0xffu, (value >> 8) & 0xffu,
             (value >> 16) & 0xffu, (value >> 24) & 0xffu);
    int i = 0;
    for (; tmp[i] != '\0' && i + 1 < len; i++) buf[i] = tmp[i];
    buf[i] = '\0';
    return buf;
}

/* ---- LVGL 补充 ---- */
void lv_obj_set_pos(lv_obj_t *object, int x, int y) { (void)object; (void)x; (void)y; }
void lv_obj_remove_flag(lv_obj_t *object, int flag) { (void)object; (void)flag; }
void lv_obj_set_style_pad_all(lv_obj_t *object, int pad, int selector) {
    (void)object; (void)pad; (void)selector;
}
void lv_obj_set_style_border_color(lv_obj_t *object, uint32_t color, int selector) {
    (void)object; (void)color; (void)selector;
}

/* ---- 电量 ---- */
int test_battery_soc = 88;
int bsp_battery_soc(void) { return test_battery_soc; }
int bsp_battery_mv(void) { return 3900; }

void test_queue_reset(void) {
    memset(test_queues, 0, sizeof test_queues);
    test_queue_count = 0;
}

/* ---- 任务/信号量池跨用例只增不减，S3 的多个用例需要显式清零 ---- */
void test_task_reset(void) {
    memset(test_tasks, 0, sizeof test_tasks);
    test_task_count = 0;
    test_task_deletes = 0;
    test_notifications = 0;
    test_latest_task = NULL;
    test_current_task = NULL;
    test_create_fails = false;
}

void test_sem_reset(void) {
    memset(test_sems, 0, sizeof test_sems);
    test_sem_count = 0;
    test_sem_fails = false;
    test_auto_ack = false;
}
