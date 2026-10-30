// tests/test_wifi_provision_service.c —— SoftAP 配网服务的生命周期与 HTTP 行为。
//
// 测试不 #include 服务的 .c（它和 profile 服务有同名 static），而是通过真实注册进
// httpd 假件的 uri 表拿到 handler 直接调用。因此"注册了什么路由"和"handler 怎么应答"
// 都被覆盖，同时保留了两个服务各自的内部状态。
#include "demo_stubs/demo_runtime.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wifi_profile_service.h"
#include "wifi_provision_form.h"
#include "wifi_provision_html.h"
#include "wifi_provision_service.h"
#include "wifi_radio_owner.h"
#include "wifi_status.h"
#include "wifi_status_shared.h"

#define AP_SSID "Passport-B2C3" /* 桩里 esp_read_mac 的固定 MAC 后两字节 */

static wifi_status_snapshot_t snapshot(void) {
    wifi_status_snapshot_t s;
    wifi_status_read(wifi_status_shared(), &s);
    return s;
}

static const httpd_uri_t *find_uri(const char *path) {
    for (unsigned i = 0; i < test_httpd_uri_count; i++) {
        if (strcmp(test_httpd_uris[i].uri, path) == 0) return &test_httpd_uris[i];
    }
    return NULL;
}

static void reset_world(void) {
    wifi_provision_service_stop();
    wifi_profile_service_stop();
    wifi_radio_owner_init(wifi_radio_shared());
    test_nvs_clear();
    test_queue_reset();
    test_task_reset();
    test_sem_reset();
    test_wifi_stack_reset();
    test_httpd_reset();
    test_wifi_session(TEST_WIFI_SESSION_AP);
    test_time_us = 0;
    test_auto_ack = false;
}

static void start_ok(void) {
    assert(wifi_provision_service_start() == ESP_OK);
    assert(wifi_provision_service_active());
}

static void test_every_start_step_rolls_back(void) {
    for (unsigned failure = 1; failure <= 13; failure++) {
        reset_world();
        test_ap_fail_at = failure;

        assert(wifi_provision_service_start() != ESP_OK);
        assert(test_ap_step == failure);
        assert(!wifi_provision_service_active());
        assert(wifi_radio_holder(wifi_radio_shared()) == WIFI_RADIO_FREE);
        assert(test_wifi_stack_clean());
        assert(test_httpd_uri_count == 0);
        // httpd 只有在真正起来之后才谈得上关：第 11 步是 start 自己失败。
        assert(test_httpd_stops == (failure >= 12 ? 1u : 0u));
    }
}

static void test_ap_is_open_single_client_on_channel_one(void) {
    reset_world();
    start_ok();

    assert(test_wifi_mode() == WIFI_MODE_AP);
    assert(test_wifi_cfg_ap.ap.authmode == WIFI_AUTH_OPEN);
    assert(test_wifi_cfg_ap.ap.max_connection == 1);
    assert(test_wifi_cfg_ap.ap.channel == 1);
    assert(strcmp((const char *)test_wifi_cfg_ap.ap.ssid, AP_SSID) == 0);
    assert(test_wifi_cfg_ap.ap.ssid_len == strlen(AP_SSID));
}

static void test_start_announces_setup_on_the_status_page(void) {
    reset_world();
    start_ok();

    wifi_status_snapshot_t s = snapshot();
    assert(s.phase == WIFI_PHASE_PROVISIONING);
    assert(strcmp(s.ssid, AP_SSID) == 0);
    assert(strcmp(s.ip, "192.168.4.1") == 0);
    assert(s.ap_clients == 0);
}

static void test_get_serves_the_form(void) {
    reset_world();
    start_ok();

    const httpd_uri_t *uri = find_uri("/");
    assert(uri != NULL && uri->method == HTTP_GET);
    assert(uri->handler(test_httpd_req()) == ESP_OK);
    assert(strstr(test_httpd_last_type, "text/html") != NULL);

    size_t form_len = 0;
    const char *form = wifi_provision_form_page(&form_len);
    assert(test_httpd_last_len == form_len);
    assert(memcmp(test_httpd_last_body, form, form_len) == 0);
}

static void test_content_length_missing_is_411(void) {
    reset_world();
    start_ok();
    test_httpd_set_request("", 0, NULL);

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "411 Length Required") == 0);
    assert(test_httpd_recv_calls == 0);
    assert(test_restarts == 0);
}

static void test_content_length_not_a_number_is_400(void) {
    reset_world();
    start_ok();
    test_httpd_set_request("", 0, "abc");

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "400 Bad Request") == 0);
    assert(test_httpd_recv_calls == 0);
}

static void test_content_length_too_large_is_413_without_reading(void) {
    reset_world();
    start_ok();
    test_httpd_set_request("", 0, "9999");

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "413 Payload Too Large") == 0);
    // 声明就超限：一个字节都不该读。
    assert(test_httpd_recv_calls == 0);
}

static void test_content_length_with_twenty_digits_is_413(void) {
    reset_world();
    start_ok();
    test_httpd_set_request("", 0, "99999999999999999999"); // 20 位

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "413 Payload Too Large") == 0);
    assert(test_httpd_recv_calls == 0);
}

static void test_malformed_form_is_rejected_without_writing(void) {
    const char *body = "ssid=%ZZ&pass=x";
    reset_world();
    start_ok();
    test_httpd_set_request(body, strlen(body), "14");
    test_auto_ack = true; // 让 handler 的 xSemaphoreTake 顺手把 worker 推起来

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "400 Bad Request") == 0);
    assert(strstr(test_httpd_last_body, wifi_provision_form_error_text(WIFI_FORM_ERR_BAD_ESCAPE)) !=
           NULL);
    assert(test_nvs_commits == 0);
    assert(test_restarts == 0);
}

static void test_short_body_is_400(void) {
    reset_world();
    start_ok();
    // 声明 20 字节，只给 5 字节：对端提前关连接。
    test_httpd_set_request("ssid=", 5, "20");

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "400 Bad Request") == 0);
    assert(test_nvs_commits == 0);
}

static void test_save_success_answers_then_restarts(void) {
    const char *body = "ssid=HomeNet&pass=secret123";
    reset_world();
    start_ok();
    test_httpd_set_request(body, strlen(body), "27");
    test_auto_ack = true;

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    // 成功页先发出去。
    assert(strstr(test_httpd_last_body, "Saved") != NULL);
    assert(test_nvs_commits >= 1);
    assert(test_restarts == 0); // 还没到重启那一步

    // worker 收到 RESP_SENT 才重启。
    test_run_worker(test_latest_task);
    assert(test_restarts == 1);
}

static void test_save_failure_is_500_and_does_not_restart(void) {
    const char *body = "ssid=HomeNet&pass=secret123";
    reset_world();
    start_ok();
    test_nvs_write_fails = true;
    test_httpd_set_request(body, strlen(body), "27");
    test_auto_ack = true;

    assert(find_uri(WIFI_PROV_SAVE_PATH)->handler(test_httpd_req()) == ESP_OK);
    assert(strcmp(test_httpd_last_status, "500 Internal Server Error") == 0);

    test_run_worker(test_latest_task);
    assert(test_restarts == 0);
}

static void test_client_count_follows_dhcp(void) {
    reset_world();
    start_ok();

    test_wifi_fire_ap_ipassigned();
    test_run_worker(test_latest_task);
    assert(snapshot().ap_clients == 1);

    test_wifi_fire_ap_stadisconnected();
    test_run_worker(test_latest_task);
    assert(snapshot().ap_clients == 0);

    // 减到 0 之后继续减不能回绕成 255。
    test_wifi_fire_ap_stadisconnected();
    test_run_worker(test_latest_task);
    assert(snapshot().ap_clients == 0);
}

static void test_timeout_closes_the_ap(void) {
    reset_world();
    start_ok();

    // 推过 5 分钟；worker 每次 tick 都会重算剩余时间。
    test_time_us = ((int64_t)WIFI_PROV_TIMEOUT_MS + 2000) * 1000;
    test_queue_expire = true;
    test_run_worker(test_latest_task);

    assert(!wifi_provision_service_active()); // 热点已关
    assert(test_httpd_stops >= 1);
    assert(test_restarts == 0);
}

static void test_stop_is_idempotent(void) {
    reset_world();
    start_ok();
    const unsigned starts = test_httpd_starts;

    wifi_provision_service_stop();
    assert(!wifi_provision_service_active());
    assert(test_httpd_uri_count == 0);
    assert(test_wifi_stack_clean());
    assert(wifi_radio_holder(wifi_radio_shared()) == WIFI_RADIO_FREE);

    const unsigned stops = test_httpd_stops;
    wifi_provision_service_stop(); // 第二次什么都不该发生
    assert(test_httpd_stops == stops);
    assert(test_httpd_starts == starts);
}

static void test_start_is_rejected_while_already_running(void) {
    reset_world();
    start_ok();
    assert(wifi_provision_service_start() == ESP_ERR_INVALID_STATE);
    assert(wifi_provision_service_active());
}

static void test_three_cycles_leave_no_residue(void) {
    for (int round = 0; round < 3; round++) {
        reset_world();
        start_ok();
        wifi_provision_service_stop();
        assert(test_wifi_stack_clean());
        assert(test_httpd_uri_count == 0);
        assert(wifi_radio_holder(wifi_radio_shared()) == WIFI_RADIO_FREE);
    }
}

int main(void) {
    test_every_start_step_rolls_back();
    test_ap_is_open_single_client_on_channel_one();
    test_start_announces_setup_on_the_status_page();
    test_get_serves_the_form();
    test_content_length_missing_is_411();
    test_content_length_not_a_number_is_400();
    test_content_length_too_large_is_413_without_reading();
    test_content_length_with_twenty_digits_is_413();
    test_malformed_form_is_rejected_without_writing();
    test_short_body_is_400();
    test_save_success_answers_then_restarts();
    test_save_failure_is_500_and_does_not_restart();
    test_client_count_follows_dhcp();
    test_timeout_closes_the_ap();
    test_stop_is_idempotent();
    test_start_is_rejected_while_already_running();
    test_three_cycles_leave_no_residue();
    puts("wifi provision service tests: PASS");
    return 0;
}
