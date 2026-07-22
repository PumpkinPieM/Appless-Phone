declare const liteModelNative: {
  initialize(configPath: string): Promise<void>;
  generate(prompt: string): Promise<string>;
  release(): Promise<void>;
  isVendorAvailable(): boolean;
};

export default liteModelNative;
