// main/main.c —— FoloToy AI Passport BSP 驱动参考示例:初始化 + 菜单 + 按键分发。
//
// 按键语义(全局统一):
//   上/下 短按   菜单中=移动选中项;演示页中=该页自定义
//   确定  短按   菜单中=进入选中项;演示页中=该页自定义
//   确定  长按   演示页中=返回菜单(由本文件统一拦截)
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"      // 错误日志里要打印 BSP_LCD_* 引脚号
#include "demo.h"
#include "demo_navigation.h"
#include "key_guard.h"
#include "page_router.h"
#include "page_status.h"
#include "ui_pixel.h"
#include "wifi_profile_service.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "main";

static const demo_entry_t DEMOS[] = {
    { .name = "Display", .enter = demo_display_enter, .exit = demo_display_exit,
      .key = demo_display_key },
    { .name = "Button", .enter = demo_button_enter, .exit = demo_button_exit,
      .key = demo_button_key },
    { .name = "Audio", .enter = demo_audio_enter, .exit = demo_audio_exit,
      .key = demo_audio_key, .start = demo_audio_start, .stop = demo_audio_stop,
      .radio_exclusive = true },
    { .name = "Battery", .enter = demo_battery_enter, .exit = demo_battery_exit,
      .key = demo_battery_key },
    { .name = "BLE", .enter = demo_ble_enter, .exit = demo_ble_exit,
      .key = demo_ble_key, .start = demo_ble_start, .stop = demo_ble_stop,
      .radio_exclusive = true },
    { .name = "Low Power", .enter = demo_low_power_enter, .exit = demo_low_power_exit,
      .key = demo_low_power_key, .start = demo_low_power_start, .stop = demo_low_power_stop,
      .radio_exclusive = true },
};

// 菜单下标用具名常量：删掉 Wi-Fi 那一项后 BLE 与 Low Power 各前移一位，而 s_ok[]
// 是按位置赋值的 —— 编译器不会发现这种错位，只能靠这张表加编译期断言兜住。
enum {
    IDX_DISPLAY = 0,
    IDX_BUTTON,
    IDX_AUDIO,
    IDX_BATTERY,
    IDX_BLE,
    IDX_LOW_POWER,
    DEMO_COUNT,
};
_Static_assert(sizeof(DEMOS) / sizeof(DEMOS[0]) == (size_t)DEMO_COUNT, "menu index drift");

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

// 各外设初始化结果:失败的项在菜单里标 [FAIL] 且不允许进入。
static bool s_ok[DEMO_COUNT];

static lv_obj_t *s_menu_scr;
static lv_obj_t *s_cards[DEMO_COUNT];
static lv_obj_t *s_rows[DEMO_COUNT];
static lv_obj_t *s_mascot;
static demo_navigation_t s_navigation;
static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 当前页。状态页是开机首屏，菜单退到它后面；s_navigation 只负责菜单光标。
static page_id_t s_page = PAGE_STATUS;
static bool s_service_paused;
static TaskHandle_t s_boot_task;

static void menu_refresh(void) {
    for (size_t i = 0; i < DEMO_COUNT; i++) {
        lv_label_set_text_fmt(s_rows[i], "%s%s",
                              DEMOS[i].name,
                              s_ok[i] ? "" : "  [FAIL]");
        ui_pixel_set_selected(s_cards[i], i == s_navigation.selected, s_ok[i]);
        lv_obj_set_style_text_color(s_rows[i],
            s_ok[i] ? lv_color_hex(UI_INK) : lv_color_hex(0x7A2020), 0);
    }
}

static void menu_build(void) {
    s_menu_scr = ui_pixel_screen_create("FoloToy");

    for (size_t i = 0; i < DEMO_COUNT; i++) {
        int x = 11 + (int)(i % 2) * 112;
        int y = 52 + (int)(i / 2) * 47;
        s_cards[i] = ui_pixel_panel_create(s_menu_scr, x, y, 102, 40, UI_PAPER);
        s_rows[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_rows[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(s_rows[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_rows[i]);
    }

    s_mascot = ui_pixel_mascot_create(s_menu_scr, 101, 242);

    menu_refresh();
    lv_screen_load(s_menu_scr);
}

static void enter_menu(void) {
    menu_build();
    s_page = PAGE_MENU;
}

// 吃无线资源的页面（Audio / BLE / Low Power）进去前要把常驻 Wi-Fi 服务停掉。
// 停不掉也要继续：页面自己会把启动失败显示出来，总比让菜单整个卡住好。
static void services_pause(void) {
    if (!wifi_profile_service_running()) return;
    wifi_profile_service_stop();
    s_service_paused = true;
    if (wifi_profile_service_running()) {
        ESP_LOGE(TAG, "Wi-Fi 服务未能停止:页面可能起不来无线功能");
    }
}

static void services_resume(void) {
    if (!s_service_paused) return;
    s_service_paused = false;
    esp_err_t e = wifi_profile_service_start();
    if (e != ESP_OK) ESP_LOGE(TAG, "Wi-Fi 服务恢复失败: %s", esp_err_to_name(e));
}

static demo_nav_input_t navigation_input(bsp_btn_t btn, bsp_btn_ev_t event) {
    if (event == BSP_BTN_LONG && btn == BSP_BTN_OK) return DEMO_NAV_INPUT_OK_LONG;
    if (event != BSP_BTN_CLICK) return DEMO_NAV_INPUT_OTHER;
    if (btn == BSP_BTN_UP) return DEMO_NAV_INPUT_UP_CLICK;
    if (btn == BSP_BTN_DOWN) return DEMO_NAV_INPUT_DOWN_CLICK;
    if (btn == BSP_BTN_OK) return DEMO_NAV_INPUT_OK_CLICK;
    return DEMO_NAV_INPUT_OTHER;
}

static void process_input(const input_event_t *input) {
    const demo_nav_input_t nav_input = navigation_input(input->btn, input->event);
    const route_action_t route = page_router_resolve(s_page, nav_input);

    if (route == ROUTE_TO_STATUS) {
        if (!bsp_lvgl_lock(500)) return;
        if (s_menu_scr != NULL) {
            lv_obj_delete(s_menu_scr);
            s_menu_scr = NULL;
            s_mascot = NULL;
        }
        status_page_build();
        s_page = PAGE_STATUS;
        bsp_lvgl_unlock();
        return;
    }

    if (route == ROUTE_TO_MENU) {
        if (!bsp_lvgl_lock(500)) return;
        status_page_destroy();
        enter_menu();
        bsp_lvgl_unlock();
        return;
    }

    if (s_page == PAGE_DEMO) {
        if (s_navigation.active < 0) return; // 防御:状态不同步
        const demo_entry_t *demo = &DEMOS[s_navigation.active];

        if (route != ROUTE_DEMO_EXIT) {
            demo->key(input->btn, input->event);
            return;
        }

        esp_err_t e = demo->stop ? demo->stop() : ESP_OK;
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "%s 页面停止失败: %s", demo->name, esp_err_to_name(e));
            return;
        }
        if (!bsp_lvgl_lock(500)) return;
        demo->exit();
        demo_navigation_complete_exit(&s_navigation);
        enter_menu();
        bsp_lvgl_unlock();
        // 页面对象已经释放,此时再把 Wi-Fi 栈拉起来。
        if (demo->radio_exclusive) services_resume();
        return;
    }

    // ---- 菜单态 ----
    if (route == ROUTE_IGNORE) return;
    if (!bsp_lvgl_lock(500)) return;

    const demo_nav_result_t result =
        demo_navigation_handle(&s_navigation, nav_input, s_ok[s_navigation.selected]);

    if (result.action == DEMO_NAV_ACTION_REFRESH) {
        menu_refresh();
        ui_pixel_mascot_jump(s_mascot);
        bsp_lvgl_unlock();
        return;
    }

    if (result.action == DEMO_NAV_ACTION_ENTER) {
        const demo_entry_t *demo = &DEMOS[result.index];
        ui_pixel_mascot_jump(s_mascot);
        lv_obj_delete(s_menu_scr);
        s_menu_scr = NULL;
        s_mascot = NULL;
        demo->enter();
        s_page = PAGE_DEMO;
        bsp_lvgl_unlock();

        // 停服务放在解锁之后：stop() 最坏等 worker 两秒,持 LVGL 锁会冻住整个 UI。
        // 也放在 enter() 之后：enter() 只吃独立的 LVGL 池,真正吃系统堆的是 start()。
        if (demo->radio_exclusive) services_pause();

        esp_err_t e = demo->start ? demo->start() : ESP_OK;
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "%s 页面启动失败: %s", demo->name, esp_err_to_name(e));
        }
        return;
    }

    bsp_lvgl_unlock();
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

static esp_err_t input_dispatch_init(void) {
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "demo_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void input_dispatch_deinit(void) {
    s_input_ready = false;
    if (s_input_task) {
        vTaskDelete(s_input_task);
        s_input_task = NULL;
    }
    if (s_input_queue) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
    }
}

// Waking from deep sleep by key reboots the chip while that key is still held: the
// button component starts a few hundred milliseconds later, registers a press that
// is already down, and grades it a long press once it has been down for its
// threshold (500 ms here), so an unintended action fires almost immediately. Drop
// key actions until that press comes up; the deadline is only a safety net so a
// stuck key cannot leave the pad dead. The state machine lives in key_guard.h so it
// can be unit-tested without the rest of the demo.
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// button callbacks run on the shared esp_timer task; enqueue only and return immediately.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // Consume the guard before the readiness check: a release can arrive after the
    // post-init voltage sample but before input dispatch is ready, and dropping it
    // here would leave the guard armed to swallow the next real press.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        // The release and the deadline both arrive on this task; log the disarm
        // transition once it happens.
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

// 一次性引导任务：栈初始化可能耗时数百毫秒（NVS 打开、预设落盘、Wi-Fi 驱动启动），
// 放在 app_main 里会让首屏黑着。跑完自杀。
static void wifi_boot_task(void *arg) {
    (void)arg;
    esp_err_t err = wifi_profile_service_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "常驻 Wi-Fi 服务启动失败: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "FoloToy AI Passport BSP demo 启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "休眠唤醒原因位图: 0x%02x", (unsigned)wake_causes);
    }
    // 按键唤醒时,那颗键在整个启动期间还按着。先挡住它的动作(见 on_key)。
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是本 demo 的 UI 载体,失败就没有菜单可言 —— 打清楚日志后退出,
    // 不做"串口菜单"降级(那会让本文件复杂一倍,违背参考示例的初衷)。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,demo 无法继续。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    demo_navigation_init(&s_navigation, DEMO_COUNT);

    // 其余外设单项失败不阻塞:菜单里标 [FAIL],其他项照常可测。
    s_ok[IDX_DISPLAY] = true;                         // Display 已确认可用
    esp_err_t input_err = input_dispatch_init();
    esp_err_t button_err = input_err == ESP_OK
                         ? bsp_button_init(on_key, NULL)
                         : ESP_ERR_INVALID_STATE;
    s_ok[IDX_BUTTON] = input_err == ESP_OK && button_err == ESP_OK;
    if (input_err != ESP_OK) {
        ESP_LOGE(TAG, "按键事件任务创建失败: %s", esp_err_to_name(input_err));
    } else if (button_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(button_err));
        input_dispatch_deinit();
    } else if (s_key_guard.armed) {
        // 若唤醒键在按键驱动启动前就已松手,组件不会产生 release 事件,守卫会一直
        // 拦到兜底。初始化完成后按真实电压同步一次:已松手立即撤防。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
        }
    }
    s_ok[IDX_AUDIO] = (bsp_audio_init() == ESP_OK);
    s_ok[IDX_BATTERY] = (bsp_battery_init() == ESP_OK);
    s_ok[IDX_BLE] = true;                              // 页面内按需初始化并显示错误
    s_ok[IDX_LOW_POWER] = true;

    if (bsp_lvgl_lock(1000)) {
        status_page_build();                            // 首屏是状态页
        s_page = PAGE_STATUS;
        bsp_lvgl_unlock();
        s_input_ready = true;
    }

    // 栈初始化挪出 app_main；状态页在 seq == 0 期间显示 "Starting Wi-Fi..."。
    if (xTaskCreate(wifi_boot_task, "wifi_boot", 4096, NULL, 4, &s_boot_task) != pdPASS) {
        ESP_LOGE(TAG, "常驻 Wi-Fi 服务任务创建失败");
    }

    ESP_LOGI(TAG, "就绪:Display=%d Button=%d Audio=%d Battery=%d", s_ok[IDX_DISPLAY],
             s_ok[IDX_BUTTON], s_ok[IDX_AUDIO], s_ok[IDX_BATTERY]);
}
