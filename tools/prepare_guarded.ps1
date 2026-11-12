param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$CMake = '',
    [string]$SdlSource = '',
    [ValidatePattern('^[A-Za-z0-9._+-]+$')][string]$BuildVersion = 'dev',
    [switch]$ReleaseDefaults,
    [switch]$ApuBoundarySync,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name = 'guarded'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'invoke_native.ps1')
. (Join-Path $PSScriptRoot 'find_native_cmake.ps1')
# A native CMake is discovered automatically; -CMake only overrides the choice.
$CMake = Find-NativeCMake -CMake $CMake
& (Join-Path $PSScriptRoot 'assert_native_cmake.ps1') -CMake $CMake
$options = @((Join-Path $PSScriptRoot 'setup.py'), '--rom', $Rom, '--cmake', $CMake,
    '--name', $Name, '--version', $BuildVersion)
if ($SdlSource) { $options += @('--sdl-source', $SdlSource) }
if ($ReleaseDefaults) { $options += '--release-defaults' }
if ($ApuBoundarySync) { $options += '--apu-boundary-sync' }
$build = Invoke-NativeCommand -FilePath 'python' -ArgumentList $options
if ($build.ExitCode -ne 0) { throw 'Guarded build failed.' }
