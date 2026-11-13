// main/page_ring.h —— 产品页环：状态页、天气页、用量页之间怎么切换（纯逻辑）。
//
// 顺序是 状态 → 天气 → 用量 → 状态，两端相接，所以上下键都能绕回来。
// 菜单（demo 测试页）不在环里：它是硬件自检入口，不是产品导航的一部分。
// P3 的行情页、P2.5 的局域网页会加进 RING，调用方不用改。
#pragma once

#include <stdbool.h>

#include "page_router.h"

// 环里的下一页 / 上一页。current 不在环里时原样返回（调用方据此判断"没变化"）。
page_id_t page_ring_next(page_id_t current);
page_id_t page_ring_prev(page_id_t current);

// current 是否是产品页环的一员。
bool page_ring_contains(page_id_t current);
