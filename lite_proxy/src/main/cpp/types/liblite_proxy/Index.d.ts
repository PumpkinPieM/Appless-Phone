interface NativeProxyOptions {
  httpPort: number;
  reversePort: number;
  authToken: string;
  requestTimeoutMs: number;
  maxBodyBytes: number;
}

interface NativeProxyStatus {
  state: string;
  running: boolean;
  serverConnected: boolean;
  httpPort: number;
  reversePort: number;
  baseUrl: string;
  authToken: string;
  pendingRequests: number;
  lastError: string;
}

export const start: (options: NativeProxyOptions) => NativeProxyStatus;
export const getStatus: () => NativeProxyStatus;
export const stop: () => void;
