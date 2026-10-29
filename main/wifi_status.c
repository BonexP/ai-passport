// main/wifi_status.c —— 状态快照的发布/读取、原因映射与文案。
#include "wifi_status.h"

#include <stdio.h>
#include <string.h>

static void bus_lock(const wifi_status_bus_t *bus) {
    if (bus->lock != NULL && bus->lock->lock != NULL) bus->lock->lock(bus->lock->ctx);
}

static void bus_unlock(const wifi_status_bus_t *bus) {
    if (bus->lock != NULL && bus->lock->unlock != NULL) bus->lock->unlock(bus->lock->ctx);
}

void wifi_status_bus_init(wifi_status_bus_t *bus, const wifi_status_lock_t *lock) {
    if (bus == NULL) return;
    memset(&bus->snapshot, 0, sizeof bus->snapshot);
    bus->snapshot.active = -1;
    bus->snapshot.trying = -1;
    bus->snapshot.rssi = -128;
    bus->lock = lock;
}

void wifi_status_publish(wifi_status_bus_t *bus, const wifi_status_snapshot_t *next) {
    if (bus == NULL || next == NULL) return;

    // 先算好新序号再整体覆盖：让"拷贝"与"计步"处在同一个临界区里，
    // 读侧不可能看到"新内容 + 旧序号"这种半成品。
    bus_lock(bus);
    uint16_t seq = (uint16_t)(bus->snapshot.seq + 1u);
    bus->snapshot = *next;
    bus->snapshot.seq = seq;
    bus_unlock(bus);
}

void wifi_status_read(const wifi_status_bus_t *bus, wifi_status_snapshot_t *out) {
    if (bus == NULL || out == NULL) return;
    bus_lock(bus);
    *out = bus->snapshot;
    bus_unlock(bus);
}

wifi_fail_reason_t wifi_status_reason_from_code(int reason_code) {
    // 数值取自 esp_wifi_types.h 的 wifi_err_reason_t。这里写死数字而不是 include
    // IDF 头文件，是为了让映射留在可主机测试的纯逻辑里；对应关系写在上面的注释里，
    // IDF 大版本升级时需要核对一次。
    switch (reason_code) {
    case 0:   // WIFI_REASON_UNSPECIFIED
        return WIFI_FAIL_NONE;
    case 2:   // WIFI_REASON_AUTH_EXPIRE
    case 3:   // WIFI_REASON_AUTH_LEAVE
    case 15:  // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
    case 204: // WIFI_REASON_HANDSHAKE_TIMEOUT
        return WIFI_FAIL_AUTH;
    case 200: // WIFI_REASON_BEACON_TIMEOUT
    case 201: // WIFI_REASON_NO_AP_FOUND
    case 205: // WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY
        return WIFI_FAIL_NO_AP;
    default:
        // 不认识的码归 OTHER：宁可说"未知"，也不要编一个可能误导用户的解释。
        return WIFI_FAIL_OTHER;
    }
}

bool wifi_status_should_auto_provision(const wifi_status_snapshot_t *s, bool already_tried) {
    if (s == NULL || already_tried) return false;
    return s->phase == WIFI_PHASE_NO_PROFILE;
}

const char *wifi_status_phase_text(wifi_phase_t phase) {
    switch (phase) {
    case WIFI_PHASE_IDLE:         return "Idle";
    case WIFI_PHASE_CONNECTING:   return "Connecting";
    case WIFI_PHASE_CONNECTED:    return "Connected";
    case WIFI_PHASE_NO_PROFILE:   return "No networks";
    case WIFI_PHASE_ALL_FAILED:   return "No connection";
    case WIFI_PHASE_PROVISIONING: return "Setup mode";
    case WIFI_PHASE_ERROR:        return "Wi-Fi error";
    }
    return "Unknown";
}

const char *wifi_status_fail_text(wifi_fail_reason_t reason) {
    switch (reason) {
    case WIFI_FAIL_NONE:    return "";
    case WIFI_FAIL_AUTH:    return "Wrong password";
    case WIFI_FAIL_NO_AP:   return "Network not found";
    case WIFI_FAIL_TIMEOUT: return "Connection timed out";
    case WIFI_FAIL_DHCP:    return "No address from router";
    case WIFI_FAIL_DRIVER:  return "Radio error";
    case WIFI_FAIL_SAVE:    return "Could not save";
    case WIFI_FAIL_OTHER:   return "Connection failed";
    }
    return "Connection failed";
}

// 把 tmp 里的内容整体搬到调用方缓冲。放不下就一个字节都不写：
// 半截的 SSID 比空白更让人困惑。
static size_t copy_out(const char *tmp, char *buf, size_t cap) {
    size_t len = strlen(tmp);
    if (len + 1 > cap) return 0;
    memcpy(buf, tmp, len + 1);
    return len;
}

size_t wifi_status_format_headline(const wifi_status_snapshot_t *s, char *buf, size_t cap) {
    if (s == NULL || buf == NULL || cap == 0) return 0;

    // 96 字节足够放下最长的一条（"Connecting: " + 32 字符 SSID + NUL = 45），
    // 因此 snprintf 不会截断。
    char tmp[96];
    int n = 0;
    switch ((wifi_phase_t)s->phase) {
    case WIFI_PHASE_CONNECTING:
        n = snprintf(tmp, sizeof tmp, "Connecting: %.*s", WIFI_PROFILE_SSID_MAX - 1, s->ssid);
        break;
    case WIFI_PHASE_CONNECTED:
        n = snprintf(tmp, sizeof tmp, "Connected: %.*s", WIFI_PROFILE_SSID_MAX - 1, s->ssid);
        break;
    case WIFI_PHASE_IDLE:
    case WIFI_PHASE_NO_PROFILE:
    case WIFI_PHASE_ALL_FAILED:
    case WIFI_PHASE_PROVISIONING:
    case WIFI_PHASE_ERROR:
        n = snprintf(tmp, sizeof tmp, "%s", wifi_status_phase_text((wifi_phase_t)s->phase));
        break;
    }
    if (n <= 0) return 0;
    return copy_out(tmp, buf, cap);
}

size_t wifi_status_format_detail(const wifi_status_snapshot_t *s, char *buf, size_t cap) {
    if (s == NULL || buf == NULL || cap == 0) return 0;

    char tmp[96];
    int n = 0;
    switch ((wifi_phase_t)s->phase) {
    case WIFI_PHASE_CONNECTING:
        if (s->trying >= 0 && s->profile_count > 0) {
            n = snprintf(tmp, sizeof tmp, "Profile %d of %u", (int)s->trying + 1,
                         (unsigned)s->profile_count);
        } else {
            n = snprintf(tmp, sizeof tmp, "Starting");
        }
        break;
    case WIFI_PHASE_CONNECTED:
        if (s->ip[0] != '\0') {
            n = snprintf(tmp, sizeof tmp, "IP %.*s", WIFI_STATUS_IP_MAX - 1, s->ip);
        } else if (s->rssi > -128) {
            n = snprintf(tmp, sizeof tmp, "Signal %d dBm", (int)s->rssi);
        } else {
            n = snprintf(tmp, sizeof tmp, "%s", "Online");
        }
        break;
    case WIFI_PHASE_ALL_FAILED:
        n = snprintf(tmp, sizeof tmp, "%s", wifi_status_fail_text((wifi_fail_reason_t)s->fail));
        break;
    case WIFI_PHASE_PROVISIONING:
        n = snprintf(tmp, sizeof tmp, "Open %.*s", WIFI_STATUS_IP_MAX - 1, s->ip);
        break;
    case WIFI_PHASE_NO_PROFILE:
        n = snprintf(tmp, sizeof tmp, "%s", "Hold UP to set up");
        break;
    case WIFI_PHASE_ERROR:
        n = snprintf(tmp, sizeof tmp, "%s", "Restart the device");
        break;
    case WIFI_PHASE_IDLE:
        n = snprintf(tmp, sizeof tmp, "%s", "");
        break;
    }
    // n == 0 是合法的空串（IDLE 行），仍然要把一个 NUL 写进缓冲；只有负数才是错误。
    // 因此"返回 0"同时代表"没有内容"和"放不下"，两种情况下调用方都是不显示。
    if (n < 0) return 0;
    return copy_out(tmp, buf, cap);
}
