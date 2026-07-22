import { describe, expect, it } from '@ohos/hypium';
import {
  AgentSkill,
  ConversationContext,
  LiteInferenceBackend,
  LiteInitializeRequest,
  LiteModel,
  LoopBackend,
  LoopBackendRequest,
  SkillSnapshot
} from '@loop/agent-core';

const LOOP_SYSTEM_PROMPT = 'You are Loop, a concise ReAct agent on HarmonyOS. Reply with Thought and either Action: tool(JSON object) or Final: answer. Use only listed tools and follow the tool-routing rules in the prompt exactly.';

class FakeLiteBackend implements LiteInferenceBackend {
  initializeCalls: number = 0;
  generateCalls: string[] = [];
  releaseCalls: number = 0;
  activeGenerations: number = 0;
  maximumActiveGenerations: number = 0;
  failNextInitialize: boolean = false;

  async initialize(_request: LiteInitializeRequest): Promise<void> {
    this.initializeCalls++;
    if (this.failNextInitialize) {
      this.failNextInitialize = false;
      throw new Error('[MODEL_LOAD_FAILED] fake load failure');
    }
  }

  async generate(prompt: string): Promise<string> {
    this.generateCalls.push(prompt);
    this.activeGenerations++;
    this.maximumActiveGenerations = Math.max(this.maximumActiveGenerations, this.activeGenerations);
    await new Promise<void>((resolve: () => void): void => {
      setTimeout(resolve, 5);
    });
    this.activeGenerations--;
    return '  ' + prompt + '  ';
  }

  async release(): Promise<void> {
    this.releaseCalls++;
  }
}

async function rejectedMessage(operation: Promise<Object | string | void>): Promise<string> {
  try {
    await operation;
    return '';
  } catch (error) {
    return error instanceof Error ? error.message : `${error}`;
  }
}

export default function liteModelTest() {
  describe('liteModel', () => {
    it('adds the system prompt without appending conversation history again', 0, async () => {
      const backend = new FakeLiteBackend();
      const model = new LiteModel(backend, { runtimeRoot: '/data/lite_llm' });
      const conversation = new ConversationContext();
      conversation.addUser('must not be appended');

      const output = await model.complete('FULL REACT PROMPT', conversation);

      expect(output).assertEqual(LOOP_SYSTEM_PROMPT + '\n\nFULL REACT PROMPT');
      expect(backend.generateCalls.length).assertEqual(1);
      expect(backend.generateCalls[0]).assertEqual(LOOP_SYSTEM_PROMPT + '\n\nFULL REACT PROMPT');
      expect(backend.generateCalls[0].indexOf('must not be appended')).assertEqual(-1);
      expect(backend.initializeCalls).assertEqual(1);
    });

    it('includes configured skills in the local system prompt', 0, async () => {
      const backend = new FakeLiteBackend();
      const skill = new AgentSkill('weather', 'Read real weather', '', '', '/skills/weather/SKILL.md', 'Use weather.');
      const snapshot = new SkillSnapshot([skill], '<available_skills>weather</available_skills>');
      const model = new LiteModel(backend, { runtimeRoot: '/data/lite_llm' }, snapshot);

      await model.complete('FULL REACT PROMPT');

      expect(backend.generateCalls[0].indexOf('## Skills')).assertLarger(-1);
      expect(backend.generateCalls[0].indexOf('<available_skills>weather</available_skills>')).assertLarger(-1);
      expect(backend.generateCalls[0].endsWith('\n\nFULL REACT PROMPT')).assertTrue();
    });

    it('initializes once and serializes concurrent generations', 0, async () => {
      const backend = new FakeLiteBackend();
      const model = new LiteModel(backend, { runtimeRoot: '/data/lite_llm' });

      const outputs = await Promise.all([model.complete('one'), model.complete('two')]);

      expect(outputs[0]).assertEqual(LOOP_SYSTEM_PROMPT + '\n\none');
      expect(outputs[1]).assertEqual(LOOP_SYSTEM_PROMPT + '\n\ntwo');
      expect(backend.initializeCalls).assertEqual(1);
      expect(backend.maximumActiveGenerations).assertEqual(1);
    });

    it('clears a failed initialization so a later call can retry', 0, async () => {
      const backend = new FakeLiteBackend();
      backend.failNextInitialize = true;
      const model = new LiteModel(backend, { runtimeRoot: '/data/lite_llm' });

      const firstError = await rejectedMessage(model.complete('first'));
      const second = await model.complete('second');

      expect(firstError).assertContain('[MODEL_LOAD_FAILED]');
      expect(second).assertEqual(LOOP_SYSTEM_PROMPT + '\n\nsecond');
      expect(backend.initializeCalls).assertEqual(2);
    });

    it('releases once and rejects later generation', 0, async () => {
      const backend = new FakeLiteBackend();
      const model = new LiteModel(backend, { runtimeRoot: '/data/lite_llm' });

      await model.release();
      await model.release();
      const error = await rejectedMessage(model.complete('after release'));

      expect(backend.releaseCalls).assertEqual(1);
      expect(error).assertContain('[BACKEND_RELEASED]');
    });

    it('never falls back to scripted output when lite injection is missing', 0, async () => {
      const request: LoopBackendRequest = {
        prompt: 'must not become scripted success',
        history: [],
        modelSettings: {
          mode: 'lite',
          provider: '',
          baseUrl: '',
          apiKey: '',
          model: '',
          customParametersJson: ''
        },
        surfaceId: 'surface_test',
        actionId: 'action_test',
        maxSteps: 1
      };

      const error = await rejectedMessage(new LoopBackend().run(request, {}));

      expect(error).assertContain('[NATIVE_UNAVAILABLE]');
    });
  });
}
