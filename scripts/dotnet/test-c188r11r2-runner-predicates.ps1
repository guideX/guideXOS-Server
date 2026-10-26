$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R2 runner predicate tests require PowerShell Core.'
}
. (Join-Path $PSScriptRoot 'c188r11r2-runner-predicates.ps1')
$script:caseCount = 0

function Assert-Predicate([string]$Name, [bool]$Condition) {
    if (-not $Condition) { throw "C188R11R2 runner predicate failed: $Name" }
    $script:caseCount++
}

$success = @'
[C188R11R2_KEY] stage=main-loop-focus window=00000004 title=Managed Notes owner=ManagedAppWindow result=focused
[C188R11R2_KEY] stage=irq-scan seq=00000001 eid=00000001 sc=0000001C ext=0 phase=key-queued key=00000061 transition=down shift=0 ctrl=0 route=queue
[C188R11R2_KEY] stage=kernel-dequeue source=ps2-irq eid=00000001 sc=0000001C key=00000061 class=printable shift=0 ctrl=0 overwritten=00000000 result=PASS
[C188R11R2_KEY] stage=focus-target eid=00000001 window=00000004 title=Managed Notes owner=ManagedAppWindow route=focused-window
[C188R11R2_KEY] stage=native-managed-entry eid=00000001 call=00000002 corr=00000002 selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=FFFFFFFF
[C102-MANAGED-OUTPUT] C188R11-MANAGED stage=entry seq=17 call=2 corr=2 id=4 gen=1 in=1 rc=-1
[C102-MANAGED-OUTPUT] C188R11 state seq=18 st=input-dispatch-entry selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=-1 gc=0 w=1:1
[C102-MANAGED-OUTPUT] C188R11R2_KEY seq=19 stage=managed-dispatch-entry selector=4 kind=key-char value=97 shift=0 ctrl=0 result=-1
[C102-MANAGED-OUTPUT] C188R11 state seq=20 st=before-tryfind selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=-1 gc=0 w=1:1
[C102-MANAGED-OUTPUT] C188R11 state seq=21 st=lookup selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=0 gc=0 w=1:1
[C102-MANAGED-OUTPUT] C188R11 state seq=22 st=resident-found selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=0 gc=0 w=1:1
[C102-MANAGED-OUTPUT] C188R11R2_KEY seq=23 stage=notes-callback-entry selector=4 kind=key-char value=97 shift=0 ctrl=0 result=-1
[C102-MANAGED-OUTPUT] C188R11R2_KEY seq=24 stage=managed-callback-return selector=4 kind=key-char value=97 shift=0 ctrl=0 result=0
[C188R11R2_KEY] stage=native-managed-return eid=00000001 call=00000002 corr=00000002 selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=00000000
'@

$evidence = Get-C188R11R2KeyboardEvidence $success
Assert-Predicate 'complete ordinary-key path qualifies through Notes callback' ([bool]$evidence.traceComplete)
Assert-Predicate 'kernel event id correlates across dequeue, focus, and native entry' (
    $evidence.eventId -eq '00000001' -and $evidence.nativeCall -eq '00000002' -and
    $evidence.managedCall -eq '2')
Assert-Predicate 'managed snapshot captures populated registry and resident Notes' (
    $evidence.registryCount -eq 5 -and $evidence.residentMask -eq 1 -and
    $evidence.registryPopulated -and $evidence.lookupResult -eq 0 -and
    $evidence.managedGcCollections -eq 0 -and $evidence.poolWriteEvents -eq 1 -and
    $evidence.lastPoolWriteSite -eq 1)

$emptyRegistry = [regex]::Replace($success,
    '(?m)^\[C102-MANAGED-OUTPUT\] C188R11R2_KEY seq=\d+ stage=notes-callback-entry[^\r\n]*\r?\n', '')
$emptyRegistry = [regex]::Replace($emptyRegistry,
    '(?m)^\[C102-MANAGED-OUTPUT\] C188R11R2_KEY seq=\d+ stage=managed-callback-return[^\r\n]*\r?\n', '')
$emptyRegistry = $emptyRegistry.Replace(
    'm=1 g=1 td=0 n=5 result=-1', 'm=0 g=1 td=0 n=0 result=-1').Replace(
    'm=1 g=1 td=0 n=5 result=0', 'm=0 g=1 td=0 n=0 result=-4')
$emptyRegistry = $emptyRegistry.Replace(
    'stage=native-managed-return eid=00000001 call=00000002 corr=00000002 selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=00000000',
    'stage=native-managed-return eid=00000001 call=00000002 corr=00000002 selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=FFFFFFFC')
$emptyEvidence = Get-C188R11R2KeyboardEvidence $emptyRegistry
Assert-Predicate 'empty registry remains a qualified dispatch observation' (
    [bool]$emptyEvidence.managedStateQualified -and
    -not $emptyEvidence.traceComplete -and $emptyEvidence.registryCount -eq 0 -and
    $emptyEvidence.residentMask -eq 0 -and -not $emptyEvidence.registryPopulated -and
    $emptyEvidence.lookupResult -eq -4)

$badEventId = $success.Replace(
    'stage=native-managed-entry eid=00000001',
    'stage=native-managed-entry eid=00000002')
Assert-Predicate 'mismatched native event id is rejected' (
    -not (Test-C188R11R2KeyboardTrace $badEventId))
$missingFocus = $success.Replace(
    '[C188R11R2_KEY] stage=focus-target eid=00000001 window=00000004 title=Managed Notes owner=ManagedAppWindow route=focused-window' + "`n", '')
Assert-Predicate 'missing active Notes target is rejected' (
    -not (Test-C188R11R2KeyboardTrace $missingFocus))
$missingCallback = $success.Replace(
    '[C102-MANAGED-OUTPUT] C188R11R2_KEY seq=23 stage=notes-callback-entry selector=4 kind=key-char value=97 shift=0 ctrl=0 result=-1' + "`n", '')
Assert-Predicate 'managed dispatch without Notes callback is not full qualification' (
    -not (Test-C188R11R2KeyboardTrace $missingCallback))
$missingSnapshot = $success.Replace(
    '[C102-MANAGED-OUTPUT] C188R11 state seq=18 st=input-dispatch-entry selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=-1 gc=0 w=1:1' + "`n", '')
Assert-Predicate 'managed entry without the earliest registry snapshot is rejected' (
    -not (Test-C188R11R2KeyboardTrace $missingSnapshot))
$missingProvenance = $success.Replace('result=-1 gc=0 w=1:1', 'result=-1')
Assert-Predicate 'managed input snapshot without GC and writer provenance is rejected' (
    -not (Test-C188R11R2KeyboardTrace $missingProvenance))

Write-Output "C188R11R2-RUNNER-PREDICATES cases=$script:caseCount result=PASS"
