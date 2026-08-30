import Fastify from "fastify";
import rateLimit from "@fastify/rate-limit";
import { createReadStream, existsSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { AppConfig } from "./config.js";
import { loadConfig, validateConfig } from "./config.js";
import { openDb, type AppDatabase } from "./db.js";
import { registerDeviceRoutes } from "./routes/deviceRoutes.js";
import { registerNeteaseRoutes } from "./routes/neteaseRoutes.js";
import { createDeviceAuthService } from "./services/deviceAuth.js";
import { createMockNeteaseApi } from "./services/mockNeteaseApi.js";
import { createNeteaseApi, type NeteaseApi } from "./services/neteaseApi.js";
import { createSessionStore } from "./services/sessionStore.js";

export type CreateAppOptions = {
  db?: AppDatabase;
  config?: AppConfig;
  neteaseApi?: NeteaseApi;
};

export function createApp(options: CreateAppOptions = {}) {
  const app = Fastify({ logger: true });
  const config = validateConfig(options.config ?? loadConfig());
  const db = options.db ?? openDb(config.databasePath);
  const auth = createDeviceAuthService(db, config);
  const sessionStore = createSessionStore(db, config.cookieEncryptionKey);
  const neteaseApi =
    options.neteaseApi ??
    (config.mockNetease
      ? createMockNeteaseApi()
      : createNeteaseApi(config.neteaseApiBaseUrl));

  app.get("/health", async () => ({ ok: true }));
  if (config.mockNetease) {
    app.get("/mock/audio/:track_id.mp3", async (_req, reply) => {
      const audioFile = process.env.MOCK_AUDIO_FILE;
      if (!audioFile || !existsSync(audioFile)) {
        return reply.status(404).send({ error: "mock_audio_file_not_found" });
      }
      return reply.type("audio/mpeg").send(createReadStream(audioFile));
    });
  }
  app.register(async (scopedApp) => {
    await scopedApp.register(rateLimit, { global: false });
    registerDeviceRoutes(scopedApp, auth);
    registerNeteaseRoutes(scopedApp, auth, sessionStore, neteaseApi, db, {
      localRelayMode: config.localRelayMode,
    });
  });

  return app;
}

export function isMainModule(metaUrl: string, argvPath?: string) {
  if (!argvPath) {
    return false;
  }
  return resolve(fileURLToPath(metaUrl)) === resolve(argvPath);
}

if (isMainModule(import.meta.url, process.argv[1])) {
  const config = loadConfig();
  const app = createApp({ config });

  await app.listen({ host: "0.0.0.0", port: config.port });
}
