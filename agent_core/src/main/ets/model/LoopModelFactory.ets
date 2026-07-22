import { common } from '@kit.AbilityKit';
import { SkillSnapshot } from '../skill/SkillSnapshot';
import { LocalModel } from './LocalModel';
import { LiteInferenceBackend } from './LiteInferenceBackend';
import { LiteModel } from './LiteModel';
import { LiteModelOptions } from './LiteModelTypes';
import { LlmProvider } from './LlmProvider';
import { OpenAiCompatibleModel } from './OpenAiCompatibleModel';
import { ScriptedLocalModel } from './ScriptedLocalModel';

export type LoopModelMode = 'lite' | 'remote' | 'scripted';

export interface LoopModelInfo {
  model: LocalModel;
  mode: LoopModelMode;
  modelName: string;
}

export interface LoopModelDependencies {
  requestedMode?: LoopModelMode;
  liteBackend?: LiteInferenceBackend;
  liteOptions?: LiteModelOptions;
  remoteProvider?: LlmProvider;
}

export async function createLoopModel(
  context: common.UIAbilityContext,
  skillSnapshot?: SkillSnapshot,
  dependencies?: LoopModelDependencies
): Promise<LocalModel> {
  return (await createLoopModelInfo(context, skillSnapshot, dependencies)).model;
}

export async function createLoopModelInfo(
  context: common.UIAbilityContext,
  skillSnapshot?: SkillSnapshot,
  dependencies?: LoopModelDependencies
): Promise<LoopModelInfo> {
  const mode: LoopModelMode = dependencies?.requestedMode ?? 'lite';
  if (mode === 'lite') {
    if (dependencies?.liteBackend === undefined || dependencies.liteOptions === undefined) {
      throw new Error('[NATIVE_UNAVAILABLE] Lite mode requires an injected inference backend and runtime options.');
    }
    return {
      model: new LiteModel(dependencies.liteBackend, dependencies.liteOptions, skillSnapshot),
      mode: 'lite',
      modelName: 'lite-local'
    };
  }
  if (mode === 'remote') {
    const provider = dependencies?.remoteProvider ?? await LlmProvider.fromContext(context);
    if (!provider.isConfigured()) {
      throw new Error('[REMOTE_NOT_CONFIGURED] Remote mode requires an API key, base URL, and model.');
    }
    return {
      model: new OpenAiCompatibleModel(provider, skillSnapshot),
      mode: 'remote',
      modelName: provider.model
    };
  }
  return { model: new ScriptedLocalModel(), mode: 'scripted', modelName: 'scripted-local' };
}
