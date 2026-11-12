# Shared helper for running native commands from the PowerShell tools.
#
# Windows PowerShell 5.1 turns a native command's standard error into an
# error record as soon as the error stream is redirected, and a script that
# runs with $ErrorActionPreference = 'Stop' then aborts on ordinary progress
# output. The build tools report progress on standard error, so every native
# call in tools/ goes through Invoke-NativeCommand: the error stream is folded
# into the success stream inside the call, the text is kept as plain strings,
# and the exit status is reported separately.
#
# Dot-source this file:
#   . (Join-Path $PSScriptRoot 'invoke_native.ps1')
#
# Invoke-NativeCommand returns a description of the run and never writes to
# the caller's pipeline, so assigning its result is safe. It echoes the
# command output to the host unless -Quiet is given.

function Invoke-NativeCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [ref]$ExitCode,
        [switch]$Quiet
    )
    if ([IO.Path]::IsPathRooted($FilePath)) {
        if (-not (Test-Path -LiteralPath $FilePath)) {
            throw "Cannot run '$FilePath'; the file does not exist."
        }
    } elseif (-not (Get-Command $FilePath -ErrorAction SilentlyContinue)) {
        throw "Cannot run '$FilePath'; it was not found on PATH."
    }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $stdout = New-Object System.Collections.Generic.List[string]
    $stderr = New-Object System.Collections.Generic.List[string]
    $code = 0
    try {
        & $FilePath @ArgumentList 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $text = $_.Exception.Message
                $stderr.Add($text)
            } else {
                $text = [string]$_
                $stdout.Add($text)
            }
            if (-not $Quiet) { Write-Host $text }
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($ExitCode) { $ExitCode.Value = $code }
    return [pscustomobject]@{
        ExitCode = $code
        Output   = $stdout.ToArray()
        Error    = $stderr.ToArray()
    }
}
