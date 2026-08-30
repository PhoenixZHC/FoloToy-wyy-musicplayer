import { createHash, createHmac, timingSafeEqual } from "node:crypto";
import type { FastifyRequest } from "fastify";
import type { AppConfig } from "../config.js";
import type { AppDatabase } from "../db.js";
import type { DeviceIdentity } from "../types.js";
import { decryptText, encryptText, parseEncryptionKey } from "./encryption.js";

declare module "fastify" {
  interface FastifyRequest {
    device?: DeviceIdentity;
  }
}

export type RegisterDeviceInput = {
  device_id: string;
  device_name: string;
  device_secret: string;
  registration_code: string;
};

type DeviceRow = {
  id: string;
  encrypted_secret: string;
  disabled: number;
};

export type DeviceAuthService = {
  registerDevice(input: RegisterDeviceInput): Promise<DeviceIdentity>;
  verifyDeviceRequest(req: FastifyRequest): Promise<DeviceIdentity>;
};

export function createDeviceAuthService(
  db: AppDatabase,
  config: AppConfig,
): DeviceAuthService {
  const key = parseEncryptionKey(config.cookieEncryptionKey);

  return {
    async registerDevice(input) {
      if (!isRegisterDeviceInput(input)) {
        throw new DeviceAuthError("invalid_device_registration");
      }
      if (input.registration_code !== config.deviceRegistrationToken) {
        throw new DeviceAuthError("invalid_registration_code");
      }
      if (!input.device_id.trim() || !input.device_name.trim()) {
        throw new DeviceAuthError("invalid_device_registration");
      }
      if (!input.device_secret || input.device_secret.length < 32) {
        throw new DeviceAuthError("invalid_device_secret");
      }

      const now = new Date().toISOString();
      try {
        db.prepare(
          `INSERT INTO devices (id, name, encrypted_secret, created_at)
           VALUES (?, ?, ?, ?)`,
        ).run(
          input.device_id,
          input.device_name,
          encryptText(input.device_secret, key),
          now,
        );
      } catch (error) {
        if (String(error).includes("UNIQUE constraint failed")) {
          throw new DeviceAuthError("device_already_registered");
        }
        throw error;
      }

      return { deviceId: input.device_id };
    },

    async verifyDeviceRequest(req) {
      const authorization = req.headers.authorization;
      const timestampHeader = req.headers["x-device-timestamp"];
      if (!authorization || !timestampHeader) {
        throw new DeviceAuthError("missing_device_auth");
      }

      const match = /^Device ([^:]+):([a-f0-9]+)$/i.exec(authorization);
      if (!match) {
        throw new DeviceAuthError("invalid_device_auth");
      }

      const [, deviceId, signature] = match;
      const timestamp = Number(
        Array.isArray(timestampHeader) ? timestampHeader[0] : timestampHeader,
      );
      if (!Number.isFinite(timestamp)) {
        throw new DeviceAuthError("invalid_device_timestamp");
      }
      const now = Math.floor(Date.now() / 1000);
      if (
        !config.allowStaleDeviceTimestamps &&
        Math.abs(now - timestamp) > 300
      ) {
        throw new DeviceAuthError("expired_device_timestamp");
      }

      const row = db
        .prepare(
          `SELECT id, encrypted_secret, disabled
           FROM devices
           WHERE id = ?`,
        )
        .get(deviceId) as DeviceRow | undefined;
      if (!row) {
        throw new DeviceAuthError("unknown_device");
      }
      if (row.disabled) {
        throw new DeviceAuthError("disabled_device");
      }

      const deviceSecret = decryptText(row.encrypted_secret, key);
      const expected = signDeviceRequest({
        secret: deviceSecret,
        method: req.method,
        path: req.url,
        body: requestBodyForSignature(req.body),
        timestamp,
      });

      if (!constantTimeEqual(signature, expected)) {
        throw new DeviceAuthError("invalid_device_signature");
      }

      db.prepare("UPDATE devices SET last_seen_at = ? WHERE id = ?").run(
        new Date().toISOString(),
        deviceId,
      );

      return { deviceId };
    },
  };
}

function isRegisterDeviceInput(input: unknown): input is RegisterDeviceInput {
  if (!input || typeof input !== "object") {
    return false;
  }
  const candidate = input as Partial<Record<keyof RegisterDeviceInput, unknown>>;
  return (
    typeof candidate.device_id === "string" &&
    typeof candidate.device_name === "string" &&
    typeof candidate.device_secret === "string" &&
    typeof candidate.registration_code === "string"
  );
}

export class DeviceAuthError extends Error {
  constructor(public readonly code: string) {
    super(code);
  }
}

function signDeviceRequest(input: {
  secret: string;
  method: string;
  path: string;
  body: string;
  timestamp: number;
}) {
  const bodyHash = createHash("sha256").update(input.body).digest("hex");
  const signatureInput = [
    input.method,
    input.path,
    bodyHash,
    String(input.timestamp),
  ].join("\n");

  return createHmac("sha256", input.secret)
    .update(signatureInput)
    .digest("hex");
}

function requestBodyForSignature(body: unknown): string {
  if (body === undefined || body === null) {
    return "";
  }
  if (typeof body === "string") {
    return body;
  }
  return JSON.stringify(body);
}

function constantTimeEqual(left: string, right: string) {
  const leftBuffer = Buffer.from(left, "hex");
  const rightBuffer = Buffer.from(right, "hex");
  if (leftBuffer.length !== rightBuffer.length) {
    return false;
  }
  return timingSafeEqual(leftBuffer, rightBuffer);
}
