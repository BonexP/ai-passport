// tests/demo_stubs/demo_wifi_stack.c —— 有状态的 STA 驱动假件。
//
// 原来这套假实现写在 tests/test_demo_wifi_runtime.c 里，删掉那个测试就会连假件
// 一起消失。独立成 TU 之后，service 测试与本文件共享同一份，不必重写。
//
// 两条注入线，彼此独立：
//   - test_wifi_fail_at 只编号"会创建资源"的 9 个步骤，与旧 9 步矩阵同构；
//   - esp_netif_init / esp_event_loop_create_default 的单例语义在 demo_runtime.c。
#include "demo_test_stubs.h"

#include <assert.h>
#include <string.h>

enum {
    STEP_NETIF_NEW = 1,
    STEP_ATTACH,
    STEP_DEFAULT_HANDLERS,
    STEP_WIFI_INIT,
    STEP_SET_STORAGE,
    STEP_SET_MODE,
    STEP_WIFI_START,
    STEP_REGISTER_WIFI,
    STEP_REGISTER_IP,
};

unsigned    test_wifi_step;
unsigned    test_wifi_fail_at;
unsigned    test_wifi_destroys;
esp_err_t   test_wifi_fail_err = ESP_ERR_NO_MEM;
esp_err_t   test_wifi_set_config_result = ESP_OK;
esp_err_t   test_wifi_connect_result = ESP_OK;
wifi_config_t test_wifi_last_cfg;
int8_t      test_wifi_rssi = -55;

static esp_netif_t s_netif;
static bool        s_netif_live;
static bool        s_driver_registered;
static bool        s_defaults_live;
static bool        s_wifi_live;
static bool        s_wifi_running;
static bool        s_wifi_handler_live;
static bool        s_ip_handler_live;

static void (*s_wifi_cb)(void *, esp_event_base_t, int32_t, void *);
static void *s_wifi_arg;
static void (*s_ip_cb)(void *, esp_event_base_t, int32_t, void *);
static void *s_ip_arg;

static esp_err_t next_step(void) {
    test_wifi_step++;
    if (test_wifi_fail_at != 0 && test_wifi_step == test_wifi_fail_at) return test_wifi_fail_err;
    return ESP_OK;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg) {
    (void)cfg;
    assert(!s_netif_live);
    if (next_step() != ESP_OK) return NULL;
    s_netif_live = true;
    return &s_netif;
}

esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif) {
    assert(netif == &s_netif && s_netif_live);
    // IDF 即使 attach 失败也会记住这个 netif，所以销毁时要用 destroy_default_wifi。
    s_driver_registered = true;
    return next_step();
}

esp_err_t esp_wifi_set_default_wifi_sta_handlers(void) {
    assert(s_driver_registered);
    esp_err_t result = next_step();
    s_defaults_live = (result == ESP_OK); // 失败时 IDF 会回滚半挂上的默认 handler
    return result;
}

void esp_netif_destroy_default_wifi(void *netif) {
    (void)netif;
    // 顺序契约：必须先把驱动与事件处理器拆干净，才能销毁 netif。
    assert(!s_wifi_live && !s_wifi_handler_live && !s_ip_handler_live);
    if (s_netif_live) test_wifi_destroys++;
    s_netif_live = false;
    s_driver_registered = false;
    s_defaults_live = false;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) {
    (void)cfg;
    assert(s_defaults_live);
    esp_err_t result = next_step();
    s_wifi_live = (result == ESP_OK);
    return result;
}

esp_err_t esp_wifi_deinit(void) {
    // 容忍未初始化：拆栈路径是守卫式的，但假件不该替调用方做断言。
    s_wifi_live = false;
    return ESP_OK;
}

esp_err_t esp_wifi_set_storage(int storage) {
    (void)storage;
    return next_step();
}

esp_err_t esp_wifi_set_mode(int mode) {
    (void)mode;
    return next_step();
}

esp_err_t esp_wifi_start(void) {
    esp_err_t result = next_step();
    s_wifi_running = (result == ESP_OK);
    return result;
}

esp_err_t esp_wifi_stop(void) {
    s_wifi_running = false;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
                                              void (*callback)(void *, esp_event_base_t, int32_t,
                                                               void *),
                                              void *arg,
                                              esp_event_handler_instance_t *instance) {
    (void)id;
    esp_err_t result = next_step();
    if (result != ESP_OK) return result;
    if (base == WIFI_EVENT) {
        s_wifi_cb = callback;
        s_wifi_arg = arg;
        s_wifi_handler_live = true;
        if (instance != NULL) *instance = (void *)1;
    } else {
        s_ip_cb = callback;
        s_ip_arg = arg;
        s_ip_handler_live = true;
        if (instance != NULL) *instance = (void *)2;
    }
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
                                                esp_event_handler_instance_t instance) {
    (void)id;
    (void)instance;
    if (base == WIFI_EVENT) {
        s_wifi_cb = NULL;
        s_wifi_arg = NULL;
        s_wifi_handler_live = false;
    } else {
        s_ip_cb = NULL;
        s_ip_arg = NULL;
        s_ip_handler_live = false;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *cfg) {
    (void)iface;
    assert(cfg != NULL);
    if (test_wifi_set_config_result != ESP_OK) return test_wifi_set_config_result;
    test_wifi_last_cfg = *cfg;
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void) { return test_wifi_connect_result; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    if (ap == NULL) return ESP_ERR_INVALID_ARG;
    memset(ap, 0, sizeof *ap);
    ap->rssi = test_wifi_rssi;
    return ESP_OK;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
    (void)netif;
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof *info);
    info->ip.addr = 0x0701A8C0u; // 192.168.1.7（小端）
    return ESP_OK;
}

void test_wifi_stack_reset(void) {
    test_wifi_step = 0;
    test_wifi_fail_at = 0;
    test_wifi_fail_err = ESP_ERR_NO_MEM;
    test_wifi_destroys = 0;
    test_wifi_set_config_result = ESP_OK;
    test_wifi_connect_result = ESP_OK;
    test_wifi_rssi = -55;
    memset(&test_wifi_last_cfg, 0, sizeof test_wifi_last_cfg);
    s_netif_live = false;
    s_driver_registered = false;
    s_defaults_live = false;
    s_wifi_live = false;
    s_wifi_running = false;
    s_wifi_handler_live = false;
    s_ip_handler_live = false;
    s_wifi_cb = NULL;
    s_wifi_arg = NULL;
    s_ip_cb = NULL;
    s_ip_arg = NULL;
}

bool test_wifi_stack_clean(void) {
    return !s_netif_live && !s_driver_registered && !s_defaults_live && !s_wifi_live &&
           !s_wifi_running && !s_wifi_handler_live && !s_ip_handler_live;
}

void test_wifi_fire_connected(void) {
    if (s_wifi_cb != NULL) s_wifi_cb(s_wifi_arg, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
}

void test_wifi_fire_got_ip(void) {
    if (s_ip_cb != NULL) s_ip_cb(s_ip_arg, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
}

void test_wifi_fire_disconnected(uint8_t reason) {
    wifi_event_sta_disconnected_t event;
    memset(&event, 0, sizeof event);
    event.reason = reason;
    if (s_wifi_cb != NULL) {
        s_wifi_cb(s_wifi_arg, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
    }
}
