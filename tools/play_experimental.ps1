param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$Executable = 'local/guarded-build/KirbyDreamCourseRecomp.exe',
    [switch]$Fallback,
    [ValidateRange(0, 2147483647)][int]$Frames = 0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Resolve-ProjectPath([string]$path) {
    if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $root $path }
    return (Resolve-Path -LiteralPath $path).Path
}
$romPath = Resolve-ProjectPath $Rom
$exePath = Resolve-ProjectPath $Executable
$expected = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'ROM SHA-256 mismatch; use the supported headerless USA image.'
}

$settings = @{
    SNESRECOMP_APU_BEAM_ELAPSED = '1'
    SNESRECOMP_APU_HANDOFF = '1'
    SNESRECOMP_APU_ABSOLUTE_PREANCHOR = '1'
    SNESRECOMP_LLE_BOUNCE = if ($Fallback) { '0' } else { '1' }
    SNESRECOMP_RUN_FRAMES = if ($Frames -gt 0) { "$Frames" } else { $null }
}
$fallbackDir = Join-Path $root 'local/launcher-fallback'
New-Item -ItemType Directory -Path $fallbackDir -Force | Out-Null
$previous = @{}
try {
    foreach ($name in $settings.Keys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $settings[$name], 'Process')
    }
    Push-Location -LiteralPath $fallbackDir
    try {
        & $exePath --no-launcher --rom $romPath
        if ($LASTEXITCODE -ne 0) { throw "Experimental host exited with code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
