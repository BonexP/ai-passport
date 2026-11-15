# 诊断 REPL 的源文件随 Kconfig 开关增减。REQUIRES 不能这样条件化：ESP-IDF 在
# "早期展开"阶段用一个独立的 CMake 脚本收集组件依赖，那里不存在 CONFIG_* 变量，
# 条件分支永远为假，console 就进不了依赖表（表现为找不到 esp_console.h）。
# console 已在本工程构建集合内，无条件列出只增加一条依赖边，不改变 OFF 构建产物。
set(DIAG_REPL_SRCS)
if(CONFIG_FOLOTOY_DIAG_REPL)
    list(APPEND DIAG_REPL_SRCS "diag_repl.c" "diag_repl_logic.c")
endif()

idf_component_register(
    SRCS "main.c"
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "demo_display.c"
         "demo_button.c"
         "demo_audio.c"
         "demo_battery.c"
         "demo_radio.c"

         "demo_ble.c"
         "demo_low_power.c"
         "app_location.c"
         "app_config.c"
         "app_clock.c"
         "app_time.c"
         "wifi_profile.c"
         "wifi_connect_policy.c"
         "wifi_profile_nvs.c"
         "wifi_profile_service.c"
         "wifi_presets.c"
         "wifi_radio_owner.c"
         "wifi_provision_service.c"
         "wifi_status.c"
         "wifi_provision_form.c"
         "wifi_provision_flow.c"
         "wifi_provision_html.c"
         "wifi_scan_list.c"
         "wifi_status_shared.c"
         "page_router.c"
         "ui_product.c"
         "page_status.c"
         "http_body.c"
         "json_field.c"
         "app_config_check.c"
         "app_data.c"
         "app_data_shared.c"
         "net_schedule.c"
         "weather_source.c"
         "usage_source.c"
         "net_http.c"
         "net_worker.c"
         "weather_view.c"
         "usage_view.c"
         "net_snapshot.c"
         "net_snapshot_store.c"
         "page_weather.c"
         "page_usage.c"
         "page_ring.c"
         ${DIAG_REPL_SRCS}
    INCLUDE_DIRS "."
    REQUIRES bsp bt console esp_event esp_hw_support esp_http_client esp_http_server esp_netif esp_timer
             esp_wifi mbedtls nvs_flash
)
