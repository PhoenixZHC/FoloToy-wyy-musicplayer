import type { FastifyInstance } from "fastify";
import { randomUUID } from "node:crypto";
import { Readable } from "node:stream";
import { DEVICE_SUPPORTED_TEXT } from "../generated/deviceCharset.js";
import type { AppDatabase } from "../db.js";
import { createDevicePreHandler } from "./deviceRoutes.js";
import type { DeviceAuthService } from "../services/deviceAuth.js";
import type { NeteaseApi } from "../services/neteaseApi.js";
import type { SessionStore } from "../services/sessionStore.js";

const DEVICE_MAX_PLAYLISTS = 20;
const DEVICE_MAX_TRACKS_PER_PAGE = 15;
const DEVICE_TEXT_FIELD_MAX_BYTES = 63;
const DEVICE_ARTIST_FIELD_MAX_BYTES = 95;
const AUDIO_TICKET_TTL_SECONDS = 900;
const DEVICE_SUPPORTED_CHARACTERS = new Set(DEVICE_SUPPORTED_TEXT);

type NeteaseRoutesOptions = {
  localRelayMode?: boolean;
};

type AudioTicket = {
  url: string;
  expiresAtMs: number;
};

export function registerNeteaseRoutes(
  app: FastifyInstance,
  auth: DeviceAuthService,
  sessionStore: SessionStore,
  neteaseApi: NeteaseApi,
  db: AppDatabase,
  options: NeteaseRoutesOptions = {},
) {
  const authPreHandler = createDevicePreHandler(auth);
  const audioTickets = new Map<string, AudioTicket>();

  app.get("/api/v1/audio/:ticket.mp3", async (req, reply) => {
    const params = req.params as { ticket: string };
    const ticket = audioTickets.get(params.ticket);
    if (!ticket || ticket.expiresAtMs <= Date.now()) {
      audioTickets.delete(params.ticket);
      return reply.status(404).send({ error: "audio_ticket_not_found" });
    }

    const upstream = await fetch(ticket.url, {
      headers: {
        "user-agent": "Mozilla/5.0",
        referer: "https://music.163.com/",
      },
    });
    if (!upstream.ok || !upstream.body) {
      return reply.status(502).send({ error: "audio_upstream_failed" });
    }

    const contentType = upstream.headers.get("content-type") ?? "audio/mpeg";
    const contentLength = upstream.headers.get("content-length");
    if (contentLength) {
      reply.header("content-length", contentLength);
    }
    reply.header("cache-control", "no-store");
    return reply
      .type(contentType)
      .send(
        Readable.fromWeb(
          upstream.body as Parameters<typeof Readable.fromWeb>[0],
        ),
      );
  });

  app.post(
    "/api/v1/auth/qr/start",
    { preHandler: [authPreHandler] },
    async (req) => {
      const login = await neteaseApi.createQrLogin();
      const loginId = `qr_${randomUUID().replaceAll("-", "")}`;
      const now = new Date();
      const expiresAt = new Date(
        now.getTime() + login.expiresInSeconds * 1000,
      ).toISOString();

      db.prepare(
        `INSERT INTO qr_login_sessions (
          login_id,
          device_id,
          qr_key,
          state,
          created_at,
          expires_at
        ) VALUES (?, ?, ?, ?, ?, ?)`,
      ).run(
        loginId,
        req.device?.deviceId,
        login.qrKey,
        "waiting",
        now.toISOString(),
        expiresAt,
      );

      return {
        login_id: loginId,
        qr_payload: login.qrPayload,
        expires_in_seconds: login.expiresInSeconds,
      };
    },
  );

  app.get(
    "/api/v1/auth/qr/status",
    { preHandler: [authPreHandler] },
    async (req, reply) => {
      const query = req.query as { login_id?: string };
      const loginId = query.login_id;
      if (!loginId) {
        return reply.status(400).send({ error: "missing_login_id" });
      }

      const row = db
        .prepare(
          `SELECT login_id, device_id, qr_key, expires_at
           FROM qr_login_sessions
           WHERE login_id = ?`,
        )
        .get(loginId) as
        | {
            login_id: string;
            device_id: string;
            qr_key: string;
            expires_at: string;
          }
        | undefined;
      if (!row || row.device_id !== req.device?.deviceId) {
        return reply.status(404).send({ error: "qr_login_not_found" });
      }
      if (Date.parse(row.expires_at) <= Date.now()) {
        db.prepare(
          "UPDATE qr_login_sessions SET state = ? WHERE login_id = ?",
        ).run("expired", loginId);
        return { state: "expired" };
      }

      const existingSession = await getSessionForRequest(row.device_id);
      if (existingSession) {
        db.prepare(
          "UPDATE qr_login_sessions SET state = ? WHERE login_id = ?",
        ).run("confirmed", loginId);
        const neteaseUserId = Number(existingSession.neteaseUserId);
        return {
          state: "confirmed",
          netease_user_id: Number.isFinite(neteaseUserId) ? neteaseUserId : 0,
          nickname: existingSession.nickname,
        };
      }

      const status = await neteaseApi.checkQrLogin(row.qr_key);
      db.prepare(
        "UPDATE qr_login_sessions SET state = ? WHERE login_id = ?",
      ).run(status.state, loginId);

      if (status.state === "confirmed") {
        const neteaseUserId = Number(status.neteaseUserId);
        await sessionStore.saveNeteaseSession(row.device_id, {
          neteaseUserId: status.neteaseUserId,
          nickname: status.nickname,
          cookie: status.cookie,
        });

        return {
          state: "confirmed",
          netease_user_id: Number.isFinite(neteaseUserId) ? neteaseUserId : 0,
          nickname: status.nickname,
        };
      }

      if (status.state === "failed") {
        return { state: "failed", message: status.message };
      }

      return { state: status.state };
    },
  );

  app.post(
    "/api/v1/auth/logout",
    { preHandler: [authPreHandler] },
    async (req) => {
      await sessionStore.deleteNeteaseSession(req.device?.deviceId ?? "");
      return { ok: true };
    },
  );

  app.get(
    "/api/v1/me/playlists",
    { preHandler: [authPreHandler] },
    async (req, reply) => {
      const session = await getSessionForRequest(req.device?.deviceId);
      if (!session) {
        return reply.status(401).send({ error: "netease_not_logged_in" });
      }

      const playlists = await neteaseApi.getUserPlaylists(
        session.cookie,
        session.neteaseUserId,
      );
      return {
	        playlists: playlists.slice(0, DEVICE_MAX_PLAYLISTS).map((playlist) => ({
	          id: playlist.id,
	          name: sanitizeDeviceText(playlist.name, DEVICE_TEXT_FIELD_MAX_BYTES),
	          track_count: playlist.trackCount,
	          owner_name: sanitizeDeviceText(
	            playlist.ownerName,
	            DEVICE_TEXT_FIELD_MAX_BYTES,
	          ),
        })),
      };
    },
  );

  app.get(
    "/api/v1/playlists/:playlist_id/tracks",
    { preHandler: [authPreHandler] },
    async (req, reply) => {
      const session = await getSessionForRequest(req.device?.deviceId);
      if (!session) {
        return reply.status(401).send({ error: "netease_not_logged_in" });
      }

      const params = req.params as { playlist_id: string };
      const query = req.query as { limit?: string; offset?: string };
      const playlistId = parseIntegerParam(params.playlist_id, {
        min: 1,
        error: "invalid_playlist_id",
      });
      if ("error" in playlistId) {
        return reply.status(400).send({ error: playlistId.error });
      }
      const limit = parseIntegerParam(query.limit ?? "30", {
        min: 1,
        max: 50,
        error: "invalid_limit",
      });
      if ("error" in limit) {
        return reply.status(400).send({ error: limit.error });
      }
      const offset = parseIntegerParam(query.offset ?? "0", {
        min: 0,
        error: "invalid_offset",
      });
      if ("error" in offset) {
        return reply.status(400).send({ error: offset.error });
      }
      const effectiveLimit = Math.min(limit.value, DEVICE_MAX_TRACKS_PER_PAGE);
      const page = await neteaseApi.getPlaylistTracks(
        session.cookie,
        playlistId.value,
        effectiveLimit,
        offset.value,
      );
      const tracks = page.tracks.slice(0, effectiveLimit);
      const nextOffset =
        page.tracks.length > effectiveLimit
          ? offset.value + effectiveLimit
          : page.nextOffset;

      return {
        playlist_id: page.playlistId,
	        tracks: tracks.map((track) => ({
	          id: track.id,
	          name: sanitizeDeviceText(track.name, DEVICE_TEXT_FIELD_MAX_BYTES),
	          artist: sanitizeDeviceText(track.artist, DEVICE_ARTIST_FIELD_MAX_BYTES),
	          duration_ms: track.durationMs,
	        })),
	        next_offset: nextOffset,
      };
    },
  );

  app.post(
    "/api/v1/tracks/:track_id/url",
    { preHandler: [authPreHandler] },
    async (req, reply) => {
      const session = await getSessionForRequest(req.device?.deviceId);
      if (!session) {
        return reply.status(401).send({ error: "netease_not_logged_in" });
      }

      const params = req.params as { track_id: string };
      const body = req.body as { bitrate?: number } | undefined;
      const trackId = parseIntegerParam(params.track_id, {
        min: 1,
        error: "invalid_track_id",
      });
      if ("error" in trackId) {
        return reply.status(400).send({ error: trackId.error });
      }
      const bitrate = parseIntegerParam(body?.bitrate ?? 128000, {
        min: 16000,
        max: 320000,
        error: "invalid_bitrate",
      });
      if ("error" in bitrate) {
        return reply.status(400).send({ error: bitrate.error });
      }
      const result = await neteaseApi.getTrackUrl(
        session.cookie,
        trackId.value,
        bitrate.value,
      );

	      if (!result.url) {
	        return reply.status(404).send({
	          error: "track_unavailable",
	          message: "这首歌当前无法播放",
	        });
	      }
	      if (result.format !== "mp3") {
	        return reply.status(415).send({
	          error: "unsupported_audio_format",
	          message: "这首歌不是 MP3，当前设备无法播放",
	        });
	      }

	      return {
	        track_id: result.trackId,
	        url: toPublicTrackUrl(req.headers.host, result, {
	          localRelayMode: options.localRelayMode === true,
	          audioTickets,
	        }),
	        format: result.format,
	        expires_in_seconds: result.expiresInSeconds,
	      };
    },
  );

  async function getSessionForRequest(deviceId: string | undefined) {
    if (!deviceId) {
      return null;
    }
    return sessionStore.getNeteaseSession(deviceId);
  }
}

function sanitizeDeviceText(text: string, maxBytes: number) {
  let output = "";
  for (const char of text) {
    const safeChar = isDeviceSupportedChar(char) ? char : "?";
    if (Buffer.byteLength(output + safeChar, "utf8") > maxBytes) {
      break;
    }
    output += safeChar;
  }
  return output;
}

function isDeviceSupportedChar(char: string) {
  const codePoint = char.codePointAt(0);
  return (
    codePoint !== undefined &&
    codePoint >= 0x20 &&
    codePoint <= 0x7e
  ) || DEVICE_SUPPORTED_CHARACTERS.has(char);
}

function toPublicTrackUrl(
  host: string | undefined,
  result: { trackId: number; url: string | null },
  options: {
    localRelayMode: boolean;
    audioTickets: Map<string, AudioTicket>;
  },
) {
  if (result.url?.startsWith("mock-audio:")) {
    return `http://${host ?? "localhost"}/mock/audio/${result.trackId}.mp3`;
  }
  if (options.localRelayMode && result.url) {
    cleanupExpiredAudioTickets(options.audioTickets);
    const ticket = randomUUID().replaceAll("-", "");
    options.audioTickets.set(ticket, {
      url: result.url,
      expiresAtMs: Date.now() + AUDIO_TICKET_TTL_SECONDS * 1000,
    });
    return `http://${host ?? "localhost"}/api/v1/audio/${ticket}.mp3`;
  }
  return result.url;
}

function cleanupExpiredAudioTickets(audioTickets: Map<string, AudioTicket>) {
  const now = Date.now();
  for (const [ticket, value] of audioTickets) {
    if (value.expiresAtMs <= now) {
      audioTickets.delete(ticket);
    }
  }
}

function parseIntegerParam(
  value: string | number,
  options: { min: number; max?: number; error: string },
): { value: number } | { error: string } {
  const parsed = typeof value === "number" ? value : Number(value);
  if (!Number.isInteger(parsed) || parsed < options.min) {
    return { error: options.error };
  }
  if (options.max !== undefined && parsed > options.max) {
    return { error: options.error };
  }

  return { value: parsed };
}
