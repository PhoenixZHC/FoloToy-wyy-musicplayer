<p align="right">
  <a href="build-and-test.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Build and Test

> Folo Music 2.3.0 uses ESP-IDF 5.5.4 and explicitly selects sdkconfig.direct.defaults. The 5.5.3/default build below is the upstream baseline, not the product release command. See the [product deployment guide](../../../../docs/deployment.md) for direct builds, packaging with --version 2.3.0, exact-8-MiB images and settings-preservation rules. Static validation also runs five packaging regressions and nine login-tool tests; font tests and previews run separately.

Use ESP-IDF 5.5.3. On a clean machine or when the toolchain is missing, follow
the [environment bootstrap](environment-setup.md) first.

> Prefer `./tools/validate.sh --firmware` for firmware builds and flash its
> verified `build/FoloToy-Wangyiyun-full.bin` at offset `0x0`. Treat
> `idf.py build` and `idf.py flash` as incremental development commands, not the
> default delivery path.

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version             # must report ESP-IDF v5.5.3
./tools/validate.sh --firmware # preferred: build and verify merged 0x0 image
idf.py set-target esp32c3     # fresh checkout or changed target
idf.py build                  # optional incremental application build
idf.py flash monitor          # optional incremental application flash
idf.py fullclean              # remove stale generated build state only
```

`idf.py fullclean` does not fully synchronize an existing `sdkconfig` with
changed defaults. Preserve intentional local settings, then run
`idf.py set-target esp32c3` when the target or tracked defaults must be
regenerated.

The tracked `dependencies.lock` pins Managed Component resolution. After changing an `idf_component.yml`, regenerate the lock with ESP-IDF 5.5.3, review version changes, and commit it with the manifest. An ordinary build must not leave an unexplained lock-file diff.

Firmware validation uses a fresh temporary build directory and an isolated `sdkconfig` generated from the tracked defaults. It does not consume or overwrite a developer's root `sdkconfig`, and it copies only the verified merged image to `build/FoloToy-Wangyiyun-full.bin`.

The baseline also has a hardware-independent logic test:

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
  tests/test_ui_pixel_math.c main/ui_pixel_math.c \
  -o /tmp/test_ui_pixel_math
/tmp/test_ui_pixel_math
```

Use the unified validation entry point:

```bash
./tools/validate.sh --static    # repository checks, workflows, links, secrets, host tests
./tools/validate.sh --firmware  # build, merge-bin, image offsets and byte verification
./tools/validate.sh             # complete gate; requires an activated ESP-IDF environment
```

CI calls the same script. Fix the shared script or environment if local and CI behavior differs; do not duplicate command sequences in workflows.

Direct-client host tests require mbedTLS development headers and `libmbedcrypto`.

For desktop firmware emulation, run `node tools/run_passportsim.cjs <wasm> <merged.bin> <app.elf> /tmp/report.json baseline`. It records image and emulator hashes and fails closed when a tripwire or unsupported production scenario is encountered. Current PassportSim 1.0.3 supports the IDF 5.5.3 Wi-Fi profile but not `esp_wifi_set_ps`; a separate temporary diagnostic build omitting that call can test `diagnostic-wifi` (connection) and `diagnostic-wifi-absent` (timeout and rescan). Neither scope constitutes release acceptance. `diagnostic-wifi-wrong-password` currently returns emulator `E_HLE` because that authentication path is not modeled. Do not flash based on diagnostic results.

Set `CC`, `DIRECT_TEST_CFLAGS`, and `DIRECT_TEST_LDFLAGS` for non-system paths.
The static gate runs 21 executables, including QQ Music MQTT/PNG/JSON projection and session failure handling, provider isolation, playlist ordering, concurrent audio-download fault recovery, display timing and theme boundaries, both provisioning modes and
isolated HTTP/NVS fakes. Set `KEEP_VALIDATION_ARTIFACTS=1` to preserve temporary
test and build directories. The fork's direct build uses ESP-IDF 5.5.4 as
specified in the repository-root README; this upstream guide's 5.5.3 baseline
does not change that choice.

Hardware-affecting changes must also run the applicable on-device checklist in the hardware guide. Report compilation separately from physical-device validation.

## QQ Music device preflight

This opt-in diagnostic entry is separate from the product UI. With ESP-IDF 5.5.4 activated, build from the firmware directory:

```bash
idf.py -B build-qq-preflight -D SDKCONFIG=build-qq-preflight/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults' \
  -DQQMUSIC_DEVICE_PREFLIGHT=ON build
```

The application version is `2.1.1-qq-preflight`. It reads existing `netease_wifi` settings, displays the actual QQ Music App QR PNG without scaling or theme inversion, saves QQ credentials only in the separate `qqmusic` NVS namespace, refreshes and verifies them, and tries up to eight pages to find an authorized full MP3. It records heap, largest free block, stack and decoded duration without logging account identifiers, credentials or media URLs. Short OK pauses/resumes; long OK starts a new QR session. Restart tests saved-session restoration. Duration comparison does not replace listening or complete hardware acceptance.

After three failed attempts on the saved network, the diagnostic opens `Folo-QQ-Test` with a random password shown only on the device. Connect a phone and open `http://192.168.4.1` to scan for and submit a local 2.4 GHz network. Provisioning waits up to ten minutes; failed submissions can be retried. These settings stay in driver RAM and never replace the saved Wi-Fi NVS. HTTP and AP resources are closed before QQ verification. After reboot, provision the temporary network again while QQ credentials restore from their separate NVS. Long OK restarts the device after network setup failure.

The preflight also has a five-second serial setup window at boot. With explicit user authorization it accepts network JSON and saves it in the separate `qq_test_wifi` namespace for automatic reconnects, without changing `netease_wifi`. Send secrets through private input only; never retain passwords in source, command output or project records. Device results are recorded in the root `docs/qqmusic-device-validation.md`.

Obtain explicit flashing approval and verify the device partition layout before an app-only update. Do not use a full-image write for this test. The test must not erase Wi-Fi, NetEase credentials or display preferences. The normal build keeps the product entry and does not activate this diagnostic. Hardware playback and memory acceptance remain prerequisites for the later dual-platform UI integration.
