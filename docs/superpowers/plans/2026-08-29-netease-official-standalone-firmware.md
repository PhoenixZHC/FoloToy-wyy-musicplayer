# NetEase Official Standalone Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a second, independently buildable ESP32-C3 firmware that logs in, reads playlists, and plays music through NetEase Cloud Music's official Open Platform without the existing relay server.

**Architecture:** Copy the proven hardware, UI, Wi-Fi, and audio baseline into `netease-official-firmware/`, then replace the relay-facing client and Cloud provisioning flow with a focused official API client. Keep signing, HTTP transport, token storage, response parsing, and UI orchestration behind separate interfaces so pure logic remains host-testable and the old `firmware/` tree remains untouched.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-C3, C11, FreeRTOS, LVGL, ESP HTTP Client, cJSON, mbedTLS RSA-SHA256/Base64, NVS, SNTP, Helix MP3, ES8311/I2S, PowerShell 5.1, GCC host tests.

**Spec:** `docs/superpowers/specs/2026-08-29-netease-official-standalone-firmware-design.md`

## Global Constraints

- Target ESP32-C3 with 8 MB Flash and no PSRAM.
- Create no branch or worktree; the current root is not a Git repository, so this plan contains no commit steps.
- Do not modify any file under the existing `firmware/` or `server/` directories.
- The new build must resolve every source and component from `netease-official-firmware/` only.
- Use `https://openapi.music.163.com` and the exact method, path, parameters, and fields shown in the user's authorized official documentation.
- Use `channel=iotapitest`, `deviceType=openapi`, `os=openapi`, and `brand=iotapitest` for this development build.
- Real AppID, rotated AppSecret, private key, access token, and refresh token must never enter tracked project files or logs.
- Do not enable Secure Boot, Flash Encryption, or irreversible eFuse settings.
- Keep button callbacks non-blocking; networking, signing, NVS, and audio work run in worker tasks.
- Hold the BSP LVGL lock whenever code outside the LVGL task accesses LVGL objects.
- Expose failures explicitly; never fabricate a successful login, playlist, URL, or playback state.
- Use PowerShell 5.1-compatible commands. Activate ESP-IDF with the exact environment commands in the project instructions.

---

## File Structure

Task 1 copies the current firmware baseline unchanged into the new tree. The implementation then creates or modifies these source, configuration, and validation files plus the root records listed in Task 8:

```text
netease-official-firmware/
  README.md
  .gitignore
  config/
    netease_secrets.example.h
    netease_secrets.h                 # local ignored file
  firmware/
    ai-passport/
      CMakeLists.txt
      sdkconfig.defaults
      partitions.csv
      main/
        CMakeLists.txt
        main.c
        netease_app.c
        netease_app.h
        netease_app_logic.c
        netease_app_logic.h
        official_auth_store.c
        official_auth_store.h
        device_music_ui.c
        device_music_ui.h
        device_music_ui_spec.c
        device_music_ui_spec.h
        ui_pixel.c
        ui_pixel.h
        ui_pixel_math.c
        ui_pixel_math.h
        demo_radio.c
        demo_radio.h
        wifi_provisioning_page.c
        wifi_provisioning_page.h
        wifi_provisioning_logic.c
        wifi_provisioning_logic.h
        fonts/
      components/bsp/                 # copied hardware baseline
      tests/
        test_netease_app_logic.c
        test_official_api_codec.c
        test_official_auth_logic.c
        test_playlist_merge.c
      tools/
        run_host_tests.ps1
        validate_firmware.ps1
    components/
      audio_stream/                    # copied playback baseline
      netease_official_client/
        CMakeLists.txt
        idf_component.yml
        include/netease_official_client.h
        official_api_codec.c
        official_api_signer.c
        official_http.c
        netease_official_client.c
```

Responsibilities:

- `official_api_codec.c`: deterministic parameter ordering, URL encoding, device JSON construction, and response parsing; host-testable without ESP-IDF networking.
- `official_api_signer.c`: RSA-SHA256 and Base64 using mbedTLS; never logs key material or canonical request text.
- `official_http.c`: bounded HTTPS request/response handling and official envelope/error decoding.
- `netease_official_client.c`: public operations for anonymous login, QR flow, refresh, playlists, songs, and play URL.
- `official_auth_store.c`: NVS persistence and expiry decisions for access/refresh tokens.
- `netease_app.c`: worker-driven flow connecting Wi-Fi, auth, playlists, UI, and audio.
- `wifi_provisioning_page.c`: Wi-Fi-only provisioning; no Cloud URL, registration code, device secret, or relay registration.

---

### Task 1: Create and Prove the Independent Firmware Tree

**Files:**
- Create: `netease-official-firmware/**`
- Create: `netease-official-firmware/.gitignore`
- Create: `netease-official-firmware/README.md`
- Test: `netease-official-firmware/firmware/ai-passport/tools/validate_firmware.ps1`

**Interfaces:**
- Consumes: current `firmware/ai-passport`, `firmware/components/audio_stream`, and their pinned dependencies as a read-only source baseline.
- Produces: a standalone ESP-IDF project rooted at `netease-official-firmware/firmware/ai-passport` whose `EXTRA_COMPONENT_DIRS` points only to `../components` inside the new tree.

- [ ] **Step 1: Record hashes of all existing firmware and server source files**

Use PowerShell to create an in-memory before snapshot; do not write it inside either source tree:

```powershell
$sourceRoots = @(
    'E:\code\folotoy-wangyiyun\firmware',
    'E:\code\folotoy-wangyiyun\server'
)
$before = Get-ChildItem -LiteralPath $sourceRoots -Recurse -File |
    Where-Object { $_.FullName -notmatch '\\build\\|\\managed_components\\|\\node_modules\\' } |
    Get-FileHash -Algorithm SHA256
```

- [ ] **Step 2: Copy the firmware baseline into the new directory**

Copy `firmware/ai-passport` to `netease-official-firmware/firmware/ai-passport` and `firmware/components/audio_stream` to `netease-official-firmware/firmware/components/audio_stream`. Exclude `build`, `sdkconfig`, `sdkconfig.old`, `managed_components`, `.exe` host artifacts, and local editor folders. Do not move or delete the sources.

- [ ] **Step 3: Lock build paths to the new tree**

Set the new project CMake file to:

```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(FoloToy-NetEase-Official)
```

Add a validation check that resolves `EXTRA_COMPONENT_DIRS` and fails unless it remains under `netease-official-firmware/firmware`.

- [ ] **Step 4: Add new-tree ignore and usage files**

The ignore file must cover:

```gitignore
firmware/ai-passport/build/
firmware/ai-passport/sdkconfig
firmware/ai-passport/sdkconfig.old
firmware/ai-passport/managed_components/
config/netease_secrets.h
*.exe
```

The README must give separate build commands for the old and new firmware and state that flashing one replaces the other on the device without changing either source directory.

- [ ] **Step 5: Verify source isolation**

Recalculate the source hashes and compare them to `$before`. Expected: zero changed, added, or deleted files below the old `firmware/` and `server/` roots.

---

### Task 2: Freeze the Official API Contract and Local Secret Boundary

**Files:**
- Create: `netease-official-firmware/README.md`
- Create: `netease-official-firmware/config/netease_secrets.example.h`
- Create locally: `netease-official-firmware/config/netease_secrets.h`
- Create: `netease-official-firmware/firmware/components/netease_official_client/include/netease_official_client.h`

**Interfaces:**
- Consumes: official documentation IDs and the approved device-value set.
- Produces: `netease_official_config_t`, `netease_token_set_t`, playlist/song/result types, and literal verified endpoint constants used by later tasks.

- [ ] **Step 1: Verify every required operation against the logged-in official documentation**

Record method, literal path, required `bizContent`, required token kind, and response fields for these official documents:

```text
IOT common parameters       0f7801d7d6d24180b8fc9058d1ffe593
Anonymous login             1a5fb2c7b30b44609fa81129a8e1908d
Get login QR code           2bb12a93e71a4be0842243b930c2f33c
Poll QR status              4d0301c68c5d45d8811d7787ee37d2f6
Refresh AccessToken         2066d57bbed2445baeb18429e44b8689
Created playlists           e4fef4e5cc564fc1adbcfcf02140f0d5
Subscribed playlists        1b3d86a47f2e45c7bd631bcd26052382
Playlist song list v4       781b8d60178f428695e8ff3b464a3736
Song detail                 2f583c5e2d764bbabaa221865f62dbc4
Song play URL               3d2c9f695ff24f4ea37611614b7f7856
```

Acceptance: every implemented path is copied from the official page; if any required page is unavailable or the app lacks permission, stop with that exact missing operation instead of guessing.

- [ ] **Step 2: Define the secret template**

Use an example header with invalid values only:

```c
#pragma once

#define NETEASE_OFFICIAL_APP_ID "replace-me"
#define NETEASE_OFFICIAL_APP_SECRET "replace-me"
#define NETEASE_OFFICIAL_PRIVATE_KEY_PEM \
    "-----BEGIN PRIVATE KEY-----\n" \
    "replace-me\n" \
    "-----END PRIVATE KEY-----\n"
```

Create the ignored local header by copying the example. Do not populate it from screenshots or chat history. The user supplies the rotated values locally before live integration.

- [ ] **Step 3: Define the public client types**

The header must expose fixed, bounded structures and these operations:

```c
typedef struct {
    const char *app_id;
    const char *app_secret;
    const char *private_key_pem;
    const char *device_id;
    const char *app_version;
    int timeout_ms;
} netease_official_config_t;

typedef struct {
    netease_official_config_t config;
} netease_official_client_t;

typedef struct {
    char access_token[256];
    char refresh_token[256];
    int64_t access_expires_at_ms;
    int64_t refresh_expires_at_ms;
} netease_token_set_t;

typedef enum {
    NETEASE_OFFICIAL_QR_EXPIRED = 800,
    NETEASE_OFFICIAL_QR_WAITING = 801,
    NETEASE_OFFICIAL_QR_SCANNED = 802,
    NETEASE_OFFICIAL_QR_CONFIRMED = 803,
    NETEASE_OFFICIAL_QR_FAILED = 804,
} netease_official_qr_state_t;

typedef struct {
    char unikey[128];
    char qr_code_url[384];
    int expires_in_seconds;
} netease_official_qr_t;

typedef struct {
    netease_official_qr_state_t state;
    netease_token_set_t tokens;
    char message[96];
} netease_official_qr_status_t;

typedef struct {
    int64_t id;
    char name[64];
    int track_count;
    char owner_name[64];
} netease_official_playlist_t;

typedef struct {
    size_t count;
    bool has_more;
    netease_official_playlist_t items[20];
} netease_official_playlist_list_t;

typedef struct {
    int64_t id;
    char name[64];
    char artist[96];
    int duration_ms;
} netease_official_track_t;

typedef struct {
    int64_t playlist_id;
    size_t count;
    uint32_t next_offset;
    bool has_more;
    netease_official_track_t items[30];
} netease_official_track_list_t;

typedef struct {
    int64_t song_id;
    char url[512];
    char format[16];
    int expires_in_seconds;
} netease_official_play_url_t;

esp_err_t netease_official_init(netease_official_client_t *client,
                                const netease_official_config_t *config);
esp_err_t netease_official_anonymous_login(netease_official_client_t *client,
                                           char *token, size_t token_size);
esp_err_t netease_official_qr_start(netease_official_client_t *client,
                                    const char *anonymous_token,
                                    netease_official_qr_t *out);
esp_err_t netease_official_qr_poll(netease_official_client_t *client,
                                   const char *anonymous_token,
                                   const char *unikey,
                                   netease_official_qr_status_t *out);
esp_err_t netease_official_refresh(netease_official_client_t *client,
                                   const char *refresh_token,
                                   netease_token_set_t *out);
esp_err_t netease_official_get_playlists(netease_official_client_t *client,
                                         const char *access_token,
                                         netease_official_playlist_list_t *out);
esp_err_t netease_official_get_tracks(netease_official_client_t *client,
                                      const char *access_token,
                                      int64_t playlist_id, uint32_t limit,
                                      uint32_t offset,
                                      netease_official_track_list_t *out);
esp_err_t netease_official_get_play_url(netease_official_client_t *client,
                                        const char *access_token,
                                        int64_t song_id, int bitrate,
                                        netease_official_play_url_t *out);
```

- [ ] **Step 4: Reject invalid local configuration before networking**

`netease_official_init()` must return `ESP_ERR_INVALID_STATE` when any secret is empty, still equals `replace-me`, or the PEM markers are missing. Unit-test each failure without printing the rejected value.

---

### Task 3: Implement Deterministic Encoding and Signing

**Files:**
- Create: `netease-official-firmware/firmware/components/netease_official_client/official_api_codec.c`
- Create: `netease-official-firmware/firmware/components/netease_official_client/official_api_signer.c`
- Test: `netease-official-firmware/firmware/ai-passport/tests/test_official_api_codec.c`
- Modify: `netease-official-firmware/firmware/ai-passport/tools/run_host_tests.ps1`

**Interfaces:**
- Consumes: `netease_official_config_t` and key/value parameter arrays.
- Produces:

```c
esp_err_t official_url_encode(const char *input, char *output, size_t output_size);
esp_err_t official_build_device_json(const char *device_id,
                                     const char *app_version,
                                     char *output, size_t output_size);
esp_err_t official_build_canonical(const official_param_t *params,
                                   size_t count,
                                   char *output, size_t output_size);
esp_err_t official_sign_rsa_sha256_base64(const char *private_key_pem,
                                          const char *canonical,
                                          char *output, size_t output_size);
```

- [ ] **Step 1: Write failing canonicalization tests**

Cover exclusion of `sign`, exclusion of empty values, ASCII ordering, exact `key=value&key=value` separators, buffer exhaustion, and UTF-8 URL encoding. Include this fixed ordering case:

```c
static const official_param_t params[] = {
    {"timestamp", "1724918400000"},
    {"sign", "ignored"},
    {"appId", "app-test"},
    {"bizContent", "{\"type\":2}"},
    {"accessToken", ""},
    {"signType", "RSA_SHA256"},
};
assert(strcmp(result,
    "appId=app-test&bizContent={\"type\":2}&signType=RSA_SHA256&timestamp=1724918400000") == 0);
```

- [ ] **Step 2: Run the codec test and verify RED**

Run `tools\run_host_tests.ps1 -Name official_api_codec`. Expected: compilation failure because codec functions are not defined.

- [ ] **Step 3: Implement minimal deterministic codec logic**

Use insertion sort over the small parameter array, never locale-aware comparison. URL encoding must leave only RFC 3986 unreserved bytes unchanged and encode UTF-8 bytes with uppercase hex.

- [ ] **Step 4: Run the codec test and verify GREEN**

Expected: all codec assertions pass and the script exits 0.

- [ ] **Step 5: Implement RSA-SHA256 signing with mbedTLS**

Parse the PEM with `mbedtls_pk_parse_key()`, hash the canonical UTF-8 bytes with SHA-256, sign with PKCS#1 v1.5/RSA-SHA256, and Base64-encode the signature. Zero temporary key/signature buffers before returning. Return the exact mbedTLS failure as `ESP_FAIL` with a sanitized stage name only.

- [ ] **Step 6: Add a device-only signing self-test fixture**

Compile a fixed test key and expected signature only when `CONFIG_NETEASE_OFFICIAL_SIGNER_SELF_TEST=y`. Compare the produced Base64 signature byte-for-byte during development builds, then keep that option disabled in `sdkconfig.defaults` for delivery.

---

### Task 4: Implement Bounded HTTPS and Official Response Parsing

**Files:**
- Create: `netease-official-firmware/firmware/components/netease_official_client/official_http.c`
- Modify: `netease-official-firmware/firmware/components/netease_official_client/official_api_codec.c`
- Test: `netease-official-firmware/firmware/ai-passport/tests/test_official_api_codec.c`
- Create: `netease-official-firmware/firmware/components/netease_official_client/CMakeLists.txt`

**Interfaces:**
- Consumes: signed parameter set, literal path, HTTP method, and bounded response capacity.
- Produces:

```c
typedef struct {
    int http_status;
    int code;
    char sub_code[48];
    char message[128];
    cJSON *root;
    cJSON *data;
} official_api_result_t;

esp_err_t official_http_request(const char *path,
                                esp_http_client_method_t method,
                                const char *encoded_query,
                                char *response, size_t response_size,
                                int *http_status);
esp_err_t official_parse_envelope(char *response,
                                  int http_status,
                                  official_api_result_t *out);
void official_api_result_clear(official_api_result_t *result);
```

- [ ] **Step 1: Write failing envelope-parser tests**

Cover success, official business error, malformed JSON, missing `code`, non-object `data`, and oversized response. Assert that an error never returns a non-null success data pointer.

- [ ] **Step 2: Run parser tests and verify RED**

Run `tools\run_host_tests.ps1 -Name official_api_codec`. Expected: failure at the first envelope assertion.

- [ ] **Step 3: Implement envelope parsing**

Parse only complete NUL-terminated responses. Preserve `code`, `subCode`, and a bounded message. Do not accept truncated JSON and do not convert unknown errors to success.

- [ ] **Step 4: Implement HTTPS transport**

Use `esp_http_client` with the ESP certificate bundle, a 10-second default timeout, redirect support limited to HTTPS, and a bounded heap response buffer. Reject non-HTTPS redirect targets and responses larger than the operation's declared maximum.

- [ ] **Step 5: Run host tests and a firmware compile**

Expected: parser tests pass; `idf.py build` compiles mbedTLS, JSON, certificate bundle, and HTTP dependencies without warnings promoted to errors.

---

### Task 5: Implement QR Authentication and NVS Token Lifecycle

**Files:**
- Create: `netease-official-firmware/firmware/ai-passport/main/official_auth_store.h`
- Create: `netease-official-firmware/firmware/ai-passport/main/official_auth_store.c`
- Create: `netease-official-firmware/firmware/ai-passport/tests/test_official_auth_logic.c`
- Modify: `netease-official-firmware/firmware/components/netease_official_client/netease_official_client.c`

**Interfaces:**
- Consumes: anonymous-login, QR-start, QR-poll, and refresh API operations.
- Produces:

```c
typedef enum {
    OFFICIAL_AUTH_LOGIN_QR = 0,
    OFFICIAL_AUTH_USE_ACCESS,
    OFFICIAL_AUTH_REFRESH,
    OFFICIAL_AUTH_CLEAR_AND_LOGIN_QR,
} official_auth_action_t;

official_auth_action_t official_auth_decide(const netease_token_set_t *tokens,
                                            int64_t now_ms,
                                            bool refresh_already_failed);
esp_err_t official_auth_load(netease_token_set_t *out);
esp_err_t official_auth_save(const netease_token_set_t *tokens);
esp_err_t official_auth_clear(void);
```

- [ ] **Step 1: Write failing pure auth-decision tests**

Test these exact decisions:

```text
no token                         -> LOGIN_QR
access token valid > 5 minutes   -> USE_ACCESS
access token expires <= 5 min    -> REFRESH
refresh token expired            -> LOGIN_QR
one refresh already failed       -> CLEAR_AND_LOGIN_QR
```

- [ ] **Step 2: Run the auth test and verify RED**

Expected: compilation failure because `official_auth_decide()` is absent.

- [ ] **Step 3: Implement the pure decision function and verify GREEN**

Use epoch milliseconds consistently. Do not refresh repeatedly inside the decision function.

- [ ] **Step 4: Add NVS persistence**

Use a dedicated `netease_oauth` namespace and keys `access`, `refresh`, `access_exp`, and `refresh_exp`. Save all four values in one opened handle and commit once. If any required read is missing or invalid, return no-token state rather than a partial token.

- [ ] **Step 5: Implement the QR flow**

Obtain the anonymous token, request the QR code with `type=2` and 300-second expiry, display `qrCodeUrl`, and poll with the returned `uniKey` plus stable `clientId`. Map official states exactly: 800 expired, 801 waiting, 802 scanned/authorized, 803 success with tokens, 804 failure.

- [ ] **Step 6: Implement one-shot refresh-and-retry**

On an official token-expired response, refresh once, save the new tokens, and repeat the original operation once. If refresh or retry fails, clear OAuth NVS and return an authentication-required result.

---

### Task 6: Implement Playlists, Lazy Song Details, and Play URL

**Files:**
- Modify: `netease-official-firmware/firmware/components/netease_official_client/netease_official_client.c`
- Modify: `netease-official-firmware/firmware/components/netease_official_client/official_api_codec.c`
- Create: `netease-official-firmware/firmware/ai-passport/tests/test_playlist_merge.c`
- Modify: `netease-official-firmware/firmware/ai-passport/tests/test_official_api_codec.c`

**Interfaces:**
- Consumes: valid access token and official API contract from Task 2.
- Produces: bounded merged playlist results, paged track IDs/details, and a fresh playable URL with format and expiry.

- [ ] **Step 1: Write failing playlist merge tests**

Test created-only, subscribed-only, duplicate IDs, empty responses, 20-item output cap, stable order, and UTF-8 truncation at a complete character boundary.

- [ ] **Step 2: Implement and verify playlist merge**

Append created playlists first, then subscribed playlists whose IDs are not already present. Preserve the official order within each source and expose `has_more` when the bounded output fills.

- [ ] **Step 3: Write failing song and URL parser tests**

Cover song ID list, name, artists, duration, MP3 URL, expiry, missing URL, `playFlag=false`, membership restriction, and unsupported format.

- [ ] **Step 4: Implement paged track loading**

Request at most 30 song IDs per page. Fetch details only for the five currently visible rows plus the selected row. Keep at most 30 track records in RAM and free each official JSON tree before starting the next request.

- [ ] **Step 5: Implement fresh URL lookup**

Request 128000 bitrate on selection. Accept MP3 for the first release; return an explicit unsupported-format error for FLAC or other formats rather than passing them to the MP3 decoder.

- [ ] **Step 6: Run all host tests**

Run `tools\run_host_tests.ps1`. Expected: codec, auth logic, playlist merge, app logic, UI specification, and existing audio tests all pass.

---

### Task 7: Replace Relay Provisioning and Integrate the Device Application

**Files:**
- Modify: `netease-official-firmware/firmware/ai-passport/main/wifi_provisioning_page.c`
- Modify: `netease-official-firmware/firmware/ai-passport/main/wifi_provisioning_logic.c`
- Modify: `netease-official-firmware/firmware/ai-passport/main/netease_app.c`
- Modify: `netease-official-firmware/firmware/ai-passport/main/netease_app_logic.c`
- Modify: `netease-official-firmware/firmware/ai-passport/main/main.c`
- Modify: `netease-official-firmware/firmware/ai-passport/main/CMakeLists.txt`
- Test: `netease-official-firmware/firmware/ai-passport/tests/test_netease_app_logic.c`

**Interfaces:**
- Consumes: official client, auth store, existing `device_music_ui`, `audio_stream`, BSP, and Wi-Fi support.
- Produces: complete on-device state flow from Wi-Fi setup through QR login, playlists, tracks, and playback.

- [ ] **Step 1: Write failing app-state tests**

Cover boot with/without Wi-Fi, valid token, refresh-needed token, QR waiting/scanned/expired/success, empty playlist, page loading, unavailable song, playback start failure, switch account, and retry after network loss.

- [ ] **Step 2: Remove relay-only states from the copied tree**

Remove Cloud URL, registration code, relay device ID/secret, registration, hello, HMAC device authentication, and relay logout from the new copy only. Wi-Fi provisioning stores only SSID and Wi-Fi password.

- [ ] **Step 3: Add time synchronization gate**

After Wi-Fi obtains an IP, start SNTP and wait in the worker task for a valid epoch with a bounded timeout. Do not issue an official signed request before time is valid. Show a retryable time-sync error if it times out.

- [ ] **Step 4: Connect auth and content flow**

On startup: load token, decide use/refresh/login, then load playlists. On QR success: save token and load playlists. On song selection: obtain a fresh URL, start `audio_stream`, and enter playing state only after `audio_stream_start()` returns success.

- [ ] **Step 5: Keep UI and callbacks safe**

All networking and NVS calls stay in the existing worker task pattern. Continue screen-object reuse and LVGL lock protection. Ignore repeated button work requests while a worker operation is active and keep long-press release suppression.

- [ ] **Step 6: Run app tests and host test suite**

Expected: all tests pass with zero changed hashes in the old `firmware/` and `server/` source snapshots.

---

### Task 8: Build, Package, Document, and Run Live Integration

**Files:**
- Create: `netease-official-firmware/firmware/ai-passport/tools/validate_firmware.ps1`
- Modify: `netease-official-firmware/README.md`
- Modify: `CONTEXT.md`
- Modify: `docs/netease-official-api.md`

**Interfaces:**
- Consumes: complete new firmware and the user's locally entered rotated credentials.
- Produces: independently verified `FoloToy-NetEase-Official-full.bin`, build instructions, and separate evidence for build, host tests, and device tests.

- [ ] **Step 1: Add PowerShell validation**

The script must activate no hidden environment, must stop on the first nonzero external-program exit, build into a temporary directory under the new tree, run `idf.py merge-bin`, verify offsets, and copy only the verified image to:

```text
netease-official-firmware/firmware/ai-passport/build/FoloToy-NetEase-Official-full.bin
```

- [ ] **Step 2: Run the complete host test gate**

Run:

```powershell
Set-Location -LiteralPath 'E:\code\folotoy-wangyiyun\netease-official-firmware\firmware\ai-passport'
& '.\tools\run_host_tests.ps1'
if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $LASTEXITCODE" }
```

Expected: all host tests pass.

- [ ] **Step 3: Activate ESP-IDF and build the new firmware**

Run:

```powershell
$env:IDF_TOOLS_PATH = 'C:\Users\User\.espressif'
$env:Path = "C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts;$env:Path"
. 'C:\esp\v5.5.4\esp-idf\export.ps1'
Set-Location -LiteralPath 'E:\code\folotoy-wangyiyun\netease-official-firmware\firmware\ai-passport'
idf.py --version
if ($LASTEXITCODE -ne 0) { throw "idf.py unavailable: $LASTEXITCODE" }
& '.\tools\validate_firmware.ps1'
if ($LASTEXITCODE -ne 0) { throw "Firmware validation failed: $LASTEXITCODE" }
```

Expected: one verified merged image with project name `FoloToy-NetEase-Official`.

- [ ] **Step 4: Recheck isolation and secret hygiene**

Verify old-tree hashes still match Task 1. Search tracked/source text and logs for the real AppID, rotated AppSecret, private-key body, access token, and refresh token; only the ignored local secrets header may contain credentials. Exclude build images from this text check because the approved development firmware necessarily embeds the signing material, but confirm no build image is added to version control.

- [ ] **Step 5: Ask the user before flashing**

Flashing changes the physical device and must not be inferred from a successful build. Report the image path and request explicit permission to flash the exact COM port.

- [ ] **Step 6: Run the live device checklist after approval**

Verify, in order: Wi-Fi provisioning, SNTP success, QR display, 801 waiting, 802 scanned, 803 login success, created/subscribed playlist merge, track-page navigation, MP3 playback, pause/resume, volume, reboot without login, switch account, and expired-token recovery. Record official error codes for any failure without logging secrets.

- [ ] **Step 7: Update records and deliver separate results**

Update `CONTEXT.md`, `docs/netease-official-api.md`, and the new README with actual verified endpoints and commands. Report exactly:

```text
Build: PASS / FAIL / NOT RUN
Host tests: PASS / FAIL / NOT RUN
Device tests: PASS / FAIL / NOT RUN
Unverified: remaining board, account-permission, or network checks
Old firmware/server source changes: 0
```

---

## Final Review Checklist

- Every spec requirement maps to Tasks 1 through 8.
- No old `firmware/` or `server/` file is modified by implementation.
- No new-tree CMake path escapes into the old firmware tree.
- No real secret appears in tracked files, logs, test fixtures, or documentation.
- All buffers have explicit sizes and all official errors remain distinguishable.
- The build result and physical-device result are reported separately.
- The user can rebuild and flash either old or official firmware from its own directory.
