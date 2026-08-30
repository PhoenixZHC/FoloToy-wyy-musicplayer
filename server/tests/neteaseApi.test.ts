import nock from "nock";
import { afterEach, describe, expect, test } from "vitest";
import { createNeteaseApi } from "../src/services/neteaseApi.js";

const baseUrl = "http://netease-api.test";

afterEach(() => {
  nock.cleanAll();
});

describe("netease api", () => {
  test("bypasses cache when creating a NetEase QR login", async () => {
    const api = createNeteaseApi(baseUrl);
    nock(baseUrl)
      .get("/login/qr/key")
      .query((query) => /^\d{13}$/.test(String(query.timestamp)))
      .reply(200, { data: { unikey: "key_a" } });
    nock(baseUrl)
      .get("/login/qr/create")
      .query(
        (query) =>
          query.key === "key_a" && /^\d{13}$/.test(String(query.timestamp)),
      )
      .reply(200, { data: { qrurl: "https://music.163.com/qr/key_a" } });

    await expect(api.createQrLogin()).resolves.toEqual({
      qrKey: "key_a",
      qrPayload: "https://music.163.com/qr/key_a",
      expiresInSeconds: 180,
    });
  });

  test("maps NetEase QR waiting response", async () => {
    const api = createNeteaseApi(baseUrl);
    nock(baseUrl)
      .get("/login/qr/check")
      .query(
        (query) =>
          query.key === "key_a" && /^\d{13}$/.test(String(query.timestamp)),
      )
      .reply(200, { code: 801, message: "waiting" });

    await expect(api.checkQrLogin("key_a")).resolves.toEqual({
      state: "waiting",
    });
  });

  test("maps playlist response to compact device shape", async () => {
    const api = createNeteaseApi(baseUrl);
    nock(baseUrl)
      .get("/user/playlist")
      .query({ uid: "1001", cookie: "MUSIC_U=a" })
      .reply(200, {
        playlist: [
          {
            id: 1,
            name: "我喜欢的音乐",
            trackCount: 3,
            creator: { nickname: "me" },
          },
        ],
      });

    await expect(api.getUserPlaylists("MUSIC_U=a", "1001")).resolves.toEqual([
      { id: 1, name: "我喜欢的音乐", trackCount: 3, ownerName: "me" },
    ]);
  });

  test("only reports a next track page when NetEase returns an extra item", async () => {
    const api = createNeteaseApi(baseUrl);
    const songs = Array.from({ length: 31 }, (_, index) => ({
      id: index + 1,
      name: `song-${index + 1}`,
      dt: 180000,
      ar: [{ name: "artist" }],
    }));
    nock(baseUrl)
      .get("/playlist/track/all")
      .query({
        id: "1",
        limit: "31",
        offset: "0",
        cookie: "MUSIC_U=a",
      })
      .reply(200, { songs });

    const page = await api.getPlaylistTracks("MUSIC_U=a", 1, 30, 0);

    expect(page.playlistId).toBe(1);
    expect(page.tracks).toHaveLength(30);
    expect(page.nextOffset).toBe(30);
  });

  test("does not invent a next track page for an exact final page", async () => {
    const api = createNeteaseApi(baseUrl);
    const songs = Array.from({ length: 30 }, (_, index) => ({
      id: index + 1,
      name: `song-${index + 1}`,
      dt: 180000,
      ar: [{ name: "artist" }],
    }));
    nock(baseUrl)
      .get("/playlist/track/all")
      .query({
        id: "1",
        limit: "31",
        offset: "0",
        cookie: "MUSIC_U=a",
      })
      .reply(200, { songs });

    const page = await api.getPlaylistTracks("MUSIC_U=a", 1, 30, 0);

    expect(page.playlistId).toBe(1);
    expect(page.tracks).toHaveLength(30);
    expect(page.nextOffset).toBeNull();
  });

  test("loads login profile when QR confirmation response omits it", async () => {
    const api = createNeteaseApi(baseUrl);
    nock(baseUrl)
      .get("/login/qr/check")
      .query(
        (query) =>
          query.key === "key_a" && /^\d{13}$/.test(String(query.timestamp)),
      )
      .reply(200, { code: 803, cookie: "MUSIC_U=a" });
    nock(baseUrl)
      .get("/login/status")
      .query({ cookie: "MUSIC_U=a" })
      .reply(200, {
        data: {
          profile: { userId: 1001, nickname: "alice" },
        },
      });

    await expect(api.checkQrLogin("key_a")).resolves.toEqual({
      state: "confirmed",
      neteaseUserId: "1001",
      nickname: "alice",
      cookie: "MUSIC_U=a",
    });
  });

  test("returns unavailable when NetEase does not provide a playable URL", async () => {
    const api = createNeteaseApi(baseUrl);
    nock(baseUrl)
      .get("/song/url")
      .query({ id: "456", br: "128000", cookie: "MUSIC_U=a" })
      .reply(200, { data: [{ id: 456, url: null }] });

    await expect(api.getTrackUrl("MUSIC_U=a", 456, 128000)).resolves.toEqual({
      trackId: 456,
      url: null,
      format: null,
      expiresInSeconds: 0,
    });
  });

  test("keeps the real NetEase audio format", async () => {
    const api = createNeteaseApi(baseUrl);
    nock(baseUrl)
      .get("/song/url")
      .query({ id: "456", br: "64000", cookie: "MUSIC_U=a" })
      .reply(200, {
        data: [{ id: 456, url: "https://audio.test/song.m4a", type: "m4a" }],
      });

    await expect(api.getTrackUrl("MUSIC_U=a", 456, 64000)).resolves.toMatchObject({
      trackId: 456,
      url: "https://audio.test/song.m4a",
      format: "m4a",
    });
  });
});
