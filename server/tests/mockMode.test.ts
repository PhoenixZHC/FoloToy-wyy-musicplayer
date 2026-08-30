import { createHash, createHmac } from "node:crypto";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, expect, test } from "vitest";
import { createApp } from "../src/app.js";
import type { AppConfig } from "../src/config.js";
import { openDb, type AppDatabase } from "../src/db.js";

const tempDirs: string[] = [];
const dbs: AppDatabase[] = [];
const deviceSecret = "secret_a_32_bytes_minimum_value_x";

afterEach(() => {
  for (const db of dbs.splice(0)) {
    db.close();
  }
  for (const dir of tempDirs.splice(0)) {
    rmSync(dir, { recursive: true, force: true });
  }
});

test("mock mode provides a local end-to-end cloud flow", async () => {
  const dir = mkdtempSync(join(tmpdir(), "folotoy-mock-mode-"));
  tempDirs.push(dir);
  const db = openDb(join(dir, "test.db"));
  dbs.push(db);
  const config: AppConfig = {
    port: 8080,
    databasePath: join(dir, "test.db"),
    cookieEncryptionKey: "12345678901234567890123456789012",
    deviceRegistrationToken: "pair-123",
    neteaseApiBaseUrl: "http://127.0.0.1:3000",
    mockNetease: true,
    localRelayMode: false,
    allowStaleDeviceTimestamps: true,
  };
  const app = createApp({ db, config });

  const register = await app.inject({
    method: "POST",
    url: "/api/v1/device/register",
    payload: {
      device_id: "dev_a",
      device_name: "AI-Passport-A",
      device_secret: deviceSecret,
      registration_code: "pair-123",
    },
  });
  expect(register.statusCode).toBe(200);

  const start = await app.inject({
    method: "POST",
    url: "/api/v1/auth/qr/start",
    headers: signedHeaders("POST", "/api/v1/auth/qr/start", "", 1),
  });
  expect(start.statusCode).toBe(200);
  expect(start.json().qr_payload).toContain("mock-login");
  const loginId = start.json().login_id as string;

  const status = await app.inject({
    method: "GET",
    url: `/api/v1/auth/qr/status?login_id=${loginId}`,
    headers: signedHeaders("GET", `/api/v1/auth/qr/status?login_id=${loginId}`),
  });
  expect(status.statusCode).toBe(200);
  expect(status.json().state).toBe("confirmed");

  const playlists = await app.inject({
    method: "GET",
    url: "/api/v1/me/playlists",
    headers: signedHeaders("GET", "/api/v1/me/playlists"),
  });
  expect(playlists.statusCode).toBe(200);
  expect(playlists.json()).toEqual({
    playlists: [
      { id: 1, name: "本地模拟歌单", track_count: 2, owner_name: "local" },
    ],
  });

  const tracks = await app.inject({
    method: "GET",
    url: "/api/v1/playlists/1/tracks?limit=10&offset=0",
    headers: signedHeaders("GET", "/api/v1/playlists/1/tracks?limit=10&offset=0"),
  });
  expect(tracks.statusCode).toBe(200);

  const body = JSON.stringify({ bitrate: 128000 });
  const trackUrl = await app.inject({
    method: "POST",
    url: "/api/v1/tracks/101/url",
    headers: {
      ...signedHeaders("POST", "/api/v1/tracks/101/url", body),
      "content-type": "application/json",
    },
    payload: body,
  });
  expect(trackUrl.statusCode).toBe(200);
  expect(trackUrl.json().url).toBe("http://localhost:80/mock/audio/101.mp3");
});

function signedHeaders(
  method: string,
  path: string,
  body = "",
  timestamp = Math.floor(Date.now() / 1000),
) {
  const bodyHash = createHash("sha256").update(body).digest("hex");
  const signatureInput = [method, path, bodyHash, String(timestamp)].join("\n");
  const signature = createHmac("sha256", deviceSecret)
    .update(signatureInput)
    .digest("hex");
  return {
    authorization: `Device dev_a:${signature}`,
    "x-device-timestamp": String(timestamp),
  };
}
