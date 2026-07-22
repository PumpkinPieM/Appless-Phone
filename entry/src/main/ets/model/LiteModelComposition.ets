import { common } from '@kit.AbilityKit';
import {
  createLoopModelInfo,
  LiteModel,
  LlmProvider,
  LocalModel,
  LocalModelSettings
} from '@loop/agent-core';
import { NapiLiteInferenceBackend } from './NapiLiteInferenceBackend';
import { resolveLiteModelRuntimeRoot } from './LiteModelRuntimeRoot';

let abilityContext: common.UIAbilityContext | null = null;
let activeLiteRoot: string = '';
let activeLiteModel: LiteModel | null = null;

export function configureLiteModelComposition(context: common.UIAbilityContext): void {
  abilityContext = context;
}

export async function entryLoopModel(settings: LocalModelSettings): Promise<LocalModel> {
  if (abilityContext === null) {
    throw new Error('[RUNTIME_ROOT_INVALID] Entry ability context is not available.');
  }
  if (settings.mode === 'lite') {
    const runtimeRoot = resolveLiteModelRuntimeRoot(abilityContext, settings.liteModelRoot);
    if (activeLiteModel !== null && activeLiteRoot === runtimeRoot) {
      return activeLiteModel;
    }
    if (activeLiteModel !== null) {
      await activeLiteModel.release();
    }
    const backend = new NapiLiteInferenceBackend(abilityContext);
    const info = await createLoopModelInfo(abilityContext, undefined, {
      requestedMode: 'lite',
      liteBackend: backend,
      liteOptions: { runtimeRoot: runtimeRoot }
    });
    activeLiteRoot = runtimeRoot;
    activeLiteModel = info.model as LiteModel;
    return info.model;
  }

  const provider = new LlmProvider(
    settings.provider,
    settings.apiKey,
    settings.baseUrl,
    settings.model,
    settings.customParametersJson
  );
  const info = await createLoopModelInfo(abilityContext, undefined, {
    requestedMode: settings.mode,
    remoteProvider: provider
  });
  return info.model;
}

export async function releaseEntryLiteModel(): Promise<void> {
  const model = activeLiteModel;
  activeLiteModel = null;
  activeLiteRoot = '';
  if (model !== null) {
    await model.release();
  }
}
