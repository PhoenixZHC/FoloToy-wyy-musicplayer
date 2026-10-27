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
    local cc_bin
    local cjson_dir

    python3 tools/check_repo.py
    python3 tests/test_package_release.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(bash ./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d "${TMPDIR:-/tmp}/ai-passport-host-tests.XXXXXX")"
    cc_bin="${CC:-cc}"
    cjson_dir="${IDF_PATH:-}/components/json/cJSON"
    if [[ ! -d "${cjson_dir}" ]]; then
        echo "ERROR: IDF_PATH is required for NetEase client host parser tests." >&2
        return 1
    fi
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_netease_app_logic.c main/netease_app_logic.c \
        -o "${test_dir}/test_netease_app_logic"
	    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
	        tests/test_music_lyrics.c main/music_lyrics.c \
	        -o "${test_dir}/test_music_lyrics"
	    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
	        tests/test_wifi_provisioning_logic.c main/wifi_provisioning_logic.c \
	        -o "${test_dir}/test_wifi_provisioning_logic"
	    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
	        tests/test_device_music_ui_spec.c main/device_music_ui_spec.c \
	        -o "${test_dir}/test_device_music_ui_spec"
	    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -Imain \
	        tests/test_wifi_provisioning_portal.c main/wifi_provisioning_portal.c \
	        -o "${test_dir}/test_wifi_provisioning_portal"
	    "${cc_bin}" -std=c11 -Wall -Wextra -Werror \
	        -I../components/audio_stream/include \
	        tests/test_audio_stream.c ../components/audio_stream/audio_stream.c \
        -o "${test_dir}/test_audio_stream"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror \
        -DNETEASE_CLIENT_PARSER_ONLY \
        -I../components/netease_client/include \
        -I"${cjson_dir}" \
        tests/test_netease_client.c \
        ../components/netease_client/netease_client.c \
        "${cjson_dir}/cJSON.c" \
        -o "${test_dir}/test_netease_client"
    "${test_dir}/test_ui_pixel_math"
	    "${test_dir}/test_netease_app_logic"
	    "${test_dir}/test_music_lyrics"
	    "${test_dir}/test_wifi_provisioning_logic"
	    "${test_dir}/test_device_music_ui_spec"
	    "${test_dir}/test_wifi_provisioning_portal"
	    "${test_dir}/test_audio_stream"
    "${test_dir}/test_netease_client"
    "${cc_bin}" -std=c11 -Wall -Wextra -Werror -DCONFIG_NETEASE_DIRECT=1 -Imain \
        tests/test_wifi_provisioning_portal.c main/wifi_provisioning_portal.c \
        -o "${test_dir}/test_wifi_provisioning_portal_direct"
    "${test_dir}/test_wifi_provisioning_portal_direct"
    DIRECT_TEST_OUT="${test_dir}/test_netease_direct" bash tools/run_direct_host_tests.sh
    if [[ "${KEEP_VALIDATION_ARTIFACTS:-0}" != 1 ]]; then
        rm -rf "${test_dir}"
    else
        echo "Host artifacts: ${test_dir}"
    fi
    echo "Host tests: PASS (12 executables)"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    if [[ "${KEEP_VALIDATION_ARTIFACTS:-0}" != 1 ]]; then
        trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT
    fi

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-Wangyiyun-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    install -D -m 0644 \
        "${validation_build_dir}/FoloToy-Wangyiyun-full.bin" \
        "${repo_root}/build/FoloToy-Wangyiyun-full.bin"
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
