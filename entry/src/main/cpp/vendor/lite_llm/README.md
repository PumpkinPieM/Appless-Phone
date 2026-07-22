# Lite LLM vendor inputs

The repository builds a runtime stub when these private vendor inputs are absent. To enable real local inference for an ABI, copy:

- Public header: `include/lite_llm.h`
- Primary library: `lib/<abi>/liblite_llm.so`
- All transitive shared libraries: `lib/<abi>/*.so`

Supported project ABI directories are `lib/arm64-v8a/` and `lib/x86_64/`. Each library must be built for HarmonyOS and for the ABI of its directory.

The initial adapter expects the conceptual API documented in `Appless_Phone_Lite_Model_Design_Report.md`:

- `lite_llm::LiteLlm::CreateFromConfig(absoluteConfigPath)`
- `lite_llm::LiteLlm::Generate(fullPrompt)`
- `lite_llm::LiteLlm` destructor/release

If the vendor API differs, change only `lite/lite_vendor_adapter.cpp` and the CMake vendor filenames. Do not place weights or tokenizer data here.

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

If `LITE_MODEL_ROOT` is empty, the app uses `<UIAbilityContext.filesDir>/lite_llm`. Valid `model` and `model/...` strings in `config.json` are rewritten into an immutable derived config under the app cache. Traversal outside `<runtimeRoot>/model` is rejected.

For remote or test-fixture behavior, select `"remote"` or `"scripted"` explicitly with `LOCAL_MODEL_MODE`. Lite failures never fall back to either mode.
