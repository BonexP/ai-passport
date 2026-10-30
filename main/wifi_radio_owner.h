// main/wifi_radio_owner.h —— 谁在用那块射频。
//
// 常驻联网服务与配网服务都要独占 Wi-Fi 驱动、netif 与事件处理器，两者绝不能同时活着。
// 这条互斥原本只靠"调用方记得先停"来维持；抽成一个显式的持有者，启动时抢不到就拒绝，
// 把"忘了停"从运行期怪现象变成一次明确的失败。
//
// 纯逻辑：不 include 任何 ESP-IDF 头文件，可在主机上直接测。
//
// 单写者不变量：所有 claim/release 都发生在输入任务里（app_main 的 wifi_boot 任务与
// 按键分发共用一把"启动窗口门"，见 main.c 的 s_boot_state）。因此这里只有一个字节，
// 不加临界区。**新增第三个入口（OTA、第二个按键、定时器）时必须重新评估这条不变量**
// —— 那时需要换成原子 CAS 或加 portMUX。
#pragma once

#include <stdbool.h>

typedef enum {
    WIFI_RADIO_FREE = 0,
    WIFI_RADIO_PROFILE,   // 常驻联网服务
    WIFI_RADIO_PROVISION, // SoftAP 配网服务
} wifi_radio_owner_id_t;

typedef struct {
    unsigned char owner; // wifi_radio_owner_id_t
} wifi_radio_owner_t;

void                  wifi_radio_owner_init(wifi_radio_owner_t *owner);
// true = 抢到。已被别人持有（或 who 非法）时返回 false，且不改动现状。
bool                  wifi_radio_claim(wifi_radio_owner_t *owner, wifi_radio_owner_id_t who);
// 只允许释放自己持有的那一个：误传别人不会把对方踢下线。
bool                  wifi_radio_release(wifi_radio_owner_t *owner, wifi_radio_owner_id_t who);
wifi_radio_owner_id_t wifi_radio_holder(const wifi_radio_owner_t *owner);

// 进程内唯一实例。永不返回 NULL。
wifi_radio_owner_t *wifi_radio_shared(void);
