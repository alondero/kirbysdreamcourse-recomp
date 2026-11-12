param(
    [switch]$SimpleBus,
    [switch]$Refresh,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name = 'snesrecomp-handoff'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'invoke_native.ps1')
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'local/snesrecomp'
$destination = Join-Path (Join-Path $root 'local') $Name
$patch = Join-Path $root 'patches/snesrecomp-apu-handoff.patch'
$pin = '147b55e252dcb3e7680a99c7c2f50e7183891844'
if (-not (Test-Path (Join-Path $source '.git'))) { throw 'Pinned source checkout is missing; see docs/framework-spike.md.' }
$head = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $source, 'rev-parse', 'HEAD') -Quiet
if ($head.ExitCode -ne 0 -or ($head.Output | Select-Object -Last 1) -ne $pin) {
    throw 'Source checkout does not match the framework pin.'
}
if (Test-Path -LiteralPath $destination) { throw "$destination already exists; choose a different -Name." }
$clone = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('clone', '-c', 'core.autocrlf=false',
    '--quiet', '--no-hardlinks', '--no-checkout', $source, $destination)
if ($clone.ExitCode -ne 0) { throw 'Framework clone failed.' }
$checkout = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'checkout', '--quiet', '--detach', $pin)
if ($checkout.ExitCode -ne 0) { throw 'Pinned checkout failed.' }
$check = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'apply', '--check', $patch) -Quiet
if ($check.ExitCode -ne 0) { throw 'Patch does not apply to the pinned framework.' }
$apply = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'apply', $patch)
if ($apply.ExitCode -ne 0) { throw 'Patch application failed.' }
if ($SimpleBus) {
    $busPatch = Join-Path $root 'patches/snesrecomp-simple-bus-timing.patch'
    $check = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'apply', '--check', $busPatch) -Quiet
    if ($check.ExitCode -ne 0) { throw 'Simple bus timing patch does not apply.' }
    $apply = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'apply', $busPatch)
    if ($apply.ExitCode -ne 0) { throw 'Simple bus timing patch application failed.' }
}
if ($Refresh) {
    $refreshPatch = Join-Path $root 'patches/snesrecomp-refresh-clock-experiment.patch'
    $check = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'apply', '--check', $refreshPatch) -Quiet
    if ($check.ExitCode -ne 0) { throw 'Refresh experiment patch does not apply.' }
    $apply = Invoke-NativeCommand -FilePath 'git' -ArgumentList @('-C', $destination, 'apply', $refreshPatch)
    if ($apply.ExitCode -ne 0) { throw 'Refresh experiment patch application failed.' }
}
Write-Host 'Prepared experimental checkout. Set SNESRECOMP_APU_HANDOFF=1 only for controlled tests.'
if ($SimpleBus) { Write-Host 'Simple bus timing is partial and requires SNESRECOMP_EMIT_SIMPLE_BUS_TIMING=1 when generating.' }

if ($Refresh) { Write-Host 'Refresh timing is experimental and remains unqualified.' }
