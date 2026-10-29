// main/ui_product.c —— 产品视觉壳的实现。
#include "ui_product.h"

#include <string.h>

// 所有色块的共同形状：平面、无圆角、无边框、无内边距、不可滚动。
static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *ui_product_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(scr, 240, 320);
    lv_obj_set_style_radius(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_P_BG), 0);
    return scr;
}

lv_obj_t *ui_product_header(lv_obj_t *parent) {
    lv_obj_t *title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_P_DIM), 0);
    lv_obj_set_pos(title, 18, 14);
    lv_label_set_text(title, "AI PASSPORT");

    lv_obj_t *battery = lv_label_create(parent);
    lv_obj_set_style_text_font(battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(battery, lv_color_hex(UI_P_DIM), 0);
    lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, -18, 14);
    lv_label_set_text(battery, "--%");
    return battery;
}

lv_obj_t *ui_product_card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    return block(parent, x, y, w, h, color);
}

lv_obj_t *ui_product_text(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                          const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text != NULL ? text : "");
    return label;
}

void ui_product_seal_bar(lv_obj_t *parent, int x, int y, int w, int h) {
    (void)block(parent, x, y, w, h, UI_P_SEAL);
}

void ui_product_rule(lv_obj_t *parent, int x, int y, int w) {
    (void)block(parent, x, y, w, 1, UI_P_RULE);
}

void ui_product_card_select(lv_obj_t *card, lv_obj_t *text, bool selected, bool enabled) {
    uint32_t bg = UI_P_CARD;
    uint32_t fg = UI_P_INK;
    if (!enabled) {
        bg = UI_P_BG;
        fg = UI_P_MUTED;
    } else if (selected) {
        bg = UI_P_SEAL;
        fg = UI_P_CARD;
    }
    lv_obj_set_style_bg_color(card, lv_color_hex(bg), 0);
    if (text != NULL) lv_obj_set_style_text_color(text, lv_color_hex(fg), 0);
}

void ui_product_battery_text(lv_obj_t *label, int soc) {
    char buf[8];
    if (soc < 0) {
        memcpy(buf, "--%", 4);
    } else {
        int value = soc > 100 ? 100 : soc;
        if (value >= 100) {
            buf[0] = '1';
            buf[1] = '0';
            buf[2] = '0';
            buf[3] = '%';
            buf[4] = '\0';
        } else if (value >= 10) {
            buf[0] = (char)('0' + value / 10);
            buf[1] = (char)('0' + value % 10);
            buf[2] = '%';
            buf[3] = '\0';
        } else {
            buf[0] = (char)('0' + value);
            buf[1] = '%';
            buf[2] = '\0';
        }
    }
    lv_label_set_text(label, buf);
}
