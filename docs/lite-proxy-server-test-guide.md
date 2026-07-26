# Lite proxy/server connection test

This guide verifies only the connection:

```text
app -> in-app lite_proxy -> lite-server -> mock liblite_llm.so
```

It does not send a chat-completion request or call `Generate()`.

## Prerequisites

- Windows PowerShell
- DevEco Studio with the HarmonyOS/OpenHarmony SDK installed
- A running simulator or connected device
- The repository root as the current directory

Set paths for your DevEco Studio installation:

```powershell
$DevEcoHome = 'D:\Applications\DevEco Studio'
$SdkRoot = Join-Path $DevEcoHome 'sdk'
$Hdc = Join-Path $SdkRoot 'default\openharmony\toolchains\hdc.exe'
$Hvigor = Join-Path $DevEcoHome 'tools\hvigor\bin\hvigorw.bat'

$env:NODE_HOME = Join-Path $DevEcoHome 'tools\node'
$env:JAVA_HOME = Join-Path $DevEcoHome 'jbr'
$env:DEVECO_SDK_HOME = $SdkRoot
$env:Path = "$env:JAVA_HOME\bin;$env:NODE_HOME;$env:Path"
```

Confirm the target and select the matching server architecture:

```powershell
& $Hdc list targets
$TargetAbi = (& $Hdc shell uname -m).Trim()

$Arch = switch ($TargetAbi) {
  'x86_64'  { 'x86_64' }
  'aarch64' { 'arm64-v8a' }
  'arm64'   { 'arm64-v8a' }
  default   { throw "Unsupported target ABI: $TargetAbi" }
}

$Arch
```

## 1. Configure the in-app proxy

Copy the example bootstrap configuration into the app resources:

```powershell
Copy-Item `
  -LiteralPath '.\lite_proxy\examples\lite_proxy_config.json' `
  -Destination '.\entry\src\main\resources\rawfile\lite_proxy_config.json'
```

The file should contain:

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

For shared or production environments, replace the example token with a strong
random per-install secret. The same token and reverse port must be passed to
`lite-server`.

The application already:

- imports `@appless/lite-proxy`;
- starts the proxy from `EntryAbility`;
- changes the local OpenAI-compatible client to
  `http://127.0.0.1:18080/v1`;
- uses `authToken` as the OpenAI API key; and
- stops the proxy when the ability is destroyed.

The app must declare `ohos.permission.INTERNET`. This repository already does.

### Integrating the proxy into another app

When testing another host app, add the HAR source module or its built `.har` as
an OHPM dependency. A source-module dependency looks like:

```json5
{
  "dependencies": {
    "@appless/lite-proxy": "file:../lite_proxy"
  }
}
```

Add the permission to the host module's `module.json5`:

```json5
{
  "module": {
    "requestPermissions": [
      {
        "name": "ohos.permission.INTERNET"
      }
    ]
  }
}
```

Start the proxy during application creation, configure the existing
OpenAI-compatible client from the returned status, and stop it during teardown:

```ts
import { LiteProxy, LiteProxyStatus } from '@appless/lite-proxy';

const status: LiteProxyStatus = await LiteProxy.start({
  httpPort: 18080,
  reversePort: 18081,
  authToken: 'replace-this-development-token',
  requestTimeoutMs: 120000,
  maxBodyBytes: 4 * 1024 * 1024
});

const openAiBaseUrl = status.baseUrl;
const openAiApiKey = status.authToken;

// During application teardown:
await LiteProxy.stop();
```

`status.baseUrl` includes the `/v1` suffix. If port `0` or an empty token is
used, always pass the returned port/token values to the server instead of
assuming the requested values. See
[`../lite_proxy/README.md`](../lite_proxy/README.md) for the complete HAR API.

## 2. Build and install the proxy-enabled app

Build the entry HAP:

```powershell
& $Hvigor assembleHap `
  --mode module `
  -p module=entry@default `
  -p product=default `
  --no-daemon
```

With a valid local signing configuration, install the generated signed HAP.
This simulator also accepts the unsigned development HAP:

```powershell
$Hap = '.\entry\build\default\outputs\default\entry-default-unsigned.hap'
& $Hdc install -r $Hap
```

If `SignHap` reports a certificate path from another workstation, verify that
the log reached `Finished :entry:default@PackageHap`. Use the unsigned HAP only
on a simulator that permits unsigned development installation. A physical
device normally requires a correctly signed HAP.

Launch the app:

```powershell
& $Hdc shell aa start -a EntryAbility -b com.example.aiphonedemo
```

Confirm that the app owns both loopback listeners:

```powershell
$Netstat = & $Hdc shell netstat -an
$Netstat | Select-String -Pattern '127.0.0.1:18080|127.0.0.1:18081'
```

Expected state before starting the server:

```text
127.0.0.1:18080 ... LISTEN
127.0.0.1:18081 ... LISTEN
```

## 3. Compile lite-server

From the repository root:

```powershell
& '.\lite-server\scripts\build.ps1' `
  -Arch $Arch `
  -BuildType Release `
  -SdkRoot $SdkRoot
```

The output directory is `lite-server/build-$Arch/` and contains:

- `lite-server`
- `liblite_llm.so`
- `lite-server-tests`

The default build links the DeepSeek-backed demo vendor implementation. Do not
pass `-RealVendor` for this test.

## 4. Deploy lite-server

The deployment script copies the server, demo vendor library, matching
`libc++_shared.so`, and DeepSeek config to
`/data/local/tmp/lite-server`:

```powershell
$AuthToken = 'replace-this-development-token'

& '.\lite-server\scripts\deploy.ps1' `
  -Arch $Arch `
  -SdkRoot $SdkRoot `
  -HdcPath $Hdc `
  -AuthToken $AuthToken `
  -ReversePort 18081
```

Do not pass `-RunTests` when the goal is connection-only verification.

## 5. Launch the server from HDC shell

Keep the app running. In the first PowerShell terminal, launch the server in
the foreground:

```powershell
& $Hdc shell "cd /data/local/tmp/lite-server && LD_LIBRARY_PATH=. ./lite-server --config ./config.json --connect-host 127.0.0.1 --connect-port 18081 --auth-token $AuthToken"
```

Expected output:

```text
lite-server: model built from ./config.json
lite-server: connected to proxy at 127.0.0.1:18081
lite-server: proxy handshake accepted; model is ready
```

The first line initializes the demo library. No generation occurs until a chat
request is sent.

## 6. Verify only the connection

In a second PowerShell terminal, recreate the path variables from
[Prerequisites](#prerequisites), then check the TCP session:

```powershell
$Netstat = & $Hdc shell netstat -an
$Netstat | Select-String -Pattern '127.0.0.1:18081.*ESTABLISHED|ESTABLISHED.*127.0.0.1:18081'
```

There should be two views of one loopback connection:

```text
127.0.0.1:18081 127.0.0.1:<ephemeral-port> ESTABLISHED
127.0.0.1:<ephemeral-port> 127.0.0.1:18081 ESTABLISHED
```

Optionally forward the proxy HTTP port and query only `/ready`:

```powershell
& $Hdc fport tcp:28080 tcp:18080
try {
  Invoke-RestMethod -Uri 'http://127.0.0.1:28080/ready' -TimeoutSec 10
} finally {
  & $Hdc fport rm tcp:28080 tcp:18080
}
```

Expected response:

```text
status
------
ready
```

An HTTP 200 response with `{"status":"ready"}` confirms that the app accepted
the authenticated reverse handshake. Do not POST to `/v1/chat/completions` for
this connection-only test.

## Cleanup

1. Press `Ctrl+C` in the foreground server terminal.
2. Stop the app:

   ```powershell
   & $Hdc shell aa force-stop com.example.aiphonedemo
   ```

3. Remove `entry/src/main/resources/rawfile/lite_proxy_config.json`, or set
   `"enabled": false`.
4. Rebuild and reinstall the app if it should remain proxy-disabled on the
   simulator.
5. Confirm there are no leftover forwards or listeners:

   ```powershell
   & $Hdc fport ls
   $Netstat = & $Hdc shell netstat -an
   $Netstat | Select-String -Pattern '18080|18081'
   ```

The deployed files may remain in `/data/local/tmp/lite-server` for later tests;
no process remains after the foreground server exits.

## Troubleshooting

### Proxy ports are not listening

- Confirm `lite_proxy_config.json` is inside the built HAP.
- Confirm `"enabled": true`.
- Relaunch `EntryAbility` after reinstalling the HAP.
- Confirm the app has `ohos.permission.INTERNET`.

### Server repeatedly reconnects

- Confirm the app is still running.
- Confirm both sides use reverse port `18081`.
- Confirm both sides use exactly the same authentication token.
- Confirm the current server binary was rebuilt and redeployed.

### `getsockopt(SO_ERROR) failed: Permission denied`

The simulator restricts `SO_ERROR` inspection for ordinary HDC-shell
processes. Current sources contain a safe poll-state fallback. Rebuild and
redeploy `lite-server`; this error indicates that an older binary is running.

### `/ready` returns 503

The proxy is running, but it has not accepted an authenticated server
connection. Check the server terminal for connect or handshake errors.

### HDC forwarding removal fails

This HDC version expects both nodes:

```powershell
& $Hdc fport rm tcp:28080 tcp:18080
```
