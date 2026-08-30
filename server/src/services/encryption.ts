import { createCipheriv, createDecipheriv, randomBytes } from "node:crypto";

export function parseEncryptionKey(rawKey: string): Buffer {
  const key = Buffer.from(rawKey, "utf8");
  if (key.length !== 32) {
    throw new Error("COOKIE_ENCRYPTION_KEY must be 32 bytes");
  }
  return key;
}

export function encryptText(text: string, key: Buffer): string {
  const iv = randomBytes(12);
  const cipher = createCipheriv("aes-256-gcm", key, iv);
  const encrypted = Buffer.concat([
    cipher.update(text, "utf8"),
    cipher.final(),
  ]);
  const tag = cipher.getAuthTag();

  return [iv, tag, encrypted].map((part) => part.toString("base64")).join(".");
}

export function decryptText(encryptedText: string, key: Buffer): string {
  const [ivBase64, tagBase64, dataBase64] = encryptedText.split(".");
  if (!ivBase64 || !tagBase64 || !dataBase64) {
    throw new Error("invalid encrypted value");
  }

  const decipher = createDecipheriv(
    "aes-256-gcm",
    key,
    Buffer.from(ivBase64, "base64"),
  );
  decipher.setAuthTag(Buffer.from(tagBase64, "base64"));

  return Buffer.concat([
    decipher.update(Buffer.from(dataBase64, "base64")),
    decipher.final(),
  ]).toString("utf8");
}
