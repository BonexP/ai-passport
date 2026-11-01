// main/net_snapshot_store.c —— 快照 NVS 存取的实现。契约见 net_snapshot_store.h。
#include "net_snapshot_store.h"

#include "esp_log.h"
#include "net_snapshot.h"

static const char *TAG = "net_snapshot";

static const char *NAMESPACE = "net";
static const char *KEY_WEATHER = "weather";

esp_err_t net_snapshot_store_open(net_snapshot_store_t *s) {
    if (s == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s->open = false;

    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &s->handle);
    if (err != ESP_OK) {
        // NVS 没初始化或空间不足：天气仍然可用，只是重启后没有旧数据。
        ESP_LOGW(TAG, "打开快照 namespace 失败: %s", esp_err_to_name(err));
        return err;
    }
    s->open = true;
    return ESP_OK;
}

void net_snapshot_store_close(net_snapshot_store_t *s) {
    if (s == NULL || !s->open) {
        return;
    }
    nvs_close(s->handle);
    s->open = false;
}

esp_err_t net_snapshot_store_save(net_snapshot_store_t *s, const weather_state_t *w) {
    if (s == NULL || !s->open || w == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t blob[NET_SNAPSHOT_MAX];
    size_t len = net_snapshot_encode(w, blob, sizeof blob);
    if (len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_set_blob(s->handle, KEY_WEATHER, blob, len);
    if (err != ESP_OK) {
        return err;
    }
    return nvs_commit(s->handle);
}

esp_err_t net_snapshot_store_load(net_snapshot_store_t *s, weather_state_t *out) {
    if (s == NULL || !s->open || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t blob[NET_SNAPSHOT_MAX];
    size_t len = sizeof blob;
    esp_err_t err = nvs_get_blob(s->handle, KEY_WEATHER, blob, &len);
    if (err != ESP_OK) {
        return err;
    }
    if (!net_snapshot_decode(blob, len, out)) {
        // 版本不符或内容损坏：当作没有快照，绝不猜。
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
