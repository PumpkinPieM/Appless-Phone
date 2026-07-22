export type LocalModelMode = 'lite' | 'remote' | 'scripted';

export interface LocalModelSettings {
  mode: LocalModelMode;
  provider: string;
  baseUrl: string;
  model: string;
  apiKey: string;
  customParametersJson: string;
  ignoreToolSurfaceUpdates: boolean;
  liteModelRoot: string;
}

interface LocalModelRawConfig {
  DASHSCOPE_API_KEY?: string;
  LITE_MODEL_ROOT?: string;
  LOCAL_MODEL_MODE?: string;
}

const DEFAULT_PROVIDER: string = 'DashScope';
const DEFAULT_BASE_URL: string = 'https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions';
const DEFAULT_MODEL: string = 'qwen-max';
const DEFAULT_CUSTOM_PARAMETERS_JSON: string = '{"enable_thinking":false}';

let dashScopeApiKey: string = '';
let liteModelRoot: string = '';
let localModelMode: LocalModelMode = 'lite';

function textOf(value: Object | string | number | boolean | null | undefined): string {
  if (value === undefined || value === null) {
    return '';
  }
  if (typeof value === 'string') {
    return value;
  }
  if (typeof value === 'number' || typeof value === 'boolean') {
    return value.toString();
  }
  return JSON.stringify(value);
}

export function configureLocalModelSettingsFromRawJson(rawJson: string): void {
  const parsed = JSON.parse(rawJson) as LocalModelRawConfig;
  dashScopeApiKey = textOf(parsed.DASHSCOPE_API_KEY).trim();
  liteModelRoot = textOf(parsed.LITE_MODEL_ROOT).trim();
  const configuredMode = textOf(parsed.LOCAL_MODEL_MODE).trim().toLowerCase();
  if (configuredMode.length === 0) {
    localModelMode = 'lite';
  } else if (configuredMode === 'lite' || configuredMode === 'remote' || configuredMode === 'scripted') {
    localModelMode = configuredMode;
  } else {
    throw new Error('[MODEL_MODE_INVALID] LOCAL_MODEL_MODE must be lite, remote, or scripted.');
  }
}

export function localModelSettingsDebugSummary(): string {
  return 'mode=' + localModelMode +
    ', provider=' + DEFAULT_PROVIDER +
    ', model=' + DEFAULT_MODEL +
    ', dashScopeApiKey=' + (dashScopeApiKey.length > 0 ? 'present(' + dashScopeApiKey.length.toString() + ')' : 'missing') +
    ', liteModelRoot=' + (liteModelRoot.length > 0 ? 'configured' : 'app-files-default');
}

export function defaultLocalModelSettings(): LocalModelSettings {
  return {
    mode: localModelMode,
    provider: DEFAULT_PROVIDER,
    baseUrl: DEFAULT_BASE_URL,
    model: DEFAULT_MODEL,
    apiKey: dashScopeApiKey,
    customParametersJson: DEFAULT_CUSTOM_PARAMETERS_JSON,
    ignoreToolSurfaceUpdates: false,
    liteModelRoot: liteModelRoot
  };
}
