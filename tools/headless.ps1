param(
    [Parameter(Mandatory=$true)][string]$Rom,
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$Executable = 'build/KirbyDreamCourseRecomp.exe',
    [int]$Frames = 600,
    [int]$ExpectedUniqueFrames = 0,
    [string]$InputScript = '',
    [string]$InputRecording = '',
    [string]$InitialSram = '',
    [string]$InitialState = '',
    [switch]$Widescreen,
    [ValidateRange(0, 272)][int]$WideExtra = 43,
    [switch]$Unpaced
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Frames -le 0) { throw 'Frames must be positive.' }
if ($ExpectedUniqueFrames -lt 0 -or $ExpectedUniqueFrames -gt $Frames) { throw 'ExpectedUniqueFrames must be between zero and Frames.' }
if ($InputScript -and $InputRecording) { throw 'Choose InputScript or InputRecording, not both.' }
if ($Unpaced -and [Environment]::GetEnvironmentVariable('SNESRECOMP_DSPOUT')) {
    throw 'Unpaced runs cannot capture deterministic DSP ring audio.'
}
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$expected = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'ROM SHA-256 mismatch.' }
$exe = (Resolve-Path -LiteralPath $Executable).Path
$localRoot = [IO.Path]::GetFullPath((Join-Path $root 'local')).TrimEnd('\') + '\'
$destination = [IO.Path]::GetFullPath((Join-Path $root $Output))
if (-not $destination.StartsWith($localRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be within local/.' }
if (Test-Path -LiteralPath $destination) { throw 'Output already exists; choose a fresh directory.' }
New-Item -ItemType Directory -Force (Join-Path $destination 'saves') | Out-Null
$copy = Join-Path $destination 'game.exe'
Copy-Item -LiteralPath $exe -Destination $copy
# Keep keyboard seats and physical controllers out of recorded runs. The host
# otherwise ORs their state with the script.
@'
[General]
Autosave = 0
DisableFrameDelay = 0
[Graphics]
NewRenderer = 1
NoSpriteLimits = 0
Widescreen = 0
[Sound]
EnableAudio = 1
AudioFreq = 32000
AudioChannels = 2
AudioSamples = 512
[Controller]
SourceP1 = 0
SourceP2 = 0
[GamepadMap]
EnableGamepad1 = false
EnableGamepad2 = false
'@ | Set-Content -LiteralPath (Join-Path $destination 'config.initial.ini') -Encoding ASCII
if ($Widescreen) {
    $configPath = Join-Path $destination 'config.initial.ini'
    $configText = [IO.File]::ReadAllText($configPath).Replace('Widescreen = 0', 'Widescreen = 1')
    [IO.File]::WriteAllText($configPath, $configText, [Text.Encoding]::ASCII)
}
if ($Unpaced) {
    $configPath = Join-Path $destination 'config.initial.ini'
    $configText = [IO.File]::ReadAllText($configPath)
    if (-not $configText.Contains('DisableFrameDelay = 0')) { throw 'Expected frame-delay setting is missing.' }
    $configText = $configText.Replace('DisableFrameDelay = 0', 'DisableFrameDelay = 1')
    [IO.File]::WriteAllText($configPath, $configText, [Text.Encoding]::ASCII)
}
Copy-Item -LiteralPath (Join-Path $destination 'config.initial.ini') -Destination (Join-Path $destination 'config.ini')
$configHash = (Get-FileHash -LiteralPath (Join-Path $destination 'config.initial.ini') -Algorithm SHA256).Hash.ToLowerInvariant()
$sram = [byte[]]::new(8192)
if ($InitialSram) {
    $sram = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $InitialSram).Path)
    if ($sram.Length -ne 8192) { throw "Initial SRAM must be exactly 8192 bytes." }
}
[IO.File]::WriteAllBytes((Join-Path $destination 'saves/save.srm'), $sram)
$initialStateHash = $null
if ($InitialState) {
    $statePath = (Resolve-Path -LiteralPath $InitialState).Path
    $initialStateHash = (Get-FileHash -LiteralPath $statePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-Item -LiteralPath $statePath -Destination (Join-Path $destination 'saves/save1.sav')
}
$initialSramHash = (Get-FileHash -LiteralPath (Join-Path $destination 'saves/save.srm') -Algorithm SHA256).Hash.ToLowerInvariant()
$recordingHash = $null
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
$env:SNESRECOMP_RUN_FRAMES = "$Frames"
$wideOverride = if ($Widescreen) { "$WideExtra" } else { '0' }
$hostArgs = @('--no-launcher', '--rom', $romPath, '--framedump', (Join-Path $destination 'frames'))
if ($InputRecording) {
    $InputRecording = (Resolve-Path -LiteralPath $InputRecording).Path
    $recordingHash = (Get-FileHash -LiteralPath $InputRecording -Algorithm SHA256).Hash.ToLowerInvariant()
    $InputScript = Join-Path $destination 'input.script'
    python (Join-Path $root 'tools/input_recording.py') $InputRecording --output $InputScript --frames $Frames
    if ($LASTEXITCODE -ne 0) { throw 'Invalid input recording.' }
}
if ($InputScript) { $hostArgs += @('--script', (Resolve-Path -LiteralPath $InputScript).Path) }
$previousWideOverride = $env:SNESRECOMP_WS_EXTRA
try {
    $env:SNESRECOMP_WS_EXTRA = $wideOverride
    & $copy @hostArgs
    if ($LASTEXITCODE -ne 0) { throw "Host failed: $LASTEXITCODE" }
} finally {
    $env:SNESRECOMP_WS_EXTRA = $previousWideOverride
}
$count = @(Get-ChildItem -LiteralPath (Join-Path $destination 'frames') -Filter '*_wram.bin').Count
$expectedCount = if ($ExpectedUniqueFrames) { $ExpectedUniqueFrames } else { $Frames }
if ($count -ne $expectedCount) { throw "Expected $expectedCount WRAM frames, got $count." }
$manifest = [ordered]@{
    schema = 1
    rom_sha256 = $expected
    executable_sha256 = (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLowerInvariant()
    initial_config_sha256 = $configHash
    runtime_options = [ordered]@{
        SNESRECOMP_WS_EXTRA = $wideOverride
        SNESRECOMP_APU_BEAM_ELAPSED = [Environment]::GetEnvironmentVariable('SNESRECOMP_APU_BEAM_ELAPSED')
        SNESRECOMP_APU_HANDOFF = [Environment]::GetEnvironmentVariable('SNESRECOMP_APU_HANDOFF')
        SNESRECOMP_APU_ABSOLUTE_PREANCHOR = [Environment]::GetEnvironmentVariable('SNESRECOMP_APU_ABSOLUTE_PREANCHOR')
        SNESRECOMP_LLE_BOUNCE = [Environment]::GetEnvironmentVariable('SNESRECOMP_LLE_BOUNCE')
        KDC_EXPERIMENTAL_REFRESH_PARITY = [Environment]::GetEnvironmentVariable('KDC_EXPERIMENTAL_REFRESH_PARITY')
        KDC_EXPERIMENTAL_AUTOJOY_LATCH = [Environment]::GetEnvironmentVariable('KDC_EXPERIMENTAL_AUTOJOY_LATCH')
        KDC_EXPERIMENTAL_AUTOJOY_PHASE = [Environment]::GetEnvironmentVariable('KDC_EXPERIMENTAL_AUTOJOY_PHASE')
        KDC_EXPERIMENTAL_APU_BYTE_PHASE = [Environment]::GetEnvironmentVariable('KDC_EXPERIMENTAL_APU_BYTE_PHASE')
        KDC_EXPERIMENTAL_APU_DRAIN = [Environment]::GetEnvironmentVariable('KDC_EXPERIMENTAL_APU_DRAIN')
        KDC_EARLY_DELAY = [Environment]::GetEnvironmentVariable('KDC_EARLY_DELAY')
        KDC_EXPERIMENTAL_SPC_D4_PHASE = [Environment]::GetEnvironmentVariable('KDC_EXPERIMENTAL_SPC_D4_PHASE')
    }
    initial_sram_sha256 = $initialSramHash
    unpaced = [bool]$Unpaced
    initial_state_sha256 = $initialStateHash
    input_recording_sha256 = $recordingHash
    input_script_sha256 = if ($InputScript) { (Get-FileHash -LiteralPath $InputScript -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
    frames = $Frames
    unique_frame_dumps = $count
    completed = $true
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'run.json') -Encoding UTF8
