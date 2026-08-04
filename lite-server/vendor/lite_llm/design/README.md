# LiteLlm framework design draft

This directory contains discussion-stage interfaces. They are intentionally not
included by the current build.

## Boundary split

- `vendor_api.h` is the server-facing singleton. It accepts request JSON,
  enforces one active generation, owns the asynchronous worker, and produces a
  complete response that `lite-server` can forward unchanged.
- `inference_runtime.h` is the narrow boundary implemented by the proprietary
  inference runtime. It loads one model eagerly, performs blocking generation,
  owns tokenizer-dependent KV caches, and supports interruption during
  shutdown.

The runtime does not parse OpenAI or vendor JSON and does not own transport
correlation. The vendor does not expose engine-specific KV-cache handles.

## Generation request

For now the vendor request can remain compatible with the OpenAI
chat-completions request and add a namespaced extension:

```json
{
  "model": "qwen3-0.6b",
  "messages": [
    {"role": "system", "content": "Be concise."},
    {"role": "user", "content": "Hello"}
  ],
  "temperature": 0.7,
  "max_completion_tokens": 512,
  "stream": false,
  "lite_llm": {
    "protocol_version": 1,
    "session_id": "550e8400-e29b-41d4-a716-446655440000",
    "cache": {"mode": "auto"}
  }
}
```

The full message history is authoritative. `session_id` is only a hint for
best-effort prefix/KV-cache reuse. Cache eviction or a changed history must not
change request correctness.

## Request flow

1. `lite-server` forwards the raw request JSON to `GenerateAsync`.
2. The vendor atomically accepts the request or rejects it as busy; requests are
   never queued.
3. The vendor validates JSON, applies the selected model's chat template, and
   calls the runtime's blocking `Generate` on its single worker.
4. The runtime tokenizes the prompt, validates session prefix reuse, runs
   inference, and updates its bounded KV cache.
5. The vendor encodes an OpenAI-compatible response and invokes the completion
   callback exactly once.
6. `lite-server` forwards the response using its own transport request ID.

No vendor-level request ID, streaming, or request cancellation is included in
this draft. `Shutdown` interruption remains required.
