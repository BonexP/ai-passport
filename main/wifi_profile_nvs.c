// main/wifi_profile_nvs.c —— 把 wifi_kv_t 接到真实 NVS。
#include "wifi_profile_nvs.h"

#include <string.h>

#include "nvs_flash.h"

static wifi_kv_result_t kv_get(void *ctx, const char *key, void *buf, size_t *len) {
    wifi_profile_nvs_t *nv = ctx;
    if (nv == NULL || !nv->opened || key == NULL || buf == NULL || len == NULL) {
        return WIFI_KV_ERR_FAIL;
    }

    size_t need = *len;
    esp_err_t err = nvs_get_blob(nv->handle, key, buf, &need);
    if (err == ESP_ERR_NVS_NOT_FOUND) return WIFI_KV_ERR_NOT_FOUND;
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        // nvs_get_blob 会把所需长度写回 need，交给调用方决定换多大的缓冲。
        *len = need;
        return WIFI_KV_ERR_NO_MEM;
    }
    if (err != ESP_OK) return WIFI_KV_ERR_FAIL;

    *len = need;
    return WIFI_KV_OK;
}

static wifi_kv_result_t kv_set(void *ctx, const char *key, const void *buf, size_t len) {
    wifi_profile_nvs_t *nv = ctx;
    if (nv == NULL || !nv->opened || key == NULL || buf == NULL) return WIFI_KV_ERR_FAIL;

    esp_err_t err = nvs_set_blob(nv->handle, key, buf, len);
    if (err != ESP_OK) return WIFI_KV_ERR_FAIL;

    // 每次写都提交。一次 save 最多写十几个 key，代价可以接受；不提交的话掉电后
    // 内存里的改动会静默丢失，而"配好的网络重启就没了"极难在真机上定位。
    if (nvs_commit(nv->handle) != ESP_OK) return WIFI_KV_ERR_FAIL;
    return WIFI_KV_OK;
}

static wifi_kv_result_t kv_erase(void *ctx, const char *key) {
    wifi_profile_nvs_t *nv = ctx;
    if (nv == NULL || !nv->opened || key == NULL) return WIFI_KV_ERR_FAIL;

    esp_err_t err = nvs_erase_key(nv->handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // 清一个本就不存在的 key 属于成功：调用方只想确保它不在了。
        return WIFI_KV_OK;
    }
    if (err != ESP_OK) return WIFI_KV_ERR_FAIL;
    if (nvs_commit(nv->handle) != ESP_OK) return WIFI_KV_ERR_FAIL;
    return WIFI_KV_OK;
}

esp_err_t wifi_profile_nvs_open(wifi_profile_nvs_t *nv) {
    if (nv == NULL) return ESP_ERR_INVALID_ARG;
    memset(nv, 0, sizeof *nv);

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 故意不擦除，理由见头文件。
        return err;
    }

    err = nvs_open(WIFI_PROFILE_NVS_NAMESPACE, NVS_READWRITE, &nv->handle);
    if (err != ESP_OK) return err;

    nv->opened = true;
    return ESP_OK;
}

wifi_kv_t wifi_profile_nvs_kv(wifi_profile_nvs_t *nv) {
    wifi_kv_t kv = {
        .ctx = nv,
        .get = kv_get,
        .set = kv_set,
        .erase = kv_erase,
    };
    return kv;
}

void wifi_profile_nvs_close(wifi_profile_nvs_t *nv) {
    if (nv == NULL || !nv->opened) return;
    nvs_close(nv->handle);
    nv->opened = false;
}
