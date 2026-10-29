// tests/test_wifi_service.c —— 连接服务的启动回滚与生命周期。
//
// 这个文件接手了原 test_demo_wifi_runtime.c 的"逐步骤失败回滚"矩阵：那套写法
// （next_step + fail_at + 资源台账）证明的是同一件事 —— 栈在任意一步失败都不能留下
// 半截资源。服务比演示页多两步（队列与任务），所以额外单独覆盖。
#include "demo_stubs/demo_runtime.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/wifi_profile_service.c"

static wifi_status_snapshot_t snapshot(void) {
    wifi_status_snapshot_t s;
    wifi_status_read(wifi_status_shared(), &s);
    return s;
}

static void reset_world(void) {
    // 上一个用例可能把服务留在运行态；先收干净，否则下一个 start() 直接返回
    // ESP_ERR_INVALID_STATE，失败会以"莫名其妙的断言"形式出现在别处。
    wifi_profile_service_stop();
    test_nvs_clear();
    test_wifi_stack_reset();
    test_create_fails = false;
    test_queue_fails = false;
    test_queue_send_fails = false;
    test_queue_expire = false;
    test_queue_poll_hook = NULL;
    test_time_us = 0;
    test_battery_soc = 88;
}

static void test_nvs_init_failure_is_reported_first(void) {
    reset_world();
    test_nvs_flash_result = ESP_FAIL;

    assert(wifi_profile_service_start() != ESP_OK);
    // NVS 最早失败：栈一步都不该动。
    assert(test_wifi_step == 0);
    assert(!wifi_profile_service_running());
    assert(snapshot().phase == WIFI_PHASE_ERROR);
}

static void test_nvs_open_failure_also_stops_before_the_stack(void) {
    reset_world();
    test_nvs_open_result = ESP_ERR_NVS_NOT_FOUND;

    assert(wifi_profile_service_start() != ESP_OK);
    assert(test_wifi_step == 0);
    assert(!wifi_profile_service_running());
}

static void test_every_stack_step_rolls_back_completely(void) {
    for (unsigned failure = 1; failure <= 9; failure++) {
        reset_world();
        test_wifi_fail_at = failure;

        assert(wifi_profile_service_start() == ESP_ERR_NO_MEM);
        assert(test_wifi_step == failure);
        assert(!wifi_profile_service_running());
        assert(s_queue == NULL);
        assert(s_task == NULL);
        // 两个账本同时归零才算回滚干净：桩持有的资源 + 服务的静态守卫。
        assert(test_wifi_stack_clean());
        assert(s_netif == NULL);
        assert(!s_wifi_inited && !s_wifi_started);
        assert(snapshot().phase == WIFI_PHASE_ERROR);
    }
}

static void test_queue_failure_tears_the_stack_back_down(void) {
    reset_world();
    test_queue_fails = true;

    assert(wifi_profile_service_start() == ESP_ERR_NO_MEM);
    assert(!wifi_profile_service_running());
    assert(s_queue == NULL);
    assert(test_wifi_stack_clean());
}

static void test_task_failure_releases_the_queue(void) {
    reset_world();
    test_create_fails = true;

    assert(wifi_profile_service_start() == ESP_ERR_NO_MEM);
    assert(!wifi_profile_service_running());
    assert(s_queue == NULL);
    assert(s_task == NULL);
    assert(test_wifi_stack_clean());
}

static void test_start_publishes_connecting_before_the_worker_runs(void) {
    reset_world();
    assert(wifi_profile_service_start() == ESP_OK);
    assert(wifi_profile_service_running());

    // worker 还没跑，但状态页不该继续显示 "Starting Wi-Fi..."。
    wifi_status_snapshot_t s = snapshot();
    assert(s.phase == WIFI_PHASE_CONNECTING);
    assert(s.seq >= 1);

    // 空库：worker 第一轮就直接进"等配网"，不会去碰驱动。
    test_run_worker(s_task);
    s = snapshot();
    assert(s.phase == WIFI_PHASE_NO_PROFILE);
    assert(s.profile_count == 0);
}

static void test_stop_publishes_idle_and_reaps_the_worker(void) {
    reset_world();
    assert(wifi_profile_service_start() == ESP_OK);
    TaskHandle_t worker = s_task;
    assert(worker != NULL);

    wifi_profile_service_stop();

    assert(!wifi_profile_service_running());
    assert(worker->deleted); // owner 回收，而不是 worker 自杀
    assert(s_task == NULL);
    assert(s_queue == NULL);
    assert(test_wifi_stack_clean());
    assert(snapshot().phase == WIFI_PHASE_IDLE);
}

static void test_start_stop_is_repeatable(void) {
    reset_world();
    for (int round = 0; round < 2; round++) {
        assert(wifi_profile_service_start() == ESP_OK);
        assert(wifi_profile_service_running());
        // 重复启动被拒绝，且不破坏已有状态。
        assert(wifi_profile_service_start() == ESP_ERR_INVALID_STATE);
        wifi_profile_service_stop();
        assert(!wifi_profile_service_running());
        assert(test_wifi_stack_clean());
    }
}

static void test_stop_without_start_is_a_noop(void) {
    reset_world();
    wifi_profile_service_stop(); // 不该崩，也不该发布任何东西
    assert(!wifi_profile_service_running());
}

int main(void) {
    test_nvs_init_failure_is_reported_first();
    test_nvs_open_failure_also_stops_before_the_stack();
    test_every_stack_step_rolls_back_completely();
    test_queue_failure_tears_the_stack_back_down();
    test_task_failure_releases_the_queue();
    test_start_publishes_connecting_before_the_worker_runs();
    test_stop_publishes_idle_and_reaps_the_worker();
    test_start_stop_is_repeatable();
    test_stop_without_start_is_a_noop();
    puts("wifi profile service tests: PASS");
    return 0;
}
