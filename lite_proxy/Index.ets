import nativeProxy from 'liblite_proxy.so';

export type LiteProxyState = 'stopped' | 'waiting' | 'ready' | 'stopping' | 'error';

export interface LiteProxyOptions {
  httpPort?: number;
  reversePort?: number;
  authToken?: string;
  requestTimeoutMs?: number;
  maxBodyBytes?: number;
}

export interface LiteProxyStatus {
  state: LiteProxyState;
  running: boolean;
  serverConnected: boolean;
  httpPort: number;
  reversePort: number;
  baseUrl: string;
  authToken: string;
  pendingRequests: number;
  lastError: string;
}

const DEFAULT_REQUEST_TIMEOUT_MS = 120000;
const DEFAULT_MAX_BODY_BYTES = 4 * 1024 * 1024;

export class LiteProxy {
  static async start(options: LiteProxyOptions = {}): Promise<LiteProxyStatus> {
    const status = nativeProxy.start({
      httpPort: options.httpPort ?? 0,
      reversePort: options.reversePort ?? 0,
      authToken: options.authToken ?? '',
      requestTimeoutMs: options.requestTimeoutMs ?? DEFAULT_REQUEST_TIMEOUT_MS,
      maxBodyBytes: options.maxBodyBytes ?? DEFAULT_MAX_BODY_BYTES
    });
    return {
      state: status.state as LiteProxyState,
      running: status.running,
      serverConnected: status.serverConnected,
      httpPort: status.httpPort,
      reversePort: status.reversePort,
      baseUrl: status.baseUrl,
      authToken: status.authToken,
      pendingRequests: status.pendingRequests,
      lastError: status.lastError
    };
  }

  static status(): LiteProxyStatus {
    const status = nativeProxy.getStatus();
    return {
      state: status.state as LiteProxyState,
      running: status.running,
      serverConnected: status.serverConnected,
      httpPort: status.httpPort,
      reversePort: status.reversePort,
      baseUrl: status.baseUrl,
      authToken: status.authToken,
      pendingRequests: status.pendingRequests,
      lastError: status.lastError
    };
  }

  static async stop(): Promise<void> {
    nativeProxy.stop();
  }
}
