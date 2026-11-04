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
$prepare = Join-Path $PSScriptRoot 'prepare_native_experiment.ps1'
$options = @{
    Rom = $Rom
    CMake = $CMake
    SdlSource = $SdlSource
    Name = $Name
    BuildVersion = $BuildVersion
    ReleaseDefaults = [bool]$ReleaseDefaults
    CurrentStack = $true
    RasterState = $true
    FieldTiming = $true
    RdnmiBeam = $true
    InstructionBoundaries = $true
    IncrementalDsp = $true
    SharedNoise = $true
    NmiEntry = $true
    NmiTrigger = $true
    RefreshShort = $true
    LiveWram = $true
    HdmaCount = $true
    BeamHdma = $true
    HdmaInterrupt = $true
    LateNmi = $true
    IrqEntry = $true
    EmuStack = $true
    CpuIoBus = $true
    NestedNmi = $true
    WaiWake = $true
    PpuLatchPhase = $true
    HvbjoyPhase = $true
    ResetPhase = $true
    WaiCycle = $true
    Ppu2Bus = $true
    ApuQueueRead = $true
    RasterLiveRestore = $true
    ManualJoypad = $true
    AutoJoyPhase = $true
    SpcD4Phase = $true
    ApuBoundarySync = [bool]$ApuBoundarySync
}
& $prepare @options
