# Locate a native Windows CMake.
#
# This project needs a native build of CMake. The MSYS build that ships with
# devkitPro can shadow the native one on PATH, and it cannot drive this
# project's Windows Ninja and MinGW toolchain: it interprets Windows drive
# paths as POSIX paths and offers Unix Makefiles instead of MinGW Makefiles.
#
# Candidates are therefore validated, not assumed. Each one is asked for its
# generator list and the first candidate that offers "MinGW Makefiles" wins.
# The candidates are tried in the order a developer would expect: the CMake
# already on PATH, then the CMake that pip installs, then the standard
# Windows install locations.
#
# Dot-source this file:
#   . (Join-Path $PSScriptRoot 'find_native_cmake.ps1')

. (Join-Path $PSScriptRoot 'invoke_native.ps1')

function Get-NativeCMakeGenerators {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)
    $result = Invoke-NativeCommand -FilePath $Path -ArgumentList @('-E', 'capabilities') -Quiet
    if ($result.ExitCode -ne 0) { throw "Could not query CMake capabilities: $Path" }
    try { $capabilities = ($result.Output -join "`n") | ConvertFrom-Json }
    catch { throw "Could not parse CMake capabilities: $Path" }
    return @($capabilities.generators.name)
}

function Test-NativeCMake {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Path)
    try { $generators = @(Get-NativeCMakeGenerators -Path $Path) }
    catch { return $false }
    return ('MinGW Makefiles' -in $generators)
}

function Resolve-RealCMakeBinary {
    # pip installs a small console script named cmake.exe next to the
    # interpreter. The real binary lives in the package's data directory and
    # is the one CMake records as CMAKE_COMMAND when it re-invokes itself.
    [CmdletBinding()]
    param([string]$Path)
    if (-not $Path) { return '' }
    $directory = Split-Path -Parent $Path
    if ((Split-Path -Leaf $directory) -ne 'Scripts') { return $Path }
    $real = Join-Path (Split-Path -Parent $directory) 'Lib/site-packages/cmake/data/bin/cmake.exe'
    if (Test-Path -LiteralPath $real) { return $real }
    return $Path
}

function Get-NativeCMakeCandidates {
    [CmdletBinding()]
    param()
    $candidates = New-Object System.Collections.Generic.List[string]
    function Add-Candidate([string]$Value) {
        if (-not $Value) { return }
        if (-not $candidates.Contains($Value)) { $candidates.Add($Value) }
    }
    foreach ($command in @(Get-Command cmake -All -ErrorAction SilentlyContinue)) {
        if ($command.Source) { Add-Candidate (Resolve-RealCMakeBinary $command.Source) }
    }
    $probe = "import os,sys,cmake; n='cmake.exe' if os.name=='nt' else 'cmake'; " +
        "p=os.path.join(os.path.dirname(cmake.__file__),'data','bin',n); " +
        "print(p if os.path.isfile(p) else '')"
    foreach ($python in @(Get-Command python -All -ErrorAction SilentlyContinue)) {
        if (-not $python.Source) { continue }
        $result = Invoke-NativeCommand -FilePath $python.Source -ArgumentList @('-c', $probe) -Quiet
        if ($result.ExitCode -ne 0) { continue }
        foreach ($line in $result.Output) {
            if ($line.Trim()) { Add-Candidate $line.Trim() }
        }
    }
    $locations = @()
    if ($env:ProgramFiles) { $locations += (Join-Path $env:ProgramFiles 'CMake/bin/cmake.exe') }
    if (${env:ProgramFiles(x86)}) { $locations += (Join-Path ${env:ProgramFiles(x86)} 'CMake/bin/cmake.exe') }
    if ($env:LOCALAPPDATA) {
        $locations += (Join-Path $env:LOCALAPPDATA 'Programs/CMake/bin/cmake.exe')
        $locations += (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Links/cmake.exe')
    }
    $locations += 'C:/ProgramData/chocolatey/bin/cmake.exe'
    if ($env:ProgramFiles) {
        $locations += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
    }
    foreach ($location in $locations) {
        foreach ($match in @(Get-Item -Path $location -ErrorAction SilentlyContinue)) {
            Add-Candidate $match.FullName
        }
    }
    return $candidates.ToArray()
}

function Find-NativeCMake {
    [CmdletBinding()]
    param([string]$CMake = '')
    if ($CMake) {
        $path = (Resolve-Path -LiteralPath $CMake).Path
        if (-not (Test-NativeCMake -Path $path)) {
            throw "The CMake at $path is not a native Windows build; specify a different -CMake."
        }
        return $path
    }
    $rejected = @()
    foreach ($candidate in (Get-NativeCMakeCandidates)) {
        if (Test-NativeCMake -Path $candidate) { return $candidate }
        $rejected += $candidate
    }
    $detail = if ($rejected.Count) { " Rejected: $($rejected -join ', ')." } else { '' }
    throw "No native Windows CMake was found. Install CMake for Windows or pass -CMake <path>.$detail"
}
