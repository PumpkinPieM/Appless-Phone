export type LiteModelErrorCode =
  'NATIVE_UNAVAILABLE' |
  'RUNTIME_ROOT_INVALID' |
  'CONFIG_NOT_FOUND' |
  'CONFIG_NOT_READABLE' |
  'CONFIG_INVALID_JSON' |
  'MODEL_DIRECTORY_NOT_FOUND' |
  'MODEL_PATH_ESCAPE' |
  'RESOLVED_CONFIG_WRITE_FAILED' |
  'MODEL_LOAD_FAILED' |
  'MODEL_NOT_INITIALIZED' |
  'GENERATION_FAILED' |
  'EMPTY_MODEL_RESPONSE' |
  'BACKEND_RELEASED';

export interface LiteModelOptions {
  runtimeRoot: string;
}

export interface LiteInitializeRequest {
  configPath: string;
}

export function liteModelError(code: LiteModelErrorCode, detail: string): Error {
  return new Error('[' + code + '] ' + detail);
}
