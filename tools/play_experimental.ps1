param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$Executable = 'local/guarded-build/KirbyDreamCourseRecomp.exe',
    [switch]$Fallback,
    [switch]$NoLauncher,
    [string]$DevHole = '',
    [switch]$DevTwoPlayer,
    [switch]$DevHoleList,
    [string]$CoursePack = '',
    [ValidateRange(0, 2147483647)][int]$Frames = 0
)

& (Join-Path $PSScriptRoot 'play.ps1') @PSBoundParameters
exit $LASTEXITCODE
