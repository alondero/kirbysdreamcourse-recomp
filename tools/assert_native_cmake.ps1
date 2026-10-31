param([Parameter(Mandatory=$true)][string]$CMake)

# MSYS CMake interprets Windows drive paths as POSIX paths and cannot drive
# this build's Windows Ninja and MinGW toolchain. Fail before generating files.
$capabilities = & $CMake -E capabilities
if ($LASTEXITCODE -ne 0) { throw 'Could not query CMake capabilities.' }
try { $generators = ($capabilities | ConvertFrom-Json).generators.name }
catch { throw 'Could not parse CMake capabilities.' }
if ('MinGW Makefiles' -notin $generators) {
    throw 'A native Windows CMake is required; specify its path with -CMake.'
}
