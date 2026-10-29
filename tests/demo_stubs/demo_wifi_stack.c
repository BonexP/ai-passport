// tests/demo_stubs/demo_wifi_stack.c —— 有状态的 Wi-Fi 驱动假件（STA + AP）。
//
// 两条注入线，彼此独立：
//   - STA 会话用 test_wifi_step / test_wifi_fail_at，编号 1..9，与旧矩阵逐位对应；
//   - AP  会话用 test_ap_step / test_ap_fail_at，编号 1..13（含 httpd 三步）。
// 用 test_wifi_session() 切换；默认 STA，所以既有用例一行都不用改。
//
// 事件处理器是一张表（base + id + cb + instance），不是"按 base 存一个"：
// AP 与 STA 都要注册 WIFI_EVENT，单槽会把 STA 的回调静默覆盖掉。
#include "demo_test_stubs.h"

#include <assert.h>
#include <string.h>

/* STA 的 9 个步骤，顺序与 wifi_profile_service_start() 一致。 */
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

unsigned      test_wifi_step;
unsigned      test_wifi_fail_at;
unsigned      test_wifi_destroys;
esp_err_t     test_wifi_fail_err = ESP_ERR_NO_MEM;
esp_err_t     test_wifi_set_config_result = ESP_OK;
esp_err_t     test_wifi_connect_result = ESP_OK;
wifi_config_t test_wifi_last_cfg;
wifi_config_t test_wifi_cfg_sta;
wifi_config_t test_wifi_cfg_ap;
int8_t        test_wifi_rssi = -55;

unsigned test_ap_step;
unsigned test_ap_fail_at;
unsigned test_ap_destroys;

typedef enum { NETIF_NONE = 0, NETIF_STA, NETIF_AP } netif_role_t;

static esp_netif_t  s_netif;
static bool         s_netif_live;
static bool         s_driver_registered;
static bool         s_defaults_live;
static bool         s_wifi_live;
static bool         s_wifi_running;
static netif_role_t s_role;
static int          s_mode;
static bool         s_ap_session;

#define TEST_HANDLER_SLOTS 8
typedef struct {
    esp_event_base_t base;
    int32_t          id;
    void           (*cb)(void *, esp_event_base_t, int32_t, void *);
    void            *arg;
    void            *instance;
    bool             live;
} test_handler_t;

static test_handler_t s_handlers[TEST_HANDLER_SLOTS];
static unsigned       s_handler_count;

static esp_err_t next_step(void) {
    if (s_ap_session) return test_ap_next_step();
    test_wifi_step++;
    if (test_wifi_fail_at != 0 && test_wifi_step == test_wifi_fail_at) return test_wifi_fail_err;
    return ESP_OK;
}

unsigned test_ap_next_step(void) {
    test_ap_step++;
    if (test_ap_fail_at != 0 && test_ap_step == test_ap_fail_at) return test_wifi_fail_err;
    return ESP_OK;
}

void test_wifi_session(test_wifi_session_t session) {
    s_ap_session = (session == TEST_WIFI_SESSION_AP);
}

int test_wifi_mode(void) { return s_mode; }

/* 桩里的 WIFI_EVENT / IP_EVENT 是字符串字面量，直接比较指针会触发 -Waddress。 */
static bool base_is(esp_event_base_t base, esp_event_base_t wanted) {
    return base != NULL && wanted != NULL && strcmp(base, wanted) == 0;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *cfg) {
    (void)cfg;
    assert(!s_netif_live);
    if (next_step() != ESP_OK) return NULL;
    s_netif_live = true;
    s_role = NETIF_NONE;
    return &s_netif;
}

esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif) {
    assert(netif == &s_netif && s_netif_live);
    // IDF 即使 attach 失败也会记住这个 netif，所以销毁时要用 destroy_default_wifi。
    s_driver_registered = true;
    s_role = NETIF_STA;
    return next_step();
}

esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *netif) {
    assert(netif == &s_netif && s_netif_live);
    s_driver_registered = true;
    s_role = NETIF_AP;
    return next_step();
}

esp_err_t esp_wifi_set_default_wifi_sta_handlers(void) {
    assert(s_driver_registered);
    esp_err_t result = next_step();
    s_defaults_live = (result == ESP_OK);
    return result;
}

esp_err_t esp_wifi_set_default_wifi_ap_handlers(void) {
    assert(s_driver_registered);
    esp_err_t result = next_step();
    s_defaults_live = (result == ESP_OK);
    return result;
}

void esp_netif_destroy_default_wifi(void *netif) {
    (void)netif;
    // 顺序契约：驱动与事件处理器都必须先拆干净，才能销毁 netif。
    // 用"处理器表已空"代替"某个具体回调为空"，STA 与 AP 两条路径共用同一条断言。
    assert(!s_wifi_live && s_handler_count == 0);
    if (s_netif_live) {
        if (s_ap_session) {
            test_ap_destroys++;
        } else {
            test_wifi_destroys++;
        }
    }
    s_netif_live = false;
    s_driver_registered = false;
    s_defaults_live = false;
    s_role = NETIF_NONE;
}

esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) {
    (void)cfg;
    assert(s_defaults_live);
    esp_err_t result = next_step();
    s_wifi_live = (result == ESP_OK);
    return result;
}

esp_err_t esp_wifi_deinit(void) {
    // 容忍未初始化：拆栈路径是守卫式的，假件不该替调用方做断言。
    s_wifi_live = false;
    return ESP_OK;
}

esp_err_t esp_wifi_set_storage(int storage) {
    (void)storage;
    return next_step();
}

esp_err_t esp_wifi_set_mode(int mode) {
    s_mode = mode;
    return next_step();
}

esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *cfg) {
    assert(cfg != NULL);
    if (test_wifi_set_config_result != ESP_OK) return test_wifi_set_config_result;
    test_wifi_last_cfg = *cfg;
    if (iface == WIFI_IF_AP) {
        test_wifi_cfg_ap = *cfg;
        // 配网启动时这一步参与编号；STA 在 connect_index 里也会调，那时不该计步。
        if (s_ap_session) return next_step();
        return ESP_OK;
    }
    test_wifi_cfg_sta = *cfg;
    return ESP_OK;
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
    esp_err_t result = next_step();
    if (result != ESP_OK) return result;
    for (unsigned i = 0; i < TEST_HANDLER_SLOTS; i++) {
        if (s_handlers[i].live) continue;
        s_handlers[i].base = base;
        s_handlers[i].id = id;
        s_handlers[i].cb = callback;
        s_handlers[i].arg = arg;
        s_handlers[i].instance = &s_handlers[i];
        s_handlers[i].live = true;
        s_handler_count++;
        if (instance != NULL) *instance = &s_handlers[i];
        return ESP_OK;
    }
    assert(!"handler table full");
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
                                                esp_event_handler_instance_t instance) {
    // instance 为 NULL 时按 base+id 全删，这样 profile service 的"两个一起解"仍可用。
    for (unsigned i = 0; i < TEST_HANDLER_SLOTS; i++) {
        if (!s_handlers[i].live) continue;
        bool match = (instance != NULL) ? (instance == &s_handlers[i])
                                        : (base_is(s_handlers[i].base, base) &&
                                           s_handlers[i].id == id);
        if (!match) continue;
        s_handlers[i].live = false;
        s_handler_count--;
    }
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
    assert(netif == &s_netif && s_netif_live);
    if (info == NULL) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof *info);
    // 小端：低字节是第一段。AP 固定 192.168.4.1，STA 给一个可预测值。
    info->ip.addr = (s_role == NETIF_AP) ? 0x0104A8C0u : 0x0701A8C0u;
    return ESP_OK;
}

esp_err_t esp_read_mac(uint8_t *mac, int type) {
    (void)type;
    if (mac == NULL) return ESP_ERR_INVALID_ARG;
    static const uint8_t fixed[6] = { 0x24, 0x6F, 0x28, 0xA1, 0xB2, 0xC3 };
    memcpy(mac, fixed, sizeof fixed);
    return ESP_OK;
}

void test_wifi_fire_event(esp_event_base_t base, int32_t id, void *data) {
    // 先拷贝一份索引再分发：回调里可能会注销自己。
    for (unsigned i = 0; i < TEST_HANDLER_SLOTS; i++) {
        if (!s_handlers[i].live) continue;
        if (!base_is(s_handlers[i].base, base)) continue;
        if (s_handlers[i].id != ESP_EVENT_ANY_ID && s_handlers[i].id != id) continue;
        if (s_handlers[i].cb != NULL) s_handlers[i].cb(s_handlers[i].arg, base, id, data);
    }
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
    memset(&test_wifi_cfg_sta, 0, sizeof test_wifi_cfg_sta);
    memset(&test_wifi_cfg_ap, 0, sizeof test_wifi_cfg_ap);

    test_ap_step = 0;
    test_ap_fail_at = 0;
    test_ap_destroys = 0;

    s_netif_live = false;
    s_driver_registered = false;
    s_defaults_live = false;
    s_wifi_live = false;
    s_wifi_running = false;
    s_role = NETIF_NONE;
    s_mode = WIFI_MODE_STA;
    s_ap_session = false;

    memset(s_handlers, 0, sizeof s_handlers);
    s_handler_count = 0;
}

bool test_wifi_stack_clean(void) {
    return !s_netif_live && !s_driver_registered && !s_defaults_live && !s_wifi_live &&
           !s_wifi_running && s_handler_count == 0 && s_role == NETIF_NONE;
}

void test_wifi_fire_connected(void) {
    test_wifi_fire_event(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
}

void test_wifi_fire_got_ip(void) {
    test_wifi_fire_event(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
}

void test_wifi_fire_disconnected(uint8_t reason) {
    wifi_event_sta_disconnected_t event;
    memset(&event, 0, sizeof event);
    event.reason = reason;
    test_wifi_fire_event(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
}

void test_wifi_fire_ap_staconnected(void) {
    wifi_event_ap_staconnected_t event;
    memset(&event, 0, sizeof event);
    test_wifi_fire_event(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &event);
}

void test_wifi_fire_ap_stadisconnected(void) {
    wifi_event_ap_stadisconnected_t event;
    memset(&event, 0, sizeof event);
    test_wifi_fire_event(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &event);
}

void test_wifi_fire_ap_ipassigned(void) {
    ip_event_ap_staipassigned_t event;
    memset(&event, 0, sizeof event);
    test_wifi_fire_event(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event);
}
