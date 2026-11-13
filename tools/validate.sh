#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local dead_strip_flag

    # Apple ld does not accept --gc-sections; -dead_strip is the equivalent.
    dead_strip_flag="-Wl,--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        dead_strip_flag="-Wl,-dead_strip"
    fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_location.c main/app_location.c \
        -o "${test_dir}/test_app_location"
    "${test_dir}/test_app_location"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_config.c main/app_config.c \
        -o "${test_dir}/test_app_config"
    "${test_dir}/test_app_config"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_config_check.c main/app_config_check.c main/app_config.c \
        main/wifi_profile.c main/app_location.c \
        -o "${test_dir}/test_app_config_check"
    "${test_dir}/test_app_config_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_clock.c main/app_clock.c \
        -o "${test_dir}/test_app_clock"
    "${test_dir}/test_app_clock"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_data.c main/app_data.c \
        -o "${test_dir}/test_app_data"
    "${test_dir}/test_app_data"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_net_schedule.c main/net_schedule.c \
        -o "${test_dir}/test_net_schedule"
    "${test_dir}/test_net_schedule"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_weather_source.c main/weather_source.c main/json_field.c \
        main/app_location.c \
        -o "${test_dir}/test_weather_source"
    "${test_dir}/test_weather_source"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_usage_source.c main/usage_source.c main/json_field.c \
        -o "${test_dir}/test_usage_source"
    "${test_dir}/test_usage_source"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_weather_view.c main/weather_view.c main/app_clock.c \
        -o "${test_dir}/test_weather_view"
    "${test_dir}/test_weather_view"
    # usage_view 把 UTC 格式化交给了 app_clock，所以两个翻译单元一起链接。
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_usage_view.c main/usage_view.c main/usage_source.c \
        main/json_field.c main/app_clock.c \
        -o "${test_dir}/test_usage_view"
    "${test_dir}/test_usage_view"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/demo_stubs -Imain \
        tests/test_net_http.c main/net_http.c main/http_body.c \
        tests/demo_stubs/demo_http_client_stack.c \
        -o "${test_dir}/test_net_http"
    "${test_dir}/test_net_http"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_net_snapshot.c main/net_snapshot.c \
        -o "${test_dir}/test_net_snapshot"
    "${test_dir}/test_net_snapshot"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_profile.c main/wifi_profile.c main/app_location.c \
        -o "${test_dir}/test_wifi_profile"
    "${test_dir}/test_wifi_profile"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_presets.c main/wifi_presets.c main/wifi_profile.c \
        main/app_config.c main/app_location.c \
        -o "${test_dir}/test_wifi_presets"
    "${test_dir}/test_wifi_presets"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_status.c main/wifi_status.c \
        -o "${test_dir}/test_wifi_status"
    "${test_dir}/test_wifi_status"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_provision_form.c main/wifi_provision_form.c main/wifi_profile.c \
        main/app_location.c \
        -o "${test_dir}/test_wifi_provision_form"
    "${test_dir}/test_wifi_provision_form"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_provision_flow.c main/wifi_provision_flow.c \
        main/wifi_provision_form.c main/wifi_profile.c main/app_location.c \
        -o "${test_dir}/test_wifi_provision_flow"
    "${test_dir}/test_wifi_provision_flow"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_provision_html.c main/wifi_provision_html.c \
        main/wifi_provision_form.c main/wifi_profile.c main/app_location.c \
        -o "${test_dir}/test_wifi_provision_html"
    "${test_dir}/test_wifi_provision_html"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_connect_policy.c main/wifi_connect_policy.c main/wifi_profile.c \
        main/app_location.c \
        -o "${test_dir}/test_wifi_connect_policy"
    "${test_dir}/test_wifi_connect_policy"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_page_router.c main/page_router.c \
        -o "${test_dir}/test_page_router"
    "${test_dir}/test_page_router"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_page_ring.c main/page_ring.c \
        -o "${test_dir}/test_page_ring"
    "${test_dir}/test_page_ring"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
        tests/test_page_status.c main/wifi_status.c main/wifi_status_shared.c \
        -Wl,--gc-sections -o "${test_dir}/test_page_status"
    "${test_dir}/test_page_status"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_radio_owner.c main/wifi_radio_owner.c \
        -o "${test_dir}/test_wifi_radio_owner"
    "${test_dir}/test_wifi_radio_owner"
    # 这两个测试链接 demo_app_config.c 而不是 main/app_config.c：后者会通过
    # __has_include 吃进开发机的 app_config.local.h，其中的 Wi-Fi 预设会在
    # wifi_profile_service_start() 里被注入，打破"空库"用例的前提
    # （本地红 CI 绿的假回归）。表内容语义由 test_wifi_presets.c 单独覆盖。
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
        tests/test_wifi_service.c tests/demo_stubs/demo_wifi_stack.c \
        tests/demo_stubs/demo_app_config.c \
        main/wifi_profile.c main/wifi_presets.c main/wifi_profile_nvs.c \
        main/app_location.c \
        main/wifi_connect_policy.c main/wifi_status.c main/wifi_status_shared.c \
        main/wifi_radio_owner.c \
        -Wl,--gc-sections -o "${test_dir}/test_wifi_service"
    "${test_dir}/test_wifi_service"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
        tests/test_wifi_provision_service.c \
        tests/demo_stubs/demo_wifi_stack.c tests/demo_stubs/demo_http_stack.c \
        tests/demo_stubs/demo_app_config.c \
        main/wifi_provision_service.c main/wifi_provision_form.c \
        main/wifi_provision_flow.c main/wifi_provision_html.c \
        main/wifi_scan_list.c \
        main/wifi_profile.c main/wifi_profile_nvs.c main/wifi_presets.c \
        main/app_location.c \
        main/wifi_connect_policy.c main/wifi_status.c main/wifi_status_shared.c \
        main/wifi_radio_owner.c main/wifi_profile_service.c \
        -Wl,--gc-sections -o "${test_dir}/test_wifi_provision_service"
    "${test_dir}/test_wifi_provision_service"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_http_body.c main/http_body.c \
        -o "${test_dir}/test_http_body"
    "${test_dir}/test_http_body"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_json_field.c main/json_field.c \
        -o "${test_dir}/test_json_field"
    "${test_dir}/test_json_field"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
        tests/test_net_snapshot_store.c main/net_snapshot_store.c main/net_snapshot.c \
        -Wl,--gc-sections -o "${test_dir}/test_net_snapshot_store"
    "${test_dir}/test_net_snapshot_store"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
        tests/test_net_worker.c \
        main/net_snapshot_store.c main/net_snapshot.c main/net_schedule.c \
        main/app_data.c main/app_data_shared.c main/app_location.c \
        main/wifi_status.c main/wifi_status_shared.c \
        -Wl,--gc-sections -o "${test_dir}/test_net_worker"
    "${test_dir}/test_net_worker"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_scan_list.c main/wifi_scan_list.c \
        -o "${test_dir}/test_wifi_scan_list"
    "${test_dir}/test_wifi_scan_list"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_provision_html_form.c \
        main/wifi_provision_html.c main/wifi_provision_form.c main/wifi_scan_list.c \
        main/app_location.c main/wifi_profile.c \
        -o "${test_dir}/test_wifi_provision_html_form"
    "${test_dir}/test_wifi_provision_html_form"

    for demo in audio low_power ble; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${dead_strip_flag}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
