# Lite LLM vendor inputs

The repository builds the native bridge even when these private vendor inputs are absent. In that case `isVendorAvailable()` returns `false`. To enable real local inference for an ABI, copy:

- Public header: `include/lite_llm.h`
- Primary library: `lib/<abi>/liblite_llm.so`
- All transitive shared libraries: `lib/<abi>/*.so`

Supported project ABI directories are `lib/arm64-v8a/` and `lib/x86_64/`. Each library must be built for HarmonyOS and for the ABI of its directory.

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
    <weights, tokenizer, vocabulary, and auxiliary files>
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
