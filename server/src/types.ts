export type DeviceIdentity = {
  deviceId: string;
};

export type NeteaseSession = {
  neteaseUserId: string;
  nickname: string;
  cookie: string;
};

export type PlaylistSummary = {
  id: number;
  name: string;
  trackCount: number;
  ownerName: string;
};

export type TrackSummary = {
  id: number;
  name: string;
  artist: string;
  durationMs: number;
};

export type TrackPage = {
  playlistId: number;
  tracks: TrackSummary[];
  nextOffset: number | null;
};

export type TrackUrlResult =
  | {
	      trackId: number;
	      url: string;
	      format: "mp3" | "m4a" | "flac" | "unknown";
	      expiresInSeconds: number;
	    }
  | {
      trackId: number;
      url: null;
      format: null;
      expiresInSeconds: 0;
    };

export type QrLoginState =
  | { state: "waiting" }
  | { state: "scanned" }
  | {
      state: "confirmed";
      neteaseUserId: string;
      nickname: string;
      cookie: string;
    }
  | { state: "expired" }
  | { state: "failed"; message: string };
