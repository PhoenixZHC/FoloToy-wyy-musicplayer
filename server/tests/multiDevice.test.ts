import { createHash, createHmac } from "node:crypto";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, test } from "vitest";
import { createApp } from "../src/app.js";
import type { AppConfig } from "../src/config.js";
import { openDb, type AppDatabase } from "../src/db.js";
import { createSessionStore } from "../src/services/sessionStore.js";

const tempDirs: string[] = [];
const dbs: AppDatabase[] = [];
const encryptionKey = "12345678901234567890123456789012";
const deviceSecretA = "secret_a_32_bytes_minimum_value_x";
const deviceSecretB = "secret_b_32_bytes_minimum_value_x";

function createTestApp() {
  const dir = mkdtempSync(join(tmpdir(), "folotoy-multi-"));
  tempDirs.push(dir);
  const db = openDb(join(dir, "test.db"));
  dbs.push(db);

  const config: AppConfig = {
    port: 8080,
    databasePath: join(dir, "test.db"),
    cookieEncryptionKey: encryptionKey,
    deviceRegistrationToken: "pair-123",
    neteaseApiBaseUrl: "http://127.0.0.1:3000",
    mockNetease: false,
    localRelayMode: false,
    allowStaleDeviceTimestamps: false,
  };

  return {
    app: createApp({ db, config }),
    db,
    store: createSessionStore(db, encryptionKey),
  };
}

async function registerDevice(
  app: ReturnType<typeof createApp>,
  deviceId: string,
  secret: string,
) {
  await app.inject({
    method: "POST",
    url: "/api/v1/device/register",
    payload: {
      device_id: deviceId,
      device_name: `AI-Passport-${deviceId}`,
      device_secret: secret,
      registration_code: "pair-123",
    },
  });
}

function signedHeaders(input: {
  deviceId: string;
  secret: string;
  method: string;
  path: string;
  body?: string;
}) {
  const body = input.body ?? "";
  const timestamp = Math.floor(Date.now() / 1000);
  const bodyHash = createHash("sha256").update(body).digest("hex");
  const signatureInput = [
    input.method,
    input.path,
    bodyHash,
    String(timestamp),
  ].join("\n");
  const signature = createHmac("sha256", input.secret)
    .update(signatureInput)
    .digest("hex");

  return {
    authorization: `Device ${input.deviceId}:${signature}`,
    "x-device-timestamp": String(timestamp),
  };
}

afterEach(() => {
  for (const db of dbs.splice(0)) {
    db.close();
  }
  for (const dir of tempDirs.splice(0)) {
    rmSync(dir, { recursive: true, force: true });
  }
});

describe("multi-device isolation", () => {
  test("device A cannot read device B session", async () => {
    const { app, store } = createTestApp();
    await registerDevice(app, "dev_a", deviceSecretA);
    await registerDevice(app, "dev_b", deviceSecretB);
    await store.saveNeteaseSession("dev_b", {
      neteaseUserId: "2002",
      nickname: "bob",
      cookie: "MUSIC_U=b",
    });

    const res = await app.inject({
      method: "GET",
      url: "/api/v1/me/playlists",
      headers: signedHeaders({
        deviceId: "dev_a",
        secret: deviceSecretA,
        method: "GET",
        path: "/api/v1/me/playlists",
      }),
    });

    expect(res.statusCode).toBe(401);
    expect(res.json().error).toBe("netease_not_logged_in");
  });
});
