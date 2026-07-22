# Appless-Phone Lite Local LLM Integration

**Design Report and Implementation Handoff**

**Date:** July 22, 2026  

**Repository baseline:** PumpkinPieM/Appless-Phone, main branch, baseline commit c30f6afbe2d27e71e21109cc0303a443e3106638  

**Target branch:** `lite`



## Executive Summary

This report specifies a production-oriented integration of a user-supplied HarmonyOS C++ local-LLM library into Appless-Phone. The implementation introduces a `LiteModel` that implements the existing `LocalModel` interface, keeps ReAct prompt ownership in `ReActAgentRunner`, and delegates inference through an injected backend implemented by the `entry` module and a dedicated N-API native module.

The vendor header and shared library are build-time inputs copied into a fixed repository folder before building the HAP. Model data is not packaged in the HAP. At runtime, the app loads an external directory containing `config.json` and a `model/` folder. To guarantee correct path behavior, the ArkTS backend creates a derived configuration in which every valid string path beginning with `model/` is converted to an absolute path under the selected runtime root. The native library receives only the derived absolute config path.

The design intentionally prevents silent fallback to `ScriptedLocalModel`. A missing vendor library, invalid model directory, model-load failure, or generation failure is reported as an explicit error. The native build includes a stub adapter when the vendor files are absent, allowing the repository to compile before those files are supplied.

## 1. Scope

### 1.1 In scope

- A `LiteModel` implementation of `LocalModel` in `agent_core/src/main/ets/model`.

- A platform-neutral inference backend interface in `agent_core`.

- An ArkTS N-API backend in `entry`.

- A dedicated native module that adapts N-API calls to the minimal vendor interface.

- A fixed build-time folder for the vendor header and shared libraries.

- Runtime loading from a device-side model root containing `config.json` and `model/`.

- Deterministic conversion of `model/...` paths to absolute paths.

- Explicit factory selection of lite, remote, or scripted modes.

- Unit, bridge, and device smoke-test requirements.

### 1.2 Out of scope

- Token-by-token streaming to ArkTS or UI.

- Downloading or updating model files.

- Loading multiple models concurrently.

- Parallel inference on one runtime.

- Automatic remote fallback after local inference failure.

- Automatic scripted fallback after local inference failure.

- A user-facing model-directory picker.

- Vendor-specific sampling controls beyond values already handled by `config.json`.

## 2. Existing Codebase Constraints

| Area | Observed contract or behavior | Design implication |

| --- | --- | --- |

| `LocalModel` | `complete(prompt, conversation?) -> Promise<string>` | Lite integration must preserve this interface in the first version. |

| `ReActAgentRunner` | Builds the full ReAct prompt, conversation, tool descriptions, scratchpad, and local datetime. | LiteModel must forward the prompt and must not build a second agent prompt. |

| `LoopModelFactory` | Chooses `OpenAiCompatibleModel` when configured and otherwise returns `ScriptedLocalModel`. | The lite branch needs explicit model mode selection and must not hide local failures behind scripted output. |

| `entry` native build | Already owns the application CMake/N-API target. | The platform-specific native module belongs in `entry`; `agent_core` must remain platform-neutral. |

| Module dependency | `entry` depends on `agent_core`. | Do not make `agent_core` import an `entry`-owned `.so`; inject the backend from `entry`. |



## 3. Requirements

### 3.1 Functional requirements

| ID | Requirement | Definition |

| --- | --- | --- |

| FR-01 | LiteModel contract | `LiteModel` shall implement `LocalModel.complete(prompt, conversation?)` and return the complete generated text. |

| FR-02 | Prompt fidelity | The exact full prompt produced by `ReActAgentRunner` shall be forwarded once to the native library. Conversation history shall not be appended a second time. |

| FR-03 | Build-time vendor inputs | The project shall define a fixed folder where the user copies the vendor header and ABI-specific `.so` files before HAP compilation. |

| FR-04 | Build without vendor inputs | When the vendor files are absent, the repository shall still compile using a stub native adapter that reports `NATIVE_UNAVAILABLE` at runtime. |

| FR-05 | Runtime model root | The model shall be loaded from a configurable absolute runtime root on the device. The root shall contain `config.json` and a directory named `model`. |

| FR-06 | Relative-path resolution | Every configuration string equal to `model` or beginning with `model/` shall be resolved against the runtime root and written as an absolute path in a derived config. |

| FR-07 | Path traversal prevention | A `model/...` value containing traversal that escapes `<runtimeRoot>/model` shall be rejected before native initialization. |

| FR-08 | No process cwd mutation | The implementation shall not call `chdir()` to make relative paths work. |

| FR-09 | Lazy loading | The native model shall load on the first inference request, only once per backend lifecycle. |

| FR-10 | Serialized inference | Only one load/generate/release operation shall execute against the vendor runtime at a time. |

| FR-11 | Same-thread vendor lifecycle | Creation, generation, and destruction shall run on one persistent native worker thread unless the vendor library explicitly documents that thread affinity is unnecessary. |

| FR-12 | Explicit model selection | The factory shall support `lite`, `remote`, and `scripted` modes and identify the selected mode in `LoopModelInfo`. |

| FR-13 | No silent fallback | A lite-mode initialization or generation failure shall produce an error event; it shall not instantiate or call `ScriptedLocalModel`. |

| FR-14 | Release | The application-owned backend shall expose release and destroy the native runtime during final shutdown or explicit model reload. |

| FR-15 | UTF-8 output validation | Generated output shall be a non-empty UTF-8 string and shall be returned without semantic rewriting. |



### 3.2 Non-functional requirements

| ID | Quality attribute | Requirement |

| --- | --- | --- |

| NFR-01 | Responsiveness | Model loading and generation shall never execute synchronously on the ArkUI/JS thread. |

| NFR-02 | Maintainability | Vendor API details shall be isolated in one C++ adapter source file. |

| NFR-03 | Testability | `LiteModel` shall be testable using a fake ArkTS backend without native binaries or a device. |

| NFR-04 | Observability | Load/generation start, completion, elapsed time, and stable error codes shall be logged without logging full prompts by default. |

| NFR-05 | Security | Only absolute runtime roots are accepted; rewritten paths shall remain under the declared `model` directory. |

| NFR-06 | Compatibility | The implementation shall preserve the existing `LocalModel` interface and ReAct parsing behavior. |

| NFR-07 | Resource use | The loaded model shall remain resident across ReAct steps to avoid repeated loading. |

| NFR-08 | Determinism | Config resolution shall not depend on process working directory, installation directory, or native library directory. |



### 3.3 Assumptions

- The supplied library is built for HarmonyOS and at least the `arm64-v8a` ABI.

- The vendor header can be adapted to the minimal interface shown in Section 7.

- The library accepts a path to a JSON configuration file and returns complete UTF-8 generation output.

- All paths inside `config.json` that require resolution are represented as JSON string values beginning with `model/` or equal to `model`.

- The selected runtime root is readable by the application process.

- The first implementation uses one process-level native model instance.

## 4. Architecture

```text
ReActAgentRunner
  |  LocalModel.complete(fullReActPrompt, conversation)
  v
LiteModel                         [agent_core, platform-neutral]
  |  LiteInferenceBackend
  v
NapiLiteInferenceBackend         [entry, ArkTS]
  |  prepare derived absolute-path config
  |  liblite_model_native.so
  v
Lite N-API bridge                [entry, C++]
  |  queued work on one native inference thread
  v
Lite vendor adapter              [entry, C++]
  |  lite_llm::LiteLlm::CreateFromConfig / Generate / destructor
  v
User-supplied local LLM library  [build-time .so + header]
  |
  v
Device-side config and model files [runtime, outside HAP]
```

### 4.1 Dependency direction

```text
entry -> agent_core
entry -> liblite_model_native.so
liblite_model_native.so -> vendor local-LLM .so

Forbidden:
agent_core -> entry
agent_core -> liblite_model_native.so
```

## 5. Design Decisions

| ID | Choice | Rationale | Rejected/Deferred alternative |

| --- | --- | --- | --- |

| D-01 | Place `LiteModel` in `agent_core` | It is a `LocalModel` implementation and belongs beside the existing model classes. | Putting all code in `entry` would weaken reuse and mix agent and platform concerns. |

| D-02 | Inject a `LiteInferenceBackend` | Keeps `agent_core` independent of N-API and makes LiteModel unit-testable. | A direct native import from LiteModel would create application-module coupling. |

| D-03 | Keep prompt ownership in `ReActAgentRunner` | The runner already builds tools, scratchpad, conversation, and policies. | A second prompt builder would duplicate context and risk conflicting instructions. |

| D-04 | Use a dedicated native module | The LLM runtime has substantial state, dependencies, and lifecycle concerns. | Extending the generic demo `libentry.so` is possible but less maintainable. |

| D-05 | Use the minimal vendor interface | Only creation from config, full-prompt generation, and destruction are required. | Do not expose vendor-specific controls in ArkTS until needed. |

| D-06 | Rewrite config paths into a derived config | Guarantees deterministic resolution even if the vendor library resolves relative paths against cwd. | `chdir()` is process-wide and unsafe; requiring vendor changes is unnecessary. |

| D-07 | Recursively rewrite safe `model/...` strings | The user-defined config convention provides a generic path marker without knowing field names. | Rewriting all strings would be unsafe; only exact `model` prefixes are transformed. |

| D-08 | Use one persistent native worker thread | Guarantees serialized access and same-thread lifecycle for an unknown vendor runtime. | Generic async-work threads may vary between calls and may violate undocumented affinity. |

| D-09 | Lazy-load and retain the model | Avoids application startup delay and repeated loading during ReAct loops. | Eager loading harms startup; per-call loading is prohibitively expensive. |

| D-10 | Compile a stub when vendor files are absent | Allows the branch to build and be reviewed before private/large vendor inputs are copied. | Failing CMake immediately makes normal repository work impossible. |

| D-11 | Explicit mode selection; no silent fallback | Scripted responses are test fixtures, not evidence of real inference. | Fallback can mask missing libraries and model failures. |

| D-12 | Keep non-streaming `Promise<string>` first | Preserves the current LocalModel contract and limits integration risk. | Streaming requires broader runner, listener, lifecycle, and callback changes. |



## 6. Repository and File Layout

```text
agent_core/
  Index.ets
  src/main/ets/model/
    LocalModel.ets                     existing
    LiteModel.ets                      new
    LiteInferenceBackend.ets           new
    LiteModelTypes.ets                 new
    LoopModelFactory.ets               modify

entry/
  oh-package.json5                     modify
  build-profile.json5                  normally unchanged
  src/main/ets/model/
    NapiLiteInferenceBackend.ets       new
    LiteModelRuntimeRoot.ets           new
  src/main/cpp/
    CMakeLists.txt                     modify
    lite/
      lite_model_napi.cpp              new
      lite_runtime.h                   new
      lite_runtime.cpp                 new
      lite_vendor_adapter.h            new
      lite_vendor_adapter.cpp          new, compiled when vendor exists
      lite_vendor_stub.cpp             new, compiled otherwise
    types/liblite_model_native/
      Index.d.ts                       new
      oh-package.json5                 new
    vendor/lite_llm/
      README.md                        new
      include/
        lite_llm.h                     user copies before build
      lib/
        arm64-v8a/
          liblite_llm.so               user copies before build
          <transitive vendor .so files>
        x86_64/
          liblite_llm.so               optional emulator build input
          <transitive vendor .so files>
```

The exact public header and primary library names are fixed to `lite_llm.h` and `liblite_llm.so`. If the supplied names differ, rename the files or change only `lite_vendor_adapter.cpp` and CMake variables.

## 7. Vendor Library Contract

The adapter targets the following minimal conceptual interface:

```cpp
#pragma once

#include <memory>
#include <string>

namespace lite_llm {

class LiteLlm {
public:
    static std::unique_ptr<LiteLlm> CreateFromConfig(
        const std::string& configPath);

    std::string Generate(const std::string& prompt);

    virtual ~LiteLlm();
};

} // namespace lite_llm
```

Only `lite_vendor_adapter.cpp` may include `lite_llm.h`. Vendor pointer types and buffers must not cross into N-API code. If the actual library uses different names, handles, error returns, or output ownership, adapt those details into standard C++ `std::unique_ptr` and `std::string` semantics inside this file.

### 7.1 Required runtime behavior

- `CreateFromConfig` receives an absolute derived config path.

- `Generate` receives the complete ReAct prompt as one string.

- Generation returns the complete answer, not partial tokens.

- Destruction releases model, tokenizer, context, and vendor resources.

- Exceptions must be caught inside the adapter and converted to a stable native error.

## 8. Device Model Layout and Loading Method

### 8.1 Runtime directory contract

```text
<runtimeRoot>/
  config.json
  model/
    <all model, tokenizer, vocabulary, and auxiliary files>
```

Example runtime root:

```text
/data/storage/el2/base/files/lite_llm
```

Example original config:

```json
{
  "weights": "model/weights.bin",
  "tokenizer": "model/tokenizer.json",
  "metadata": {
    "vocabulary": "model/vocab.json"
  }
}
```

### 8.2 Runtime-root selection

Add `LITE_MODEL_ROOT` to the existing local model settings raw JSON. Resolution order:

- Use trimmed `LITE_MODEL_ROOT` when configured.

- Otherwise default to `<UIAbilityContext.filesDir>/lite_llm`.

- Reject a non-absolute root.

- Log the selected root in redacted form or full form only in debug builds.

```typescript
interface LocalModelRawConfig {
  DASHSCOPE_API_KEY?: string;
  LITE_MODEL_ROOT?: string;
}
```

This supports both app-owned storage and another device path, provided the application process has read permission. The model is provisioned separately from the HAP, for example by a development HDC workflow or device-side file management. The app never copies the model during ordinary startup.

### 8.3 Derived-config algorithm

The ArkTS backend prepares a config that contains absolute model paths before calling native initialization. This is the authoritative relative-path solution.

```text
function resolveConfigValue(value, runtimeRoot, modelRoot): value
  if value is a string:
    normalized = replaceBackslashesWithSlashes(value)
    if normalized == "model" or normalized starts with "model/":
      relative = normalized after optional "model/" prefix
      candidate = canonicalize(join(modelRoot, relative))
      require candidate == modelRoot or candidate starts with modelRoot + "/"
      return candidate
    return value

  if value is an array:
    return map each item through resolveConfigValue

  if value is an object:
    return map each property value through resolveConfigValue

  return value
```

The implementation reads `<runtimeRoot>/config.json`, recursively transforms values, and writes the result to:

```text
<UIAbilityContext.cacheDir>/lite_llm/resolved-config-<content-hash>.json
```

The resolved config is regenerated when the original config content or runtime root changes. The original config is never modified.

### 8.4 Validation before loading

- `runtimeRoot` is absolute and canonicalizable.

- `runtimeRoot/config.json` exists, is a regular file, and is readable.

- `runtimeRoot/model` exists, is a directory, and is readable.

- The original config parses as a JSON object.

- Each rewritten path remains within `<runtimeRoot>/model`.

- The derived config can be written and read.

- No empty path is passed to native code.

## 9. ArkTS Interfaces

### 9.1 Agent-core types

```typescript
export interface LiteModelOptions {
  runtimeRoot: string;
}

export interface LiteInitializeRequest {
  configPath: string;
}

export interface LiteInferenceBackend {
  initialize(request: LiteInitializeRequest): Promise<void>;
  generate(prompt: string): Promise<string>;
  release(): Promise<void>;
}
```

### 9.2 LiteModel behavior

```typescript
export class LiteModel implements LocalModel {
  private initialization: Promise<void> | null = null;
  private tail: Promise<void> = Promise.resolve();

  constructor(
    private readonly backend: LiteInferenceBackend,
    private readonly options: LiteModelOptions
  ) {}

  async complete(prompt: string, _conversation?: ConversationContext): Promise<string> {
    return this.enqueue(async () => {
      await this.ensureInitialized();
      const text = (await this.backend.generate(prompt)).trim();
      if (text.length === 0) {
        throw new Error('Local LLM returned an empty response.');
      }
      return text;
    });
  }
}
```

The concrete implementation may use a small promise-chain helper for serialization. Initialization failure clears the cached initialization promise so a later explicit request can retry.

### 9.3 NapiLiteInferenceBackend responsibilities

- Resolve and validate the runtime root using `UIAbilityContext` and local settings.

- Prepare the derived absolute-path config.

- Import `liblite_model_native.so`.

- Call native `initialize`, `generate`, and `release`.

- Map native error objects to ArkTS `Error` messages while retaining stable error codes.

- Prevent use after release.

- Avoid logging full prompts or generated personal data by default.

## 10. Native Module Design

### 10.1 N-API surface

```typescript
export const initialize: (configPath: string) => Promise<void>;
export const generate: (prompt: string) => Promise<string>;
export const release: () => Promise<void>;
export const isVendorAvailable: () => boolean;
```

`isVendorAvailable()` is synchronous and returns compile-time adapter availability only. It does not load the model.

### 10.2 Native threading model

```text
JS thread
  -> N-API callback creates Promise and napi_async_work
  -> async-work execute callback submits a job to LiteRuntime worker queue
  -> execute callback waits on job future (no JS/N-API access)
  -> one persistent LiteRuntime worker thread performs vendor call
  -> async-work completion callback resolves/rejects Promise on JS thread
```

All `CreateFromConfig`, `Generate`, and destructor calls execute on the same persistent worker thread. The queue is FIFO. Release waits for prior work and marks the runtime unavailable for later generation.

### 10.3 Native state machine

```text
UNAVAILABLE   vendor files not compiled in; stub adapter only
UNINITIALIZED no model object
LOADING       CreateFromConfig executing
READY         model loaded
GENERATING    Generate executing
FAILED        last initialization made runtime unusable
RELEASING     queued destruction
RELEASED      backend shut down
```

Invalid transitions return stable errors. A generation call in `UNINITIALIZED` is rejected because ArkTS must initialize first. Only one generation is active.

## 11. Build and Packaging

### 11.1 Vendor detection

CMake checks for both:

```text
entry/src/main/cpp/vendor/lite_llm/include/lite_llm.h
entry/src/main/cpp/vendor/lite_llm/lib/<current-abi>/liblite_llm.so
```

When both exist, compile `lite_vendor_adapter.cpp`, import and link `liblite_llm.so`, and package any shared dependencies. Otherwise compile `lite_vendor_stub.cpp`. This permits code review and non-device tests before private vendor artifacts are present.

### 11.2 ABI behavior

- Use the real vendor adapter for ABIs with a matching vendor library.

- Use the stub adapter for ABIs without one, including `x86_64` unless a vendor binary is provided.

- Do not remove existing ABI filters solely for this feature; keep emulator builds possible through the stub.

- On the target ARM64 device, `isVendorAvailable()` must return true before attempting model load.

### 11.3 Package declaration

```text
// entry/oh-package.json5 dependency
"liblite_model_native.so": "file:./src/main/cpp/types/liblite_model_native"
```

## 12. Model Factory Integration

```typescript
export type LoopModelMode = 'lite' | 'remote' | 'scripted';

export interface LoopModelInfo {
  model: LocalModel;
  mode: LoopModelMode;
  modelName: string;
}

export interface LoopModelDependencies {
  requestedMode?: LoopModelMode;
  liteBackend?: LiteInferenceBackend;
  liteOptions?: LiteModelOptions;
}
```

### 12.1 Selection rules

| Requested mode | Required inputs | Result |

| --- | --- | --- |

| `lite` | Backend and options; vendor availability is checked by backend | Return `LiteModel`; any later load/generation error is surfaced. |

| `remote` | Configured `LlmProvider` | Return `OpenAiCompatibleModel`; missing configuration is an error. |

| `scripted` | None | Return `ScriptedLocalModel`; intended for tests/demos only. |

| Unspecified on `lite` branch | Lite backend and runtime-root settings | Default to `lite`. Do not silently choose scripted. |



The entry composition root constructs `NapiLiteInferenceBackend`, resolves `LiteModelOptions`, and passes them to `createLoopModelInfo`. `agent_core` does not import the native module.

## 13. Error Model

| Code | Meaning |

| --- | --- |

| NATIVE_UNAVAILABLE | Vendor header/library was not compiled for the current ABI. |

| RUNTIME_ROOT_INVALID | Runtime root is empty, relative, or cannot be canonicalized. |

| CONFIG_NOT_FOUND | `config.json` is missing. |

| CONFIG_NOT_READABLE | `config.json` cannot be read. |

| CONFIG_INVALID_JSON | Configuration is not valid JSON or not a JSON object. |

| MODEL_DIRECTORY_NOT_FOUND | `model/` is missing or not a directory. |

| MODEL_PATH_ESCAPE | A rewritten `model/...` value escapes the model directory. |

| RESOLVED_CONFIG_WRITE_FAILED | Derived config cannot be written. |

| MODEL_LOAD_FAILED | Vendor creation returned null or threw. |

| MODEL_NOT_INITIALIZED | Generation was requested before successful initialization. |

| GENERATION_FAILED | Vendor generation threw or failed. |

| EMPTY_MODEL_RESPONSE | Generation returned an empty string. |

| BACKEND_RELEASED | Operation requested after release. |



The ArkTS error message should include the stable code and a concise diagnostic. The UI may show a user-readable summary. Full native details belong in debug logs. No error path may return the scripted success sentence.

## 14. Logging and Privacy

- Log model mode, vendor availability, runtime-root selection source, initialization elapsed time, generation elapsed time, and error code.

- Do not log the full prompt or generated answer by default.

- Do not log contents of `config.json`; log its path and hash only when needed.

- Do not log model file contents.

- In release builds, redact absolute runtime roots if they may expose user-specific storage paths.

## 15. Testing Strategy

### 15.1 Agent-core unit tests

- LiteModel forwards the exact prompt once.

- ConversationContext is not appended separately.

- Initialization is single-flight.

- Two concurrent `complete()` calls are serialized.

- Initialization failures propagate and can be retried later.

- Generation failure propagates.

- Empty output is rejected.

- No ScriptedLocalModel fallback is invoked.

### 15.2 ArkTS path-resolution tests

- `model/weights.bin` becomes `<runtimeRoot>/model/weights.bin`.

- Nested objects and arrays are transformed recursively.

- Non-path strings remain unchanged.

- `model/../outside.bin` is rejected.

- Backslash variants are normalized or rejected consistently.

- The original config remains unchanged.

- Same config and root reuse the same content-hash result.

### 15.3 Native stub/bridge tests

- Native module loads on supported build targets without vendor files.

- `isVendorAvailable()` returns false in stub builds.

- Initialize rejects with `NATIVE_UNAVAILABLE` in stub builds.

- Promise resolution and rejection work without blocking JS.

- Release is idempotent or returns a documented stable error.

- The queue preserves request order.

### 15.4 Device smoke tests with real library

- Load a valid runtime root and complete a plain `Final:` response.

- Return a valid `Action: tool({...})` response and complete a second generation after observation.

- Handle Chinese prompts and UTF-8 output.

- Reject missing config, missing model directory, malformed config, and path escape.

- Keep the model loaded across several ReAct steps.

- Release and verify later generation fails clearly.

- Verify no ArkUI freeze during load or generation.

- Verify ARM64 HAP packages the vendor and transitive shared libraries.

## 16. Acceptance Criteria

| ID | Acceptance criterion |

| --- | --- |

| AC-01 | The `lite` branch builds without vendor files by compiling the stub adapter. |

| AC-02 | After copying `lite_llm.h` and `liblite_llm.so` to the documented folder, the ARM64 HAP links and packages successfully. |

| AC-03 | A device-side runtime root outside the HAP is selected through `LITE_MODEL_ROOT` or the app-files default. |

| AC-04 | All valid `model/...` config values are converted to absolute paths in a derived config; the original config is unchanged. |

| AC-05 | A traversal attempt is rejected before calling the vendor library. |

| AC-06 | A real local model response reaches `ReActAgentRunner` through `LiteModel.complete()`. |

| AC-07 | The model remains resident across a multi-step ReAct run. |

| AC-08 | Missing vendor files or model data produce explicit errors and never scripted success output. |

| AC-09 | Initialization, generation, and destruction occur off the JS thread and on one vendor worker thread. |

| AC-10 | Agent-core and path-resolution unit tests pass; device smoke results are documented. |



## 17. Implementation Sequence for the Local Agent

| Step | Workstream | Required result |

| --- | --- | --- |

| 1 | Create branch | Create `lite` from current `main`; verify clean baseline. |

| 2 | Add agent-core contracts | Add LiteModelTypes, LiteInferenceBackend, LiteModel; export from Index.ets. |

| 3 | Add unit tests | Use fake backend to validate initialization, serialization, prompt fidelity, and errors. |

| 4 | Extend settings | Add `LITE_MODEL_ROOT` parsing and runtime-root resolver with filesDir fallback. |

| 5 | Implement config resolver | Read/transform/write derived JSON; add path-security tests. |

| 6 | Add native module types | Declare initialize/generate/release/isVendorAvailable and add oh-package dependency. |

| 7 | Implement native runtime | Add state machine, persistent worker queue, async Promise bridge, stable errors. |

| 8 | Add vendor adapter and stub | Real adapter includes only lite_llm.h; stub preserves builds without vendor files. |

| 9 | Update CMake | Detect vendor files per ABI, import shared library, package dependencies, select adapter source. |

| 10 | Integrate factory | Add explicit modes and inject backend from entry composition root; default lite branch to lite. |

| 11 | Remove silent behavior | Ensure native failure cannot select or call ScriptedLocalModel. |

| 12 | Build and test | Run unit tests and HAP build first without, then with vendor files; execute device smoke tests. |

| 13 | Document provisioning | Add vendor README and model-root setup instructions, including the exact expected directory tree. |



## 18. Implementation Guardrails

- Do not modify `ReActAgentRunner` prompt construction solely for LiteModel.

- Do not parse or repair `Thought:`, `Action:`, or `Final:` inside LiteModel.

- Do not import a native `.so` from `agent_core`.

- Do not call `chdir()`.

- Do not rewrite arbitrary config strings; only transform the declared `model` path convention.

- Do not package model files into the HAP.

- Do not store vendor header/library contents in Git unless intended by the repository owner.

- Do not log full prompts or answers by default.

- Do not silently fall back to remote or scripted inference.

- Do not add streaming until the non-streaming path is stable.

## Appendix A. Expected Vendor Folder README

```markdown
# Lite LLM vendor inputs

Before building the ARM64 HAP, copy:

- public header: include/lite_llm.h
- primary library: lib/<abi>/liblite_llm.so
- all transitive shared libraries: lib/<abi>/*.so

The adapter expects the `lite_llm::LiteLlm` API with:

- CreateFromConfig(absoluteConfigPath)
- Generate(fullPrompt)
- destructor/release

Do not place model weights in this directory. Model data is loaded from the device-side runtime root.
```

## Appendix B. Config Resolution Example

Runtime root: `/data/storage/el2/base/files/lite_llm`

Original:

```json
{
  "main": "model/a.bin",
  "parts": ["model/b.bin", "not-a-path"],
  "nested": { "tokenizer": "model/tokenizer.json" }
}
```

Derived:

```json
{
  "main": "/data/storage/el2/base/files/lite_llm/model/a.bin",
  "parts": [
    "/data/storage/el2/base/files/lite_llm/model/b.bin",
    "not-a-path"
  ],
  "nested": {
    "tokenizer": "/data/storage/el2/base/files/lite_llm/model/tokenizer.json"
  }
}
```

## Appendix C. Final Handoff Checklist

- [ ] Branch `lite` created from current main.

- [ ] Agent-core types and LiteModel implemented.

- [ ] Runtime-root config key and default implemented.

- [ ] Derived-config path rewriting and traversal checks implemented.

- [ ] Native N-API module and persistent worker implemented.

- [ ] Vendor stub compiles without private files.

- [ ] Vendor adapter compiles with copied header/library.

- [ ] Factory defaults to lite on the lite branch.

- [ ] No scripted fallback on native failures.

- [ ] Unit tests and device smoke tests completed.

- [ ] Vendor and model provisioning README added.
