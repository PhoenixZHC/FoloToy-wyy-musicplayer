import type { AppDatabase } from "../db.js";
import type { NeteaseSession } from "../types.js";
import { decryptText, encryptText, parseEncryptionKey } from "./encryption.js";

export type SessionStore = {
  saveNeteaseSession(
    deviceId: string,
    session: NeteaseSession,
  ): Promise<void>;
  getNeteaseSession(deviceId: string): Promise<NeteaseSession | null>;
  deleteNeteaseSession(deviceId: string): Promise<void>;
};

type SessionRow = {
  netease_user_id: string;
  nickname: string;
  encrypted_cookie: string;
};

export function createSessionStore(
  db: AppDatabase,
  cookieEncryptionKey: string,
): SessionStore {
  const key = parseEncryptionKey(cookieEncryptionKey);

  return {
    async saveNeteaseSession(deviceId, session) {
      const now = new Date().toISOString();
      db.prepare(
        `INSERT INTO netease_sessions (
          device_id,
          netease_user_id,
          nickname,
          encrypted_cookie,
          created_at,
          updated_at
        ) VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(device_id) DO UPDATE SET
          netease_user_id = excluded.netease_user_id,
          nickname = excluded.nickname,
          encrypted_cookie = excluded.encrypted_cookie,
          updated_at = excluded.updated_at`,
      ).run(
        deviceId,
        session.neteaseUserId,
        session.nickname,
        encryptText(session.cookie, key),
        now,
        now,
      );
    },

    async getNeteaseSession(deviceId) {
      const row = db
        .prepare(
          `SELECT netease_user_id, nickname, encrypted_cookie
           FROM netease_sessions
           WHERE device_id = ?`,
        )
        .get(deviceId) as SessionRow | undefined;

      if (!row) {
        return null;
      }

      return {
        neteaseUserId: row.netease_user_id,
        nickname: row.nickname,
        cookie: decryptText(row.encrypted_cookie, key),
      };
    },

    async deleteNeteaseSession(deviceId) {
      db.prepare("DELETE FROM netease_sessions WHERE device_id = ?").run(
        deviceId,
      );
    },
  };
}
