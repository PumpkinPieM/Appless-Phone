import { ConversationContext } from '../agent/ConversationContext';
import { buildSkillsSection, SkillSnapshot } from '../skill/SkillSnapshot';
import { LiteInferenceBackend } from './LiteInferenceBackend';
import { LocalModel } from './LocalModel';
import { LiteModelOptions, liteModelError } from './LiteModelTypes';

export class LiteModel implements LocalModel {
  private backend: LiteInferenceBackend;
  private options: LiteModelOptions;
  private skillSnapshot: SkillSnapshot | null;
  private initialization: Promise<void> | null = null;
  private tail: Promise<void> = Promise.resolve();
  private released: boolean = false;

  constructor(backend: LiteInferenceBackend, options: LiteModelOptions, skillSnapshot?: SkillSnapshot) {
    this.backend = backend;
    this.options = options;
    this.skillSnapshot = skillSnapshot ?? null;
  }

  complete(prompt: string, _conversation?: ConversationContext): Promise<string> {
    const operation: Promise<string> = this.tail.then(async (): Promise<string> => {
      if (this.released) {
        throw liteModelError('BACKEND_RELEASED', 'The local model backend has been released.');
      }
      await this.ensureInitialized();
      const text = (await this.backend.generate(this.buildPrompt(prompt))).trim();
      if (text.length === 0) {
        throw liteModelError('EMPTY_MODEL_RESPONSE', 'Local LLM returned an empty response.');
      }
      return text;
    });
    this.tail = operation.then((): void => {}, (): void => {});
    return operation;
  }

  release(): Promise<void> {
    const operation: Promise<void> = this.tail.then(async (): Promise<void> => {
      if (this.released) {
        return;
      }
      this.released = true;
      this.initialization = null;
      await this.backend.release();
    });
    this.tail = operation.then((): void => {}, (): void => {});
    return operation;
  }

  private ensureInitialized(): Promise<void> {
    if (this.initialization !== null) {
      return this.initialization;
    }
    const root = this.options.runtimeRoot.trim().replace(/\\/g, '/').replace(/\/$/, '');
    if (root.length === 0) {
      return Promise.reject(liteModelError('RUNTIME_ROOT_INVALID', 'Runtime root is empty.'));
    }
    const initialization = this.backend.initialize({ configPath: root + '/config.json' });
    this.initialization = initialization;
    initialization.catch((): void => {
      if (this.initialization === initialization) {
        this.initialization = null;
      }
    });
    return initialization;
  }

  private buildPrompt(prompt: string): string {
    return this.systemPrompt() + '\n\n' + prompt;
  }

  private systemPrompt(): string {
    let content = 'You are Loop, a concise ReAct agent on HarmonyOS. Reply with Thought and either Action: tool(JSON object) or Final: answer. Use only listed tools and follow the tool-routing rules in the prompt exactly.';
    const skills = buildSkillsSection(this.skillSnapshot);
    if (skills.length > 0) {
      content += `\n\n${skills}`;
    }
    return content;
  }
}
