# LiteLlm vendor boundary

The server is built against the vendor contract in `lite_llm.h`:

```cpp
auto model = lite_llm::LiteLlm::CreateFromConfig(config_path);
std::string response = model->Generate(prompt);
```

The bundled implementation under `mock/` is selected by default with
`-DLITE_USE_MOCK_VENDOR=ON`. It builds as a separate `liblite_llm.so`, preserving
the same shared-library boundary while sending prompts to DeepSeek over HTTPS.

For the real vendor, configure with `-DLITE_USE_MOCK_VENDOR=OFF` and provide:

```text
vendor/lite_llm/
  include/lite_llm.h
  lib/
    arm64-v8a/liblite_llm.so
    x86_64/liblite_llm.so
```

All libraries crossing this C++ ABI must use a compatible HarmonyOS NDK,
compiler generation, architecture, and `libc++_shared.so`.

## DeepSeek demo configuration

The demo reads its endpoint, model, and API key from the config:

```json
{
  "endpoint": "https://api.deepseek.com/chat/completions",
  "model": "deepseek-v4-flash",
  "apiKey": "..."
}
```

`Generate` sends the prompt as a user message, prints the returned assistant
content to stdout, and returns the same content. HTTPS is provided by the pinned
Mbed TLS dependency configured in the top-level CMake project. Because the demo
runtime has no portable CA-bundle path, this local-demo transport encrypts the
connection but does not verify the server certificate.
