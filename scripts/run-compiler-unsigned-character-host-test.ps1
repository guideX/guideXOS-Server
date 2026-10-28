param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot 'out/comp4i-validation'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$suites = @(
    @{ Name = 'byte-storage-and-arrays'; Script = 'run-compiler-character-bytes-host-test.ps1' },
    @{ Name = 'function-abi-and-objects'; Script = 'run-compiler-character-functions-host-test.ps1' },
    @{ Name = 'structure-fields-and-layout'; Script = 'run-compiler-struct-char-fields-host-test.ps1' }
)

foreach ($suite in $suites) {
    $suiteOutput = Join-Path $OutputDirectory $suite.Name
    & (Join-Path $PSScriptRoot $suite.Script) -OutputDirectory $suiteOutput
    if ($LASTEXITCODE -ne 0) {
        throw "COMP4I production compiler suite failed: $($suite.Name)"
    }
}

Write-Host 'COMP4I unsigned character hosted suite: PASS'
