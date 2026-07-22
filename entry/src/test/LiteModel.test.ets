import { describe, expect, it } from '@ohos/hypium';
import { buildLitePrompt, liteModelConfigPath, resolveLiteModelRoot } from '../main/ets/model/LiteModel';

const LOOP_SYSTEM_PROMPT = 'You are Loop, a concise ReAct agent on HarmonyOS. Reply with Thought and either Action: tool(JSON object) or Final: answer. Use only listed tools and follow the tool-routing rules in the prompt exactly.';

export default function liteModelTest() {
  describe('liteModel', () => {
    it('uses the app files directory when no root is configured', 0, () => {
      expect(resolveLiteModelRoot('', '/data/storage/el2/base/files'))
        .assertEqual('/data/storage/el2/base/files/lite_llm');
      expect(liteModelConfigPath('', '/data/storage/el2/base/files'))
        .assertEqual('/data/storage/el2/base/files/lite_llm/config.json');
    });

    it('normalizes a configured absolute root', 0, () => {
      expect(resolveLiteModelRoot(' /data/models/lite/ ', '/unused'))
        .assertEqual('/data/models/lite');
    });

    it('rejects a relative root', 0, () => {
      let message = '';
      try {
        resolveLiteModelRoot('models/lite', '/unused');
      } catch (error) {
        message = error instanceof Error ? error.message : `${error}`;
      }
      expect(message).assertContain('[RUNTIME_ROOT_INVALID]');
    });

    it('prepends the Loop system prompt', 0, () => {
      expect(buildLitePrompt('FULL REACT PROMPT'))
        .assertEqual(LOOP_SYSTEM_PROMPT + '\n\nFULL REACT PROMPT');
    });
  });
}
