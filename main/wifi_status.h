// main/wifi_status.h —— Wi-Fi 状态快照：类型、注入式临界区、文案格式化。
//
// 纯逻辑：不 include 任何 ESP-IDF 头文件。临界区以 vtable 注入，所以主机测试能用
// 计数器验证"每次访问恰好锁一次"，设备上则注入 portMUX 的实现。
//
// 写者不变量：同一时刻只有一个写者——"当前持有 Wi-Fi 栈的那一方"（联网服务或配网
// 服务，两者互斥）。因此 publish 不需要与另一个写者竞争，也不需要在两个服务之间
// 再加一把锁；这条不变量必须保持，否则快照会出现撕裂。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_profile.h"

// 页面要显示的粗粒度阶段。刻意不照搬状态机的内部状态：UI 不该知道"这一轮试到第几条"
// 之外的结构。
typedef enum {
    WIFI_PHASE_IDLE = 0,    // 服务没在跑
    WIFI_PHASE_CONNECTING,  // 正在尝试某一条
    WIFI_PHASE_CONNECTED,   // 已拿到 IP
    WIFI_PHASE_NO_PROFILE,  // 库为空（含预设也没注入成功）
    WIFI_PHASE_ALL_FAILED,  // 有 profile，一轮全失败
    WIFI_PHASE_PROVISIONING,// 配网态（AP 已起）
    WIFI_PHASE_ERROR,       // 栈级故障：NVS 打不开、驱动起不来
} wifi_phase_t;

// 失败原因只保留用户能据此行动的几类。"2 还是 3"这种区别对用户没有意义。
typedef enum {
    WIFI_FAIL_NONE = 0,
    WIFI_FAIL_AUTH,    // 密码错、握手超时
    WIFI_FAIL_NO_AP,   // 找不到 AP、beacon 丢失
    WIFI_FAIL_TIMEOUT, // 本地单条超时
    WIFI_FAIL_DHCP,    // 关联成功但没拿到 IP
    WIFI_FAIL_DRIVER,  // 本地配置或驱动错误
    WIFI_FAIL_SAVE,    // profile 落盘失败
    WIFI_FAIL_OTHER,   // 驱动给了不认识的原因码
} wifi_fail_reason_t;

#define WIFI_STATUS_IP_MAX 16 // "192.168.4.1" + NUL

// POD，无指针，可整体拷贝。约 72 B。
typedef struct {
    uint8_t  phase;  // wifi_phase_t
    uint8_t  fail;   // wifi_fail_reason_t：最近一次失败原因
    uint8_t  profile_count;
    uint8_t  ap_clients; // 配网态：已拿到 DHCP 的终端数
    int8_t   active;     // -1 = 未选定
    int8_t   trying;     // -1 = 未在尝试
    int8_t   rssi;       // -128 = 未知
    uint16_t seq;        // 每次 publish +1，UI 据此判断"变没变"
    char     ssid[WIFI_PROFILE_SSID_MAX]; // 连接/尝试中的 SSID；配网态复用为 AP SSID
    char     ip[WIFI_STATUS_IP_MAX];
} wifi_status_snapshot_t;

typedef struct {
    void *ctx;
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} wifi_status_lock_t;

typedef struct {
    wifi_status_snapshot_t    snapshot;
    const wifi_status_lock_t *lock; // 可为 NULL，等价于不加锁（只在单线程场景用）
} wifi_status_bus_t;

// 清零快照并绑定临界区。bus 不可为 NULL。
void wifi_status_bus_init(wifi_status_bus_t *bus, const wifi_status_lock_t *lock);

// 整体覆盖并 seq++，全程持锁。next->seq 被忽略，由 bus 自己维护。
void wifi_status_publish(wifi_status_bus_t *bus, const wifi_status_snapshot_t *next);

// 整体拷贝出来，全程持锁。读侧通常是 LVGL 定时器，200-500 ms 一次。
void wifi_status_read(const wifi_status_bus_t *bus, wifi_status_snapshot_t *out);

// WIFI_REASON_* 的数值 → 失败原因。放在纯逻辑里，因为映射本身不需要 IDF。
// 只映射用户能据此行动的四类，其余归 OTHER。
wifi_fail_reason_t wifi_status_reason_from_code(int reason_code);

// 该不该自动进配网：只有"库为空且本次开机还没试过"才返回 true。
// ALL_FAILED 返回 false —— 那时只提示用户长按 UP，不抢屏。
bool wifi_status_should_auto_provision(const wifi_status_snapshot_t *s, bool already_tried);

// 静态文案。返回值永不为 NULL，且全是 ASCII（Montserrat 14/20 没有中文字形）。
const char *wifi_status_phase_text(wifi_phase_t phase);
const char *wifi_status_fail_text(wifi_fail_reason_t reason);

// 主行/次行。返回写入字节数（不含 NUL）；缓冲不足返回 0 且**不写半个字段**。
// 传入 NULL 缓冲或 cap==0 同样返回 0。
size_t wifi_status_format_headline(const wifi_status_snapshot_t *s, char *buf, size_t cap);
size_t wifi_status_format_detail(const wifi_status_snapshot_t *s, char *buf, size_t cap);
