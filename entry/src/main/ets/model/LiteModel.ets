import { hilog } from '@kit.PerformanceAnalysisKit';
import { ConversationContext, LlmProvider, LocalModel, LocalModelSettings, OpenAiCompatibleModel, ScriptedLocalModel } from '@loop/agent-core';
import liteModelNative from 'liblite_model_native.so';

const LOG_DOMAIN = 0x0000;
const LOG_TAG = 'LiteLlmModel';
const RESPONSE_PREVIEW_CHARS = 1000;
const LOOP_SYSTEM_PROMPT = 'You are Loop, a concise ReAct agent on HarmonyOS. Reply with Thought and either Action: tool(JSON object) or Final: answer. Use only listed tools and follow the tool-routing rules in the prompt exactly.';

let appFilesDir: string = '';
let activeRoot: string = '';
let activeModel: LiteModel | null = null;

export function configureLiteModel(filesDir: string): void {
  appFilesDir = filesDir.trim().replace(/\\/g, '/');
}

export function resolveLiteModelRoot(configuredRoot: string, filesDir: string): string {
  const selectedRoot = configuredRoot.trim();
  const selectedFilesDir = filesDir.trim();
  if (selectedRoot.length === 0 && selectedFilesDir.length === 0) {
    throw new Error('[RUNTIME_ROOT_INVALID] App files directory is not configured.');
  }
  let root = selectedRoot.length > 0 ? selectedRoot : selectedFilesDir + '/lite_llm';
  root = root.replace(/\\/g, '/');
  while (root.length > 1 && root.endsWith('/')) {
    root = root.substring(0, root.length - 1);
  }
  if (!root.startsWith('/')) {
    throw new Error('[RUNTIME_ROOT_INVALID] Lite model root must be an absolute path.');
  }
  return root;
}

export function liteModelConfigPath(configuredRoot: string, filesDir: string): string {
  return resolveLiteModelRoot(configuredRoot, filesDir) + '/config.json';
}

export function buildLitePrompt(prompt: string): string {
  return LOOP_SYSTEM_PROMPT + '\n\n' + prompt;
}

function errorText(error: Error | Object | string | number | boolean | null | undefined): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (error === undefined || error === null) {
    return '';
  }
  return error.toString();
}

function hasResponseTag(response: string, tag: string): boolean {
  return response.startsWith(tag) || response.indexOf('\n' + tag) >= 0 || response.indexOf('\r' + tag) >= 0;
}

function logVendorResponse(response: string): void {
  const hasAction = hasResponseTag(response, 'Action:');
  const hasFinal = hasResponseTag(response, 'Final:');
  const valid = hasAction || hasFinal;
  const kind = hasFinal ? 'final' : (hasAction ? 'action' : 'invalid');
  const preview = response.replace(/\r/g, '\\r').replace(/\n/g, '\\n').substring(0, RESPONSE_PREVIEW_CHARS);
  if (valid) {
    hilog.info(LOG_DOMAIN, LOG_TAG,
      'VENDOR_RESPONSE valid=true kind=%{public}s chars=%{public}d preview=%{public}s',
      kind, response.length, preview);
  } else {
    hilog.warn(LOG_DOMAIN, LOG_TAG,
      'VENDOR_RESPONSE valid=false reason=missing_Action_or_Final chars=%{public}d preview=%{public}s',
      response.length, preview);
  }
}

export class LiteModel implements LocalModel {
  private configPath: string;
  private initialization: Promise<void> | null = null;
  private released: boolean = false;

  constructor(configPath: string) {
    this.configPath = configPath;
  }

  async complete(prompt: string, _conversation?: ConversationContext): Promise<string> {
    if (this.released) {
      throw new Error('[BACKEND_RELEASED] The local model has been released.');
    }
    await this.ensureInitialized();
    hilog.info(LOG_DOMAIN, LOG_TAG, 'GENERATION_START implementation=LiteModel promptChars=%{public}d', prompt.length);
    const output = (await liteModelNative.generate(buildLitePrompt(prompt))).trim();
    if (output.length === 0) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'VENDOR_RESPONSE valid=false reason=empty');
      throw new Error('[EMPTY_MODEL_RESPONSE] Local LLM returned an empty response.');
    }
    logVendorResponse(output);
    return output;
  }

  async release(): Promise<void> {
    if (this.released) {
      return;
    }
    this.released = true;
    const initialization = this.initialization;
    if (initialization !== null) {
      try {
        await initialization;
      } catch (_error) {
      }
    }
    this.initialization = null;
    await liteModelNative.release();
    hilog.info(LOG_DOMAIN, LOG_TAG, 'MODEL_RELEASED implementation=LiteModel');
  }

  private ensureInitialized(): Promise<void> {
    if (this.initialization !== null) {
      return this.initialization;
    }
    if (!liteModelNative.isVendorAvailable()) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MODEL_INIT vendorAvailable=false configPath=%{public}s', this.configPath);
      return Promise.reject(new Error('[NATIVE_UNAVAILABLE] The vendor header/library was not compiled for this ABI.'));
    }
    hilog.info(LOG_DOMAIN, LOG_TAG, 'MODEL_INIT_START vendorAvailable=true configPath=%{public}s', this.configPath);
    const initialization = liteModelNative.initialize(this.configPath);
    this.initialization = initialization;
    initialization.then((): void => {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MODEL_INIT_COMPLETE implementation=LiteModel configPath=%{public}s', this.configPath);
    }, (error: Error | Object | string | number | boolean | null | undefined): void => {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MODEL_INIT_FAILED error=%{public}s', errorText(error));
      if (this.initialization === initialization) {
        this.initialization = null;
      }
    });
    return initialization;
  }
}

export async function createEntryModel(settings: LocalModelSettings): Promise<LocalModel> {
  if (settings.mode === 'lite') {
    const root = resolveLiteModelRoot(settings.liteModelRoot, appFilesDir);
    if (activeModel !== null && activeRoot === root) {
      hilog.info(LOG_DOMAIN, LOG_TAG,
        'MODEL_SELECTED mode=lite implementation=LiteModel reused=true root=%{public}s', root);
      return activeModel;
    }
    await releaseLiteModel();
    activeRoot = root;
    activeModel = new LiteModel(root + '/config.json');
    hilog.info(LOG_DOMAIN, LOG_TAG,
      'MODEL_SELECTED mode=lite implementation=LiteModel reused=false root=%{public}s', root);
    return activeModel;
  }

  await releaseLiteModel();
  if (settings.mode === 'scripted') {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'MODEL_SELECTED mode=scripted implementation=ScriptedLocalModel');
    return new ScriptedLocalModel();
  }
  const provider = new LlmProvider(
    settings.provider,
    settings.apiKey,
    settings.baseUrl,
    settings.model,
    settings.customParametersJson
  );
  if (!provider.isConfigured()) {
    hilog.error(LOG_DOMAIN, LOG_TAG, 'MODEL_SELECTED mode=remote configured=false');
    throw new Error('[REMOTE_NOT_CONFIGURED] Remote mode requires an API key, base URL, and model.');
  }
  hilog.info(LOG_DOMAIN, LOG_TAG,
    'MODEL_SELECTED mode=remote implementation=OpenAiCompatibleModel provider=%{public}s model=%{public}s',
    settings.provider, settings.model);
  return new OpenAiCompatibleModel(provider);
}

export async function releaseLiteModel(): Promise<void> {
  const model = activeModel;
  activeModel = null;
  activeRoot = '';
  if (model !== null) {
    await model.release();
  }
}
