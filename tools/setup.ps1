param(
    [string]$Rom = "Kirby's Dream Course (USA).sfc",
    [string]$CMake = '',
    [string]$SdlSource = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($Rom)) { $Rom = Join-Path $root $Rom }
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$expected = '0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b'
if ((Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'ROM SHA-256 mismatch. Use the supported headerless USA ROM.'
}
foreach ($command in @('git', 'python', 'ninja', 'gcc')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Missing $command on PATH; see README.md." }
}
python -c "import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)"
if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 or newer is required.' }
if (-not $CMake) {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if (-not $command) { throw 'Missing CMake; install it or specify -CMake.' }
    $CMake = $command.Source
}
$CMake = (Resolve-Path -LiteralPath $CMake).Path
$framework = Join-Path $root 'local/snesrecomp'
$pin = '147b55e252dcb3e7680a99c7c2f50e7183891844'
New-Item -ItemType Directory -Path (Join-Path $root 'local') -Force | Out-Null
if (-not (Test-Path -LiteralPath $framework)) {
    git clone -c core.autocrlf=false --no-checkout https://github.com/RetroPortingToolKit/snesrecomp.git $framework
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone SNESRecomp.' }
    git -C $framework checkout --detach $pin
    if ($LASTEXITCODE -ne 0) { throw 'Could not check out the pinned SNESRecomp revision.' }
}
if (-not (Test-Path (Join-Path $framework '.git'))) { throw 'local/snesrecomp is not a Git checkout.' }
$head = git -C $framework rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $pin) { throw 'local/snesrecomp is not at the required revision.' }
if (git -C $framework status --porcelain) { throw 'local/snesrecomp has local changes; use a clean pinned checkout.' }
$options = @{ Rom = $romPath; CMake = $CMake; Name = 'guarded' }
if ($SdlSource) { $options.SdlSource = $SdlSource }
& (Join-Path $PSScriptRoot 'prepare_guarded.ps1') @options
Write-Host 'Build complete. Run: ./tools/play_experimental.ps1'
