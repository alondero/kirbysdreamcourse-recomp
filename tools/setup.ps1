param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$CMake = '',
    [string]$SdlSource = '',
    [ValidatePattern('^[A-Za-z0-9._+-]+$')][string]$BuildVersion = 'dev',
    [switch]$ReleaseDefaults
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'invoke_native.ps1')
. (Join-Path $PSScriptRoot 'find_native_cmake.ps1')
$root = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($Rom)) { $Rom = Join-Path $root $Rom }
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$expected = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'ROM SHA-256 mismatch. Use the supported headerless USA ROM.'
}
foreach ($command in @('git', 'python', 'ninja', 'gcc')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Missing $command on PATH; see README.md." }
}
$version = Invoke-NativeCommand -FilePath 'python' `
    -ArgumentList @('-c', 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)') -Quiet
if ($version.ExitCode -ne 0) { throw 'Python 3.11 or newer is required.' }
# A native CMake is discovered automatically; -CMake only overrides the choice.
$CMake = Find-NativeCMake -CMake $CMake
Write-Host "Using native CMake: $CMake"
& (Join-Path $PSScriptRoot 'assert_native_cmake.ps1') -CMake $CMake
$framework = Join-Path $root 'local/snesrecomp'
$pin = '147b55e252dcb3e7680a99c7c2f50e7183891844'
New-Item -ItemType Directory -Path (Join-Path $root 'local') -Force | Out-Null
if (-not (Test-Path -LiteralPath $framework)) {
    $clone = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('clone', '-c', 'core.autocrlf=false',
        '--no-checkout', 'https://github.com/RetroPortingToolKit/snesrecomp.git', $framework)
    if ($clone.ExitCode -ne 0) { throw 'Could not clone SNESRecomp.' }
    $checkout = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $framework, 'checkout', '--detach', $pin)
    if ($checkout.ExitCode -ne 0) { throw 'Could not check out the pinned SNESRecomp revision.' }
}
if (-not (Test-Path (Join-Path $framework '.git'))) { throw 'local/snesrecomp is not a Git checkout.' }
$head = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $framework, 'rev-parse', 'HEAD') -Quiet
if ($head.ExitCode -ne 0 -or ($head.Output | Select-Object -Last 1) -ne $pin) {
    throw 'local/snesrecomp is not at the required revision.'
}
$status = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $framework, 'status', '--porcelain') -Quiet
if ($status.ExitCode -ne 0) { throw 'Could not inspect local/snesrecomp.' }
if (@($status.Output).Count -gt 0) { throw 'local/snesrecomp has local changes; use a clean pinned checkout.' }
$options = @{ Rom = $romPath; CMake = $CMake; Name = 'guarded';
    BuildVersion = $BuildVersion; ReleaseDefaults = [bool]$ReleaseDefaults }
if ($SdlSource) { $options.SdlSource = $SdlSource }
& (Join-Path $PSScriptRoot 'prepare_guarded.ps1') @options
Write-Host 'Build complete. Run: ./tools/play_experimental.ps1'
