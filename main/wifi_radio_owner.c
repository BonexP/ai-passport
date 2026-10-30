// main/wifi_radio_owner.c —— 射频持有者的唯一实例与最简状态机。
#include "wifi_radio_owner.h"

#include <stddef.h>

void wifi_radio_owner_init(wifi_radio_owner_t *owner) {
    if (owner == NULL) return;
    owner->owner = (unsigned char)WIFI_RADIO_FREE;
}

bool wifi_radio_claim(wifi_radio_owner_t *owner, wifi_radio_owner_id_t who) {
    if (owner == NULL) return false;
    if (who != WIFI_RADIO_PROFILE && who != WIFI_RADIO_PROVISION) return false;
    if (owner->owner != (unsigned char)WIFI_RADIO_FREE) return false;
    owner->owner = (unsigned char)who;
    return true;
}

bool wifi_radio_release(wifi_radio_owner_t *owner, wifi_radio_owner_id_t who) {
    if (owner == NULL) return false;
    if (owner->owner != (unsigned char)who) return false;
    owner->owner = (unsigned char)WIFI_RADIO_FREE;
    return true;
}

wifi_radio_owner_id_t wifi_radio_holder(const wifi_radio_owner_t *owner) {
    if (owner == NULL) return WIFI_RADIO_FREE;
    return (wifi_radio_owner_id_t)owner->owner;
}

// 静态初始化而不是惰性初始化：两个服务来自不同任务，"第一次调用顺便清零"
// 会在启动窗口里与另一个任务打架。
static wifi_radio_owner_t s_owner = { .owner = (unsigned char)WIFI_RADIO_FREE };

wifi_radio_owner_t *wifi_radio_shared(void) { return &s_owner; }
