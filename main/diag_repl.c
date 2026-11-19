// main/diag_repl.c —— 可选 USB Serial/JTAG 诊断 REPL。
//
// 设计边界：
//   - 只在 CONFIG_FOLOTOY_DIAG_REPL=y 时编译和启动，默认关闭；
//   - 只读快照，不启动/停止 Wi-Fi 或网络任务，不打印任何凭据；
//   - page 命令只走 main.c 的 page_switch()，由调用方持 LVGL 锁；
//   - 日志与命令共用 USB Serial/JTAG 字节流，不能用于二进制传输。
#include "sdkconfig.h"

#if CONFIG_FOLOTOY_DIAG_REPL

#include "diag_repl.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_data.h"
#include "app_data_shared.h"
#include "app_time.h"
#include "diag_repl_logic.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "net_worker.h"
#include "usage_view.h"
#include "wifi_profile.h"
#include "wifi_profile_nvs.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

static const char *TAG = "diag_repl";
static bool s_started;

// export 命令的 NVS/store 缓冲放在静态存储里：wifi_profile_store_t 约 1.3 KB，
// 而 REPL 任务栈只有 4096 B，压栈风险大。命令串行执行，静态复用是安全的。
static wifi_profile_nvs_t   s_export_nvs;
static wifi_profile_store_t s_export_store;

static int64_t age_ms_from_uptime(int64_t fetched_uptime_ms) {
    if (fetched_uptime_ms < 0) {
        return -1;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    return now_ms >= fetched_uptime_ms ? now_ms - fetched_uptime_ms : -1;
}

static bool has_amd_key(void) {
    const char *key = app_config_amd_key();
    return key != NULL && key[0] != '\0';
}

static const char *text_or_dash(const char *text) {
    return (text != NULL && text[0] != '\0') ? text : "-";
}

static int cmd_status(int argc, char **argv) {
    (void)argc;
    (void)argv;

    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    const int64_t weather_age_ms = age_ms_from_uptime(data.weather.fetched_uptime_ms);
    const int64_t usage_age_ms = age_ms_from_uptime(data.usage.fetched_uptime_ms);

    printf("status\n");
    printf("  page=%s boot=%s services_paused=%d net_worker=%d\n",
           diag_page_name(diag_current_page()), diag_boot_state_name(),
           diag_services_paused() ? 1 : 0, net_worker_running() ? 1 : 0);
    printf("  wifi: phase=%s fail=%s ssid=%s ip=%s rssi=%d profiles=%u clients=%u seq=%u\n",
           wifi_status_phase_text((wifi_phase_t)wifi.phase),
           wifi_status_fail_text((wifi_fail_reason_t)wifi.fail), text_or_dash(wifi.ssid),
           text_or_dash(wifi.ip), (int)wifi.rssi, (unsigned)wifi.profile_count,
           (unsigned)wifi.ap_clients, (unsigned)wifi.seq);
    printf("  time: valid=%d epoch=%" PRId64 "\n", app_time_is_valid() ? 1 : 0,
           app_time_now());
    printf("  weather: seq=%u has_data=%d stale=%d temp_dc=%d code=%d age_ms=%" PRId64
           " place=%s\n",
           (unsigned)data.weather.seq, data.weather.has_data ? 1 : 0,
           data.weather.stale ? 1 : 0, (int)data.weather.temp_dc,
           (int)data.weather.weather_code, weather_age_ms,
           wifi.location.has_location ? text_or_dash(wifi.location.place) : "-");
    printf("  usage: seq=%u has_data=%d status=%s key=%s age_ms=%" PRId64 "\n",
           (unsigned)data.usage.seq, data.usage.usage.has_data ? 1 : 0,
           diag_usage_status_text(data.usage.status), has_amd_key() ? "configured" : "absent",
           usage_age_ms);
    return 0;
}

static int cmd_usage(int argc, char **argv) {
    (void)argc;
    (void)argv;

    wifi_status_snapshot_t wifi;
    app_data_t data;
    wifi_status_read(wifi_status_shared(), &wifi);
    app_data_read(app_data_shared(), &data);

    const usage_record_t *u = &data.usage;
    const bool connected = (wifi.phase == WIFI_PHASE_CONNECTED);
    const int64_t age_ms = age_ms_from_uptime(u->fetched_uptime_ms);

    printf("usage: status=%s has_key=%d has_data=%d age_ms=%" PRId64 "\n",
           diag_usage_status_text(u->status), has_amd_key() ? 1 : 0,
           u->usage.has_data ? 1 : 0, age_ms);

    if (!u->usage.has_data) {
        const usage_view_state_t state =
            usage_view_state(has_amd_key(), connected, app_time_is_valid(), false, u->status,
                             false);
        const char *message = usage_view_message(state);
        printf("  state: %s\n", message != NULL ? message : "no data");
        return 0;
    }

    printf("  raw_1e4: has_limit=%d limit=%ld used=%ld remaining=%ld\n",
           u->usage.has_limit ? 1 : 0, (long)u->usage.limit_1e4,
           (long)u->usage.used_1e4, (long)u->usage.remaining_1e4);

    char buf[96];
    if (usage_view_remaining(USAGE_VIEW_DATA, &u->usage, buf, sizeof buf)) {
        printf("  remaining=%s\n", buf);
    } else {
        printf("  remaining=--\n");
    }
    if (usage_view_used_line(USAGE_VIEW_DATA, &u->usage, buf, sizeof buf)) {
        printf("  used=%s\n", buf);
    }
    const int permille = usage_view_used_permille(USAGE_VIEW_DATA, &u->usage);
    printf("  used_permille=%d\n", permille);
    if (usage_view_reset_line(USAGE_VIEW_DATA, &u->usage, buf, sizeof buf)) {
        printf("  %s\n", buf);
    }
    if (usage_view_today_line(USAGE_VIEW_DATA, &u->usage, buf, sizeof buf)) {
        printf("  %s\n", buf);
    }
    if (usage_view_30d_line(USAGE_VIEW_DATA, &u->usage, buf, sizeof buf)) {
        printf("  %s\n", buf);
    }
    return 0;
}

static int cmd_net(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "refresh") != 0) {
        printf("usage: net refresh [weather|usage|all]\n");
        return 1;
    }
    if (argc > 3) {
        printf("usage: net refresh [weather|usage|all]\n");
        return 1;
    }

    bool weather = false;
    bool usage = false;
    if (!diag_net_arg_parse(argc == 3 ? argv[2] : NULL, &weather, &usage)) {
        printf("unknown source: %s\n", argv[2]);
        return 1;
    }

    if (!net_worker_running()) {
        printf("net_worker=stopped; no request sent. Open page 1 or page 2 first.\n");
        return 1;
    }

    if (weather) {
        net_worker_request_refresh(NET_SOURCE_WEATHER);
    }
    if (usage) {
        net_worker_request_refresh(NET_SOURCE_USAGE);
    }
    printf("refresh requested: weather=%d usage=%d (only visible sources fetch)\n",
           weather ? 1 : 0, usage ? 1 : 0);
    return 0;
}

static int cmd_heap(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("heap: free=%u min_free=%u internal_free=%u largest_internal=%u\n",
           (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return 0;
}

static int cmd_page(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: page <0|1|2|3>  (0=Status, 1=Weather, 2=Usage, 3=Quote)\n");
        return 1;
    }

    page_id_t target;
    if (!diag_page_arg_parse(argv[1], &target)) {
        printf("invalid page: %s\n", argv[1]);
        return 1;
    }

    char err[96] = {0};
    if (!diag_page_switch(target, err, sizeof err)) {
        printf("page switch failed: %s\n", err[0] != '\0' ? err : "unknown");
        return 1;
    }
    printf("page=%s\n", diag_page_name(diag_current_page()));
    return 0;
}

static int cmd_export(int argc, char **argv) {
    bool reveal_secrets = false;
    if (!diag_export_arg_parse(argc, argv, &reveal_secrets)) {
        printf("usage: export [--secrets]\n");
        return 1;
    }

    // 只读：单独开一份 NVS 句柄与 store 快照，不碰联网服务的私有状态。
    esp_err_t err = wifi_profile_nvs_open(&s_export_nvs);
    if (err != ESP_OK) {
        printf("WIFIPROF_ERROR,stage=open,err=%s\n", esp_err_to_name(err));
        return 1;
    }
    const wifi_kv_t kv = wifi_profile_nvs_kv(&s_export_nvs);
    const wifi_profile_result_t result = wifi_profile_store_load(&s_export_store, &kv);
    if (result != WIFI_PROFILE_OK) {
        printf("WIFIPROF_ERROR,stage=load,result=%d\n", (int)result);
        wifi_profile_nvs_close(&s_export_nvs);
        return 1;
    }

    char buf[DIAG_EXPORT_LINE_MAX];
    size_t n;
    if (reveal_secrets) {
        n = diag_export_warning(buf, sizeof buf);
        if (n > 0) {
            printf("%s\n", buf);
        }
    }
    n = diag_export_header(wifi_profile_store_count(&s_export_store), buf, sizeof buf);
    if (n > 0) {
        printf("%s\n", buf);
    }
    const uint8_t count = wifi_profile_store_count(&s_export_store);
    for (uint8_t i = 0; i < count; i++) {
        const wifi_profile_t *p = wifi_profile_store_at(&s_export_store, i);
        if (p != NULL && diag_export_line(p, i, reveal_secrets, buf, sizeof buf) > 0) {
            printf("%s\n", buf);
        }
    }
    n = diag_export_footer(buf, sizeof buf);
    if (n > 0) {
        printf("%s\n", buf);
    }

    wifi_profile_nvs_close(&s_export_nvs);
    return 0;
}

static const esp_console_cmd_t s_commands[] = {
    {
        .command = "status",
        .help = "Print app, Wi-Fi, time, weather and usage snapshots.",
        .func = cmd_status,
    },
    {
        .command = "usage",
        .help = "Print the last AMD usage record and formatted points.",
        .func = cmd_usage,
    },
    {
        .command = "net",
        .help = "Request a network refresh: net refresh [weather|usage|all].",
        .hint = "refresh [weather|usage|all]",
        .func = cmd_net,
    },
    {
        .command = "heap",
        .help = "Print internal heap free, watermark and largest block.",
        .func = cmd_heap,
    },
    {
        .command = "page",
        .help = "Switch product page: page <0|1|2|3> (0=Status, 1=Weather, 2=Usage, 3=Quote).",
        .hint = "<0|1|2|3>",
        .func = cmd_page,
    },
    {
        .command = "export",
        .help = "Export saved Wi-Fi profiles and locations as CSV (--secrets to include passwords).",
        .hint = "[--secrets]",
        .func = cmd_export,
    },
};

void diag_repl_start(void) {
    if (s_started) {
        return;
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "passport>";
    repl_config.task_priority = 2;
    repl_config.task_stack_size = 4096;

    esp_console_dev_usb_serial_jtag_config_t dev_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_err_t err =
        esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "REPL 创建失败: %s", esp_err_to_name(err));
        return;
    }

    for (size_t i = 0; i < sizeof(s_commands) / sizeof(s_commands[0]); i++) {
        err = esp_console_cmd_register(&s_commands[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "命令注册失败 %s: %s", s_commands[i].command,
                     esp_err_to_name(err));
            return;
        }
    }

    err = esp_console_register_help_command();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "help 注册失败: %s", esp_err_to_name(err));
        return;
    }

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "REPL 启动失败: %s", esp_err_to_name(err));
        return;
    }

    s_started = true;
    ESP_LOGI(TAG, "诊断 REPL 已启动: passport> (USB Serial/JTAG)");
}

#endif // CONFIG_FOLOTOY_DIAG_REPL
