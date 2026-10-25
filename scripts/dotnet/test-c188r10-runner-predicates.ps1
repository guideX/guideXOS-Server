$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r10-runner-predicates.ps1')
$fixtureRoot = Join-Path $PSScriptRoot 'fixtures/c188r10'

$fixtures = @(
    [pscustomobject]@{Name='lf-decimal';Path='notes-ready-lf-decimal.serial.txt';Expected=$true},
    [pscustomobject]@{Name='crlf-decimal';Path='notes-ready-crlf-decimal.serial.txt';Expected=$true},
    [pscustomobject]@{Name='lf-padded';Path='notes-ready-lf-padded.serial.txt';Expected=$true},
    [pscustomobject]@{Name='crlf-padded';Path='notes-ready-crlf-padded.serial.txt';Expected=$true}
)
$passed = 0
foreach ($fixture in $fixtures) {
    $serial = Get-Content -LiteralPath (Join-Path $fixtureRoot $fixture.Path) -Raw
    $actual = Test-C188R10NotesReady $serial
    if ($actual -ne $fixture.Expected) {
        throw "C188R10 runner predicate fixture '$($fixture.Name)' expected $($fixture.Expected), got $actual."
    }
    $passed++
}
$lfReady = Get-Content -LiteralPath (Join-Path $fixtureRoot 'notes-ready-lf-decimal.serial.txt') -Raw
foreach ($negative in @(
    [pscustomobject]@{Name='wrong-generation';Serial=$lfReady.Replace('generation=1','generation=2')},
    [pscustomobject]@{Name='wrong-selector';Serial=$lfReady.Replace('selector=4 kind=normal','selector=5 kind=normal')}
)) {
    if (Test-C188R10NotesReady $negative.Serial) {
        throw "C188R10 runner accepted negative saved-fixture mutation '$($negative.Name)'."
    }
    $passed++
}

$inputSerial = Get-Content -LiteralPath (Join-Path $fixtureRoot 'input-reentry-crlf-padded.serial.txt') -Raw
if (-not (Test-C188R10InputManagedEntry $inputSerial) -or
    -not (Test-C188R10InputRegistrySnapshot $inputSerial) -or
    -not (Test-C188R10InputHandler $inputSerial) -or
    -not (Test-C188R10InputReturn $inputSerial)) {
    throw 'C188R10 input evidence predicates rejected the expected input/dispatch/handler/return fixtures.'
}
if (Test-C188R10InputHandler ($inputSerial -replace 'valid=1','valid=0')) {
    throw 'C188R10 handler predicate accepted an invalid lifetime-generation fixture.'
}
$launchSerial = Get-Content -LiteralPath (Join-Path $fixtureRoot 'launch-return-after-probes.serial.txt') -Raw
if (-not (Test-C188R10ManagedLaunchExit $launchSerial) -or
    -not (Test-C188R10NativeLaunchReturn $launchSerial)) {
    throw 'C188R10 launch-return predicates rejected invocation numbering after earlier selector probes.'
}

$passed += 7
Write-Output "C188R10-RUNNER-PREDICATES fixtures=$passed result=PASS"
