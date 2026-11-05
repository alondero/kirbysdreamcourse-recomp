param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [Parameter(Mandatory=$true)][string]$CMake,
    [string]$SdlSource = '',
    [ValidatePattern('^[A-Za-z0-9._+-]+$')][string]$BuildVersion = 'dev',
    [switch]$ReleaseDefaults,
    [switch]$ApuBoundarySync,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name = 'guarded'
)

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'assert_native_cmake.ps1') -CMake $CMake
$options = @((Join-Path $PSScriptRoot 'setup.py'), '--rom', $Rom, '--cmake', $CMake,
    '--name', $Name, '--version', $BuildVersion)
if ($SdlSource) { $options += @('--sdl-source', $SdlSource) }
if ($ReleaseDefaults) { $options += '--release-defaults' }
if ($ApuBoundarySync) { $options += '--apu-boundary-sync' }
python @options
if ($LASTEXITCODE -ne 0) { throw 'Guarded build failed.' }