import { common } from '@kit.AbilityKit';
import fs from '@ohos.file.fs';

type JsonRecord = Record<string, Object | string | number | boolean | null>;

function fail(code: string, detail: string): Error {
  return new Error('[' + code + '] ' + detail);
}

export function canonicalAbsoluteLitePath(path: string): string {
  const normalized = path.trim().replace(/\\/g, '/');
  if (!normalized.startsWith('/')) {
    throw fail('RUNTIME_ROOT_INVALID', 'Path must be absolute: ' + normalized);
  }
  const output: string[] = [];
  const parts = normalized.split('/');
  for (let index = 0; index < parts.length; index++) {
    const part = parts[index];
    if (part.length === 0 || part === '.') {
      continue;
    }
    if (part === '..') {
      if (output.length === 0) {
        throw fail('RUNTIME_ROOT_INVALID', 'Path escapes the filesystem root.');
      }
      output.pop();
    } else {
      output.push(part);
    }
  }
  return '/' + output.join('/');
}

export function resolveLiteModelRuntimeRoot(context: common.UIAbilityContext, configuredRoot: string): string {
  const selected = configuredRoot.trim().length > 0 ? configuredRoot.trim() : context.filesDir + '/lite_llm';
  return canonicalAbsoluteLitePath(selected);
}

function isInsideModelRoot(candidate: string, modelRoot: string): boolean {
  return candidate === modelRoot || candidate.startsWith(modelRoot + '/');
}

export function resolveLiteConfigValue(
  value: Object | string | number | boolean | null,
  runtimeRoot: string
): Object | string | number | boolean | null {
  const root = canonicalAbsoluteLitePath(runtimeRoot);
  const modelRoot = canonicalAbsoluteLitePath(root + '/model');
  if (typeof value === 'string') {
    const normalized = value.replace(/\\/g, '/');
    if (normalized === 'model' || normalized.startsWith('model/')) {
      const relative = normalized === 'model' ? '' : normalized.substring('model/'.length);
      const candidate = canonicalAbsoluteLitePath(modelRoot + '/' + relative);
      if (!isInsideModelRoot(candidate, modelRoot)) {
        throw fail('MODEL_PATH_ESCAPE', 'A model path escapes the declared model directory.');
      }
      return candidate;
    }
    return value;
  }
  if (value === null || typeof value === 'number' || typeof value === 'boolean') {
    return value;
  }
  if (Array.isArray(value)) {
    const source = value as Array<Object | string | number | boolean | null>;
    const resolved: Array<Object | string | number | boolean | null> = [];
    for (let index = 0; index < source.length; index++) {
      resolved.push(resolveLiteConfigValue(source[index], root));
    }
    return resolved;
  }
  const source = value as JsonRecord;
  const resolved: JsonRecord = {};
  Object.keys(source).forEach((key: string): void => {
    resolved[key] = resolveLiteConfigValue(source[key], root);
  });
  return resolved;
}

export function liteResolvedConfigHash(runtimeRoot: string, originalContent: string): string {
  const input = canonicalAbsoluteLitePath(runtimeRoot) + '\n' + originalContent;
  let hash = 5381;
  for (let index = 0; index < input.length; index++) {
    hash = (hash * 33 + input.charCodeAt(index)) % 2147483647;
  }
  let hex = Math.abs(hash).toString(16);
  while (hex.length < 8) {
    hex = '0' + hex;
  }
  return hex;
}

function writeText(path: string, text: string): void {
  const file = fs.openSync(path, fs.OpenMode.READ_WRITE | fs.OpenMode.CREATE | fs.OpenMode.TRUNC);
  try {
    fs.writeSync(file.fd, text);
  } finally {
    fs.closeSync(file);
  }
}

export function prepareResolvedLiteConfig(context: common.UIAbilityContext, originalConfigPath: string): string {
  const configPath = canonicalAbsoluteLitePath(originalConfigPath);
  const suffix = '/config.json';
  if (!configPath.endsWith(suffix)) {
    throw fail('RUNTIME_ROOT_INVALID', 'Lite configuration path must end with /config.json.');
  }
  const runtimeRoot = canonicalAbsoluteLitePath(configPath.substring(0, configPath.length - suffix.length));
  const modelRoot = runtimeRoot + '/model';

  try {
    if (!fs.statSync(configPath).isFile()) {
      throw fail('CONFIG_NOT_FOUND', 'config.json is not a regular file.');
    }
  } catch (error) {
    if (error instanceof Error && error.message.startsWith('[CONFIG_NOT_FOUND]')) {
      throw error;
    }
    throw fail('CONFIG_NOT_FOUND', 'config.json was not found at the selected runtime root.');
  }
  try {
    if (!fs.statSync(modelRoot).isDirectory()) {
      throw fail('MODEL_DIRECTORY_NOT_FOUND', 'model is not a directory.');
    }
  } catch (error) {
    if (error instanceof Error && error.message.startsWith('[MODEL_DIRECTORY_NOT_FOUND]')) {
      throw error;
    }
    throw fail('MODEL_DIRECTORY_NOT_FOUND', 'The runtime root does not contain a model directory.');
  }

  let content = '';
  try {
    content = fs.readTextSync(configPath, { encoding: 'utf-8' });
  } catch (_error) {
    throw fail('CONFIG_NOT_READABLE', 'config.json could not be read.');
  }

  let parsed: Object | string | number | boolean | null;
  try {
    parsed = JSON.parse(content) as Object | string | number | boolean | null;
  } catch (_error) {
    throw fail('CONFIG_INVALID_JSON', 'config.json is not valid JSON.');
  }
  if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
    throw fail('CONFIG_INVALID_JSON', 'config.json must contain a JSON object.');
  }

  const resolved = resolveLiteConfigValue(parsed, runtimeRoot);
  const cacheDirectory = canonicalAbsoluteLitePath(context.cacheDir + '/lite_llm');
  const outputPath = cacheDirectory + '/resolved-config-' + liteResolvedConfigHash(runtimeRoot, content) + '.json';
  try {
    fs.mkdirSync(cacheDirectory, true);
    writeText(outputPath, JSON.stringify(resolved));
    if (!fs.statSync(outputPath).isFile()) {
      throw new Error('Resolved configuration is not a file.');
    }
    fs.readTextSync(outputPath, { encoding: 'utf-8' });
  } catch (_error) {
    throw fail('RESOLVED_CONFIG_WRITE_FAILED', 'The derived configuration could not be written or verified.');
  }
  return outputPath;
}
