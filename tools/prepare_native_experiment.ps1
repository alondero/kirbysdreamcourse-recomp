param(
    [Parameter(Mandatory=$true)][string]$Rom,
    [Parameter(Mandatory=$true)][string]$CMake,
    [string]$SdlSource = '',
    [switch]$CurrentStack,
    [switch]$RasterState,
    [switch]$FieldTiming,
    [switch]$RdnmiBeam,
    [switch]$InstructionBoundaries,
    [switch]$IncrementalDsp,
    [switch]$SharedNoise,
    [switch]$NmiEntry,
    [switch]$NmiTrigger,
    [switch]$RefreshShort,
    [switch]$LiveWram,
    [switch]$HdmaCount,
    [switch]$BeamHdma,
    [switch]$HdmaInterrupt,
    [switch]$LateNmi,
    [switch]$IrqEntry,
    [switch]$EmuStack,
    [switch]$CpuIoBus,
    [switch]$NestedNmi,
    [switch]$WaiWake,
    [switch]$PpuLatchPhase,
    [switch]$HvbjoyPhase,
    [switch]$ResetPhase,
    [switch]$WaiCycle,
    [switch]$Ppu2Bus,
    [switch]$ApuQueueRead,
    [switch]$SpcC4Phase,
    [switch]$RasterLiveRestore,
    [switch]$ManualJoypad,
    [switch]$AutoJoyPhase,
    [switch]$SpcD4Phase,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name = 'native-experiment'
)
$ErrorActionPreference = 'Stop'
if ($RasterState -and -not $CurrentStack) { throw '-RasterState requires -CurrentStack.' }
if ($FieldTiming -and -not $RasterState) { throw '-FieldTiming requires -RasterState.' }
if ($RdnmiBeam -and -not $FieldTiming) { throw '-RdnmiBeam requires -FieldTiming.' }
if ($InstructionBoundaries -and -not $RdnmiBeam) { throw '-InstructionBoundaries requires -RdnmiBeam.' }
if ($IncrementalDsp -and -not $InstructionBoundaries) { throw '-IncrementalDsp requires -InstructionBoundaries.' }
if ($SharedNoise -and -not $IncrementalDsp) { throw '-SharedNoise requires -IncrementalDsp.' }
if ($NmiEntry -and -not $SharedNoise) { throw '-NmiEntry requires -SharedNoise.' }
if ($NmiTrigger -and -not $NmiEntry) { throw '-NmiTrigger requires -NmiEntry.' }
if ($RefreshShort -and -not $NmiTrigger) { throw '-RefreshShort requires -NmiTrigger.' }
if ($LiveWram -and -not $RefreshShort) { throw '-LiveWram requires -RefreshShort.' }
if ($HdmaCount -and -not $LiveWram) { throw '-HdmaCount requires -LiveWram.' }
if ($BeamHdma -and -not $HdmaCount) { throw '-BeamHdma requires -HdmaCount.' }
if ($HdmaInterrupt -and -not $BeamHdma) { throw '-HdmaInterrupt requires -BeamHdma.' }
if ($LateNmi -and -not $HdmaInterrupt) { throw '-LateNmi requires -HdmaInterrupt.' }
if ($IrqEntry -and -not $LateNmi) { throw '-IrqEntry requires -LateNmi.' }
if ($EmuStack -and -not $IrqEntry) { throw '-EmuStack requires -IrqEntry.' }
if ($CpuIoBus -and -not $EmuStack) { throw '-CpuIoBus requires -EmuStack.' }
if ($NestedNmi -and -not $CpuIoBus) { throw '-NestedNmi requires -CpuIoBus.' }
if ($WaiWake -and -not $NestedNmi) { throw '-WaiWake requires -NestedNmi.' }
if ($PpuLatchPhase -and -not $WaiWake) { throw '-PpuLatchPhase requires -WaiWake.' }
if ($HvbjoyPhase -and -not $PpuLatchPhase) { throw '-HvbjoyPhase requires -PpuLatchPhase.' }
if ($ResetPhase -and -not $HvbjoyPhase) { throw '-ResetPhase requires -HvbjoyPhase.' }
if ($WaiCycle -and -not $ResetPhase) { throw '-WaiCycle requires -ResetPhase.' }
if ($Ppu2Bus -and -not $WaiCycle) { throw '-Ppu2Bus requires -WaiCycle.' }
if ($ApuQueueRead -and -not $Ppu2Bus) { throw '-ApuQueueRead requires -Ppu2Bus.' }
if ($SpcC4Phase -and -not $ApuQueueRead) { throw '-SpcC4Phase requires -ApuQueueRead.' }
if ($RasterLiveRestore -and -not $ApuQueueRead) { throw '-RasterLiveRestore requires -ApuQueueRead.' }
if ($AutoJoyPhase -and (-not $ManualJoypad -or -not $Ppu2Bus -or $SpcC4Phase)) { throw '-AutoJoyPhase requires -ManualJoypad and -Ppu2Bus and cannot be combined with -SpcC4Phase.' }
if ($SpcD4Phase -and (-not $AutoJoyPhase -or $SpcC4Phase)) { throw '-SpcD4Phase requires -AutoJoyPhase and cannot be combined with -SpcC4Phase.' }
$root = Split-Path -Parent $PSScriptRoot
$local = Join-Path $root 'local'
$frameworkName = "$Name-framework"
$framework = Join-Path $local $frameworkName
$project = Join-Path $local $Name
$build = Join-Path $local "$Name-build"
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$expected = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'ROM SHA-256 mismatch.'
}
$cmakePath = (Resolve-Path -LiteralPath $CMake).Path
foreach ($path in @($framework, $project, $build)) {
    if (Test-Path -LiteralPath $path) { throw "$path already exists; choose a fresh -Name." }
}
if ($SdlSource) { $SdlSource = (Resolve-Path -LiteralPath $SdlSource).Path }
& (Join-Path $root 'tools/prepare_handoff.ps1') -SimpleBus -Refresh -Name $frameworkName
$patches = @(
    'snesrecomp-apu-rate-experiment.patch',
    'snesrecomp-preanchor-experiment.patch',
    'snesrecomp-beam-apu-clock-experiment.patch',
    'snesrecomp-beam-apu-fraction-experiment.patch',
    'snesrecomp-hdma-prime-experiment.patch'

)
if ($CurrentStack) {
    $patches += @(
        'snesrecomp-spc-timer-phase-experiment.patch',
        'snesrecomp-refresh-park-experiment.patch',
        'snesrecomp-refresh-position-experiment.patch',
        'snesrecomp-soft-reset-vram-experiment.patch',
        'snesrecomp-savestate-script-probe.patch',
        'snesrecomp-beam-state-experiment.patch',
        'snesrecomp-state-preflight-experiment.patch',
        'snesrecomp-refresh-zero-epoch-experiment.patch',
        'snesrecomp-zero-dma-experiment.patch',
        'snesrecomp-absolute-preanchor-experiment.patch'
    )
}
if ($RasterState) { $patches += 'snesrecomp-raster-state-experiment.patch' }
if ($FieldTiming) { $patches += 'snesrecomp-field-timing-experiment.patch' }
if ($RdnmiBeam) { $patches += 'snesrecomp-rdnmi-beam-experiment.patch' }
if ($InstructionBoundaries) { $patches += 'snesrecomp-instruction-boundaries-experiment.patch' }
if ($IncrementalDsp) { $patches += 'snesrecomp-incremental-brr-experiment.patch' }
if ($SharedNoise) { $patches += 'snesrecomp-shared-noise-experiment.patch' }
if ($NmiEntry) { $patches += 'snesrecomp-nmi-entry-experiment.patch' }
if ($NmiTrigger) { $patches += 'snesrecomp-nmi-trigger-experiment.patch' }
if ($RefreshShort) { $patches += 'snesrecomp-refresh-short-experiment.patch' }
if ($LiveWram) { $patches += 'snesrecomp-live-wram-experiment.patch' }
if ($HdmaCount) { $patches += 'snesrecomp-hdma-count-experiment.patch' }
if ($BeamHdma) { $patches += 'snesrecomp-beam-hdma-experiment.patch' }
if ($HdmaInterrupt) { $patches += 'snesrecomp-hdma-interrupt-experiment.patch' }
if ($LateNmi) { $patches += 'snesrecomp-late-nmi-experiment.patch' }
if ($IrqEntry) { $patches += 'snesrecomp-irq-entry-experiment.patch' }
if ($EmuStack) { $patches += 'snesrecomp-emu-stack-experiment.patch' }
if ($CpuIoBus) { $patches += 'snesrecomp-cpu-io-openbus-experiment.patch' }
if ($NestedNmi) { $patches += 'snesrecomp-nested-nmi-experiment.patch' }
if ($WaiWake) { $patches += 'snesrecomp-wai-wake-experiment.patch' }
if ($PpuLatchPhase) { $patches += 'snesrecomp-ppu-latch-phase-experiment.patch' }
if ($HvbjoyPhase) { $patches += 'snesrecomp-hvbjoy-phase-experiment.patch' }
if ($ResetPhase) { $patches += 'snesrecomp-reset-phase-experiment.patch' }
if ($WaiCycle) { $patches += 'snesrecomp-wai-cycle-experiment.patch' }
if ($Ppu2Bus) { $patches += 'snesrecomp-ppu2-openbus-experiment.patch' }
if ($ApuQueueRead) { $patches += 'snesrecomp-apu-guest-read-experiment.patch' }
if ($SpcC4Phase) { $patches += 'snesrecomp-spc-mov-port-phase-experiment.patch' }
if ($RasterLiveRestore) { $patches += 'snesrecomp-raster-live-restore-experiment.patch' }
if ($ManualJoypad) { $patches += 'snesrecomp-manual-joypad-strobe-experiment.patch' }
if ($AutoJoyPhase) { $patches += 'snesrecomp-autojoy-phase-experiment.patch' }
if ($SpcD4Phase) { $patches += 'snesrecomp-spc-indexed-write-state-experiment.patch' }
foreach ($patchName in $patches) {
    $patch = Join-Path $root "patches/$patchName"
    git -C $framework apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw "Patch does not apply: $patchName" }
    git -C $framework apply $patch
    if ($LASTEXITCODE -ne 0) { throw "Patch application failed: $patchName" }
}
$config = Join-Path $project 'recomp'
New-Item -ItemType Directory -Path $config | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'recomp/bank00.cfg') -Destination $config
$symbols = [IO.File]::ReadAllText((Join-Path $root 'recomp/symbols.toml'))
$pattern = '(?ms)(\[\[func\]\]\s*\r?\nname\s*=\s*"I_RESET".*?\r?\nemit\s*=\s*)false'
if ([regex]::Matches($symbols, $pattern).Count -ne 1) { throw 'Expected one disabled I_RESET symbol.' }
$symbols = [regex]::Replace($symbols, $pattern, '${1}true')
[IO.File]::WriteAllText((Join-Path $config 'symbols.toml'), $symbols, [Text.UTF8Encoding]::new($false))
$oldSimple = [Environment]::GetEnvironmentVariable('SNESRECOMP_EMIT_SIMPLE_BUS_TIMING')
$oldGate = [Environment]::GetEnvironmentVariable('SNESRECOMP_EMIT_AOT_DENY_GATE')
try {
    $env:SNESRECOMP_EMIT_SIMPLE_BUS_TIMING = '1'
    $env:SNESRECOMP_EMIT_AOT_DENY_GATE = '1'
    python (Join-Path $framework 'snesrecomp_cli.py') generate --rom $romPath `
        --project-root $project --cfg-dir recomp --out-dir generated --funcs-h recomp/funcs.h `
        --expected-crc32 df8153d9 --expected-sha256 $expected
    if ($LASTEXITCODE -ne 0) { throw 'Experimental generation failed.' }
} finally {
    [Environment]::SetEnvironmentVariable('SNESRECOMP_EMIT_SIMPLE_BUS_TIMING', $oldSimple)
    [Environment]::SetEnvironmentVariable('SNESRECOMP_EMIT_AOT_DENY_GATE', $oldGate)
}
$cmakeArgs = @('-S', $root, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    "-DSNESRECOMP_ROOT=$framework", "-DKDC_GENERATED_DIR=$(Join-Path $project 'generated')",
    "-DKDC_FUNCTIONS_DIR=$(Join-Path $project 'recomp')")
# Prefer the resource compiler paired with the selected GCC over an unrelated
# MSYS windres earlier on PATH, which may invoke an incompatible shell.
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($gcc) {
    $windres = Join-Path (Split-Path -Parent $gcc.Source) 'windres.exe'
    if (Test-Path -LiteralPath $windres) { $cmakeArgs += "-DCMAKE_RC_COMPILER=$($windres.Replace('\', '/'))" }
}
if ($SdlSource) { $cmakeArgs += "-DFETCHCONTENT_SOURCE_DIR_SDL3=$SdlSource" }
if ($CurrentStack) { $cmakeArgs += "-DKDC_EXPERIMENTAL_STATE=ON" }
if ($RasterState) { $cmakeArgs += "-DKDC_EXPERIMENTAL_RASTER_STATE=ON" }
if ($FieldTiming) { $cmakeArgs += "-DKDC_EXPERIMENTAL_FIELD_STATE=ON" }
if ($IncrementalDsp) { $cmakeArgs += "-DKDC_EXPERIMENTAL_DSP_STATE=ON" }
if ($BeamHdma) { $cmakeArgs += "-DKDC_EXPERIMENTAL_BEAM_HDMA_STATE=ON" }
if ($LateNmi) { $cmakeArgs += "-DKDC_EXPERIMENTAL_LATE_NMI_STATE=ON" }
if ($Ppu2Bus) { $cmakeArgs += "-DKDC_EXPERIMENTAL_PPU2_BUS_STATE=ON" }
if ($SpcC4Phase) { $cmakeArgs += "-DKDC_EXPERIMENTAL_SPC_C4_STATE=ON" }
if ($AutoJoyPhase) { $cmakeArgs += "-DKDC_EXPERIMENTAL_AUTOJOY_STATE=ON" }
if ($SpcD4Phase) { $cmakeArgs += "-DKDC_EXPERIMENTAL_SPC_D4_STATE=ON" }
& $cmakePath @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw 'Experimental CMake configuration failed.' }
& $cmakePath --build $build --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Experimental build failed.' }
Write-Host "Built $build/KirbyDreamCourseRecomp.exe. This experimental build requires the documented runtime switches."
