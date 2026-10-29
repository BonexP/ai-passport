// main/ui_product.h —— 产品视觉壳：深墨底 + 米色卡片 + 印章红书脊。
//
// 与 ui_pixel（卡通风、带投影和吉祥物）没有任何复用：上游规则明确要求衍生应用
// 重新设计界面，不得沿用 demo 的测试外壳。这里只有平面色块、一条竖书脊和一条
// 分隔线，没有圆角、没有阴影、没有动画。
//
// 所有函数都要求调用方持有 LVGL 锁（页面在持锁路径里调用）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define UI_P_BG    0x14161B // 深墨底
#define UI_P_CARD  0xF3EDE1 // 米色纸
#define UI_P_INK   0x1C1B19 // 纸上的正文
#define UI_P_MUTED 0x6E6558 // 纸上的次级文字
#define UI_P_DIM   0x9A9489 // 墨底上的次级文字
#define UI_P_SEAL  0xA6402F // 印章红
#define UI_P_RULE  0xCFC5B4 // 纸上的分隔线

// 240x320 全屏底色，无装饰。返回 screen，调用方负责 lv_screen_load 与销毁。
lv_obj_t *ui_product_screen(void);

// 页眉：左上角标题，右上角电量。返回电量 label，用 ui_product_battery_text 更新。
lv_obj_t *ui_product_header(lv_obj_t *parent);

// 平面卡片：圆角 0、边框 0、内边距 0。
lv_obj_t *ui_product_card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);

// 左对齐、自动换行的文字块。
lv_obj_t *ui_product_text(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                          const lv_font_t *font, uint32_t color);

// 卡片左侧的印章红竖条。
void ui_product_seal_bar(lv_obj_t *parent, int x, int y, int w, int h);

// 1 px 分隔线。
void ui_product_rule(lv_obj_t *parent, int x, int y, int w);

// 菜单卡片的选中/不可用着色。text 可为 NULL（只改卡片底色）。
void ui_product_card_select(lv_obj_t *card, lv_obj_t *text, bool selected, bool enabled);

// 电量文案：soc < 0 → "--%"，>100 按 100 处理。手写数字，避免 snprintf 的截断告警。
void ui_product_battery_text(lv_obj_t *label, int soc);
