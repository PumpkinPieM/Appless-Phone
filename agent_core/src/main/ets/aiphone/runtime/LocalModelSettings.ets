export interface LocalModelSettings {
  provider: string;
  baseUrl: string;
  model: string;
  apiKey: string;
  customParametersJson: string;
  ignoreToolSurfaceUpdates: boolean;
}

interface LocalModelRawConfig {
  DASHSCOPE_API_KEY?: string;
  apiKey?: string;
}

const DEFAULT_PROVIDER: string = 'DashScope';
const DEFAULT_BASE_URL: string = 'http://127.0.0.1:18080/v1';
const DEFAULT_MODEL: string = 'qwen-max';
const DEFAULT_API_KEY: string = 'lite_proxy_token';
const DEFAULT_CUSTOM_PARAMETERS_JSON: string = '{"enable_thinking":false}';

let dashScopeApiKey: string = DEFAULT_API_KEY;

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
  const configuredKey = textOf(parsed.DASHSCOPE_API_KEY).trim();
  const proxyKey = textOf(parsed.apiKey).trim();
  dashScopeApiKey = configuredKey.length > 0 ? configuredKey :
    (proxyKey.length > 0 ? proxyKey : DEFAULT_API_KEY);
}

export function localModelSettingsDebugSummary(): string {
  return 'provider=' + DEFAULT_PROVIDER +
    ', model=' + DEFAULT_MODEL +
    ', dashScopeApiKey=' + (dashScopeApiKey.length > 0 ? 'present(' + dashScopeApiKey.length.toString() + ')' : 'missing');
}

export function defaultLocalModelSettings(): LocalModelSettings {
  return {
    provider: DEFAULT_PROVIDER,
    baseUrl: DEFAULT_BASE_URL,
    model: DEFAULT_MODEL,
    apiKey: dashScopeApiKey,
    customParametersJson: DEFAULT_CUSTOM_PARAMETERS_JSON,
    ignoreToolSurfaceUpdates: false
  };
}
