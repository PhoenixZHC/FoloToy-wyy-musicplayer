# Wangyiyun Cloud Player Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a cloud-assisted NetEase Cloud Music player for FoloToy AI Passport so each device can log in by QR code, browse the bound account's playlists, choose a song, and play it over Wi-Fi from anywhere with internet access.

**Architecture:** Keep `folotoy/ai-passport` as the firmware base. Add a small public cloud service that owns NetEase login/session work and exposes simple device APIs. The device only handles Wi-Fi, QR display, playlist/song UI, HTTP audio streaming, MP3 decoding, and ES8311 playback.

**Tech Stack:** ESP-IDF 5.5.x, ESP32-C3, LVGL, ES8311/I2S BSP from ai-passport, C firmware modules, Node.js 20 LTS, TypeScript, Fastify, SQLite for small deployments, Docker Compose, NetEase official Open Platform API first; temporary compatibility adapter only while official per-endpoint parameters are unavailable.

**Spec:** This plan implements the current user requirement: cloud server relay, multiple devices, device QR login, owned/favorited playlists, selectable songs, and on-device playback.

## Global Constraints

- Target hardware is ESP32-C3 with 8 MB Flash and no PSRAM.
- Do not create a branch or worktree unless the user explicitly approves.
- Do not store a NetEase account password on the device or in firmware.
- Firmware may be distributed publicly, but it must not contain any shared device secret, server admin secret, NetEase cookie, or NetEase account data.
- On first boot, each device generates its own `device_id` and `device_secret`, stores them in NVS, and registers with the cloud service through the Wi-Fi setup page.
- Device registration must require a user-provided server registration code so random internet clients cannot freely enroll devices.
- Use QR login as the first login method.
- Treat each physical device as an independent client with its own device token.
- Support multiple devices requesting the cloud service at the same time.
- Do not proxy full music audio through the cloud service in version 1; return the playable URL and let the device stream from the source URL.
- Do not implement lyrics, cover art, search, favorites editing, local music cache, or lossless playback in version 1.
- Device UI must work with UP, DOWN, and OK buttons only.
- Device firmware must surface login failure, expired QR code, empty playlist, unavailable song URL, network failure, and playback failure as visible states.
- Follow ai-passport rules: reusable board logic in `components/bsp`, app pages and state machines in `main`, LVGL access under lock, button callbacks non-blocking.

---

## File Structure

Create this repository layout:

```text
firmware/
  ai-passport/                         # upstream ai-passport firmware base
  components/netease_client/
    include/netease_client.h
    netease_client.c
  components/audio_stream/
    include/audio_stream.h
    audio_stream.c
    mp3_decoder_helix.c
    mp3_decoder_helix.h
  main/netease_app.c
  main/netease_app.h
  main/netease_app_logic.c
  main/netease_app_logic.h
  main/wifi_provisioning_page.c
  main/wifi_provisioning_page.h
server/
  package.json
  tsconfig.json
  Dockerfile
  docker-compose.yml
  .env.example
  src/app.ts
  src/config.ts
  src/db.ts
  src/routes/deviceRoutes.ts
  src/routes/neteaseRoutes.ts
  src/services/deviceAuth.ts
  src/services/neteaseApi.ts
  src/services/sessionStore.ts
  src/services/rateLimit.ts
  src/types.ts
  migrations/001_initial.sql
  tests/deviceAuth.test.ts
  tests/neteaseRoutes.test.ts
  tests/multiDevice.test.ts
docs/
  deployment.md
  device-flow.md
```

Responsibility boundaries:

- `server/src/services/neteaseApi.ts`: NetEase API adapter. Prefer official Open Platform endpoints; temporary compatibility code must stay isolated here.
- `server/src/services/sessionStore.ts`: stores encrypted NetEase cookies per device.
- `server/src/services/deviceAuth.ts`: verifies device tokens and request signatures.
- `server/src/routes/*`: exposes stable device-facing HTTP JSON APIs.
- `firmware/components/netease_client`: cloud API client with no UI code.
- `firmware/components/audio_stream`: HTTP MP3 stream, decoder, and PCM output.
- `firmware/main/netease_app.c`: playlist/song/playback UI.
- `firmware/main/netease_app_logic.c`: button-driven UI state machine.
- `firmware/main/wifi_provisioning_page.c`: Wi-Fi setup screen and captive portal/BLE provisioning entry.

## Server API Contract

All device APIs use HTTPS in production. `GET /health` and first-time `POST /api/v1/device/register` do not use device HMAC. Every other `/api/v1/*` endpoint requires:

```http
Authorization: Device <device_id>:<signature>
X-Device-Timestamp: <unix_seconds>
```

Signature input:

```text
METHOD + "\n" + PATH_WITH_QUERY + "\n" + SHA256_BODY_HEX + "\n" + X-Device-Timestamp
```

Signature algorithm:

```text
HMAC-SHA256(device_secret, signature_input)
```

Endpoints:

```text
GET  /health
POST /api/v1/device/register
POST /api/v1/device/hello
POST /api/v1/auth/qr/start
GET  /api/v1/auth/qr/status?login_id=<id>
POST /api/v1/auth/logout
GET  /api/v1/me/playlists
GET  /api/v1/playlists/:playlist_id/tracks?limit=30&offset=0
POST /api/v1/tracks/:track_id/url
```

`POST /api/v1/device/register` body:

```json
{
  "device_id": "dev_01J7X4Y6A6ZV9B5EJ4FA",
  "device_name": "AI-Passport-1234",
  "device_secret": "32-or-more-random-bytes-base64",
  "registration_code": "user-entered-code"
}
```

Success response:

```json
{
  "device_id": "dev_01J7X4Y6A6ZV9B5EJ4FA",
  "registered": true
}
```

Registration rules:

```text
The firmware generates device_id and device_secret locally; they are not compiled into the public firmware image.
The registration request is allowed only over HTTPS in production.
The server accepts registration only when registration_code matches the server-side DEVICE_REGISTRATION_TOKEN.
The server rejects duplicate device_id values.
The server stores only encrypted_secret, never a plain device_secret.
After registration, the device uses HMAC-SHA256(device_secret, signature_input) for every protected API.
```

`POST /api/v1/auth/qr/start` returns:

```json
{
  "login_id": "qr_01J7X4Y6A6ZV9B5EJ4FA",
  "qr_payload": "https://music.163.com/login?...",
  "expires_in_seconds": 180
}
```

`GET /api/v1/auth/qr/status` returns one of:

```json
{ "state": "waiting" }
{ "state": "scanned" }
{ "state": "confirmed", "netease_user_id": 123456, "nickname": "name" }
{ "state": "expired" }
{ "state": "failed", "message": "login rejected" }
```

`GET /api/v1/me/playlists` returns:

```json
{
  "playlists": [
    { "id": 123, "name": "我喜欢的音乐", "track_count": 88, "owner_name": "name" }
  ]
}
```

`GET /api/v1/playlists/:playlist_id/tracks` returns:

```json
{
  "playlist_id": 123,
  "tracks": [
    { "id": 456, "name": "song", "artist": "artist", "duration_ms": 210000 }
  ],
  "next_offset": 30
}
```

`POST /api/v1/tracks/:track_id/url` body:

```json
{ "bitrate": 128000 }
```

Success response:

```json
{
  "track_id": 456,
  "url": "https://...",
  "format": "mp3",
  "expires_in_seconds": 900
}
```

Unavailable response:

```json
{
  "error": "track_unavailable",
  "message": "这首歌当前无法播放"
}
```

## Database Schema

Use SQLite first. It is enough for a personal cloud service with many devices.

```sql
CREATE TABLE devices (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  encrypted_secret TEXT NOT NULL,
  disabled INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  last_seen_at TEXT
);

CREATE TABLE netease_sessions (
  device_id TEXT PRIMARY KEY REFERENCES devices(id),
  netease_user_id TEXT NOT NULL,
  nickname TEXT NOT NULL,
  encrypted_cookie TEXT NOT NULL,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE qr_login_sessions (
  login_id TEXT PRIMARY KEY,
  device_id TEXT NOT NULL REFERENCES devices(id),
  qr_key TEXT NOT NULL,
  state TEXT NOT NULL,
  created_at TEXT NOT NULL,
  expires_at TEXT NOT NULL
);

CREATE TABLE request_log (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id TEXT NOT NULL,
  path TEXT NOT NULL,
  status_code INTEGER NOT NULL,
  created_at TEXT NOT NULL
);
```

## Task 1: Server Project Skeleton

**Files:**
- Create: `server/package.json`
- Create: `server/tsconfig.json`
- Create: `server/src/app.ts`
- Create: `server/src/config.ts`
- Create: `server/src/types.ts`
- Create: `server/tests/health.test.ts`

**Interfaces:**
- Produces: `createApp(): FastifyInstance`
- Produces: `loadConfig(): AppConfig`

- [x] **Step 1: Write the failing health test**

```ts
import { createApp } from "../src/app";

test("GET /health returns ok", async () => {
  const app = createApp();
  const res = await app.inject({ method: "GET", url: "/health" });
  expect(res.statusCode).toBe(200);
  expect(res.json()).toEqual({ ok: true });
});
```

- [x] **Step 2: Run the test and confirm it fails**

Run:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\server"
npm test -- health.test.ts
```

Expected: the test fails because `createApp` does not exist yet.

- [x] **Step 3: Implement the minimal Fastify app**

Create `server/src/app.ts`:

```ts
import Fastify from "fastify";

export function createApp() {
  const app = Fastify({ logger: true });
  app.get("/health", async () => ({ ok: true }));
  return app;
}
```

Create `server/src/config.ts`:

```ts
export type AppConfig = {
  port: number;
  databasePath: string;
  cookieEncryptionKey: string;
  neteaseApiBaseUrl: string;
};

export function loadConfig(): AppConfig {
  return {
    port: Number(process.env.PORT ?? "8080"),
    databasePath: process.env.DATABASE_PATH ?? "./data/app.db",
    cookieEncryptionKey: process.env.COOKIE_ENCRYPTION_KEY ?? "",
    neteaseApiBaseUrl: process.env.NETEASE_API_BASE_URL ?? "",
  };
}
```

- [x] **Step 4: Run the health test again**

Run:

```powershell
npm test -- health.test.ts
```

Expected: 1 passed, 0 failed.

- [x] **Step 5: Review**

Check that the server can start without touching NetEase code and `/health` has no auth requirement.

## Task 2: Database and Session Storage

**Files:**
- Create: `server/migrations/001_initial.sql`
- Create: `server/src/db.ts`
- Create: `server/src/services/sessionStore.ts`
- Create: `server/tests/sessionStore.test.ts`

**Interfaces:**
- Produces: `openDb(path: string): Database`
- Produces: `saveNeteaseSession(deviceId, session): Promise<void>`
- Produces: `getNeteaseSession(deviceId): Promise<NeteaseSession | null>`
- Consumes: `AppConfig.databasePath`

- [x] **Step 1: Write session storage tests**

```ts
test("stores and reads a NetEase session by device id", async () => {
  const store = createTestSessionStore();
  await store.saveNeteaseSession("dev_a", {
    neteaseUserId: "1001",
    nickname: "alice",
    cookie: "MUSIC_U=abc"
  });
  await expect(store.getNeteaseSession("dev_a")).resolves.toEqual({
    neteaseUserId: "1001",
    nickname: "alice",
    cookie: "MUSIC_U=abc"
  });
});

test("returns null when device has no NetEase session", async () => {
  const store = createTestSessionStore();
  await expect(store.getNeteaseSession("missing")).resolves.toBeNull();
});
```

- [x] **Step 2: Run tests and confirm failure**

Run:

```powershell
npm test -- sessionStore.test.ts
```

Expected: failure because the store does not exist.

- [x] **Step 3: Implement SQLite migration and store**

Use the schema in the Database Schema section. Encrypt `cookie` before writing `encrypted_cookie`. Use AES-256-GCM with a 32-byte key from `COOKIE_ENCRYPTION_KEY`. Device secrets are encrypted before writing `devices.encrypted_secret`; plain device secrets must not be stored in SQLite.

- [x] **Step 4: Run storage tests**

Run:

```powershell
npm test -- sessionStore.test.ts
```

Expected: session storage tests pass.

- [x] **Step 5: Review**

Check that plain NetEase cookies and plain device secrets are never stored in SQLite.

## Task 3: Device Authentication and Multi-Device Isolation

**Files:**
- Create: `server/src/services/deviceAuth.ts`
- Create: `server/src/routes/deviceRoutes.ts`
- Create: `server/tests/deviceAuth.test.ts`
- Create: `server/tests/multiDevice.test.ts`

**Interfaces:**
- Produces: `verifyDeviceRequest(req): Promise<DeviceIdentity>`
- Produces: `registerDevice(input): Promise<DeviceIdentity>`
- Produces: `POST /api/v1/device/register`
- Produces: `POST /api/v1/device/hello`
- Consumes: `devices.encrypted_secret`
- Consumes: `DEVICE_REGISTRATION_TOKEN`

- [x] **Step 1: Write first-time registration tests**

```ts
test("registers a new public-firmware device with a valid registration code", async () => {
  const app = await createTestApp({ registrationCode: "pair-123" });
  const res = await app.inject({
    method: "POST",
    url: "/api/v1/device/register",
    payload: {
      device_id: "dev_a",
      device_name: "AI-Passport-A",
      device_secret: "secret_a_32_bytes_minimum_value",
      registration_code: "pair-123"
    }
  });
  expect(res.statusCode).toBe(200);
  expect(res.json()).toEqual({ device_id: "dev_a", registered: true });
});

test("rejects registration with a wrong registration code", async () => {
  const app = await createTestApp({ registrationCode: "pair-123" });
  const res = await app.inject({
    method: "POST",
    url: "/api/v1/device/register",
    payload: {
      device_id: "dev_a",
      device_name: "AI-Passport-A",
      device_secret: "secret_a_32_bytes_minimum_value",
      registration_code: "wrong"
    }
  });
  expect(res.statusCode).toBe(401);
});
```

- [x] **Step 2: Write signature tests**

```ts
test("accepts a valid signed device request", async () => {
  const result = await verifySignedFixture({
    deviceId: "dev_a",
    secret: "secret_a",
    method: "POST",
    path: "/api/v1/device/hello",
    body: "{}"
  });
  expect(result.deviceId).toBe("dev_a");
});

test("rejects a request signed with another device secret", async () => {
  await expect(verifySignedFixture({
    deviceId: "dev_a",
    secret: "secret_b",
    method: "POST",
    path: "/api/v1/device/hello",
    body: "{}"
  })).rejects.toThrow("invalid device signature");
});
```

- [x] **Step 3: Write multi-device isolation test**

```ts
test("device A cannot read device B session", async () => {
  const app = await createTestAppWithTwoDevices();
  await saveSessionForDevice("dev_b", "MUSIC_U=b");
  const res = await signedInject(app, "dev_a", "GET", "/api/v1/me/playlists");
  expect(res.statusCode).toBe(401);
  expect(res.json().error).toBe("netease_not_logged_in");
});
```

- [x] **Step 4: Run tests and confirm failure**

Run:

```powershell
npm test -- deviceAuth.test.ts multiDevice.test.ts
```

Expected: failure because auth middleware does not exist.

- [x] **Step 5: Implement registration and auth middleware**

Rules:

```text
Allow unsigned POST /api/v1/device/register only for first-time registration.
Reject invalid registration_code.
Reject duplicate device_id registration.
Reject blank or too-short device_secret.
Encrypt device_secret before writing devices.encrypted_secret.
Reject missing Authorization.
Reject unknown device id.
Reject disabled device.
Reject timestamp older than 300 seconds.
Decrypt encrypted_secret and reject HMAC mismatch.
Attach device_id to the request context after success.
Never log device_secret, encrypted_secret, NetEase cookie, or registration_code.
```

- [x] **Step 6: Run auth tests**

Run:

```powershell
npm test -- deviceAuth.test.ts multiDevice.test.ts
```

Expected: registration, signature, and multi-device isolation tests pass.

- [x] **Step 7: Review**

Check every `/api/v1/*` route except `/health` and `/api/v1/device/register` uses device authentication.

## Task 4: NetEase API Wrapper

**Files:**
- Create: `server/src/services/neteaseApi.ts`
- Create: `server/tests/neteaseApi.test.ts`

**Interfaces:**
- Produces: `createQrLogin(): Promise<{ loginIdData: { qrKey: string; qrPayload: string; expiresInSeconds: number } }>`
- Produces: `checkQrLogin(qrKey): Promise<QrLoginState>`
- Produces: `getUserPlaylists(cookie): Promise<PlaylistSummary[]>`
- Produces: `getPlaylistTracks(cookie, playlistId, limit, offset): Promise<TrackPage>`
- Produces: `getTrackUrl(cookie, trackId, bitrate): Promise<TrackUrlResult>`
- Consumes: `NETEASE_API_BASE_URL`

- [x] **Step 1: Write wrapper tests with mocked HTTP responses**

```ts
test("maps NetEase QR waiting response", async () => {
  mockNetease.get("/login/qr/check").reply(200, { code: 801, message: "waiting" });
  await expect(api.checkQrLogin("key_a")).resolves.toEqual({ state: "waiting" });
});

test("maps playlist response to compact device shape", async () => {
  mockNetease.get("/user/playlist").reply(200, {
    playlist: [{ id: 1, name: "我喜欢的音乐", trackCount: 3, creator: { nickname: "me" } }]
  });
  await expect(api.getUserPlaylists("MUSIC_U=a")).resolves.toEqual([
    { id: 1, name: "我喜欢的音乐", trackCount: 3, ownerName: "me" }
  ]);
});
```

- [x] **Step 2: Run wrapper tests and confirm failure**

Run:

```powershell
npm test -- neteaseApi.test.ts
```

Expected: failure because wrapper does not exist.

- [x] **Step 3: Implement wrapper**

The first implementation used these temporary compatibility endpoints. Replace them with official Open Platform endpoints after confirming the matching official document parameter tables:

```text
/login/qr/key
/login/qr/create
/login/qr/check
/user/playlist
/playlist/track/all
/song/url
```

Pass the saved cookie on routes that require login.

- [x] **Step 4: Run wrapper tests**

Run:

```powershell
npm test -- neteaseApi.test.ts
```

Expected: 2 passed, 0 failed.

- [x] **Step 5: Review**

Check that raw NetEase responses are not returned directly to devices. Device JSON must stay small.

## Task 5: Device-Facing NetEase Routes

**Files:**
- Create: `server/src/routes/neteaseRoutes.ts`
- Modify: `server/src/app.ts`
- Create: `server/tests/neteaseRoutes.test.ts`

**Interfaces:**
- Produces: all endpoints listed in Server API Contract.
- Consumes: `verifyDeviceRequest`, `neteaseApi`, `sessionStore`.

- [x] **Step 1: Write route tests**

```ts
test("starts QR login for a signed device", async () => {
  const app = await createSignedTestApp();
  const res = await signedInject(app, "dev_a", "POST", "/api/v1/auth/qr/start");
  expect(res.statusCode).toBe(200);
  expect(res.json()).toMatchObject({
    login_id: expect.any(String),
    qr_payload: expect.any(String),
    expires_in_seconds: expect.any(Number)
  });
});

test("requires login before returning playlists", async () => {
  const app = await createSignedTestApp();
  const res = await signedInject(app, "dev_a", "GET", "/api/v1/me/playlists");
  expect(res.statusCode).toBe(401);
  expect(res.json().error).toBe("netease_not_logged_in");
});
```

- [x] **Step 2: Run route tests and confirm failure**

Run:

```powershell
npm test -- neteaseRoutes.test.ts
```

Expected: failure because routes are not registered.

- [x] **Step 3: Implement routes**

Route behavior:

```text
QR confirmed: save cookie under the authenticated device id.
QR expired: keep existing session unchanged.
Logout: delete only this device's NetEase session.
Playlists: return only compact playlist fields.
Tracks: paginate with limit max 50.
Track URL: request 128000 bitrate first; return track_unavailable if URL is empty.
```

- [x] **Step 4: Run route tests**

Run:

```powershell
npm test -- neteaseRoutes.test.ts
```

Expected: all route tests pass.

- [x] **Step 5: Review**

Check that one device cannot access another device's QR login state or NetEase cookie.

## Task 6: Server Packaging and Deployment

**Files:**
- Create: `server/Dockerfile`
- Create: `server/docker-compose.yml`
- Create: `server/.env.example`
- Create: `docs/deployment.md`

**Interfaces:**
- Produces: `docker compose up -d`
- Consumes: public HTTPS reverse proxy in front of the Node service.

- [x] **Step 1: Write deployment smoke script**

Add a documented smoke check:

```powershell
curl.exe -f "https://your-domain.example/health"
```

Expected response:

```json
{ "ok": true }
```

- [x] **Step 2: Create Docker packaging**

Docker rules:

```text
Build TypeScript during image build.
Run as non-root user.
Mount SQLite data at /data.
Expose port 8080.
Set NODE_ENV=production.
```

- [x] **Step 3: Create deployment doc**

`docs/deployment.md` must include:

```text
Minimum server: 1 vCPU, 1 GB RAM.
Required services: Node app, NetEase official Open Platform API gateway or temporary compatibility adapter, HTTPS reverse proxy.
Required env vars: DATABASE_PATH, COOKIE_ENCRYPTION_KEY, DEVICE_REGISTRATION_TOKEN, NETEASE_API_BASE_URL.
Backups: copy SQLite database after stopping the service.
Audio flow: device streams audio URL directly; server does not relay full music files.
```

- [x] **Step 4: Run local package check**

Run:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\server"
npm run build
docker compose config
```

Expected: build passes and Docker Compose config is valid.

- [x] **Step 5: Review**

Check `.env.example` contains no real secrets.

## Task 7: Firmware Import and Build Baseline

**Files:**
- Create or import: `firmware/ai-passport/`
- Create: `firmware/README.md`
- Modify after import: `firmware/ai-passport/main/main.c`

**Interfaces:**
- Produces: an unchanged ai-passport firmware baseline that builds before NetEase work starts.

- [x] **Step 1: Import ai-passport**

Use the upstream ai-passport source as the firmware base. Do not change behavior in this task.

- [x] **Step 2: Activate ESP-IDF**

Run in PowerShell:

```powershell
$env:IDF_TOOLS_PATH = "C:\Users\User\.espressif"
$env:Path = "C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts;$env:Path"
. "C:\esp\v5.5.4\esp-idf\export.ps1"
```

- [x] **Step 3: Build baseline**

Run:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\firmware\ai-passport"
idf.py --version
idf.py build
```

Expected: firmware build passes before feature work.

- [x] **Step 4: Review**

Check that the imported baseline still targets ESP32-C3, 8 MB Flash, and no PSRAM.

## Task 8: Firmware Cloud Client

**Files:**
- Create: `firmware/components/netease_client/include/netease_client.h`
- Create: `firmware/components/netease_client/netease_client.c`
- Create: `firmware/components/netease_client/idf_component.yml`
- Create: `firmware/ai-passport/tests/test_netease_client.c`

**Interfaces:**
- Produces: `netease_client_hello`
- Produces: `netease_client_qr_start`
- Produces: `netease_client_qr_status`
- Produces: `netease_client_get_playlists`
- Produces: `netease_client_get_tracks`
- Produces: `netease_client_get_track_url`
- Consumes: server API contract.

- [x] **Step 1: Write parser tests**

Test compact JSON parsing, not live network:

```c
TEST_CASE("parse playlists response", "[netease_client]") {
    const char *json = "{\"playlists\":[{\"id\":1,\"name\":\"Liked\",\"track_count\":2,\"owner_name\":\"me\"}]}";
    netease_playlist_list_t out = {0};
    TEST_ASSERT_EQUAL(ESP_OK, netease_parse_playlists(json, &out));
    TEST_ASSERT_EQUAL(1, out.items[0].id);
    TEST_ASSERT_EQUAL_STRING("Liked", out.items[0].name);
}
```

- [x] **Step 2: Run host/component tests**

Run:

```powershell
idf.py build
```

Expected: failure until the component exists.

- [x] **Step 3: Implement client**

Implementation rules:

```text
Use esp_http_client.
Use cJSON.
Limit playlist page to 20 visible items on device.
Limit tracks page to 30 visible items on device.
Store strings in fixed-size buffers.
Reject responses larger than a fixed cap.
Return explicit esp_err_t failures.
```

- [x] **Step 4: Run firmware build**

Run:

```powershell
idf.py build
```

Expected: build passes.

- [x] **Step 5: Review**

Check no NetEase password, cookie, or cloud secret is printed in logs.

## Task 9: Wi-Fi Provisioning for Anywhere Use

**Files:**
- Create: `firmware/main/wifi_provisioning_page.c`
- Create: `firmware/main/wifi_provisioning_page.h`
- Modify: `firmware/ai-passport/main/main.c`

**Interfaces:**
- Produces: UI state `WIFI_NEEDS_SETUP`, `WIFI_CONNECTING`, `WIFI_CONNECTED`, `WIFI_FAILED`.
- Consumes: ESP-IDF Wi-Fi station APIs and NVS.

- [x] **Step 1: Add a Wi-Fi setup page testable state machine**

States:

```text
No saved Wi-Fi -> show setup instructions.
Saved Wi-Fi -> connect automatically.
Connection failed -> show retry and setup options.
OK click -> retry.
OK long press -> clear saved Wi-Fi and enter setup mode.
```

- [x] **Step 2: Implement provisioning**

Preferred first version:

```text
Device starts SoftAP named AI-Passport-XXXX.
Screen shows SSID and setup URL.
User connects by phone and enters home/hotspot Wi-Fi credentials, cloud server URL, and server registration code.
Device generates device_id and device_secret if they do not exist yet.
Device calls POST /api/v1/device/register over HTTPS.
Device stores Wi-Fi credentials, cloud server URL, device_id, and device_secret in NVS.
Device switches to station mode and connects.
```

Local test shortcut:

```text
If Wi-Fi SSID is left empty, the device skips station connection and registers
against the cloud URL reachable from the setup SoftAP network. This is for local
automation only; real distribution still uses a normal 2.4 GHz Wi-Fi SSID.
```

- [x] **Step 3: Build**

Run:

```powershell
idf.py build
```

Expected: build passes.

- [x] **Step 4: Device test**

Manual test:

```text
Boot with no saved Wi-Fi.
Connect phone to setup AP.
Submit Wi-Fi credentials, cloud server URL, and registration code.
Device joins the Wi-Fi.
Device registers once with the cloud service.
Reboot device.
Device reconnects automatically.
Device does not register again unless local device identity is cleared.
```

2026-08-28 local result: COM14 was flashed successfully. With the PC connected
to the device SoftAP and `MOCK_NETEASE=true`,
`ALLOW_STALE_DEVICE_TIMESTAMPS=true`, and Cloud URL
`http://192.168.4.2:18080`, the device completed registration, hello, QR start,
QR confirmed, playlists, tracks, and track URL requests against the local mock
cloud service. Real 2.4 GHz Wi-Fi reboot auto-reconnect remains for the formal
NetEase UI/playback pass.

- [x] **Step 5: Review**

Check the button callback only posts events and does not block on network work.

## Task 10: Standalone NetEase Device App

**Files:**
- Create: `firmware/ai-passport/main/netease_app_logic.c`
- Create: `firmware/ai-passport/main/netease_app_logic.h`
- Create: `firmware/ai-passport/main/netease_app.c`
- Create: `firmware/ai-passport/main/netease_app.h`
- Modify: `firmware/ai-passport/main/main.c`

**Interfaces:**
- Produces: page states `LOGIN_QR`, `LOGIN_WAITING`, `PLAYLISTS`, `TRACKS`, `PLAYING`, `ERROR`.
- Consumes: `netease_client_*`.
- Firmware is a standalone device app, not an app inside the upstream demo menu.
- Boot flow: no saved cloud config -> Cloud setup page; saved cloud config -> NetEase app.

- [x] **Step 1: Define button behavior**

```text
UP click: previous item.
DOWN click: next item.
OK click on playlist: open selected playlist.
OK click on song: play selected song.
OK click while playing: pause/resume.
OK long press: go back one screen.
UP long press while playing: previous song in current list.
DOWN long press while playing: next song in current list.
```

- [x] **Step 2: Add QR rendering**

Render `qr_payload` from the server as a QR code on the 240 x 320 display. Keep text short:

```text
网易云扫码登录
打开网易云 App 扫码
```

- [x] **Step 3: Add playlist and track screens**

Display rules:

```text
Show 5 to 7 rows per screen.
Truncate long names with ellipsis.
Show selected row with inverted color.
Show loading and error states as text.
Do not show cover images in version 1.
```

- [x] **Step 4: Build**

Run:

```powershell
idf.py build
```

Expected: build passes.

- [ ] **Step 5: Device test**

Manual test:

```text
Enter NetEase Music page.
Scan QR with NetEase App.
See playlists.
Open a playlist.
See songs.
Back navigation works.
```

2026-08-28 local result: QR-enabled `FoloToy-Wangyiyun.bin` builds and was
flashed to COM14. Boot logs confirm project name `FoloToy-Wangyiyun` and the
standalone app entry. Real 2.4 GHz Wi-Fi with real NetEase QR scanning still
needs a manual pass.

- [x] **Step 6: Review**

Check every LVGL object access outside the LVGL task uses `bsp_lvgl_lock()`.

## Task 11: Streaming MP3 Playback

**Files:**
- Create: `firmware/components/audio_stream/include/audio_stream.h`
- Create: `firmware/components/audio_stream/audio_stream.c`
- Create: `firmware/components/audio_stream/mp3_decoder_helix.c`
- Create: `firmware/components/audio_stream/mp3_decoder_helix.h`
- Modify: `firmware/ai-passport/main/netease_app.c`

**Interfaces:**
- Produces: `audio_stream_start(url)`
- Produces: `audio_stream_pause()`
- Produces: `audio_stream_resume()`
- Produces: `audio_stream_stop()`
- Consumes: `bsp_audio_set_format`, `bsp_audio_write`.

- [x] **Step 1: Write decoder boundary tests**

Test that the stream module rejects unsupported inputs cleanly:

```c
TEST_CASE("audio stream rejects empty url", "[audio_stream]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, audio_stream_start(""));
}
```

- [x] **Step 2: Implement stream worker**

Rules:

```text
Use one download/decode/playback worker task.
Use a 16 KB to 32 KB compressed audio buffer.
Do not allocate PSRAM.
Skip ID3 tags before MP3 frame sync.
Decode MP3 frames to 16-bit PCM.
Downmix stereo to mono for version 1.
Call bsp_audio_set_format(sample_rate, 16, 1) when sample rate changes.
Write PCM with bsp_audio_write.
Stop cleanly before leaving the music page.
```

- [x] **Step 3: Connect UI to playback**

Flow:

```text
Selected song -> request /tracks/:id/url -> if URL exists, call audio_stream_start(url).
If URL is empty, show "这首歌当前无法播放".
If playback ends, advance to the next song in the current track list.
```

- [x] **Step 4: Build**

Run:

```powershell
idf.py build
```

Expected: build passes.

- [ ] **Step 5: Device test**

Manual test:

```text
Play one song for 60 seconds.
Pause and resume.
Skip to next song.
Leave the page while playing.
Confirm audio stops and UI remains responsive.
```

2026-08-28 local result: Added `firmware/components/audio_stream/` with HTTP
MP3 streaming, Helix decode, mono PCM output, pause/resume/stop APIs, and
NetEase UI playback wiring. Host boundary tests pass. Firmware builds and was
flashed to COM14. Startup log confirms the app boots and ES8311 initializes.
Real NetEase QR login plus 60-second playback/pause/skip/page-exit testing is
still pending.

- [ ] **Step 6: Review**

Check heap usage during playback. If free heap drops too low, reduce compressed buffer before adding features.

## Task 12: End-to-End Multi-Device Test

**Files:**
- Create: `server/tests/multiDevice.e2e.test.ts`
- Create: `docs/device-flow.md`

**Interfaces:**
- Consumes: server deployment and firmware behavior.
- Produces: verified flow for at least two devices.

- [ ] **Step 1: Add server multi-device e2e test**

Test flow:

```text
Device A and Device B start from the same public firmware image.
Device A generates its own device_id and device_secret, then registers.
Device B generates its own device_id and device_secret, then registers.
Device A starts QR login and confirms as account A.
Device B starts QR login and confirms as account B.
Device A playlists return account A playlists.
Device B playlists return account B playlists.
Device A cannot poll Device B login id.
Concurrent track URL requests return independently.
```

- [ ] **Step 2: Run server tests**

Run:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\server"
npm test
```

Expected: all server tests pass.

- [ ] **Step 3: Run firmware build**

Run:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\firmware\ai-passport"
idf.py build
```

Expected: firmware build passes.

- [ ] **Step 4: Manual device test**

Use two devices or one device with two flashed device identities:

```text
Bind device A.
Bind device B.
Confirm both can browse their own playlists.
Start playback on A.
Start playlist browsing on B.
Confirm A playback is not interrupted by B requests.
```

- [ ] **Step 5: Document the user flow**

`docs/device-flow.md` must include:

```text
First boot Wi-Fi setup.
Cloud server URL setup.
Device registration with a server registration code.
QR login.
Playlist browsing.
Song playback.
Logout and re-login.
Common errors and user-visible messages.
```

- [ ] **Step 6: Review**

Check the plan's first release still excludes lyrics, cover art, search, favorites editing, local cache, and lossless playback.

## Final Validation Gate

Run:

```powershell
Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\server"
npm test
npm run build
docker compose config

Set-Location -LiteralPath "E:\code\folotoy-wangyiyun\firmware\ai-passport"
$env:IDF_TOOLS_PATH = "C:\Users\User\.espressif"
$env:Path = "C:\Users\User\.espressif\python_env\idf5.5_py3.13_env\Scripts;$env:Path"
. "C:\esp\v5.5.4\esp-idf\export.ps1"
idf.py build
```

Expected result:

```text
Server tests: all passed.
Server build: passed.
Docker config: valid.
Firmware build: passed.
Device tests: QR login, playlist browsing, song playback, pause/resume, skip, page exit cleanup passed.
```

## Self-Review

- The plan covers cloud relay, QR login, multiple devices, account-bound playlists, track selection, and on-device playback.
- The first release deliberately avoids lyrics, covers, search, editing favorites, local cache, and lossless playback.
- The cloud service supports multiple devices by using per-device tokens and per-device NetEase sessions.
- The public firmware contains no shared device secret; each device generates and registers its own secret on first boot.
- The device does not store NetEase passwords or cookies.
- The audio design respects ESP32-C3 memory limits by using small buffers and no PSRAM.
- The server does not proxy full songs in version 1, reducing cloud traffic and cost.
