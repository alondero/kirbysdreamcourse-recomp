param([Parameter(Mandatory=$true)][string]$CMake)

# MSYS CMake interprets Windows drive paths as POSIX paths and cannot drive
# this build's Windows Ninja and MinGW toolchain. Fail before generating files.
. (Join-Path $PSScriptRoot 'find_native_cmake.ps1')
$generators = @(Get-NativeCMakeGenerators -Path $CMake)
if ('MinGW Makefiles' -notin $generators) {
    throw 'A native Windows CMake is required; specify its path with -CMake.'
}
