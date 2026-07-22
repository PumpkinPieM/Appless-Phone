import { common } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { LiteInferenceBackend, LiteInitializeRequest } from '@loop/agent-core';
import liteModelNative from 'liblite_model_native.so';
import { prepareResolvedLiteConfig } from './LiteModelRuntimeRoot';

const DOMAIN = 0x0000;
const TAG = 'LiteModel';
const RESPONSE_LOG_CHUNK_CHARS = 3000;

function errorMessage(error: Error | Object | string | number | boolean | null | undefined): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (error === undefined || error === null) {
    return '';
  }
  return error.toString();
}

function mappedError(error: Error | Object | string | number | boolean | null | undefined, fallbackCode: string): Error {
  const message = errorMessage(error);
  if (message.startsWith('[')) {
    return new Error(message);
  }
  return new Error('[' + fallbackCode + '] ' + (message.length > 0 ? message : 'Native local-model operation failed.'));
}

function logModelResponse(output: string): void {
  const escaped = output.replace(/\r/g, '\\r').replace(/\n/g, '\\n');
  const chunkCount = Math.max(1, Math.ceil(escaped.length / RESPONSE_LOG_CHUNK_CHARS));
  for (let index = 0; index < chunkCount; index++) {
    const chunk = escaped.substring(index * RESPONSE_LOG_CHUNK_CHARS, (index + 1) * RESPONSE_LOG_CHUNK_CHARS);
    hilog.info(DOMAIN, TAG, 'model response chunk=%{public}d/%{public}d data=%{public}s',
      index + 1, chunkCount, chunk);
  }
}

export class NapiLiteInferenceBackend implements LiteInferenceBackend {
  private context: common.UIAbilityContext;
  private initialized: boolean = false;
  private released: boolean = false;

  constructor(context: common.UIAbilityContext) {
    this.context = context;
  }

  async initialize(request: LiteInitializeRequest): Promise<void> {
    if (this.released) {
      throw new Error('[BACKEND_RELEASED] The local-model backend has been released.');
    }
    if (this.initialized) {
      return;
    }
    hilog.info(DOMAIN, TAG, 'load requested configPath=%{public}s', request.configPath);
    let vendorAvailable = false;
    try {
      vendorAvailable = liteModelNative.isVendorAvailable();
    } catch (error) {
      const mapped = mappedError(error, 'NATIVE_UNAVAILABLE');
      hilog.error(DOMAIN, TAG, 'load bridge check failed error=%{public}s', mapped.message);
      throw mapped;
    }
    hilog.info(DOMAIN, TAG, 'load bridge ready vendorAvailable=%{public}s',
      vendorAvailable ? 'true' : 'false');
    if (!vendorAvailable) {
      throw new Error('[NATIVE_UNAVAILABLE] The vendor header/library was not compiled for this ABI.');
    }
    const started = Date.now();
    const resolvedConfig = prepareResolvedLiteConfig(this.context, request.configPath);
    hilog.info(DOMAIN, TAG, 'load start configChars=%{public}d', resolvedConfig.length);
    try {
      await liteModelNative.initialize(resolvedConfig);
      this.initialized = true;
      hilog.info(DOMAIN, TAG, 'load complete elapsedMs=%{public}d', Date.now() - started);
    } catch (error) {
      const mapped = mappedError(error, 'MODEL_LOAD_FAILED');
      hilog.error(DOMAIN, TAG, 'load failed elapsedMs=%{public}d error=%{public}s',
        Date.now() - started, mapped.message);
      throw mapped;
    }
  }

  async generate(prompt: string): Promise<string> {
    if (this.released) {
      throw new Error('[BACKEND_RELEASED] The local-model backend has been released.');
    }
    if (!this.initialized) {
      throw new Error('[MODEL_NOT_INITIALIZED] initialize must complete before generation.');
    }
    const started = Date.now();
    hilog.info(DOMAIN, TAG, 'generation start promptChars=%{public}d', prompt.length);
    try {
      const output: string = await liteModelNative.generate(prompt);
      hilog.info(DOMAIN, TAG, 'generation complete elapsedMs=%{public}d outputChars=%{public}d',
        Date.now() - started, output.length);
      logModelResponse(output);
      return output;
    } catch (error) {
      const mapped = mappedError(error, 'GENERATION_FAILED');
      hilog.error(DOMAIN, TAG, 'generation failed elapsedMs=%{public}d error=%{public}s',
        Date.now() - started, mapped.message);
      throw mapped;
    }
  }

  async release(): Promise<void> {
    if (this.released) {
      return;
    }
    this.released = true;
    this.initialized = false;
    try {
      await liteModelNative.release();
    } catch (error) {
      throw mappedError(error, 'BACKEND_RELEASED');
    }
  }
}
