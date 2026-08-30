import { createHash, createHmac } from "node:crypto";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, test, vi } from "vitest";
import { createApp } from "../src/app.js";
import type { AppConfig } from "../src/config.js";
import { openDb, type AppDatabase } from "../src/db.js";
import type { NeteaseApi } from "../src/services/neteaseApi.js";

const tempDirs: string[] = [];
const dbs: AppDatabase[] = [];
const encryptionKey = "12345678901234567890123456789012";
const deviceSecretA = "secret_a_32_bytes_minimum_value_x";

function createFakeNeteaseApi(overrides: Partial<NeteaseApi> = {}): NeteaseApi {
  return {
    createQrLogin: vi.fn(async () => ({
      qrKey: "qr_key_a",
      qrPayload: "https://music.163.com/login?code=a",
      expiresInSeconds: 180,
    })),
    checkQrLogin: vi.fn(async () => ({ state: "waiting" })),
    getUserPlaylists: vi.fn(async () => [
      { id: 1, name: "我喜欢的音乐", trackCount: 3, ownerName: "me" },
    ]),
    getPlaylistTracks: vi.fn(async () => ({
      playlistId: 1,
      tracks: [{ id: 456, name: "song", artist: "artist", durationMs: 210000 }],
      nextOffset: null,
    })),
    getTrackUrl: vi.fn(async () => ({
      trackId: 456,
      url: "https://audio.test/song.mp3",
      format: "mp3",
      expiresInSeconds: 900,
    })),
    ...overrides,
  };
}

function createSignedTestApp(
  overrides: Partial<NeteaseApi> = {},
  configOverrides: Partial<AppConfig> = {},
) {
  const dir = mkdtempSync(join(tmpdir(), "folotoy-routes-"));
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
  Object.assign(config, configOverrides);
  const neteaseApi = createFakeNeteaseApi(overrides);
  const app = createApp({ db, config, neteaseApi });

  return { app, neteaseApi };
}

async function registerDevice(app: ReturnType<typeof createApp>) {
  await app.inject({
    method: "POST",
    url: "/api/v1/device/register",
    payload: {
      device_id: "dev_a",
      device_name: "AI-Passport-A",
      device_secret: deviceSecretA,
      registration_code: "pair-123",
    },
  });
}

function signedHeaders(input: {
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
  const signature = createHmac("sha256", deviceSecretA)
    .update(signatureInput)
    .digest("hex");

  return {
    authorization: `Device dev_a:${signature}`,
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

describe("netease routes", () => {
  test("starts QR login for a signed device", async () => {
    const { app } = createSignedTestApp();
    await registerDevice(app);

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });

    expect(res.statusCode).toBe(200);
    expect(res.json()).toMatchObject({
      login_id: expect.any(String),
      qr_payload: "https://music.163.com/login?code=a",
      expires_in_seconds: 180,
    });
  });

  test("requires login before returning playlists", async () => {
    const { app } = createSignedTestApp();
    await registerDevice(app);

    const res = await app.inject({
      method: "GET",
      url: "/api/v1/me/playlists",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/me/playlists",
      }),
    });

    expect(res.statusCode).toBe(401);
    expect(res.json().error).toBe("netease_not_logged_in");
  });

  test("saves NetEase cookie when QR login is confirmed", async () => {
    const { app } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;

    const status = await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });
    const playlists = await app.inject({
      method: "GET",
      url: "/api/v1/me/playlists",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/me/playlists",
      }),
    });

    expect(status.statusCode).toBe(200);
    expect(status.json()).toMatchObject({
      state: "confirmed",
      netease_user_id: 1001,
      nickname: "alice",
    });
    expect(playlists.statusCode).toBe(200);
    expect(playlists.json()).toEqual({
      playlists: [
        { id: 1, name: "我喜欢的音乐", track_count: 3, owner_name: "me" },
      ],
    });
  });

  test("caps playlist response to the device parser limits", async () => {
    const longName = "很长的歌单名称".repeat(16);
    const { app } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
      getUserPlaylists: vi.fn(async () =>
        Array.from({ length: 67 }, (_, index) => ({
          id: index + 1,
          name: `${longName}-${index + 1}`,
          trackCount: 2000 + index,
          ownerName: `${longName}-owner`,
        })),
      ),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const playlists = await app.inject({
      method: "GET",
      url: "/api/v1/me/playlists",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/me/playlists",
      }),
    });
    const body = playlists.json() as {
      playlists: Array<{ name: string; owner_name: string }>;
    };

    expect(playlists.statusCode).toBe(200);
    expect(body.playlists).toHaveLength(20);
    expect(Buffer.byteLength(playlists.body)).toBeLessThan(4096);
    for (const playlist of body.playlists) {
      expect(Buffer.byteLength(playlist.name)).toBeLessThan(64);
      expect(Buffer.byteLength(playlist.owner_name)).toBeLessThan(64);
    }
  });

  test("sanitizes playlist and track text to characters the device font can render", async () => {
    const { app } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
      getUserPlaylists: vi.fn(async () => [
        { id: 1, name: "我喜欢🚀𠮷", trackCount: 1, ownerName: "me🚀" },
      ]),
      getPlaylistTracks: vi.fn(async () => ({
        playlistId: 1,
        tracks: [
          {
            id: 456,
            name: "song🚀𠮷",
            artist: "artist🚀𠮷",
            durationMs: 210000,
          },
        ],
        nextOffset: null,
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const playlists = await app.inject({
      method: "GET",
      url: "/api/v1/me/playlists",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/me/playlists",
      }),
    });
    const tracks = await app.inject({
      method: "GET",
      url: "/api/v1/playlists/1/tracks?limit=10&offset=0",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/playlists/1/tracks?limit=10&offset=0",
      }),
    });

    expect(playlists.json().playlists[0]).toMatchObject({
      name: "我喜欢??",
      owner_name: "me?",
    });
    expect(tracks.json().tracks[0]).toMatchObject({
      name: "song??",
      artist: "artist??",
    });
  });

  test("keeps the largest track page within the firmware response buffer", async () => {
    const longName = "歌".repeat(40);
    const longArtist = "手".repeat(50);
    const { app } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
      getPlaylistTracks: vi.fn(async () => ({
        playlistId: 1,
        tracks: Array.from({ length: 30 }, (_, index) => ({
          id: index + 1,
          name: longName,
          artist: longArtist,
          durationMs: 210000,
        })),
        nextOffset: 30,
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const tracks = await app.inject({
      method: "GET",
      url: "/api/v1/playlists/1/tracks?limit=30&offset=0",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/playlists/1/tracks?limit=30&offset=0",
      }),
    });

    expect(tracks.statusCode).toBe(200);
    expect(Buffer.byteLength(tracks.body)).toBeLessThan(4096);
    expect(tracks.json().tracks.length).toBeLessThan(30);
    expect(tracks.json().next_offset).toBeGreaterThan(0);
  });

  test("treats an existing NetEase session as already logged in during QR polling", async () => {
    const { app, neteaseApi } = createSignedTestApp({
      checkQrLogin: vi
        .fn()
        .mockResolvedValueOnce({
          state: "confirmed",
          neteaseUserId: "1001",
          nickname: "alice",
          cookie: "MUSIC_U=a",
        })
        .mockResolvedValue({ state: "waiting" }),
    });
    await registerDevice(app);

    const firstStart = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const firstLoginId = firstStart.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${firstLoginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${firstLoginId}`,
      }),
    });

    const secondStart = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const secondLoginId = secondStart.json().login_id as string;
    const secondStatus = await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${secondLoginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${secondLoginId}`,
      }),
    });

    expect(secondStatus.statusCode).toBe(200);
    expect(secondStatus.json()).toEqual({
      state: "confirmed",
      netease_user_id: 1001,
      nickname: "alice",
    });
    expect(neteaseApi.checkQrLogin).toHaveBeenCalledTimes(1);
  });

  test("returns track_unavailable when song url is empty", async () => {
    const body = JSON.stringify({ bitrate: 128000 });
    const { app } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
      getTrackUrl: vi.fn(async () => ({
        trackId: 456,
        url: null,
        format: null,
        expiresInSeconds: 0,
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/tracks/456/url",
      headers: {
        ...signedHeaders({
          method: "POST",
          path: "/api/v1/tracks/456/url",
          body,
        }),
        "content-type": "application/json",
      },
      payload: body,
    });

    expect(res.statusCode).toBe(404);
    expect(res.json()).toEqual({
      error: "track_unavailable",
      message: "这首歌当前无法播放",
    });
  });

  test("rejects non-mp3 track urls because the device can only decode mp3", async () => {
    const body = JSON.stringify({ bitrate: 64000 });
    const { app } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
      getTrackUrl: vi.fn(async () => ({
        trackId: 456,
        url: "https://audio.test/song.m4a",
        format: "m4a",
        expiresInSeconds: 900,
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/tracks/456/url",
      headers: {
        ...signedHeaders({
          method: "POST",
          path: "/api/v1/tracks/456/url",
          body,
        }),
        "content-type": "application/json",
      },
      payload: body,
    });

    expect(res.statusCode).toBe(415);
    expect(res.json()).toEqual({
      error: "unsupported_audio_format",
      message: "这首歌不是 MP3，当前设备无法播放",
    });
  });

  test("returns a local relay playback URL in local relay mode", async () => {
    const body = JSON.stringify({ bitrate: 64000 });
    const { app } = createSignedTestApp(
      {
        checkQrLogin: vi.fn(async () => ({
          state: "confirmed",
          neteaseUserId: "1001",
          nickname: "alice",
          cookie: "MUSIC_U=a",
        })),
      },
      { localRelayMode: true },
    );
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/tracks/456/url",
      headers: {
        ...signedHeaders({
          method: "POST",
          path: "/api/v1/tracks/456/url",
          body,
        }),
        "content-type": "application/json",
        host: "192.168.5.18:18080",
      },
      payload: body,
    });

    expect(res.statusCode).toBe(200);
    expect(res.json()).toMatchObject({
      track_id: 456,
      format: "mp3",
    });
    expect(res.json().url).toMatch(
      /^http:\/\/192\.168\.5\.18:18080\/api\/v1\/audio\/[a-f0-9]{32}\.mp3$/,
    );
  });

  test("rejects invalid playlist pagination before calling NetEase API", async () => {
    const { app, neteaseApi } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const res = await app.inject({
      method: "GET",
      url: "/api/v1/playlists/not-a-number/tracks?limit=-1&offset=nope",
      headers: signedHeaders({
        method: "GET",
        path: "/api/v1/playlists/not-a-number/tracks?limit=-1&offset=nope",
      }),
    });

    expect(res.statusCode).toBe(400);
    expect(res.json().error).toBe("invalid_playlist_id");
    expect(neteaseApi.getPlaylistTracks).not.toHaveBeenCalled();
  });

  test("rejects invalid track url request before calling NetEase API", async () => {
    const body = JSON.stringify({ bitrate: -1 });
    const { app, neteaseApi } = createSignedTestApp({
      checkQrLogin: vi.fn(async () => ({
        state: "confirmed",
        neteaseUserId: "1001",
        nickname: "alice",
        cookie: "MUSIC_U=a",
      })),
    });
    await registerDevice(app);
    const start = await app.inject({
      method: "POST",
      url: "/api/v1/auth/qr/start",
      headers: signedHeaders({
        method: "POST",
        path: "/api/v1/auth/qr/start",
      }),
    });
    const loginId = start.json().login_id as string;
    await app.inject({
      method: "GET",
      url: `/api/v1/auth/qr/status?login_id=${loginId}`,
      headers: signedHeaders({
        method: "GET",
        path: `/api/v1/auth/qr/status?login_id=${loginId}`,
      }),
    });

    const res = await app.inject({
      method: "POST",
      url: "/api/v1/tracks/0/url",
      headers: {
        ...signedHeaders({
          method: "POST",
          path: "/api/v1/tracks/0/url",
          body,
        }),
        "content-type": "application/json",
      },
      payload: body,
    });

    expect(res.statusCode).toBe(400);
    expect(res.json().error).toBe("invalid_track_id");
    expect(neteaseApi.getTrackUrl).not.toHaveBeenCalled();
  });
});
