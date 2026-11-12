param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$Executable = 'local/guarded-build/KirbyDreamCourseRecomp.exe',
    [switch]$Fallback,
    [switch]$NoLauncher,
    [string]$DevHole = '',
    [switch]$DevTwoPlayer,
    [switch]$DevHoleList,
    [ValidateRange(0, 2147483647)][int]$Frames = 0
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'invoke_native.ps1')
if ($DevTwoPlayer -and -not $DevHole.StartsWith('map:')) {
    throw '-DevTwoPlayer requires -DevHole map:ID.'
}
$root = Split-Path -Parent $PSScriptRoot
function Resolve-ProjectPath([string]$path) {
    if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $root $path }
    return (Resolve-Path -LiteralPath $path).Path
}
$exePath = Resolve-ProjectPath $Executable
if ($DevHoleList) {
    $list = Invoke-NativeCommand -FilePath $exePath -ArgumentList @('--dev-hole-list')
    exit $list.ExitCode
}
$romPath = Resolve-ProjectPath $Rom
$expected = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'ROM SHA-256 mismatch; use the supported headerless USA image.'
}
if ($DevHole) {
    # Host persistence is executable-relative. Give test starts their own
    # saves/config rather than the normal play session's battery image.
    $devDirectory = Join-Path $root 'local/dev-play'
    New-Item -ItemType Directory -Force -Path $devDirectory | Out-Null
    $devExe = Join-Path $devDirectory 'KirbyDreamCourseRecomp.exe'
    if ($exePath -ne $devExe) {
        $sourceDirectory = Split-Path -Parent $exePath
        Copy-Item -LiteralPath $exePath -Destination $devExe
        foreach ($name in @('assets', 'licenses')) {
            $source = Join-Path $sourceDirectory $name
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination $devDirectory -Recurse -Force
            }
        }
        foreach ($name in @('config.ini', 'keybinds.ini')) {
            $source = Join-Path $sourceDirectory $name
            $destination = Join-Path $devDirectory $name
            if ((Test-Path -LiteralPath $source) -and -not (Test-Path -LiteralPath $destination)) {
                Copy-Item -LiteralPath $source -Destination $destination
            }
        }
    }
    $exePath = $devExe
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
        $hostArgs = @('--rom', $romPath)
        if ($NoLauncher -or $Frames -gt 0 -or $DevHole) { $hostArgs += '--no-launcher' }
        if ($DevHole) { $hostArgs += @('--dev-hole', $DevHole) }
        if ($DevTwoPlayer) { $hostArgs += '--dev-two-player' }
        $host = Invoke-NativeCommand -FilePath $exePath -ArgumentList $hostArgs
        if ($host.ExitCode -ne 0) { throw "Experimental host exited with code $($host.ExitCode)." }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $previous.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
    }
}
