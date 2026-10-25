$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r11-runner-predicates.ps1')
$fixture = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures/c188r11/state-provenance.serial.txt') -Raw
$passed = 0

if (-not (Test-C188R11NativeLaunchBoundary $fixture)) { throw 'Native launch boundary was rejected.' }
$passed++
if (-not (Test-C188R11NativeInputBoundary $fixture '00000000')) { throw 'Native input boundary was rejected.' }
$passed++
if ((Get-C188R11NativeInputResult $fixture) -cne '00000000') { throw 'Native input result was not extracted.' }
$passed++
if (-not (Test-C188R11ImageContextMatch $fixture)) { throw 'Matching NativeAOT image context was rejected.' }
$passed++
if (-not (Test-C188R11ManagedLaunchState $fixture)) { throw 'Successful managed-return snapshot was rejected.' }
$passed++
if (-not (Test-C188R11ManagedInputState $fixture 5 1 '-1')) { throw 'Populated input-dispatch snapshot was rejected.' }
$passed++
if (-not (Test-C188R11TryFindResult $fixture 0)) { throw 'Successful TryFind result was rejected.' }
$passed++
if (-not (Test-C188R11InitializationEvidence $fixture)) { throw 'Managed and native initialization evidence was rejected.' }
$passed++
if (-not (Test-C188R11ManagedSequenceOrder $fixture)) { throw 'Managed sequence order was rejected.' }
$passed++
if (-not (Test-C188R11ManagedBoundaryCorrelation $fixture)) { throw 'Managed/native boundary correlation was rejected.' }
$passed++
if (-not (Test-C188R11NotesReady $fixture)) { throw 'Notes launch, surface, or main-loop readiness was rejected.' }
$passed++
if (-not [string]::IsNullOrEmpty((Get-C188R11ReadinessFailureReason $fixture))) { throw 'Complete readiness fixture returned a failure reason.' }
$passed++

$missingManagedReturn = $fixture -replace '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=15 st=managed-return.*\r?\n',''
if ((Get-C188R11ReadinessFailureReason $missingManagedReturn) -cne 'managed-return-state-missing-or-invalid') { throw 'Missing managed-return did not produce its explicit failure reason.' }
$passed++
$staleLaunchReturn = $fixture.Replace('st=managed-return selector=4', 'st=launch-return selector=4')
if ((Get-C188R11ReadinessFailureReason $staleLaunchReturn) -cne 'managed-return-state-missing-or-invalid') { throw 'Stale launch-return stage was accepted.' }
$passed++
$badResidency = $fixture.Replace('st=managed-return selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1', 'st=managed-return selector=4 ownerGen=0 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=0')
if ((Get-C188R11ReadinessFailureReason $badResidency) -cne 'managed-return-state-missing-or-invalid') { throw 'Incorrect Notes resident state passed the readiness gate.' }
$passed++
$missingMainLoop = $fixture -replace '(?m)^\[KERNEL\] Entering main loop.*\r?\n',''
if ((Get-C188R11ReadinessFailureReason $missingMainLoop) -cne 'main-loop-not-reached') { throw 'Missing main-loop marker did not stop readiness.' }
$passed++
$missingNotesLaunch = $fixture -replace '(?m)^\[C150-APP-LAUNCH\].*\r?\n',''
if ((Get-C188R11ReadinessFailureReason $missingNotesLaunch) -cne 'notes-launch-marker-missing') { throw 'Missing Notes launch marker did not stop readiness.' }
$passed++
$missingNotesSurface = $fixture -replace '(?m)^\[C150-SURFACE\].*\r?\n',''
if ((Get-C188R11ReadinessFailureReason $missingNotesSurface) -cne 'notes-surface-not-created') { throw 'Missing Notes surface marker did not stop readiness.' }
$passed++
$missingInitialization = $fixture -replace '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-INIT.*\r?\n',''
if ((Get-C188R11ReadinessFailureReason $missingInitialization) -cne 'initialization-epoch-evidence-missing') { throw 'Missing initialization evidence did not stop readiness.' }
$passed++
$missingNativeLaunch = $fixture -replace '(?m)^\[C188R11-NATIVE\] nseq=00000006 stage=after-managed-return route=resident-launch.*\r?\n',''
if ((Get-C188R11ReadinessFailureReason $missingNativeLaunch) -cne 'native-launch-boundary-missing-or-invalid') { throw 'Missing native launch boundary did not stop readiness.' }
$passed++
if (Test-C188R11ManagedInputState $fixture 0 0 '-1') { throw 'Populated dispatch state was misclassified as empty.' }
$passed++
if (Test-C188R11ManagedInputState $fixture 5 0 '-1') { throw 'Wrong resident mask was accepted for input dispatch.' }
$passed++

$changedImage = $fixture.Replace('route=input call=00000002 corr=00000002 id=4 gen=00000001 input=00000001 module=00000001', 'route=input call=00000002 corr=00000002 id=4 gen=00000001 input=00000001 module=00000002')
if (Test-C188R11ImageContextMatch $changedImage) { throw 'Different module identity was accepted as the same image context.' }
$passed++
$inputWithWrongReturn = $fixture.Replace('route=input call=00000002 corr=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=00000000', 'route=input call=00000002 corr=00000002 id=4 gen=00000001 input=00000001 module=00000001 entryOff=0000000000001000 segments=00000007 span=000000000004BE80 rc=FFFFFFFC')
if (-not (Test-C188R11NativeInputBoundary $inputWithWrongReturn 'FFFFFFFC')) { throw 'Nonzero native input result was not retained as observable evidence.' }
$passed++
$missingInputEntry = $fixture -replace '(?m)^\[C188R11-NATIVE\] nseq=00000007 stage=before-managed-entry route=input.*\r?\n',''
if (Test-C188R11NativeInputBoundary $missingInputEntry) { throw 'Missing first-input native entry was accepted.' }
$passed++
$nonMonotonic = $fixture.Replace('C188R11 state seq=19 st=before-tryfind', 'C188R11 state seq=18 st=before-tryfind')
if (Test-C188R11ManagedSequenceOrder $nonMonotonic) { throw 'Non-monotonic managed sequence values were accepted.' }
$passed++

Write-Output "C188R11-RUNNER-PREDICATES cases=$passed result=PASS"
