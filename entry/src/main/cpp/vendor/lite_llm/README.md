# Lite LLM vendor inputs

Select the implementation with one variable in `entry/src/main/cpp/CMakeLists.txt`:

```cmake
set(LITE_USE_MOCK_VENDOR ON)  # ON for mock, OFF for real vendor
```

When set to `OFF`, provide:

- Public header: `include/lite_llm.h`
- Primary library: `lib/<abi>/liblite_llm.so`
- All transitive shared libraries: `lib/<abi>/*.so`

The entry module builds both `arm64-v8a` and `x86_64`. With the toggle `ON`, both use the mock. With the toggle `OFF`, ARM64 requires the real vendor library while x86-64 automatically uses the mock when a real x86-64 library is absent. This supports a real ARM64 device and an x86 simulator without editing `abiFilters`.

## Mock behavior

Before launching the app, provision `config.json` and its model files under `LITE_MODEL_ROOT`. The mock reads the configuration path passed by the app, resolves its `foo` entry relative to `LITE_MODEL_ROOT`, reads that file, and logs its content with tag `LiteLlmMock`.

`Generate` ignores its prompt and returns the exact text loaded from the configured `foo` file. Put the desired ReAct response in that file.

## Verification logs

Filter HiLog by `LiteLlmModel` and `LiteLlmMock`. A successful Lite path reports:

- `MODEL_SELECTED mode=lite implementation=LiteModel`
- `MODEL_INIT_COMPLETE implementation=LiteModel`
- `Generate returning foo content ...` from the mock vendor
- `VENDOR_RESPONSE valid=true kind=action|final ...`

The spike bridge expects this API:

- `lite_llm::LiteLlm::CreateFromConfig(absoluteConfigPath)`
- `lite_llm::LiteLlm::Generate(fullPrompt)`
- `lite_llm::LiteLlm` destructor/release

If the vendor API differs, change `lite/lite_model_napi.cpp` and the CMake vendor filenames. Do not place weights or tokenizer data here.

## Device model data

Provision model data separately from the HAP:

```text
<runtimeRoot>/
  config.json
  model/
    foo.txt
```

```json
{
  "foo": "model/foo.txt"
}
```

Set these optional fields in the ignored `entry/src/main/resources/rawfile/aiphone_provider_config.json`:

```json
{
  "LOCAL_MODEL_MODE": "lite",
  "LITE_MODEL_ROOT": "/data/storage/el2/base/files/lite_llm"
}
```

If `LITE_MODEL_ROOT` is empty, the app uses `<UIAbilityContext.filesDir>/lite_llm`. The spike passes `<runtimeRoot>/config.json` directly to the vendor library, so the configuration must already contain paths understood by that library.

For remote or test-fixture behavior, select `"remote"` or `"scripted"` explicitly with `LOCAL_MODEL_MODE`. Lite failures never fall back to either mode.
