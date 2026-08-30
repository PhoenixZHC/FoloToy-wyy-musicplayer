import { createHmac, createHash } from "node:crypto";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, test } from "vitest";
import { createApp } from "../src/app.js";
import { openDb, type AppDatabase } from "../src/db.js";
import type { AppConfig } from "../src/config.js";
import { validateConfig } from "../src/config.js";

const tempDirs: string[] = [];
const dbs: AppDatabase[] = [];
const encryptionKey = "12345678901234567890123456789012";
const deviceSecretA = "secret_a_32_bytes_minimum_value_x";
const deviceSecretB = "secret_b_32_bytes_minimum_value_x";

function createTestApp(options: { registrationCode?: string } = {}) {
  const dir = mkdtempSync(join(tmpdir(), "folotoy-auth-"));
  tempDirs.push(dir);
  const db = openDb(join(dir, "test.db"));
  dbs.push(db);

  const config: AppConfig = {
    port: 8080,
    databasePath: join(dir, "test.db"),
    cookieEncryptionKey: encryptionKey,
    deviceRegistrationToken: options.registrationCode ?? "pair-123",
    neteaseApiBaseUrl: "http://127.0.0.1:3000",
    mockNetease: false,
    localRelayMode: false,
    allowStaleDeviceTimestamps: false,
  };

  return { app: createApp({ db, config }), db };
}

function signRequest(input: {
  secret: string;
  method: string;
  path: string;
  body?: string;
  timestamp?: number;
}) {
  const body = input.body ?? "";
  const timestamp = input.timestamp ?? Math.floor(Date.now() / 1000);
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

  return { signature, timestamp };
}

async function registerDevice(
  app: ReturnType<typeof createApp>,
  deviceId: string,
  secret: string,
) {
  return app.inject({
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

afterEach(async () => {
  for (const db of dbs.splice(0)) {
    db.close();
  }
  for (const dir of tempDirs.splice(0)) {
    rmSync(dir, { recursive: true, force: true });
  }
});

describe("device auth", () => {
  test("rejects unsafe production config without a registration token", () => {
    expect(() =>
      validateConfig({
        port: 8080,
        databasePath: "./data/app.db",
        cookieEncryptionKey: encryptionKey,
        deviceRegistrationToken: "",
        neteaseApiBaseUrl: "http://127.0.0.1:3000",
        mockNetease: false,
        localRelayMode: false,
        allowStaleDeviceTimestamps: false,
      }),
    ).toThrow("DEVICE_REGISTRATION_TOKEN is required");
  });

  test("registers a new public-firmware device with a valid registration code", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/device/register",
      payload: {
        device_id: "dev_a",
        device_name: "AI-Passport-A",
        device_secret: deviceSecretA,
        registration_code: "pair-123",
      },
    });

    expect(res.statusCode).toBe(200);
    expect(res.json()).toEqual({ device_id: "dev_a", registered: true });
  });

  test("rejects registration with a wrong registration code", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/device/register",
      payload: {
        device_id: "dev_a",
        device_name: "AI-Passport-A",
        device_secret: deviceSecretA,
        registration_code: "wrong",
      },
    });

    expect(res.statusCode).toBe(401);
  });

  test("returns 400 for malformed registration payload", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/device/register",
      payload: {
        device_id: "dev_a",
        registration_code: "pair-123",
      },
    });

    expect(res.statusCode).toBe(400);
    expect(res.json().error).toBe("invalid_device_registration");
  });

  test("returns 409 when a device registers twice", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });

    await registerDevice(app, "dev_a", deviceSecretA);
    const res = await registerDevice(app, "dev_a", deviceSecretA);

    expect(res.statusCode).toBe(409);
    expect(res.json().error).toBe("device_already_registered");
  });

  test("rate limits repeated registration attempts", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });
    let lastStatus = 0;

    for (let i = 0; i < 21; i++) {
      const res = await app.inject({
        method: "POST",
        url: "/api/v1/device/register",
        payload: {
          device_id: `dev_${i}`,
          device_name: `AI-Passport-${i}`,
          device_secret: deviceSecretA,
          registration_code: "wrong",
        },
      });
      lastStatus = res.statusCode;
    }

    expect(lastStatus).toBe(429);
  });

  test("does not store a plain device secret in SQLite", async () => {
    const { app, db } = createTestApp({ registrationCode: "pair-123" });

    await registerDevice(app, "dev_a", deviceSecretA);
    const row = db
      .prepare("SELECT encrypted_secret FROM devices WHERE id = ?")
      .get("dev_a") as { encrypted_secret: string };

    expect(row.encrypted_secret).not.toContain(
      deviceSecretA,
    );
  });

  test("accepts a valid signed device request", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });
    await registerDevice(app, "dev_a", deviceSecretA);
    const body = "{}";
    const { signature, timestamp } = signRequest({
      secret: deviceSecretA,
      method: "POST",
      path: "/api/v1/device/hello",
      body,
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/device/hello",
      headers: {
        authorization: `Device dev_a:${signature}`,
        "x-device-timestamp": String(timestamp),
        "content-type": "application/json",
      },
      payload: body,
    });

    expect(res.statusCode).toBe(200);
    expect(res.json()).toEqual({ device_id: "dev_a" });
  });

  test("rejects a request signed with another device secret", async () => {
    const { app } = createTestApp({ registrationCode: "pair-123" });
    await registerDevice(app, "dev_a", deviceSecretA);
    const body = "{}";
    const { signature, timestamp } = signRequest({
      secret: deviceSecretB,
      method: "POST",
      path: "/api/v1/device/hello",
      body,
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/device/hello",
      headers: {
        authorization: `Device dev_a:${signature}`,
        "x-device-timestamp": String(timestamp),
        "content-type": "application/json",
      },
      payload: body,
    });

    expect(res.statusCode).toBe(401);
    expect(res.json().error).toBe("invalid_device_signature");
  });
});
