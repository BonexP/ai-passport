// main/wifi_profile.c —— profile 记录的编解码与 store 的增删改查实现。
//
// 全部是纯逻辑，不调用任何 ESP-IDF API；持久化只通过 wifi_kv_t 走。
#include "wifi_profile.h"

#include <string.h>

// NVS key 名。namespace 由 wifi_profile_nvs.c 负责，这里只管 key。
// 全部压到 3 字节以内，NVS key 上限是 15 字节，给未来留空间。
static const char KEY_VERSION[] = "ver";
static const char KEY_COUNT[] = "n";
static const char KEY_ACTIVE[] = "active";

#define SLOT_KEY_LEN 3

static void slot_key(uint8_t index, char out[SLOT_KEY_LEN]) {
    out[0] = 'p';
    out[1] = (char)('0' + (char)index);
    out[2] = '\0';
}

// strnlen 是 POSIX 而非 ISO C11，主机测试要能在最小的编译器上跑，所以自己实现。
static size_t bounded_strlen(const char *s, size_t cap) {
    size_t n = 0;
    if (s == NULL) return 0;
    while (n < cap && s[n] != '\0') n++;
    return n;
}

// 记录里的经纬度是小端 int32。C 的负数→无符号是模运算（有定义），
// ESP32 与主机都是补码，反方向按位还原即可；不依赖 float 或端序宏。
static void put_i32le(uint8_t *p, int32_t value) {
    uint32_t raw = (uint32_t)value;
    p[0] = (uint8_t)(raw & 0xFFu);
    p[1] = (uint8_t)((raw >> 8) & 0xFFu);
    p[2] = (uint8_t)((raw >> 16) & 0xFFu);
    p[3] = (uint8_t)((raw >> 24) & 0xFFu);
}

static int32_t get_i32le(const uint8_t *p) {
    uint32_t raw = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[3] << 24);
    return (int32_t)raw;
}

// 记录本身是否可编码：每个字段都在缓冲区内以 NUL 结尾，长度符合 802.11 上限，
// 地点结构自洽。地点的规则只维护在 app_location.c 一处。
static bool fields_valid(const wifi_profile_t *p) {
    if (p == NULL) return false;
    size_t name_len = bounded_strlen(p->name, sizeof p->name);
    size_t ssid_len = bounded_strlen(p->ssid, sizeof p->ssid);
    size_t pass_len = bounded_strlen(p->password, sizeof p->password);
    if (name_len >= sizeof p->name) return false; // 没在缓冲区内结尾
    if (ssid_len >= sizeof p->ssid) return false;
    if (pass_len >= sizeof p->password) return false;
    if (ssid_len == 0 || ssid_len > 32) return false;
    if (pass_len > 64) return false;
    if (!app_location_is_valid(&p->location)) return false;
    return true;
}

size_t wifi_profile_encode(const wifi_profile_t *p, uint8_t *buf, size_t cap) {
    if (p == NULL || buf == NULL || !fields_valid(p)) return 0;

    size_t name_len = bounded_strlen(p->name, sizeof p->name);
    size_t ssid_len = bounded_strlen(p->ssid, sizeof p->ssid);
    size_t pass_len = bounded_strlen(p->password, sizeof p->password);
    // 未配置地点时不写 place，保证"没有 place_len 却挂着地点名"的记录不会出现。
    size_t place_len = p->location.has_location
                         ? bounded_strlen(p->location.place, sizeof p->location.place)
                         : 0;
    size_t total = WIFI_PROFILE_RECORD_HEADER + name_len + ssid_len + pass_len + place_len;
    if (total > cap || total > WIFI_PROFILE_RECORD_MAX) return 0;

    buf[0] = (uint8_t)WIFI_PROFILE_RECORD_VERSION;
    buf[1] = p->location.has_location ? 0x01u : 0x00u;
    // 没有地点时坐标写 0：结构体里的残留值不该进入 blob。
    put_i32le(buf + 2, p->location.has_location ? p->location.lat_udeg : 0);
    put_i32le(buf + 6, p->location.has_location ? p->location.lon_udeg : 0);
    buf[10] = (uint8_t)name_len;
    buf[11] = (uint8_t)ssid_len;
    buf[12] = (uint8_t)pass_len;
    buf[13] = (uint8_t)place_len;

    uint8_t *cursor = buf + WIFI_PROFILE_RECORD_HEADER;
    memcpy(cursor, p->name, name_len);
    cursor += name_len;
    memcpy(cursor, p->ssid, ssid_len);
    cursor += ssid_len;
    memcpy(cursor, p->password, pass_len);
    cursor += pass_len;
    if (place_len > 0) {
        memcpy(cursor, p->location.place, place_len);
    }
    return total;
}

// v2 记录：只有三段字符串。老固件写下的记录必须继续可读，否则升级即丢网络。
static bool decode_v2(const uint8_t *buf, size_t len, wifi_profile_t *out) {
    if (len < WIFI_PROFILE_RECORD_HEADER_V2) return false;

    size_t name_len = buf[1];
    size_t ssid_len = buf[2];
    size_t pass_len = buf[3];
    if (name_len >= WIFI_PROFILE_NAME_MAX) return false;
    if (ssid_len == 0 || ssid_len >= WIFI_PROFILE_SSID_MAX) return false;
    if (pass_len >= WIFI_PROFILE_PASS_MAX) return false;
    if (len != WIFI_PROFILE_RECORD_HEADER_V2 + name_len + ssid_len + pass_len) return false;

    memset(out, 0, sizeof *out);
    const uint8_t *cursor = buf + WIFI_PROFILE_RECORD_HEADER_V2;
    memcpy(out->name, cursor, name_len);
    cursor += name_len;
    memcpy(out->ssid, cursor, ssid_len);
    cursor += ssid_len;
    memcpy(out->password, cursor, pass_len);
    // location 已在 memset 里清零：老记录没有地点，天气页据此显示未配置。
    return true;
}

bool wifi_profile_decode(const uint8_t *buf, size_t len, wifi_profile_t *out) {
    if (buf == NULL || out == NULL || len < 1) return false;
    if (buf[0] == WIFI_PROFILE_RECORD_VERSION_V2) return decode_v2(buf, len, out);
    if (buf[0] != WIFI_PROFILE_RECORD_VERSION) return false;
    if (len < WIFI_PROFILE_RECORD_HEADER) return false;

    // 只认识 bit0。将来加别的 flag 时必须同时升级版本号，否则这里会把
    // "新固件写的记录"当成损坏——那正是版本号存在的意义。
    uint8_t flags = buf[1];
    if ((flags & 0xFEu) != 0) return false;
    bool has_location = (flags & 0x01u) != 0;

    size_t name_len = buf[10];
    size_t ssid_len = buf[11];
    size_t pass_len = buf[12];
    size_t place_len = buf[13];
    if (name_len >= WIFI_PROFILE_NAME_MAX) return false;
    if (ssid_len == 0 || ssid_len >= WIFI_PROFILE_SSID_MAX) return false;
    if (pass_len >= WIFI_PROFILE_PASS_MAX) return false;
    if (place_len >= APP_LOCATION_PLACE_MAX) return false;
    // 没有坐标却带地点名，说明写侧逻辑坏了，不猜意图。
    if (!has_location && place_len != 0) return false;
    // 长度自洽：记录必须正好由头部加四段字符串构成，多一个字节都当作损坏。
    if (len != WIFI_PROFILE_RECORD_HEADER + name_len + ssid_len + pass_len + place_len) {
        return false;
    }

    memset(out, 0, sizeof *out);
    const uint8_t *cursor = buf + WIFI_PROFILE_RECORD_HEADER;
    memcpy(out->name, cursor, name_len);
    cursor += name_len;
    memcpy(out->ssid, cursor, ssid_len);
    cursor += ssid_len;
    memcpy(out->password, cursor, pass_len);
    cursor += pass_len;

    if (has_location) {
        // 记录里的 place 没有结尾 NUL，必须搬进本地缓冲再交给字符串接口，
        // 否则 strlen 会读出记录边界。
        char place[APP_LOCATION_PLACE_MAX];
        memset(place, 0, sizeof place);
        if (place_len > 0) {
            memcpy(place, cursor, place_len);
        }
        int32_t lat = get_i32le(buf + 2);
        int32_t lon = get_i32le(buf + 6);
        // 用 app_location_set 统一校验范围与控制字符，规则只维护一处。
        if (!app_location_set(&out->location, lat, lon, place_len > 0 ? place : NULL)) {
            return false;
        }
    }
    return true;
}

bool wifi_profile_set_fields(wifi_profile_t *p, const char *name, const char *ssid,
                             const char *password) {
    if (p == NULL || ssid == NULL) return false;

    size_t name_len = name != NULL ? bounded_strlen(name, sizeof p->name) : 0;
    size_t ssid_len = bounded_strlen(ssid, sizeof p->ssid);
    size_t pass_len = password != NULL ? bounded_strlen(password, sizeof p->password) : 0;

    if (name != NULL && name_len >= sizeof p->name) return false;
    if (ssid_len == 0 || ssid_len >= sizeof p->ssid || ssid_len > 32) return false;
    if (password != NULL && pass_len >= sizeof p->password) return false;
    if (pass_len > 64) return false;

    memset(p, 0, sizeof *p);
    if (name_len > 0) memcpy(p->name, name, name_len);
    memcpy(p->ssid, ssid, ssid_len);
    if (pass_len > 0) memcpy(p->password, password, pass_len);
    return true;
}

bool wifi_profile_set_location(wifi_profile_t *p, const app_location_t *loc) {
    if (p == NULL) return false;

    app_location_t next;
    app_location_clear(&next);

    if (loc != NULL && loc->has_location) {
        // 先校验原结构（含 place 的 NUL 结尾），再规范化拷贝；直接传 loc->place
        // 给 app_location_set 会对其 strlen，未终结的缓冲会被读出边界。
        if (!app_location_is_valid(loc)) return false;
        if (!app_location_set(&next, loc->lat_udeg, loc->lon_udeg, loc->place)) return false;
    }

    p->location = next;
    return true;
}

wifi_profile_result_t wifi_profile_store_load(wifi_profile_store_t *s, const wifi_kv_t *kv) {
    if (s == NULL || kv == NULL || kv->get == NULL) return WIFI_PROFILE_ERR_INVALID;

    memset(s, 0, sizeof *s);
    s->kv = *kv;
    s->active = -1;

    uint8_t version = 0;
    size_t len = sizeof version;
    wifi_kv_result_t r = s->kv.get(s->kv.ctx, KEY_VERSION, &version, &len);
    if (r == WIFI_KV_ERR_FAIL) return WIFI_PROFILE_ERR_STORAGE;
    if (r == WIFI_KV_OK && version != WIFI_PROFILE_RECORD_VERSION &&
        version != WIFI_PROFILE_RECORD_VERSION_V2) {
        // 未知版本：不猜字段含义，也不擦除。空库比误读的库安全。
        // v2 必须继续接受：升级固件不能让用户已经配好的网络消失。
        return WIFI_PROFILE_OK;
    }

    uint8_t stored_count = 0;
    len = sizeof stored_count;
    r = s->kv.get(s->kv.ctx, KEY_COUNT, &stored_count, &len);
    if (r == WIFI_KV_ERR_FAIL) return WIFI_PROFILE_ERR_STORAGE;
    if (r != WIFI_KV_OK || stored_count > WIFI_PROFILE_MAX) stored_count = 0;

    for (uint8_t i = 0; i < stored_count; i++) {
        uint8_t buf[WIFI_PROFILE_RECORD_MAX];
        size_t blob_len = sizeof buf;
        char key[SLOT_KEY_LEN];
        slot_key(i, key);

        r = s->kv.get(s->kv.ctx, key, buf, &blob_len);
        if (r != WIFI_KV_OK) continue; // 缺槽、缓冲异常都只跳过这一格

        wifi_profile_t decoded;
        if (!wifi_profile_decode(buf, blob_len, &decoded)) continue; // 坏记录不能拖垮设备
        s->slots[s->count++] = decoded;
    }

    int8_t active = -1;
    len = sizeof active;
    r = s->kv.get(s->kv.ctx, KEY_ACTIVE, &active, &len);
    if (r == WIFI_KV_OK && active >= 0 && (uint8_t)active < s->count) {
        s->active = active;
    } else {
        s->active = -1;
    }
    return WIFI_PROFILE_OK;
}

wifi_profile_result_t wifi_profile_store_save(wifi_profile_store_t *s) {
    if (s == NULL || s->kv.set == NULL || s->kv.erase == NULL) {
        return WIFI_PROFILE_ERR_INVALID;
    }

    // 先写槽位：数量 key 一旦增大就代表这些槽位已经有效，反过来则会出现
    // "读得到数量、读不到内容"的组合。
    for (uint8_t i = 0; i < s->count; i++) {
        uint8_t buf[WIFI_PROFILE_RECORD_MAX];
        size_t blob_len = wifi_profile_encode(&s->slots[i], buf, sizeof buf);
        if (blob_len == 0) return WIFI_PROFILE_ERR_INVALID; // RAM 里的记录本身非法
        char key[SLOT_KEY_LEN];
        slot_key(i, key);
        if (s->kv.set(s->kv.ctx, key, buf, blob_len) != WIFI_KV_OK) {
            return WIFI_PROFILE_ERR_STORAGE;
        }
    }

    uint8_t version = (uint8_t)WIFI_PROFILE_RECORD_VERSION;
    if (s->kv.set(s->kv.ctx, KEY_VERSION, &version, sizeof version) != WIFI_KV_OK) {
        return WIFI_PROFILE_ERR_STORAGE;
    }
    if (s->kv.set(s->kv.ctx, KEY_COUNT, &s->count, sizeof s->count) != WIFI_KV_OK) {
        return WIFI_PROFILE_ERR_STORAGE;
    }
    int8_t active = s->active;
    if (s->kv.set(s->kv.ctx, KEY_ACTIVE, &active, sizeof active) != WIFI_KV_OK) {
        return WIFI_PROFILE_ERR_STORAGE;
    }

    // 清掉多余的旧槽位。erase 不存在的 key 在 NVS 与桩里都算成功，忽略返回。
    for (uint8_t i = s->count; i < WIFI_PROFILE_MAX; i++) {
        char key[SLOT_KEY_LEN];
        slot_key(i, key);
        (void)s->kv.erase(s->kv.ctx, key);
    }
    return WIFI_PROFILE_OK;
}

wifi_profile_result_t wifi_profile_store_add(wifi_profile_store_t *s, const wifi_profile_t *p) {
    if (s == NULL || p == NULL) return WIFI_PROFILE_ERR_INVALID;
    if (s->count >= WIFI_PROFILE_MAX) return WIFI_PROFILE_ERR_FULL;
    if (!fields_valid(p)) return WIFI_PROFILE_ERR_INVALID;

    s->slots[s->count] = *p;
    s->count++;
    // 空库的第一条直接成为 active：配网后首次开机就能连上，不需要额外的选择动作。
    if (s->active < 0) s->active = (int8_t)(s->count - 1);
    return WIFI_PROFILE_OK;
}

wifi_profile_result_t wifi_profile_store_update(wifi_profile_store_t *s, uint8_t index,
                                               const wifi_profile_t *p) {
    if (s == NULL || p == NULL) return WIFI_PROFILE_ERR_INVALID;
    if (index >= s->count) return WIFI_PROFILE_ERR_NOT_FOUND;
    if (!fields_valid(p)) return WIFI_PROFILE_ERR_INVALID;

    s->slots[index] = *p;
    return WIFI_PROFILE_OK;
}

wifi_profile_result_t wifi_profile_store_remove(wifi_profile_store_t *s, uint8_t index) {
    if (s == NULL) return WIFI_PROFILE_ERR_INVALID;
    if (index >= s->count) return WIFI_PROFILE_ERR_NOT_FOUND;

    for (uint8_t i = index; (uint8_t)(i + 1) < s->count; i++) {
        s->slots[i] = s->slots[i + 1];
    }
    s->count--;
    memset(&s->slots[s->count], 0, sizeof s->slots[0]);

    if (s->active == (int8_t)index) {
        s->active = -1;
    } else if (s->active > (int8_t)index) {
        s->active = (int8_t)(s->active - 1);
    }
    return WIFI_PROFILE_OK;
}

wifi_profile_result_t wifi_profile_store_set_active(wifi_profile_store_t *s, int8_t index) {
    if (s == NULL) return WIFI_PROFILE_ERR_INVALID;
    if (index < -1 || index >= (int8_t)s->count) return WIFI_PROFILE_ERR_INVALID;
    s->active = index;
    return WIFI_PROFILE_OK;
}

wifi_profile_result_t wifi_profile_store_erase_all(wifi_profile_store_t *s) {
    if (s == NULL || s->kv.erase == NULL) return WIFI_PROFILE_ERR_INVALID;

    bool clean = true;
    for (uint8_t i = 0; i < WIFI_PROFILE_MAX; i++) {
        char key[SLOT_KEY_LEN];
        slot_key(i, key);
        if (s->kv.erase(s->kv.ctx, key) != WIFI_KV_OK) clean = false;
    }
    if (s->kv.erase(s->kv.ctx, KEY_ACTIVE) != WIFI_KV_OK) clean = false;
    if (s->kv.erase(s->kv.ctx, KEY_COUNT) != WIFI_KV_OK) clean = false;
    if (s->kv.erase(s->kv.ctx, KEY_VERSION) != WIFI_KV_OK) clean = false;

    // 用户要的是"忘记这些网络"，RAM 必须先反映这个意图；磁盘是否真的干净由返回值说明。
    memset(s->slots, 0, sizeof s->slots);
    s->count = 0;
    s->active = -1;
    return clean ? WIFI_PROFILE_OK : WIFI_PROFILE_ERR_STORAGE;
}

uint8_t wifi_profile_store_count(const wifi_profile_store_t *s) {
    return s != NULL ? s->count : 0;
}

const wifi_profile_t *wifi_profile_store_at(const wifi_profile_store_t *s, uint8_t index) {
    if (s == NULL || index >= s->count) return NULL;
    return &s->slots[index];
}

int8_t wifi_profile_store_active(const wifi_profile_store_t *s) {
    return s != NULL ? s->active : -1;
}
