export type AppConfig = {
  port: number;
  databasePath: string;
  cookieEncryptionKey: string;
  deviceRegistrationToken: string;
  neteaseApiBaseUrl: string;
  mockNetease: boolean;
  localRelayMode: boolean;
  allowStaleDeviceTimestamps: boolean;
};

export function loadConfig(): AppConfig {
  const isTest = process.env.NODE_ENV === "test" || process.env.VITEST === "true";
  const mockNetease = process.env.MOCK_NETEASE === "true";
  const localRelayMode = process.env.LOCAL_RELAY_MODE === "true";

  return validateConfig({
    port: Number(process.env.PORT ?? "8080"),
    databasePath: process.env.DATABASE_PATH ?? "./data/app.db",
    cookieEncryptionKey:
      process.env.COOKIE_ENCRYPTION_KEY ??
      (isTest ? "12345678901234567890123456789012" : ""),
    deviceRegistrationToken:
      process.env.DEVICE_REGISTRATION_TOKEN ?? (isTest ? "test-token" : ""),
    neteaseApiBaseUrl:
      process.env.NETEASE_API_BASE_URL ??
      (isTest || mockNetease || localRelayMode ? "http://127.0.0.1:3000" : ""),
    mockNetease,
    localRelayMode,
    allowStaleDeviceTimestamps:
      process.env.ALLOW_STALE_DEVICE_TIMESTAMPS === "true",
  });
}

export function validateConfig(config: AppConfig): AppConfig {
  if (!Number.isInteger(config.port) || config.port <= 0 || config.port > 65535) {
    throw new Error("PORT must be an integer from 1 to 65535");
  }
  if (!config.cookieEncryptionKey) {
    throw new Error("COOKIE_ENCRYPTION_KEY is required");
  }
  if (!config.deviceRegistrationToken.trim()) {
    throw new Error("DEVICE_REGISTRATION_TOKEN is required");
  }
  if (!config.mockNetease && !config.localRelayMode && !config.neteaseApiBaseUrl.trim()) {
    throw new Error("NETEASE_API_BASE_URL is required");
  }
  try {
    new URL(config.neteaseApiBaseUrl);
  } catch {
    throw new Error("NETEASE_API_BASE_URL must be a valid URL");
  }
  if (config.allowStaleDeviceTimestamps && !config.mockNetease && !config.localRelayMode) {
    throw new Error("ALLOW_STALE_DEVICE_TIMESTAMPS is only allowed in mock or local relay mode");
  }

  return config;
}
