param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot 'c188r12r1-settings-handoff-predicates.ps1')

$script:cases = 0
function Assert-C188R12R1([string]$Name, [bool]$Condition) {
    $script:cases++
    if (-not $Condition) { throw "C188R12R1 predicate failed: $Name" }
}

$notesSurface = '[C188R12R1-SURFACE] stage=created appId=com.guidexos.apps.managed.notes selector=00000004 generation=00000001 surfaceGeneration=00000001 window=000003E8 x=00000154 y=000000DC width=00000258 height=00000168 visible=1 minimized=0 focused=1 front=1 dirty=1 result=PASS'
$acceptedTrace = @'
[NATIVEAOT-HEAP] action=initialize count=0000000000000001 base=00000001001F2600 bytes=0000000000010000 reason=initial-managed-thread-attachment
[C112-ACTION-DISPATCH] selector=00000004 action=00000018 managedReturn=00000000 result=PASS
[APPMODEL-MANAGED-LAUNCH] source=DesktopLaunch appId=com.guidexos.apps.managed.settingscenter selector=00000005 image=/system/apps/GXOSAPP.ELF metadata=valid
[NATIVEAOT-HEAP] action=preserve count=000000000000000E resetCount=0000000000000001 base=00000001001F2600 allocation=0000000100201008 reason=resident-managed-thread
[C188R12R1-NATIVE] stage=before-managed-entry selector=00000005 generation=00000002 call=00000001 result=BEGIN
[C102-MANAGED-OUTPUT] C188R12R1-MANAGED stage=main-entry selector=5 generation=2 result=BEGIN
[C102-MANAGED-OUTPUT] C188R12R1-MANAGED stage=dispatch-entry selector=5 result=BEGIN
[C102-MANAGED-OUTPUT] C188R11-MEM seq=12 st=instance-start ra=00000001001F5F50 owner=00000001001F61E0 slot=00000001001F61E8 live=00000001001F6218 held=00000001001F6218 gen=2147483647:2147483647:2147483647:2147483647 gc=0 heldLen=5
[C102-MANAGED-OUTPUT] C188R11 state seq=69 st=settings-dispatch-entry selector=5 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=0 reset=0 m=0 g=1 td=1 n=5 result=-1 gc=0 w=1:1
[C102-MANAGED-OUTPUT] C188R12R1-MANAGED stage=application-launch-entry selector=5 result=BEGIN
[C102-MANAGED-OUTPUT] C188R12R1-SETTINGS stage=launch-entry selector=5 generation=2
[C102-MANAGED-OUTPUT] C188R12R1-SETTINGS stage=before-surface-request selector=5 generation=2
[C188R12R1-SURFACE] stage=created appId=com.guidexos.apps.managed.settingscenter selector=00000005 generation=00000002 surfaceGeneration=00000002 window=000003E9 x=00000154 y=000000DC width=00000320 height=00000258 visible=1 minimized=0 focused=1 front=1 dirty=1 result=PASS
[C102-MANAGED-OUTPUT] C188R12R1-SETTINGS stage=surface-created selector=5 generation=2 result=0
[C188R12R1-COMPOSITOR] stage=composed appId=com.guidexos.apps.managed.settingscenter generation=00000002 surfaceGeneration=00000002 window=000003E9 visible=1 minimized=0 focused=1 front=1 result=DRAWN
[C188R12R1-COMPOSITOR] stage=presented window=000003E9 result=PASS
[C102-MANAGED-OUTPUT] C188R12R1-SETTINGS stage=launch-return selector=5 generation=2 result=0
[C102-MANAGED-OUTPUT] C188R12R1-MANAGED stage=application-launch-return selector=5 result=PASS
[C102-MANAGED-OUTPUT] C188R11 state seq=70 st=settings-launch-return selector=5 ownerGen=1 rg=1 re=1 rr=0 rcl=0 p=1 pr=0 ps=1 psr=0 pe=1 sr=0 pc=1 reset=0 m=1 g=2 td=1 n=5 result=0 gc=0 w=2:2
[C102-MANAGED-OUTPUT] C188R11-MEM seq=70 st=settings-launch-return ra=00000001001F5F50 owner=00000001001F61E0 slot=00000001001F61E8 live=00000001001F6218 held=00000001001F6218 gen=2147483647:2147483647:2147483647:2147483647 gc=0 heldLen=5
[C188R12R1-NATIVE] stage=after-managed-return selector=00000005 generation=00000002 call=00000001 rc=00000000 result=RETURNED
[APPMODEL-MANAGED-RESULT] appId=com.guidexos.apps.managed.settingscenter status=success result=PASS
'@

$positive = Get-C188R12R1SettingsHandoffEvidence $acceptedTrace
Assert-C188R12R1 'matching Settings surface, focus, composition and presentation are accepted' $positive.accepted
Assert-C188R12R1 'generation and window identity remain correlated through presentation' (
    $positive.generation -eq '00000002' -and $positive.window -eq '000003E9' -and
    $positive.earliestMissingBoundary -eq 'presented-settings-surface' -and
    $positive.settingsResidentMask -eq 1 -and $positive.notesActionId -eq '00000018' -and
    $positive.notesSettingsActionInvoked -and $positive.notesSettingsActionSucceeded -and
    $positive.settingsRegistryMemoryStable -and $positive.persistentHeapPreserved)

$wrongAction = $acceptedTrace.Replace(
    '[C112-ACTION-DISPATCH] selector=00000004 action=00000018 managedReturn=00000000 result=PASS',
    '[C112-ACTION-DISPATCH] selector=00000004 action=00000017 managedReturn=FFFFFFFE result=FAIL')
$wrongActionEvidence = Get-C188R12R1SettingsHandoffEvidence $wrongAction
Assert-C188R12R1 'Notes action 0x17 can never qualify a Settings handoff' (
    -not $wrongActionEvidence.accepted -and
    -not $wrongActionEvidence.notesSettingsActionInvoked -and
    $wrongActionEvidence.notesActionId -eq '00000017' -and
    $wrongActionEvidence.earliestMissingBoundary -eq 'notes-settings-action')

$failedSettingsAction = $acceptedTrace.Replace(
    'action=00000018 managedReturn=00000000 result=PASS',
    'action=00000018 managedReturn=FFFFFFFE result=FAIL')
$failedSettingsActionEvidence = Get-C188R12R1SettingsHandoffEvidence $failedSettingsAction
Assert-C188R12R1 'Settings action invocation and its return are reported separately' (
    -not $failedSettingsActionEvidence.accepted -and
    $failedSettingsActionEvidence.notesSettingsActionInvoked -and
    -not $failedSettingsActionEvidence.notesSettingsActionSucceeded -and
    $failedSettingsActionEvidence.earliestMissingBoundary -eq 'settings-action-return')

$zeroResident = $acceptedTrace.Replace(
    'reset=0 m=1 g=2 td=1 n=5 result=0 gc=0 w=2:2',
    'reset=0 m=0 g=1 td=1 n=5 result=0 gc=0 w=2:2')
$zeroResidentEvidence = Get-C188R12R1SettingsHandoffEvidence $zeroResident
Assert-C188R12R1 'Settings presentation without a resident registry entry is rejected' (
    -not $zeroResidentEvidence.accepted -and
    $zeroResidentEvidence.earliestMissingBoundary -eq 'managed-registry-return' -and
    $zeroResidentEvidence.settingsResidentMask -eq 0)

$heapReset = $acceptedTrace -replace '(?m)^\[NATIVEAOT-HEAP\] action=preserve.*\r?\n',''
$heapResetEvidence = Get-C188R12R1SettingsHandoffEvidence $heapReset
Assert-C188R12R1 'Settings handoff without persistent heap preservation is rejected' (
    -not $heapResetEvidence.accepted -and
    $heapResetEvidence.earliestMissingBoundary -eq 'persistent-managed-heap')

$launchOnly = Get-C188R12R1SettingsHandoffEvidence (
    '[APPMODEL-MANAGED-LAUNCH] source=DesktopLaunch appId=com.guidexos.apps.managed.settingscenter selector=00000005 image=/system/apps/GXOSAPP.ELF metadata=valid')
Assert-C188R12R1 'launch request alone is never surface readiness' (
    -not $launchOnly.accepted -and $launchOnly.earliestMissingBoundary -eq 'notes-settings-action')

$managedOnly = Get-C188R12R1SettingsHandoffEvidence (
    '[C112-ACTION-DISPATCH] selector=00000004 action=00000018 managedReturn=00000000 result=PASS' + "`n" +
    '[APPMODEL-MANAGED-LAUNCH] source=DesktopLaunch appId=com.guidexos.apps.managed.settingscenter selector=00000005 image=/system/apps/GXOSAPP.ELF metadata=valid' + "`n" +
    '[C188R12R1-NATIVE] stage=before-managed-entry selector=00000005 generation=00000002 call=00000001 result=BEGIN')
Assert-C188R12R1 'native entry without managed main entry remains unready' (
    -not $managedOnly.accepted -and $managedOnly.earliestMissingBoundary -eq 'managed-main-entry')

$withoutCreate = $acceptedTrace -replace '(?m)^\[C188R12R1-SURFACE\] stage=created.*\r?\n',''
$withoutCreate = $withoutCreate -replace '(?m)^\[C188R12R1-COMPOSITOR\].*\r?\n',''
$withoutCreate = $withoutCreate -replace '(?m)^\[APPMODEL-MANAGED-RESULT\].*\r?\n',''
$noWindow = Get-C188R12R1SettingsHandoffEvidence $withoutCreate
Assert-C188R12R1 'managed initialization without window creation localizes to creation' (
    -not $noWindow.accepted -and $noWindow.earliestMissingBoundary -eq 'window-creation')

$managedCreateFailure = $acceptedTrace -replace '(?m)^\[C188R12R1-SURFACE\] stage=created.*\r?\n',''
$managedCreateFailure = $managedCreateFailure -replace '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=surface-created.*\r?\n',''
$managedCreateFailure = $managedCreateFailure -replace '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=launch-return.*\r?\n',''
$managedCreateFailure = $managedCreateFailure -replace '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-MANAGED stage=application-launch-return.*\r?\n',''
$managedCreateFailure = $managedCreateFailure -replace '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=70.*\r?\n',''
$managedCreateFailure = $managedCreateFailure -replace '(?m)^\[C188R12R1-NATIVE\] stage=after-managed-return.*\r?\n',''
$managedCreateFailure = $managedCreateFailure -replace '(?m)^\[APPMODEL-MANAGED-RESULT\].*\r?\n',''
$managedCreateFailure += "`n[C102-MANAGED-OUTPUT] C188R12R1-SETTINGS stage=surface-request-return selector=5 generation=2 result=6"
$managedCreateFailureEvidence = Get-C188R12R1SettingsHandoffEvidence $managedCreateFailure
Assert-C188R12R1 'managed surface request failure localizes to creation and preserves the result code' (
    -not $managedCreateFailureEvidence.accepted -and
    $managedCreateFailureEvidence.earliestMissingBoundary -eq 'window-creation' -and
    $managedCreateFailureEvidence.managedSurfaceRequestResult -eq 6)

$mismatchedManagedSurface = $acceptedTrace.Replace(
    'C188R12R1-SETTINGS stage=surface-created selector=5 generation=2 result=0',
    'C188R12R1-SETTINGS stage=surface-created selector=5 generation=3 result=0')
Assert-C188R12R1 'managed surface creation return must match the launch generation' (
    -not (Test-C188R12R1SettingsHandoffAccepted $mismatchedManagedSurface))

$hiddenSurface = $acceptedTrace.Replace('visible=1 minimized=0 focused=1 front=1 dirty=1 result=PASS',
    'visible=0 minimized=0 focused=0 front=1 dirty=1 result=PASS')
Assert-C188R12R1 'registered but hidden or unfocused window is rejected' (
    -not (Test-C188R12R1SettingsHandoffAccepted $hiddenSurface))
$notInvalidated = $acceptedTrace.Replace(
    'visible=1 minimized=0 focused=1 front=1 dirty=1 result=PASS',
    'visible=1 minimized=0 focused=1 front=1 dirty=0 result=PASS')
Assert-C188R12R1 'a created window without the compositor dirty request is rejected' (
    -not (Test-C188R12R1SettingsHandoffAccepted $notInvalidated))

$notComposed = $acceptedTrace -replace '(?m)^\[C188R12R1-COMPOSITOR\] stage=composed.*\r?\n',''
$noComposition = Get-C188R12R1SettingsHandoffEvidence $notComposed
Assert-C188R12R1 'visible surface without Settings-specific composition is rejected' (
    -not $noComposition.accepted -and $noComposition.earliestMissingBoundary -eq 'compositor-composition')

$behind = $acceptedTrace.Replace('window=000003E9 visible=1 minimized=0 focused=1 front=1 result=DRAWN',
    'window=000003E9 visible=1 minimized=0 focused=1 front=0 result=DRAWN')
$behindEvidence = Get-C188R12R1SettingsHandoffEvidence $behind
Assert-C188R12R1 'composed but behind another window is rejected' (
    -not $behindEvidence.accepted -and $behindEvidence.earliestMissingBoundary -eq 'compositor-order-or-visibility')

$notPresented = $acceptedTrace -replace '(?m)^\[C188R12R1-COMPOSITOR\] stage=presented.*\r?\n',''
$noPresentation = Get-C188R12R1SettingsHandoffEvidence $notPresented
Assert-C188R12R1 'composition without frame presentation is rejected' (
    -not $noPresentation.accepted -and $noPresentation.earliestMissingBoundary -eq 'frame-presentation')

$mismatchedGeneration = $acceptedTrace.Replace(
    'selector=00000005 generation=00000002 surfaceGeneration=00000002 window=000003E9',
    'selector=00000005 generation=00000003 surfaceGeneration=00000002 window=000003E9')
Assert-C188R12R1 'a surface from another Settings generation cannot qualify' (
    -not (Test-C188R12R1SettingsHandoffAccepted $mismatchedGeneration))

$mismatchedWindow = $acceptedTrace.Replace(
    'stage=presented window=000003E9 result=PASS',
    'stage=presented window=000003E8 result=PASS')
Assert-C188R12R1 'an old Notes frame cannot satisfy Settings presentation' (
    -not (Test-C188R12R1SettingsHandoffAccepted $mismatchedWindow))

$notesTrace = $notesSurface + "`n" + $acceptedTrace
$clickTarget = Get-C188R12R1NotesSettingsClickTarget $notesTrace $repo 1280 800
Assert-C188R12R1 'click target comes from the live Notes geometry and source button center' (
    $clickTarget.accepted -and $clickTarget.x -eq 815 -and $clickTarget.y -eq 550 -and
    $clickTarget.reason -eq 'source-defined-settings-button-center')
$misplacedPointer = [pscustomobject]@{found=$true;matches=1;x=815;y=520}
$misplacedPointerEvidence = Test-C188R12R1PointerOnSettingsButton $misplacedPointer $clickTarget
Assert-C188R12R1 'a cursor that missed the Settings button is rejected before clicking' (
    -not $misplacedPointerEvidence.accepted -and
    $misplacedPointerEvidence.reason -eq 'observed-pointer-outside-settings-button-safe-inset')
$verifiedPointer = [pscustomobject]@{found=$true;matches=1;x=815;y=550}
$verifiedPointerEvidence = Test-C188R12R1PointerOnSettingsButton $verifiedPointer $clickTarget
Assert-C188R12R1 'the observed cursor must be inside the source-defined Settings button inset' (
    $verifiedPointerEvidence.accepted -and
    $verifiedPointerEvidence.reason -eq 'observed-pointer-inside-settings-button-safe-inset')
$popupTarget = Get-C188R12R1NotesSettingsClickTarget (
    $notesTrace + "`n[C102-MANAGED-OUTPUT] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS") $repo 1280 800
Assert-C188R12R1 'a pre-existing popup blocks pointer placement and the click' (
    -not $popupTarget.accepted -and $popupTarget.reason -eq 'notes-popup-or-modal-already-open')
$modalTarget = Get-C188R12R1NotesSettingsClickTarget (
    $notesTrace + "`n[C102-MANAGED-OUTPUT] C151-DIALOG open=PASS modal=active member-registration=3 popup=closed capture=none drag=none result=PASS") $repo 1280 800
Assert-C188R12R1 'a pre-existing modal blocks pointer placement and the click' (
    -not $modalTarget.accepted -and $modalTarget.reason -eq 'notes-popup-or-modal-already-open')

$oneClickQmp = @'
request={"execute":"input-send-event","arguments":{"events":[{"type":"rel","data":{"axis":"x","value":1}}]}}
request={"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":true}}]}}
request={"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":false}}]}}
'@
$clickEvidence = Test-C188R12R1SingleClickQmpEvidence $oneClickQmp
Assert-C188R12R1 'one left-click QMP sequence with no key input is accepted' $clickEvidence.accepted
$misdirectedTrace = $notesTrace.Replace(
    '[C112-ACTION-DISPATCH] selector=00000004 action=00000018 managedReturn=00000000 result=PASS',
    '[C112-ACTION-DISPATCH] selector=00000004 action=00000017 managedReturn=FFFFFFFE result=FAIL') +
    "`n[C102-MANAGED-OUTPUT] C135-POPUP-OPEN invoke=Options capture=PASS popup=open result=PASS"
$misdirectedInputEvidence = Get-C188R12R1RunnerInputEvidence `
    $misdirectedTrace $oneClickQmp $true $verifiedPointerEvidence
Assert-C188R12R1 'an unrelated Notes popup localizes the failure to runner input target delivery' (
    $misdirectedInputEvidence.earliestMissingBoundary -eq 'runner-input-target-delivery' -and
    $misdirectedInputEvidence.unrelatedNotesMenuObserved -and
    $misdirectedInputEvidence.notesActionId -eq '00000017' -and
    -not $misdirectedInputEvidence.notesSettingsActionSucceeded)
$doubleClickEvidence = Test-C188R12R1SingleClickQmpEvidence ($oneClickQmp + $oneClickQmp)
Assert-C188R12R1 'duplicate click sequence is rejected' (-not $doubleClickEvidence.accepted)
$keyEvidence = Test-C188R12R1SingleClickQmpEvidence ($oneClickQmp +
    'request={"execute":"input-send-event","arguments":{"events":[{"type":"key","data":{"down":true,"key":{"type":"qcode","data":"a"}}}]}}')
Assert-C188R12R1 'character or key injection invalidates the handoff run' (-not $keyEvidence.accepted)
$extraButtonEvidence = Test-C188R12R1SingleClickQmpEvidence ($oneClickQmp +
    'request={"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"right","down":true}}]}}')
Assert-C188R12R1 'any extra mouse-button event invalidates the single-click run' (-not $extraButtonEvidence.accepted)

$runnerSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'run-c188r12r1-settings-handoff.ps1') -Raw
Assert-C188R12R1 'runner contains one non-retryable Settings click site' (
    [regex]::Matches($runnerSource,"Invoke-C188R12R1Control 'click-settings-once'").Count -eq 1 -and
    $runnerSource.Contains('$clickAttempted = $true'))
Assert-C188R12R1 'runner has no key or application-launch input path' (
    -not [regex]::IsMatch($runnerSource,"Invoke-C188R12R1Control[^\r\n]*@\('key'") -and
    -not [regex]::IsMatch($runnerSource,"Invoke-C188R12R1Control[^\r\n]*@\('launch'"))
Assert-C188R12R1 'runner verifies the source button position and clicks without moving the pointer' (
    $runnerSource.Contains('Invoke-C188R12R1PointerProbe') -and
    $runnerSource.Contains("Invoke-C188R12R1Control 'click-settings-once' @('click-current')") -and
    $runnerSource.Contains('Test-C188R12R1PointerOnSettingsButton') -and
    $runnerSource.Contains('if (-not [bool]$clickTarget.accepted)'))
$clickCommandIndex = $runnerSource.IndexOf("Invoke-C188R12R1Control 'click-settings-once' @('click-current')")
$placementProbeIndex = $runnerSource.IndexOf("Invoke-C188R12R1PointerProbe 'settings-pointer-probe-001'")
$readinessWaitIndex = $runnerSource.IndexOf('$settingsDeadline = (Get-Date).AddSeconds(30)')
$checkpointIndex = $runnerSource.IndexOf("Invoke-C188R12R1Control 'capture-settings-checkpoint'")
Assert-C188R12R1 'cursor is observed before the single click and no pointer command follows while readiness is unknown' (
    $placementProbeIndex -ge 0 -and $placementProbeIndex -lt $clickCommandIndex -and
    $clickCommandIndex -lt $readinessWaitIndex -and $readinessWaitIndex -lt $checkpointIndex -and
    [regex]::Matches($runnerSource,"Invoke-C188R12R1Control 'click-settings-once'").Count -eq 1)
$qmpControlSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'c188-qmp-control.py') -Raw
Assert-C188R12R1 'QMP relative steps are paced and the verified click path sends no motion' (
    $qmpControlSource.Contains('time.sleep(0.04)') -and
    $qmpControlSource.Contains('def click_current(qmp: Qmp)') -and
    [regex]::IsMatch($qmpControlSource,
        '(?s)def click_current\(qmp: Qmp\):(?<body>.*?)\n\ndef key\(') -and
    -not ([regex]::Match($qmpControlSource,
        '(?s)def click_current\(qmp: Qmp\):(?<body>.*?)\n\ndef key\(').Groups['body'].Value.Contains('"type": "rel"')))
$ps2MouseSource = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/ps2mouse.cpp') -Raw
$cursorFinderSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'find-c188r12r1-cursor.py') -Raw
$cursorDesktopSource = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/desktop.cpp') -Raw
Assert-C188R12R1 'QMP deltas use paced relative counts capped below the PS/2 packet clamp' (
    [regex]::IsMatch($qmpControlSource,
        '(?s)for axis, distance in \(\("x", dx\), \("y", dy\)\):\s+while distance:\s+step = .*?min\(abs\(distance\), 40\).*?"type": "rel"') -and
    $qmpControlSource.Contains('time.sleep(0.04)'))
Assert-C188R12R1 'PS/2 relative counts map one-for-one into clamped screen pixels with Y inverted' (
    [regex]::IsMatch($ps2MouseSource,
        '(?s)int32_t dx = static_cast<int32_t>\(s_packet\[1\]\);\s+int32_t dy = static_cast<int32_t>\(s_packet\[2\]\);.*?dy = -dy;.*?s_mouseX = clamp\(s_mouseX \+ dx, 0, s_screenW - 1\);\s+s_mouseY = clamp\(s_mouseY \+ dy, 0, s_screenH - 1\);'))
Assert-C188R12R1 'screenshot cursor coordinates and the kernel mouse hotspot share the bitmap origin' (
    $cursorFinderSource.Contains('"x": unique[0] if unique else None') -and
    $cursorFinderSource.Contains('"y": unique[1] if unique else None') -and
    [regex]::IsMatch($cursorDesktopSource,
        '(?s)void draw_cursor\(int32_t mx, int32_t my\).*?int32_t px = mx \+ col;\s+int32_t py = my \+ row;'))
$compositorSource = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/kernel_compositor.cpp') -Raw
$desktopSource = $cursorDesktopSource
Assert-C188R12R1 'compositor and presentation tracing emit once per Settings window' (
    $compositorSource.Contains('s_c188r12r1SettingsLoggedComposedWindowId != win->id') -and
    $desktopSource.Contains('settingsWindow != lastPresentedSettingsWindow'))

Write-Output "C188R12R1_SETTINGS_HANDOFF_PREDICATES=PASS cases=$script:cases"
