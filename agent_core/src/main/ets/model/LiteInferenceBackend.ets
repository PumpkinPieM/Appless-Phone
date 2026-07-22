import { LiteInitializeRequest } from './LiteModelTypes';

export interface LiteInferenceBackend {
  initialize(request: LiteInitializeRequest): Promise<void>;
  generate(prompt: string): Promise<string>;
  release(): Promise<void>;
}
