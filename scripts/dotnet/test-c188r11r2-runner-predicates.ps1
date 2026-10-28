$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R2 runner predicate tests require PowerShell Core.'
}
. (Join-Path $PSScriptRoot 'c188r11r2-runner-predicates.ps1')
. (Join-Path $PSScriptRoot 'c188r11r4r2-runner-predicates.ps1')
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

$callbackReturnAsManagedState = [regex]::Replace($success,
    '(?m)^\[C102-MANAGED-OUTPUT\] C188R11R2_KEY seq=24 stage=managed-callback-return[^\r\n]*\r?\n', '')
$callbackReturnAsManagedState += "`n[C102-MANAGED-OUTPUT] C188R11 state seq=25 st=managed-return selector=4 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=0 gc=0 w=1:1`n"
$callbackReturnAsManagedState += '[C102-MANAGED-OUTPUT] C188R11-MANAGED stage=exit seq=26 call=2 corr=2 id=4 gen=1 in=1 rc=0'
$fallbackEvidence = Get-C188R11R2KeyboardEvidence $callbackReturnAsManagedState
Assert-Predicate 'managed-return state records Notes callback success when the dedicated return marker is absent' (
    [bool]$fallbackEvidence.traceComplete -and $fallbackEvidence.notesCallbackReturn -and
    $fallbackEvidence.notesCallbackReturnResult -eq 0)

$failedManagedReturn = [regex]::Replace($callbackReturnAsManagedState,
    '(?m)(st=managed-return selector=4 .*?result=)-?\d+( gc=)', '$1-4$2')
$failedManagedReturn = $failedManagedReturn.Replace(
    'stage=native-managed-return eid=00000001 call=00000002 corr=00000002 selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=00000000',
    'stage=native-managed-return eid=00000001 call=00000002 corr=00000002 selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=FFFFFFFC')
$failedReturnEvidence = Get-C188R11R2KeyboardEvidence $failedManagedReturn
Assert-Predicate 'a failed Notes callback result is not accepted as a successful keyboard dispatch' (
    -not [bool]$failedReturnEvidence.traceComplete -and
    -not [bool]$failedReturnEvidence.notesCallbackReturn)

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

$notesTextEntry = @'
[C188R11R2_KEY] stage=main-loop-focus window=00000004 title=Managed Notes owner=ManagedAppWindow result=focused
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=before-pointer kind=pointer-down x=24 y=99 value=0 focused=0 len=3 caret=0 edit=none render=0
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=before-pointer offset=0 count=3 hex=78797A
[C102-MANAGED-OUTPUT] C120-POINTER target=document result=PASS
[C116-NATIVE-INPUT] kind=pointer-down x=00000018 y=00000063 result=PASS
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=after-pointer kind=pointer-down x=24 y=99 value=0 focused=1 len=3 caret=0 edit=2 render=1
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=after-pointer offset=0 count=3 hex=78797A
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=before-key kind=key-char x=0 y=0 value=97 focused=1 len=3 caret=0 edit=none render=0
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=before-key offset=0 count=3 hex=78797A
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=after-key kind=key-char x=0 y=0 value=97 focused=1 len=4 caret=1 edit=1 render=1
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=after-key offset=0 count=4 hex=6178797A
'@
$notesEntryEvidence = Get-C188R11R4R2NotesTextEntryEvidence $notesTextEntry $true
Assert-Predicate 'C120 document click, existing-text insertion at caret, caret advance, and redraw qualify' (
    $notesEntryEvidence.accepted -and $notesEntryEvidence.editorFocusEstablished -and
    $notesEntryEvidence.preInputStateQualified -and $notesEntryEvidence.oneCharacterInserted -and
    $notesEntryEvidence.redrawCompleted -and $notesEntryEvidence.beforeText -ceq 'xyz' -and
    $notesEntryEvidence.afterText -ceq 'axyz' -and $notesEntryEvidence.beforeLength -eq 3 -and
    $notesEntryEvidence.afterLength -eq 4 -and $notesEntryEvidence.beforeCaret -eq 0 -and
    $notesEntryEvidence.afterCaret -eq 1)

$notesC120Route = @'
[C188R11R2_KEY] stage=main-loop-focus window=00000004 title=Managed Notes owner=ManagedAppWindow result=focused
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=before-pointer kind=pointer-down x=24 y=99 value=0 focused=0 len=7 caret=0 edit=none render=0
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=before-pointer offset=0 count=7 hex=6F6E650A74776F
[C102-MANAGED-OUTPUT] C120-POINTER target=document result=PASS
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=after-pointer kind=pointer-down x=24 y=99 value=0 focused=1 len=7 caret=4 edit=2 render=1
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=after-pointer offset=0 count=7 hex=6F6E650A74776F
[C116-NATIVE-INPUT] kind=pointer-down x=00000018 y=00000063 result=PASS
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=before-key kind=key-char x=0 y=0 value=97 focused=1 len=7 caret=4 edit=none render=0
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=before-key offset=0 count=7 hex=6F6E650A74776F
[C102-MANAGED-OUTPUT] C188R11R4R2-NOTES stage=after-key kind=key-char x=0 y=0 value=97 focused=1 len=8 caret=5 edit=1 render=1
[C102-MANAGED-OUTPUT] C188R11R4R2-TEXT stage=after-key offset=0 count=8 hex=6F6E650A6174776F
'@
$c120RouteEvidence = Get-C188R11R4R2NotesTextEntryEvidence $notesC120Route $true
Assert-Predicate 'control-host route preserves its nonzero caret and qualifies insertion in existing text' (
    $c120RouteEvidence.accepted -and $c120RouteEvidence.beforeText -ceq "one`ntwo" -and
    $c120RouteEvidence.afterText -ceq "one`natwo" -and
    $c120RouteEvidence.beforeCaret -eq 4 -and $c120RouteEvidence.afterCaret -eq 5)

$preservedR2FixturePath = Join-Path $PSScriptRoot 'fixtures/c188r11r2/c120-document-route.serial.txt'
$preservedR2Serial = Get-Content -LiteralPath $preservedR2FixturePath -Raw
$preservedR2Focus = Get-C188R11R4R2NotesFocusEvidence $preservedR2Serial
Assert-Predicate 'preserved R2 serial evidence recognizes the active Notes window and exact C120 document route while leaving widget focus unknown' (
    $preservedR2Focus.notesWindowActive -and
    $preservedR2Focus.documentControlRouteEstablished -and
    -not $preservedR2Focus.documentRouteCorrelatedToPointer -and
    -not $preservedR2Focus.editableDocumentWidgetFocused -and
    $preservedR2Focus.focusUnknown -and
    $preservedR2Focus.focusState -eq 'unknown')

$windowOnlyFocus = Get-C188R11R4R2NotesFocusEvidence '[C188R11R2_KEY] stage=main-loop-focus window=00000004 title=Managed Notes owner=ManagedAppWindow result=focused'
Assert-Predicate 'active Notes window alone leaves document route and widget focus unknown' (
    $windowOnlyFocus.notesWindowActive -and
    -not $windowOnlyFocus.documentControlRouteEstablished -and
    -not $windowOnlyFocus.editableDocumentWidgetFocused -and
    $windowOnlyFocus.focusUnknown)

$unrelatedControlFocus = Get-C188R11R4R2NotesFocusEvidence (
    '[C188R11R2_KEY] stage=main-loop-focus window=00000004 title=Managed Notes owner=ManagedAppWindow result=focused' + [Environment]::NewLine +
    '[C102-MANAGED-OUTPUT] C120-ROUTING active=control result=PASS')
Assert-Predicate 'C120 active-control routing is distinguished from editable document focus' (
    $unrelatedControlFocus.unrelatedControlFocused -and
    -not $unrelatedControlFocus.editableDocumentWidgetFocused -and
    $unrelatedControlFocus.focusState -eq 'unrelated-control')

$documentThenUnrelatedControl = Get-C188R11R4R2NotesFocusEvidence (
    $notesC120Route + [Environment]::NewLine +
    '[C102-MANAGED-OUTPUT] C120-ROUTING active=control result=PASS')
Assert-Predicate 'a later unrelated control route invalidates earlier document focus evidence' (
    $documentThenUnrelatedControl.documentControlRouteEstablished -and
    -not $documentThenUnrelatedControl.editableDocumentWidgetFocused -and
    $documentThenUnrelatedControl.unrelatedControlFocused -and
    $documentThenUnrelatedControl.focusState -eq 'unrelated-control')

$unsupportedRouteFocus = Get-C188R11R4R2NotesFocusEvidence (
    '[C188R11R2_KEY] stage=main-loop-focus window=00000004 title=Managed Notes owner=ManagedAppWindow result=focused' + [Environment]::NewLine +
    '[C102-MANAGED-OUTPUT] C120-POINTER target=disabled result=PASS')
Assert-Predicate 'disabled or unsupported pointer routing is retained as a non-editable focus observation' (
    $unsupportedRouteFocus.unsupportedRouteObserved -and
    -not $unsupportedRouteFocus.documentControlRouteEstablished -and
    -not $unsupportedRouteFocus.editableDocumentWidgetFocused -and
    $unsupportedRouteFocus.focusUnknown -and
    $unsupportedRouteFocus.focusState -eq 'unsupported-route-focus-unknown')

$missingC120Route = $notesC120Route -replace '(?m)^\[C102-MANAGED-OUTPUT\] C120-POINTER target=document result=PASS\r?\n', ''
$missingC120RouteEvidence = Get-C188R11R4R2NotesTextEntryEvidence $missingC120Route $true
Assert-Predicate 'editable buffer evidence without the established C120 document-control route is rejected' (
    -not $missingC120RouteEvidence.accepted -and
    -not $missingC120RouteEvidence.documentControlRouteEstablished -and
    -not $missingC120RouteEvidence.editorFocusEstablished)

$callbackOnly = Get-C188R11R4R2NotesTextEntryEvidence $notesTextEntry $false
Assert-Predicate 'a successful callback flag without an insertion trace is not accepted' (
    -not $callbackOnly.accepted -and $callbackOnly.oneCharacterInserted)

$wrongInsertion = $notesTextEntry.Replace(
    'stage=after-key kind=key-char x=0 y=0 value=97 focused=1 len=4 caret=1 edit=1 render=1',
    'stage=after-key kind=key-char x=0 y=0 value=97 focused=1 len=4 caret=1 edit=1 render=1').Replace(
    'stage=after-key offset=0 count=4 hex=6178797A',
    'stage=after-key offset=0 count=4 hex=7879617A')
$wrongInsertionEvidence = Get-C188R11R4R2NotesTextEntryEvidence $wrongInsertion $true
Assert-Predicate 'a changed buffer with the wrong insertion position or text is rejected' (
    -not $wrongInsertionEvidence.accepted -and -not $wrongInsertionEvidence.oneCharacterInserted)

$noRedraw = $notesTextEntry.Replace(
    'kind=key-char x=0 y=0 value=97 focused=1 len=4 caret=1 edit=1 render=1',
    'kind=key-char x=0 y=0 value=97 focused=1 len=4 caret=1 edit=1 render=0')
$noRedrawEvidence = Get-C188R11R4R2NotesTextEntryEvidence $noRedraw $true
Assert-Predicate 'buffer insertion remains distinct from redraw completion' (
    -not $noRedrawEvidence.accepted -and $noRedrawEvidence.oneCharacterInserted -and
    -not $noRedrawEvidence.redrawCompleted)

$wrongFocus = $notesTextEntry.Replace(
    'stage=after-pointer kind=pointer-down x=24 y=99 value=0 focused=1 len=3 caret=0 edit=2 render=1',
    'stage=after-pointer kind=pointer-down x=24 y=99 value=0 focused=0 len=3 caret=0 edit=2 render=1')
$wrongFocusEvidence = Get-C188R11R4R2NotesTextEntryEvidence $wrongFocus $true
Assert-Predicate 'successful callback and text change cannot bypass explicit editor focus' (
    -not $wrongFocusEvidence.accepted -and -not $wrongFocusEvidence.editorFocusEstablished)

Assert-Predicate 'instrumentation rows remain bounded below the managed log cap' (
    @($notesTextEntry -split "`r?`n" | Where-Object {
        [System.Text.Encoding]::UTF8.GetByteCount($_.Substring('[C102-MANAGED-OUTPUT] '.Length)) -gt 255
    }).Count -eq 0)

Write-Output "C188R11R2-RUNNER-PREDICATES cases=$script:caseCount result=PASS"
