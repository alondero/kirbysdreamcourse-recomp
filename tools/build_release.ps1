param(
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$')][string]$Version,
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$CMake = '',
    [string]$SdlSource = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $root
try {
    $target = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the source commit.' }
    if (git status --porcelain) { throw 'Release builds require a clean source checkout.' }
    # Setup is shared with local development: same ROM checks, pins, patches,
    # generation and guarded configuration. Private inputs never enter staging.
    $options = @{ Rom = $Rom; BuildVersion = $Version; ReleaseDefaults = $true }
    if ($CMake) { $options.CMake = $CMake }
    if ($SdlSource) { $options.SdlSource = $SdlSource }
    & (Join-Path $PSScriptRoot 'setup.ps1') @options
    $gccPath = (Get-Command gcc -ErrorAction Stop).Source
    $compilerBin = Split-Path -Parent $gccPath
    $objdump = Join-Path $compilerBin 'objdump.exe'
    if (-not (Test-Path -LiteralPath $objdump)) { throw 'objdump.exe is required beside GCC to inspect DLL imports.' }
    $toolchainRoot = Split-Path -Parent $compilerBin
    $notices = Join-Path $toolchainRoot 'licenses'
    foreach ($name in @('gcc', 'mingw-w64')) {
        if (-not (Test-Path -LiteralPath (Join-Path $notices $name))) {
            throw "MinGW notice directory missing: $notices/$name"
        }
    }
    python .agents/skills/release/scripts/release.py stage-windows --target $target --version $Version `
        --build local/guarded-build --stage local/release-stage --objdump $objdump `
        --toolchain-notices $notices
    if ($LASTEXITCODE -ne 0) { throw 'Windows release staging failed.' }
    python .agents/skills/release/scripts/release.py windows-package --target $target --version $Version --stage local/release-stage
    if ($LASTEXITCODE -ne 0) { throw 'Windows release packaging failed.' }
} finally {
    Pop-Location
}
