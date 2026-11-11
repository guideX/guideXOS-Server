$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compiler) {
    throw "g++ is required to run the deterministic clock-assurance contract test."
}

$testDir = Join-Path $env:TEMP ("guidex-clock-assurance-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testDir | Out-Null
try {
    $testExe = Join-Path $testDir "clock_assurance_test.exe"
    & $compiler.Source -std=c++17 -Wall -Wextra -Werror -I $root `
        (Join-Path $root "tests\clock_assurance_test.cpp") -o $testExe
    if ($LASTEXITCODE -ne 0) { throw "Clock-assurance test compilation failed with exit code $LASTEXITCODE." }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "Clock-assurance contract assertions failed with exit code $LASTEXITCODE." }
    Write-Output "CLOCK_ASSURANCE_RESULT: PASS"
    Write-Output "Assertions: plausible/invalid RTC, rollback, forward jump, source failure, authenticated evidence expiry/freshness/metadata, reboot without continuity, expired/future/valid certificate dates, and insufficient public TLS assurance."
}
finally {
    Remove-Item -LiteralPath $testDir -Recurse -Force -ErrorAction SilentlyContinue
}
