import { afterEach, describe, expect, test, vi } from "vitest";
import { loadConfig } from "../src/config.js";

describe("server config", () => {
  afterEach(() => {
    vi.unstubAllEnvs();
  });

  test("requires an explicit NetEase API base URL outside tests and mock mode", () => {
    vi.stubEnv("NODE_ENV", "production");
    vi.stubEnv("VITEST", "false");
    vi.stubEnv("COOKIE_ENCRYPTION_KEY", "12345678901234567890123456789012");
    vi.stubEnv("DEVICE_REGISTRATION_TOKEN", "pair-123");
    vi.stubEnv("MOCK_NETEASE", "false");
    delete process.env.NETEASE_API_BASE_URL;

    expect(() => loadConfig()).toThrow("NETEASE_API_BASE_URL is required");
  });

  test("local relay mode uses a local NetEase adapter by default", () => {
    vi.stubEnv("NODE_ENV", "production");
    vi.stubEnv("VITEST", "false");
    vi.stubEnv("COOKIE_ENCRYPTION_KEY", "12345678901234567890123456789012");
    vi.stubEnv("DEVICE_REGISTRATION_TOKEN", "pair-123");
    vi.stubEnv("LOCAL_RELAY_MODE", "true");
    vi.stubEnv("MOCK_NETEASE", "false");
    delete process.env.NETEASE_API_BASE_URL;

    expect(loadConfig()).toMatchObject({
      localRelayMode: true,
      neteaseApiBaseUrl: "http://127.0.0.1:3000",
    });
  });

  test("local relay mode may accept stale device timestamps", () => {
    vi.stubEnv("NODE_ENV", "production");
    vi.stubEnv("VITEST", "false");
    vi.stubEnv("COOKIE_ENCRYPTION_KEY", "12345678901234567890123456789012");
    vi.stubEnv("DEVICE_REGISTRATION_TOKEN", "pair-123");
    vi.stubEnv("LOCAL_RELAY_MODE", "true");
    vi.stubEnv("ALLOW_STALE_DEVICE_TIMESTAMPS", "true");
    vi.stubEnv("MOCK_NETEASE", "false");
    delete process.env.NETEASE_API_BASE_URL;

    expect(loadConfig().allowStaleDeviceTimestamps).toBe(true);
  });
});
