[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$nativeScript = Join-Path $PSScriptRoot 'run-c186r1-nativeaot.ps1'
$powershell = Get-Command powershell.exe -ErrorAction Stop | Select-Object -First 1
$safeOutput = Join-Path $RepoRoot 'out\dotnet\c186r1-artifact-isolation\nativeaot'
$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$validOutput = @(& $powershell.Source -NoProfile -ExecutionPolicy Bypass -File $nativeScript `
    -RepoRoot $RepoRoot -OutputRoot $safeOutput -PreflightOnly 2>&1)
$validExitCode = $LASTEXITCODE
$ErrorActionPreference = $savedErrorActionPreference
if ($validExitCode -ne 0 -or (($validOutput | Out-String) -notmatch 'result=PASS')) {
    throw "The dedicated C186R1 output root failed preflight: $($validOutput | Out-String)"
}

$probeId = [guid]::NewGuid().ToString('N')
$unsafeOutputs = @(
    (Join-Path $RepoRoot "out\dotnet\c185-managed-host-services\c186r1-route-probe-$probeId"),
    (Join-Path $RepoRoot "out\dotnet\c182-managed-application-authoring\c186r1-route-probe-$probeId"),
    (Join-Path $RepoRoot "route-probe-$probeId")
)
$rejected = 0
foreach ($unsafeOutput in $unsafeOutputs) {
    if (Test-Path -LiteralPath $unsafeOutput) {
        throw "Negative-test probe path unexpectedly exists: $unsafeOutput"
    }
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $rejection = @(& $powershell.Source -NoProfile -ExecutionPolicy Bypass -File $nativeScript `
        -RepoRoot $RepoRoot -OutputRoot $unsafeOutput -PreflightOnly 2>&1)
    $rejectionExitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorActionPreference
    if ($rejectionExitCode -eq 0) {
        throw "Unsafe C186R1 output path was accepted: $unsafeOutput"
    }
    if (Test-Path -LiteralPath $unsafeOutput) {
        throw "Rejected output-path preflight created a path: $unsafeOutput"
    }
    $rejected++
}

Write-Output "C186R1-OUTPUT-ROUTING safeRoot=PASS unsafeRootsRejected=$rejected createdPaths=0 result=PASS"
