param(
  [ValidateSet('arm64-v8a', 'x86_64')]
  [string]$Arch = 'arm64-v8a',
  [ValidateSet('Debug', 'Release')]
  [string]$BuildType = 'Release',
  [string]$SdkRoot = '',
  [switch]$RealVendor,
  [switch]$WithoutTests
)

$ErrorActionPreference = 'Stop'

function Resolve-SdkRoot([string]$Requested) {
  if ($Requested.Length -gt 0) {
    return (Resolve-Path -LiteralPath $Requested).Path
  }
  if ($env:DEVECO_SDK_HOME -and (Test-Path -LiteralPath $env:DEVECO_SDK_HOME)) {
    return (Resolve-Path -LiteralPath $env:DEVECO_SDK_HOME).Path
  }
  $homeMarker = Join-Path $env:LOCALAPPDATA 'Huawei\DevEcoStudio6.1\.home'
  if (Test-Path -LiteralPath $homeMarker) {
    $studioHome = (Get-Content -Raw -LiteralPath $homeMarker).Trim()
    $candidate = Join-Path $studioHome 'sdk'
    if (Test-Path -LiteralPath $candidate) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw 'Unable to locate the DevEco SDK. Pass -SdkRoot or set DEVECO_SDK_HOME.'
}

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resolvedSdk = Resolve-SdkRoot $SdkRoot
$ohosNative = Join-Path $resolvedSdk 'default\openharmony\native'
$hmosNative = Join-Path $resolvedSdk 'default\hms\native'
$cmake = Join-Path $ohosNative 'build-tools\cmake\bin\cmake.exe'
$ninja = Join-Path $ohosNative 'build-tools\cmake\bin\ninja.exe'
$toolchain = Join-Path $hmosNative 'build\cmake\hmos.toolchain.bisheng.cmake'
foreach ($required in @($cmake, $ninja, $toolchain)) {
  if (!(Test-Path -LiteralPath $required)) {
    throw "Required SDK tool not found: $required"
  }
}

$buildDir = Join-Path $projectRoot ("build-$Arch")
$mockValue = if ($RealVendor) { 'OFF' } else { 'ON' }
$testsValue = if ($WithoutTests) { 'OFF' } else { 'ON' }

& $cmake -S $projectRoot -B $buildDir -G Ninja `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  "-DCMAKE_MAKE_PROGRAM=$ninja" `
  "-DHMOS_SDK_NATIVE=$hmosNative" `
  "-DOHOS_SDK_NATIVE=$ohosNative" `
  '-DCMAKE_SYSTEM_NAME=OHOS' `
  "-DOHOS_ARCH=$Arch" `
  "-DCMAKE_OHOS_ARCH_ABI=$Arch" `
  "-DCMAKE_BUILD_TYPE=$BuildType" `
  "-DLITE_USE_MOCK_VENDOR=$mockValue" `
  "-DLITE_SERVER_BUILD_TESTS=$testsValue"
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& $cmake --build $buildDir
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Output "Built lite-server in $buildDir"
