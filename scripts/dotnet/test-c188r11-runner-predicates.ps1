$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r11-runner-predicates.ps1')
$fixture = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures/c188r11/state-provenance.serial.txt') -Raw
$passed = 0
if (-not (Test-C188R11NativeLaunchBoundary $fixture)) { throw 'Native launch marker fixture was rejected.' }
$passed++
if (-not (Test-C188R11NativeInputBoundary $fixture)) { throw 'Native input marker fixture was rejected.' }
$passed++
if ((Get-C188R11NativeInputResult $fixture) -cne 'FFFFFFFC') { throw 'Native input return result was not extracted.' }
$passed++
if (-not (Test-C188R11ImageContextMatch $fixture)) { throw 'Matching image context fixture was rejected.' }
$passed++
if (-not (Test-C188R11ManagedLaunchState $fixture)) { throw 'Managed launch state fixture was rejected.' }
$passed++
if (-not (Test-C188R11ManagedInputState $fixture 0 0 '-1')) { throw 'Empty managed input-state fixture was rejected.' }
$passed++
if (-not (Test-C188R11TryFindResult $fixture -4)) { throw 'Rejected TryFind result was not recognized.' }
$passed++
if (-not (Test-C188R11InitializationEvidence $fixture)) { throw 'Independent initialization evidence fixture was rejected.' }
$passed++
$changedImage = $fixture.Replace('route=input call=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=FFFFFFFF', 'route=input call=00000002 id=4 gen=00000001 input=00000001 module=00000002 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=FFFFFFFF')
if (Test-C188R11ImageContextMatch $changedImage) { throw 'Different module identity was accepted as the same image context.' }
$passed++
$successfulInput = $fixture.Replace('route=input call=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=FFFFFFFC', 'route=input call=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=00000000')
if (-not (Test-C188R11NativeInputBoundary $successfulInput '00000000')) { throw 'Successful native input result was not accepted.' }
$passed++
$otherInputResult = $fixture.Replace('route=input call=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=FFFFFFFC', 'route=input call=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=FFFFFFFE')
if (-not (Test-C188R11NativeInputBoundary $otherInputResult 'FFFFFFFE')) { throw 'Other native input return codes must remain observable.' }
$passed++
$missingLaunch = $fixture -replace '(?m)^\[C188R11-NATIVE\] stage=after-managed-return route=resident-launch.*\r?\n',''
$failedExplicitly = $false
try { Assert-C188R11NativeLaunchBoundary $missingLaunch }
catch { $failedExplicitly = $_.Exception.Message -like '*native-launch-boundary-missing-or-invalid*' }
if (-not $failedExplicitly) { throw 'Missing native launch marker did not cause an explicit diagnostic failure.' }
$passed++
Write-Output "C188R11-RUNNER-PREDICATES cases=$passed result=PASS"
