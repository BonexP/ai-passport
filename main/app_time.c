// main/app_time.c —— SNTP 校时的设备侧实现。契约见 app_time.h。
#include "app_time.h"

#include <time.h>

#include "app_clock.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "app_time";

// 是否已经初始化过 SNTP。esp_netif_sntp_init/deinit 必须配对，
// 重复 start 会返回 INVALID_STATE，所以用这个标志做幂等。
static bool s_started;

// 取当前 epoch 秒。time() 在未同步时返回 1970 附近的秒数，由 app_clock 判定。
static int64_t current_epoch(void) {
    time_t now = 0;
    (void)time(&now);
    return (int64_t)now;
}

esp_err_t app_time_start(esp_netif_t *netif) {
    if (s_started) {
        return ESP_OK;
    }
    if (netif == NULL) {
        // Wi-Fi 服务没在跑：没有 netif 可绑定。调用方应先启 Wi-Fi。
        return ESP_ERR_INVALID_STATE;
    }

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.netif = netif;
    // 默认配置会阻塞到首次同步完成。这里必须关掉：调用方是输入/启动任务，
    // 卡住十几秒会让首屏和按键全部失去响应。worker 自己轮询 app_time_is_valid()。
    cfg.wait_for_sync = false;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP 启动失败: %s", esp_err_to_name(err));
        return err;
    }

    s_started = true;
    ESP_LOGI(TAG, "SNTP 已启动: pool.ntp.org");
    return ESP_OK;
}

void app_time_stop(void) {
    if (!s_started) {
        return;
    }
    esp_netif_sntp_deinit();
    s_started = false;
    ESP_LOGI(TAG, "SNTP 已停止");
}

bool app_time_is_valid(void) {
    return app_clock_epoch_is_plausible(current_epoch());
}

int64_t app_time_now(void) {
    int64_t now = current_epoch();
    return app_clock_epoch_is_plausible(now) ? now : 0;
}
