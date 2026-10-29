// main/wifi_profile.h —— 多地点 Wi-Fi profile 的记录、键值抽象与增删改查。
//
// 这里刻意不包含任何 ESP-IDF 头文件：记录的编解码和 CRUD 都是纯逻辑，主机测试
// (tests/test_wifi_profile.c) 直接覆盖。持久化通过 wifi_kv_t 注入，NVS 实现在
// wifi_profile_nvs.h；这样逻辑层可以在没有 flash 的机器上跑完整往返测试。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- 容量与字段上限 ----
//
// NVS 分区只有 24 KB（partitions.csv: 0x9000/0x6000）。单条记录最长 128 B，
// 8 条约 1 KB，给未来的离线快照留出余量；同时 8 条也够覆盖"家 / 公司 / 备用"
// 这类真实场景，再多就要靠 UI 滚动，而本机只有三键。
#define WIFI_PROFILE_MAX 8

// 802.11 规定 SSID 最长 32 字节、WPA2 口令最长 63 个可打印字符。这里各多留 1 字节
// 放 NUL；口令放宽到 64 是照顾部分 WPA3 实现允许的长度。
#define WIFI_PROFILE_NAME_MAX 24
#define WIFI_PROFILE_SSID_MAX 33
#define WIFI_PROFILE_PASS_MAX 65

// 序列化格式版本。字段布局一旦变化必须递增；读到未知版本时按"没有可用 profile"
// 处理，不猜测、更不擦除（擦除由用户显式触发）。
// v2：只保留三个字符串字段。曾经有过 last_ok_seq 与 flags，两者都没有任何消费方
// ——记录头因此由 9 字节缩到 4 字节，一条记录的字节数就是它真正承载的信息。
#define WIFI_PROFILE_RECORD_VERSION 2u

// 记录头长度：版本 + name/ssid/pass 三个长度。
#define WIFI_PROFILE_RECORD_HEADER 4u

// 单条记录的容量上界，用来在栈上开临时缓冲。
#define WIFI_PROFILE_RECORD_MAX                                              \
    (WIFI_PROFILE_RECORD_HEADER + (WIFI_PROFILE_NAME_MAX - 1) +              \
     (WIFI_PROFILE_SSID_MAX - 1) + (WIFI_PROFILE_PASS_MAX - 1))

typedef struct {
    char name[WIFI_PROFILE_NAME_MAX];
    char ssid[WIFI_PROFILE_SSID_MAX];
    char password[WIFI_PROFILE_PASS_MAX];
} wifi_profile_t;

// ---- 键值存储抽象 ----
//
// 用函数指针表而不是 extern 回调或弱符号：每个 store 实例自带一份 vtable，
// 主机测试可以在栈上造桩、多次实例化互不干扰，也不需要 #ifdef 区分平台。
// 返回值不使用 esp_err_t，保持逻辑层零依赖。

typedef enum {
    WIFI_KV_OK = 0,
    WIFI_KV_ERR_NOT_FOUND, // key 不存在；对读取而言是正常状态，不是故障
    WIFI_KV_ERR_NO_MEM,    // 调用方缓冲不足，*len 已回填所需长度
    WIFI_KV_ERR_FAIL,      // 后端故障（NVS 损坏、空间耗尽等）
} wifi_kv_result_t;

typedef struct {
    void *ctx;
    wifi_kv_result_t (*get)(void *ctx, const char *key, void *buf, size_t *len);
    wifi_kv_result_t (*set)(void *ctx, const char *key, const void *buf, size_t len);
    wifi_kv_result_t (*erase)(void *ctx, const char *key);
} wifi_kv_t;

// ---- store ----

typedef enum {
    WIFI_PROFILE_OK = 0,
    WIFI_PROFILE_ERR_INVALID,   // 参数非法或字段超长
    WIFI_PROFILE_ERR_FULL,      // 槽位已满
    WIFI_PROFILE_ERR_NOT_FOUND, // 索引越界
    WIFI_PROFILE_ERR_STORAGE,   // 键值后端失败
} wifi_profile_result_t;

typedef struct {
    wifi_kv_t      kv;
    wifi_profile_t slots[WIFI_PROFILE_MAX]; // RAM 缓存，约 1 KB 静态
    uint8_t        count;
    int8_t         active; // -1 = 未选定
} wifi_profile_store_t;

// 从 kv 读出全部记录。坏记录只跳过该槽，不让一条脏数据拖垮整台设备；
// 缺少版本/数量 key 视为空库，返回 WIFI_PROFILE_OK。
wifi_profile_result_t wifi_profile_store_load(wifi_profile_store_t *s, const wifi_kv_t *kv);

// 把 RAM 缓存整体写回。先写槽位再写数量，中途断电最多丢一条，不会出现
// "数量已增大但槽位还是旧值"的组合。
wifi_profile_result_t wifi_profile_store_save(wifi_profile_store_t *s);

// 以下四个只改 RAM，落盘要另外调 save()，避免每次按键都擦写 flash。
// add 在库为空时把新记录设为 active。
wifi_profile_result_t wifi_profile_store_add(wifi_profile_store_t *s, const wifi_profile_t *p);
wifi_profile_result_t wifi_profile_store_update(wifi_profile_store_t *s, uint8_t index,
                                               const wifi_profile_t *p);
// 删除后槽位保持紧凑（后续前移）；active 指向被删项时归 -1，指向其后则减一。
wifi_profile_result_t wifi_profile_store_remove(wifi_profile_store_t *s, uint8_t index);
// index 取 -1 表示清除选定。
wifi_profile_result_t wifi_profile_store_set_active(wifi_profile_store_t *s, int8_t index);

// 清除全部凭据：擦掉所有 key 并把 RAM 清空。返回 STORAGE 表示磁盘上可能仍有残留，
// 调用方应据此提示用户，而不是当作已彻底清除。
wifi_profile_result_t wifi_profile_store_erase_all(wifi_profile_store_t *s);

uint8_t                wifi_profile_store_count(const wifi_profile_store_t *s);
const wifi_profile_t  *wifi_profile_store_at(const wifi_profile_store_t *s, uint8_t index);
int8_t                 wifi_profile_store_active(const wifi_profile_store_t *s);

// ---- 记录的编解码 ----
//
// 用显式字节布局而不是 memcpy 整个结构体：padding、编译器差异和跨固件版本都会让
// 直拷的 blob 静默读错，而错位的密码字段只会表现为"连不上"，极难排查。
//
//   0  u8   version
//   1  u8   name_len
//   2  u8   ssid_len
//   3  u8   pass_len
//   4  ...  name | ssid | password   （无结尾 NUL）
//
// encode 返回写入字节数，0 表示记录非法或缓冲不足。
// decode 校验版本、长度自洽与字段上限，任一不满足都返回 false。
size_t wifi_profile_encode(const wifi_profile_t *p, uint8_t *buf, size_t cap);
bool   wifi_profile_decode(const uint8_t *buf, size_t len, wifi_profile_t *out);

// 一次性填好三个字符串字段并校验长度。
bool wifi_profile_set_fields(wifi_profile_t *p, const char *name, const char *ssid,
                             const char *password);
