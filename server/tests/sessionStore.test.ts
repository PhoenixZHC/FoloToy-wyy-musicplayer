import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, test } from "vitest";
import { openDb } from "../src/db.js";
import { createSessionStore } from "../src/services/sessionStore.js";

const tempDirs: string[] = [];
const dbs: ReturnType<typeof openDb>[] = [];
const encryptionKey = "12345678901234567890123456789012";

function createTestSessionStore() {
  const dir = mkdtempSync(join(tmpdir(), "folotoy-session-"));
  tempDirs.push(dir);
  const db = openDb(join(dir, "test.db"));
  dbs.push(db);
  db.prepare(
    "INSERT INTO devices (id, name, encrypted_secret, created_at) VALUES (?, ?, ?, ?)",
  ).run("dev_a", "Device A", "encrypted", new Date().toISOString());
  return {
    db,
    store: createSessionStore(db, encryptionKey),
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

describe("session store", () => {
  test("stores and reads a NetEase session by device id", async () => {
    const { store } = createTestSessionStore();

    await store.saveNeteaseSession("dev_a", {
      neteaseUserId: "1001",
      nickname: "alice",
      cookie: "MUSIC_U=abc",
    });

    await expect(store.getNeteaseSession("dev_a")).resolves.toEqual({
      neteaseUserId: "1001",
      nickname: "alice",
      cookie: "MUSIC_U=abc",
    });
  });

  test("returns null when device has no NetEase session", async () => {
    const { store } = createTestSessionStore();

    await expect(store.getNeteaseSession("missing")).resolves.toBeNull();
  });

  test("does not write a plain NetEase cookie to SQLite", async () => {
    const { db, store } = createTestSessionStore();

    await store.saveNeteaseSession("dev_a", {
      neteaseUserId: "1001",
      nickname: "alice",
      cookie: "MUSIC_U=abc",
    });

    const row = db
      .prepare("SELECT encrypted_cookie FROM netease_sessions WHERE device_id = ?")
      .get("dev_a") as { encrypted_cookie: string };

    expect(row.encrypted_cookie).not.toContain("MUSIC_U=abc");
  });
});
