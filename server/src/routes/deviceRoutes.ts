import type { FastifyInstance, FastifyReply, FastifyRequest } from "fastify";
import {
  DeviceAuthError,
  type DeviceAuthService,
  type RegisterDeviceInput,
} from "../services/deviceAuth.js";

export function registerDeviceRoutes(
  app: FastifyInstance,
  auth: DeviceAuthService,
) {
  app.post(
    "/api/v1/device/register",
    {
      preHandler: [app.rateLimit({ max: 20, timeWindow: "1 minute" })],
    },
    async (req, reply) => {
      try {
        const identity = await auth.registerDevice(req.body as RegisterDeviceInput);
        return {
          device_id: identity.deviceId,
          registered: true,
        };
      } catch (error) {
        return sendDeviceAuthError(reply, error);
      }
    },
  );

  app.post(
    "/api/v1/device/hello",
    { preHandler: [createDevicePreHandler(auth)] },
    async (req) => ({ device_id: req.device?.deviceId }),
  );
}

export function createDevicePreHandler(auth: DeviceAuthService) {
  return async (req: FastifyRequest, reply: FastifyReply) => {
    try {
      req.device = await auth.verifyDeviceRequest(req);
    } catch (error) {
      return sendDeviceAuthError(reply, error);
    }
  };
}

function sendDeviceAuthError(reply: FastifyReply, error: unknown) {
  if (error instanceof DeviceAuthError) {
    return reply.status(statusForDeviceAuthError(error.code)).send({
      error: error.code,
    });
  }
  throw error;
}

function statusForDeviceAuthError(code: string) {
  if (code === "device_already_registered") {
    return 409;
  }
  if (code === "invalid_device_registration" || code === "invalid_device_secret") {
    return 400;
  }
  return 401;
}
