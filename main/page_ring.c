// main/page_ring.c —— 产品页环的实现。契约见 page_ring.h。
#include "page_ring.h"

#include <stddef.h>

// 顺序即用户短按上下键看到的顺序。菜单与配网不在其中。
static const page_id_t RING[] = {
    PAGE_STATUS,
    PAGE_WEATHER,
};

#define RING_COUNT (sizeof RING / sizeof RING[0])

// 返回下标；不在环里返回 -1。
static int ring_index(page_id_t page) {
    for (size_t i = 0; i < RING_COUNT; i++) {
        if (RING[i] == page) {
            return (int)i;
        }
    }
    return -1;
}

bool page_ring_contains(page_id_t current) {
    return ring_index(current) >= 0;
}

page_id_t page_ring_next(page_id_t current) {
    int index = ring_index(current);
    if (index < 0) {
        return current;
    }
    return RING[(size_t)(index + 1) % RING_COUNT];
}

page_id_t page_ring_prev(page_id_t current) {
    int index = ring_index(current);
    if (index < 0) {
        return current;
    }
    // 先加 RING_COUNT 再取模，避免负数取模的实现差异。
    return RING[(size_t)(index + (int)RING_COUNT - 1) % RING_COUNT];
}
