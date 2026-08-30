import type {
  PlaylistSummary,
  QrLoginState,
  TrackPage,
  TrackSummary,
  TrackUrlResult,
} from "../types.js";

export type NeteaseApi = {
  createQrLogin(): Promise<{
    qrKey: string;
    qrPayload: string;
    expiresInSeconds: number;
  }>;
  checkQrLogin(qrKey: string): Promise<QrLoginState>;
  getUserPlaylists(cookie: string, userId: string): Promise<PlaylistSummary[]>;
  getPlaylistTracks(
    cookie: string,
    playlistId: number,
    limit: number,
    offset: number,
  ): Promise<TrackPage>;
  getTrackUrl(
    cookie: string,
    trackId: number,
    bitrate: number,
  ): Promise<TrackUrlResult>;
};

export function createNeteaseApi(baseUrl: string): NeteaseApi {
  const root = baseUrl.replace(/\/$/, "");

  return {
    async createQrLogin() {
      const keyResponse = (await getJson(root, "/login/qr/key", {
        timestamp: String(Date.now()),
      })) as {
        data?: { unikey?: string };
      };
      const qrKey = keyResponse.data?.unikey;
      if (!qrKey) {
        throw new Error("netease qr key missing");
      }

      const qrResponse = (await getJson(root, "/login/qr/create", {
        key: qrKey,
        timestamp: String(Date.now()),
      })) as { data?: { qrurl?: string } };
      const qrPayload = qrResponse.data?.qrurl;
      if (!qrPayload) {
        throw new Error("netease qr payload missing");
      }

      return {
        qrKey,
        qrPayload,
        expiresInSeconds: 180,
      };
    },

    async checkQrLogin(qrKey) {
      const body = (await getJson(root, "/login/qr/check", {
        key: qrKey,
        timestamp: String(Date.now()),
      })) as {
        code?: number;
        message?: string;
        cookie?: string;
        profile?: { userId?: number | string; nickname?: string };
      };

      if (body.code === 801) {
        return { state: "waiting" };
      }
      if (body.code === 802) {
        return { state: "scanned" };
      }
      if (body.code === 803 && body.cookie) {
        const profile = body.profile?.userId
          ? body.profile
          : await getLoginProfile(root, body.cookie);
        const userId = profile?.userId;
        if (!userId) {
          return {
            state: "failed",
            message: "netease login profile missing",
          };
        }
        return {
          state: "confirmed",
          neteaseUserId: String(userId),
          nickname: profile?.nickname ?? "",
          cookie: body.cookie,
        };
      }
      if (body.code === 800) {
        return { state: "expired" };
      }

      return {
        state: "failed",
        message: body.message ?? "netease login failed",
      };
    },

    async getUserPlaylists(cookie, userId) {
      const body = (await getJson(root, "/user/playlist", {
        uid: userId,
        cookie,
      })) as { playlist?: unknown[] };

      return (body.playlist ?? []).map(mapPlaylist);
    },

    async getPlaylistTracks(cookie, playlistId, limit, offset) {
      const upstreamLimit = limit + 1;
      const body = (await getJson(
        root,
        "/playlist/track/all",
        {
          id: String(playlistId),
          limit: String(upstreamLimit),
          offset: String(offset),
          cookie,
        },
      )) as { songs?: unknown[] };

      const upstreamTracks = (body.songs ?? []).map(mapTrack);
      const hasNextPage = upstreamTracks.length > limit;
      return {
        playlistId,
        tracks: upstreamTracks.slice(0, limit),
        nextOffset: hasNextPage ? offset + limit : null,
      };
    },

    async getTrackUrl(cookie, trackId, bitrate) {
      const body = (await getJson(
        root,
        "/song/url",
        { id: String(trackId), br: String(bitrate), cookie },
      )) as { data?: Array<{ id?: number; url?: string | null; type?: string }> };
      const item = body.data?.[0];

      if (!item?.url) {
        return {
          trackId,
          url: null,
          format: null,
          expiresInSeconds: 0,
        };
      }

      return {
        trackId: item.id ?? trackId,
        url: item.url,
        format: mapAudioFormat(item.type, item.url),
        expiresInSeconds: 900,
      };
    },
  };
}

function mapAudioFormat(type: string | undefined, url: string): "mp3" | "m4a" | "flac" | "unknown" {
  const normalized = type?.toLowerCase();
  if (normalized === "mp3" || normalized === "m4a" || normalized === "flac") {
    return normalized;
  }
  const path = new URL(url).pathname.toLowerCase();
  if (path.endsWith(".mp3")) return "mp3";
  if (path.endsWith(".m4a")) return "m4a";
  if (path.endsWith(".flac")) return "flac";
  return "unknown";
}

async function getLoginProfile(root: string, cookie: string) {
  const body = (await getJson(root, "/login/status", { cookie })) as {
    data?: {
      profile?: { userId?: number | string; nickname?: string } | null;
    };
  };

  return body.data?.profile ?? null;
}

async function getJson(
  root: string,
  path: string,
  query?: Record<string, string>,
) {
  const url = new URL(`${root}${path}`);
  for (const [key, value] of Object.entries(query ?? {})) {
    url.searchParams.set(key, value);
  }

  const res = await fetch(url);
  if (!res.ok) {
    throw new Error(`netease api failed: ${res.status}`);
  }

  return res.json();
}

function mapPlaylist(raw: unknown): PlaylistSummary {
  const item = raw as {
    id?: number;
    name?: string;
    trackCount?: number;
    creator?: { nickname?: string };
  };

  return {
    id: Number(item.id),
    name: item.name ?? "",
    trackCount: item.trackCount ?? 0,
    ownerName: item.creator?.nickname ?? "",
  };
}

function mapTrack(raw: unknown): TrackSummary {
  const item = raw as {
    id?: number;
    name?: string;
    dt?: number;
    ar?: Array<{ name?: string }>;
  };

  return {
    id: Number(item.id),
    name: item.name ?? "",
    artist: (item.ar ?? []).map((artist) => artist.name).filter(Boolean).join("/"),
    durationMs: item.dt ?? 0,
  };
}
