import type { NeteaseApi } from "./neteaseApi.js";

export function createMockNeteaseApi(): NeteaseApi {
  return {
    async createQrLogin() {
      return {
        qrKey: `mock-${Date.now()}`,
        qrPayload: "https://mock-login.local/netease",
        expiresInSeconds: 180,
      };
    },

    async checkQrLogin() {
      return {
        state: "confirmed",
        neteaseUserId: "100000",
        nickname: "local",
        cookie: "MOCK_NETEASE_COOKIE",
      };
    },

    async getUserPlaylists() {
      return [
        { id: 1, name: "本地模拟歌单", trackCount: 2, ownerName: "local" },
      ];
    },

    async getPlaylistTracks(_cookie, playlistId, limit, offset) {
      const tracks = [
        { id: 101, name: "模拟歌曲 A", artist: "local", durationMs: 180000 },
        { id: 102, name: "模拟歌曲 B", artist: "local", durationMs: 210000 },
      ];
      const page = tracks.slice(offset, offset + limit);
      return {
        playlistId,
        tracks: page,
        nextOffset: offset + limit < tracks.length ? offset + limit : null,
      };
    },

    async getTrackUrl(_cookie, trackId) {
      return {
        trackId,
        url: `mock-audio:${trackId}`,
        format: "mp3",
        expiresInSeconds: 900,
      };
    },
  };
}
