# Lite Proxy HAR

`@appless/lite-proxy` is a reusable HarmonyOS HAR that lets an existing
OpenAI-compatible HTTP client call a model process launched from HDC shell.
The HDC process does not bind any socket. It connects outbound to the reverse
listener owned by the application.

```text
OpenAI-compatible client
  -> HTTP 127.0.0.1:<httpPort>
  -> @appless/lite-proxy HAR / liblite_proxy.so
  -> persistent framed reverse TCP
  -> HDC-launched model server
```

Both listeners bind exclusively to `127.0.0.1`. The same bearer token
authenticates the HTTP caller and the reverse server.

## Host application integration

Add the HAR module or built HAR as an OHPM dependency, declare
`ohos.permission.INTERNET`, and start it from the application lifecycle:

```ts
import { LiteProxy, LiteProxyStatus } from '@appless/lite-proxy';

const status: LiteProxyStatus = await LiteProxy.start({
  httpPort: 18080,
  reversePort: 18081,
  authToken: 'a-random-per-install-secret',
  requestTimeoutMs: 120000
});

// Configure the existing OpenAI client with these values.
const baseUrl = status.baseUrl;
const apiKey = status.authToken;

// During application teardown:
await LiteProxy.stop();
```

Passing port `0` asks the operating system to choose an available port. Passing
an empty token generates a 48-character token. Use the returned values; do not
assume the requested values were retained.

This repository's `EntryAbility` also supports optional bootstrap through
`entry/src/main/resources/rawfile/lite_proxy_config.json`. Copy and customize
[`examples/lite_proxy_config.json`](examples/lite_proxy_config.json) to that
location. When enabled, the app starts the proxy and replaces only its local
model `baseUrl` and `apiKey`; the existing OpenAI model implementation is not
modified. The example token is for development only.

## HTTP API

- `GET /health` returns whether the proxy process is running.
- `GET /ready` returns `200` only after an authenticated reverse server connects.
- `POST /v1/chat/completions` forwards the unmodified JSON body.
- `POST /chat/completions` is an alias.

Chat requests require `Authorization: Bearer <authToken>`. Responses from the
reverse server are returned as ordinary OpenAI-compatible JSON or buffered SSE.
The current Appless client accepts ordinary JSON even when it requests streaming.

## Reverse wire protocol

Every message is one frame:

```text
4 bytes "LTS1" | uint32 payload size, big endian | UTF-8 JSON payload
```

The configured maximum HTTP body is also enforced on reverse frames, with a
small allowance for envelope escaping. A connection begins with:

```json
{"type":"hello","protocol":1,"auth_token":"TOKEN"}
```

The proxy replies:

```json
{"type":"hello_ack","protocol":1}
```

For each HTTP completion request, the proxy sends:

```json
{
  "type": "chat_request",
  "id": "1",
  "body": "{\"model\":\"...\",\"messages\":[...]}"
}
```

`body` is a JSON string containing the exact HTTP request body. The server
returns either:

```json
{
  "type": "chat_response",
  "id": "1",
  "status": 200,
  "content_type": "application/json; charset=utf-8",
  "body": "{\"choices\":[{\"message\":{\"content\":\"...\"}}]}"
}
```

or:

```json
{"type":"error","id":"1","status":500,"message":"generation failed"}
```

The proxy also accepts `status` and `pong`, and replies to `ping` with `pong`.
Request IDs permit concurrent HTTP callers, although the model server may still
serialize generation.

## Native behavior

- Maximum 16 concurrent HTTP connections.
- Default body limit: 4 MiB.
- Default completion timeout: 120 seconds.
- Malformed, oversized, unauthenticated, or unknown messages are rejected.
- A reverse disconnect fails pending requests and returns the proxy to `waiting`.
- A new authenticated server can reconnect without restarting the app.
- `stop()` closes listeners, active connections, and pending requests before
  joining native threads.

The native implementation exposes no C++ ABI to consumers. ArkTS calls it only
through Node-API, and the `.so` is packaged as an implementation detail of the
HAR.

## Build

Build the `lite_proxy` module as a HAR with DevEco Studio or Hvigor. The module
build produces `liblite_proxy.so` for `arm64-v8a` and `x86_64` and packages both
inside the HAR.

For proxy bootstrap, app installation, server deployment, and connection-only
verification on a simulator, see
[`../docs/lite-proxy-server-test-guide.md`](../docs/lite-proxy-server-test-guide.md).

The protocol codec test is dependency-free and can be enabled in a standalone
CMake build with `-DLITE_PROXY_BUILD_TESTS=ON`.
