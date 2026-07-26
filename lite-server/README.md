# lite-server

`lite-server` is a standalone HarmonyOS C++ executable that connects outbound
to the app-owned `@appless/lite-proxy` HAR and invokes `LiteLlm` for generation.
It never binds a socket, so it can be launched from ordinary HDC shell on the
simulator configuration where shell processes cannot bind TCP or Unix sockets.

```text
OpenAiCompatibleModel
  -> HTTP 127.0.0.1:18080
  -> in-app lite_proxy HAR
  <- persistent LTS1 connection initiated by lite-server
  -> liblite_llm.so (DeepSeek-backed demo by default)
```

## Model API

The server exposes a generic internal `Model` boundary:

```cpp
virtual bool Build(const std::string& config_path, std::string* error) = 0;
virtual GenerationResult Generate(const std::string& prompt) = 0;
```

`Build` takes only `config_path`. The default adapter calls the bundled
DeepSeek-backed demo vendor:

```cpp
lite_llm::LiteLlm::CreateFromConfig(config_path);
model->Generate(prompt);
```

The demo is intentionally built as a separate `liblite_llm.so`. Its config
contains the DeepSeek chat-completions endpoint, model, and API key. Each
generation prints the assistant response to stdout and returns it.

## Build

From PowerShell:

```powershell
.\scripts\build.ps1 -Arch x86_64 -BuildType Debug
.\scripts\build.ps1 -Arch arm64-v8a -BuildType Release
```

Outputs are written to `build-<abi>/`:

- `lite-server`
- `liblite_llm.so`
- `lite-server-tests`

The executable and vendor library use the same SDK `libc++_shared.so`. Deploy
that runtime beside them to keep the C++ ABI consistent.

## App configuration

Enable the proxy using a config matching the server launch values:

```json
{
  "enabled": true,
  "httpPort": 18080,
  "reversePort": 18081,
  "authToken": "replace-this-development-token",
  "requestTimeoutMs": 120000,
  "maxBodyBytes": 4194304
}
```

Place it at:

```text
entry/src/main/resources/rawfile/lite_proxy_config.json
```

The app then continues to use its existing `OpenAiCompatibleModel`, configured
with `http://127.0.0.1:18080/v1` and the same token.

## Deploy and test

For a complete connection-only simulator procedure, see
[`../docs/lite-proxy-server-test-guide.md`](../docs/lite-proxy-server-test-guide.md).

With the simulator or device connected:

```powershell
.\scripts\deploy.ps1 `
  -Arch x86_64 `
  -AuthToken replace-this-development-token `
  -ReversePort 18081 `
  -RunTests
```

The script prints the foreground launch command. Start the app first, then run:

```text
hdc shell "cd /data/local/tmp/lite-server && LD_LIBRARY_PATH=. ./lite-server \
  --config ./config.json \
  --connect-host 127.0.0.1 \
  --connect-port 18081 \
  --auth-token replace-this-development-token"
```

Stop with `Ctrl+C` or `SIGTERM`. If the app is absent or restarts, the server
reconnects with bounded exponential backoff. Pending requests are not replayed.

## Request handling

The reverse protocol matches [`../lite_proxy/README.md`](../lite_proxy/README.md):

1. Connect outbound and send authenticated `hello`.
2. Receive `chat_request` frames containing the exact OpenAI JSON body.
3. Convert all role/content messages into one role-labelled prompt.
4. Call the vendor model serially.
5. Return an ordinary OpenAI-compatible `chat.completion` JSON response inside
   a correlated `chat_response` frame.

The full prompt is passed to DeepSeek as one user message. Streaming and
cancellation are not claimed because the vendor
API returns only one completed string and exposes no cancellation operation.

## Real vendor

See [`vendor/lite_llm/README.md`](vendor/lite_llm/README.md). Place the vendor
header and ABI-specific `.so` files there, then build with `-RealVendor`.
