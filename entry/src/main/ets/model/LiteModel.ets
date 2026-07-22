import { ConversationContext, LlmProvider, LocalModel, LocalModelSettings, OpenAiCompatibleModel, ScriptedLocalModel } from '@loop/agent-core';
import liteModelNative from 'liblite_model_native.so';

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
    const output = (await liteModelNative.generate(buildLitePrompt(prompt))).trim();
    if (output.length === 0) {
      throw new Error('[EMPTY_MODEL_RESPONSE] Local LLM returned an empty response.');
    }
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
  }

  private ensureInitialized(): Promise<void> {
    if (this.initialization !== null) {
      return this.initialization;
    }
    if (!liteModelNative.isVendorAvailable()) {
      return Promise.reject(new Error('[NATIVE_UNAVAILABLE] The vendor header/library was not compiled for this ABI.'));
    }
    const initialization = liteModelNative.initialize(this.configPath);
    this.initialization = initialization;
    initialization.catch((): void => {
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
      return activeModel;
    }
    await releaseLiteModel();
    activeRoot = root;
    activeModel = new LiteModel(root + '/config.json');
    return activeModel;
  }

  await releaseLiteModel();
  if (settings.mode === 'scripted') {
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
    throw new Error('[REMOTE_NOT_CONFIGURED] Remote mode requires an API key, base URL, and model.');
  }
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
