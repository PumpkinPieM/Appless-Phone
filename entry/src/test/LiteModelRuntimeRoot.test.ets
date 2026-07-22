import { describe, expect, it } from '@ohos/hypium';
import {
  canonicalAbsoluteLitePath,
  liteResolvedConfigHash,
  resolveLiteConfigValue
} from '../main/ets/model/LiteModelRuntimeRoot';

interface NestedLiteConfig {
  tokenizer: string;
}

interface TestLiteConfig {
  weights: string;
  note: string;
  nested: NestedLiteConfig;
  parts: string[];
}

export default function liteModelRuntimeRootTest() {
  describe('liteModelRuntimeRoot', () => {
    it('recursively resolves model paths and preserves non-path strings', 0, () => {
      const original: TestLiteConfig = {
        weights: 'model/weights.bin',
        note: 'not-a-path',
        nested: { tokenizer: 'model\\tokenizer.json' },
        parts: ['model/a.bin', 'plain']
      };

      const resolved = resolveLiteConfigValue(original, '/data/files/lite_llm') as TestLiteConfig;

      expect(resolved.weights).assertEqual('/data/files/lite_llm/model/weights.bin');
      expect(resolved.note).assertEqual('not-a-path');
      expect(resolved.nested.tokenizer).assertEqual('/data/files/lite_llm/model/tokenizer.json');
      expect(resolved.parts[0]).assertEqual('/data/files/lite_llm/model/a.bin');
      expect(original.weights).assertEqual('model/weights.bin');
    });

    it('rejects traversal outside the model directory', 0, () => {
      let message = '';
      try {
        resolveLiteConfigValue('model/../outside.bin', '/data/files/lite_llm');
      } catch (error) {
        message = error instanceof Error ? error.message : `${error}`;
      }
      expect(message).assertContain('[MODEL_PATH_ESCAPE]');
    });

    it('canonicalizes absolute paths and rejects relative roots', 0, () => {
      expect(canonicalAbsoluteLitePath('/data/files/./lite/../lite_llm'))
        .assertEqual('/data/files/lite_llm');
      let message = '';
      try {
        canonicalAbsoluteLitePath('relative/lite_llm');
      } catch (error) {
        message = error instanceof Error ? error.message : `${error}`;
      }
      expect(message).assertContain('[RUNTIME_ROOT_INVALID]');
    });

    it('uses runtime root and content for a stable derived-config hash', 0, () => {
      const first = liteResolvedConfigHash('/data/lite_llm', '{"a":1}');
      expect(liteResolvedConfigHash('/data/lite_llm', '{"a":1}')).assertEqual(first);
      expect(liteResolvedConfigHash('/data/other', '{"a":1}') === first).assertFalse();
      expect(liteResolvedConfigHash('/data/lite_llm', '{"a":2}') === first).assertFalse();
    });
  });
}
