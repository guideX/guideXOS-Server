param(
    [Parameter(Mandatory=$true)][string]$SerialPath,
    [string]$OutputPath = '',
    [string]$RunnerResultPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not (Test-Path -LiteralPath $SerialPath -PathType Leaf)) {
    throw "Serial input not found: $SerialPath"
}
$raw = [System.IO.File]::ReadAllText([System.IO.Path]::GetFullPath($SerialPath))
$threadEvents = [System.Collections.Generic.List[object]]::new()
$threadPattern = '\[C188R12R4R5R4_THREAD\](?: seq=([0-9A-Fa-f]{8}|[0-9A-Fa-f]{16}))? event=([A-Za-z0-9_]+) raw0=([0-9A-Fa-f]{16}) raw1=([0-9A-Fa-f]{16})'
foreach ($match in [regex]::Matches($raw, $threadPattern)) {
    $sequence = if ($match.Groups[1].Success) { $match.Groups[1].Value.ToUpperInvariant() } else { $null }
    $threadEvents.Add([pscustomobject]@{
        sequence=$sequence; event=$match.Groups[2].Value
        raw0=$match.Groups[3].Value.ToUpperInvariant(); raw1=$match.Groups[4].Value.ToUpperInvariant()
    })
}

$traceRows = [System.Collections.Generic.List[object]]::new()
$tracePattern = '\[C188R12R4R5R1_INIT\] invocation=([0-9A-Fa-f]{8}) stage=([0-9A-Fa-f]{8}) event=([0-9A-Fa-f]{8}) code=([0-9A-Fa-f]{8}) singleton=([0-9A-Fa-f]{8})'
foreach ($match in [regex]::Matches($raw, $tracePattern)) {
    $traceRows.Add([pscustomobject]@{
        invocation=$match.Groups[1].Value.ToUpperInvariant()
        stage=$match.Groups[2].Value.ToUpperInvariant()
        event=[Convert]::ToUInt32($match.Groups[3].Value,16)
        code=$match.Groups[4].Value.ToUpperInvariant()
        auxiliary=$match.Groups[5].Value.ToUpperInvariant()
    })
}

$traceEnd = $null
$traceEndMatch = [regex]::Match($raw, '\[C188R12R4R5R1_INIT\] trace_end invocation=([0-9A-Fa-f]{8}) count=([0-9A-Fa-f]{8}) overflow=([0-9A-Fa-f]{8}) serialDropped=([0-9A-Fa-f]{8}) adapterStatus=([0-9A-Fa-f]{8})')
if ($traceEndMatch.Success) {
    $traceEnd = [ordered]@{
        invocation=$traceEndMatch.Groups[1].Value.ToUpperInvariant()
        count=[Convert]::ToUInt32($traceEndMatch.Groups[2].Value,16)
        overflow=[Convert]::ToUInt32($traceEndMatch.Groups[3].Value,16)
        serialDropped=[Convert]::ToUInt32($traceEndMatch.Groups[4].Value,16)
        adapterStatus=$traceEndMatch.Groups[5].Value.ToUpperInvariant()
    }
}

$threadByEvent = @{}
foreach ($event in $threadEvents) {
    if (-not $threadByEvent.ContainsKey([string]$event.event)) {
        $threadByEvent[[string]$event.event] = [System.Collections.Generic.List[object]]::new()
    }
    $threadByEvent[[string]$event.event].Add($event)
}
function Get-LastThreadEvent([string]$Name) {
    if (-not $threadByEvent.ContainsKey($Name)) { return $null }
    return $threadByEvent[$Name][$threadByEvent[$Name].Count - 1]
}
function Get-FirstLineIndex([string]$Pattern) {
    $m = [regex]::Match($raw,$Pattern)
    if (-not $m.Success) { return -1 }
    return $raw.Substring(0,$m.Index).Split("`n").Count
}

$initResult = $null
$initResultMatch = [regex]::Match($raw,'\[C188R12R4R5R3_INIT\] init_result nativeBool=([0-9A-Fa-f]{8}) adapterStatus=([0-9A-Fa-f]{8})')
if ($initResultMatch.Success) {
    $initResult = [ordered]@{ nativeBoolean=$initResultMatch.Groups[1].Value.ToUpperInvariant(); adapterStatus=$initResultMatch.Groups[2].Value.ToUpperInvariant() }
}
$safeStatusMatch = [regex]::Match($raw,'\[nativeaot-safe-handoff\] status=intentionally_stopped initialization=accepted moduleRegistration=accepted managedTransition=not_attempted managedEntry=not_reached runtimeError=none')
$safeStopIncompleteMatch = [regex]::Match($raw,'\[nativeaot-safe-handoff\] diagnosticError=([^ ]+)(?: status=([0-9A-Fa-f]{8}))? runtimeError=([^\r\n ]+)')
$failFastMatch = [regex]::Match($raw,'\[nativeaot-pal-qemu-test\] FAIL_FAST reason=([0-9A-Fa-f]{8})')
$failFastReason = if ($failFastMatch.Success) { '0x' + $failFastMatch.Groups[1].Value.ToUpperInvariant() } else { $null }

$gateClosed = $null -ne (Get-LastThreadEvent 'managed_entry_gate_closed')
$gateOpen = $null -ne (Get-LastThreadEvent 'managed_entry_gate_open')
$tlsInstalled = $null -ne (Get-LastThreadEvent 'tls_installed') -and
    (Get-LastThreadEvent 'tls_installed').raw0 -ne '0000000000000000' -and
    (Get-LastThreadEvent 'tls_installed').raw1 -ne '0000000000000000'
$runtimeInstance = Get-LastThreadEvent 'runtime_instance'
$threadStore = Get-LastThreadEvent 'threadstore'
$currentThreadTls = Get-LastThreadEvent 'current_thread_tls'
$currentThreadState = Get-LastThreadEvent 'current_thread_state'
$moduleRegistrationEnter = Get-LastThreadEvent 'module_registration_enter'
$moduleRegistrationComplete = Get-LastThreadEvent 'module_registration_complete'
$safeStopMarker = $null -ne (Get-LastThreadEvent 'diagnostic_safe_stop')
$moduleInitializeAttempted = $null -ne (Get-LastThreadEvent 'module_initialize_enter') -or
    $null -ne (Get-LastThreadEvent 'transition_enter') -or
    $null -ne (Get-LastThreadEvent 'managed_dispatch') -or
    $null -ne (Get-LastThreadEvent 'managed_entry') -or
    @($traceRows | Where-Object { $_.stage -eq '00000063' }).Count -gt 0
$managedEntryReached = $null -ne (Get-LastThreadEvent 'managed_entry') -or
    $raw.Contains('[nativeaot-gc-ownership] outcome=PASS') -or
    $raw.Contains('[nativeaot-gc-first-allocation] entering ManagedMain')
$managedDispatchAttempted = $moduleInitializeAttempted -or $raw.Contains('managed_main_call ordinal=')
$moduleRegistrationAccepted = $null -ne $moduleRegistrationComplete -and
    $moduleRegistrationComplete.raw1 -eq '0000000000000001'
$initializationAccepted = $null -ne $initResult -and
    $initResult.nativeBoolean -eq '00000001' -and $initResult.adapterStatus -eq '00000000'
if ($null -eq $initResult -and $traceRows.Count -gt 0) {
    $rhRows = @($traceRows | Where-Object { $_.stage -eq '00000002' -and $_.event -eq 2 })
    $initializationAccepted = $rhRows.Count -gt 0 -and $rhRows[-1].code -eq '00000000'
}

$safeIntentional = $safeStatusMatch.Success -and $gateClosed -and $safeStopMarker -and
    -not $managedDispatchAttempted -and -not $managedEntryReached -and -not $failFastMatch.Success
$v2StageRows = @{}
$v2FirstFailure = $null
$v2Totals = $null
$v2Malformed = $false
$v2Order = [System.Collections.Generic.List[string]]::new()
foreach ($row in $traceRows) {
    if ($row.event -eq 2147483650) {
        if ($null -ne $v2FirstFailure -or $row.stage -eq 'FFFFFFFF') { $v2Malformed=$true; continue }
        $v2FirstFailure = [ordered]@{ stage=$row.stage; result=$row.code; singleton=$row.auxiliary }
        continue
    }
    if ($row.event -eq 2147483651) {
        if ($null -ne $v2Totals -or $row.stage -ne 'FFFFFFFF') { $v2Malformed=$true; continue }
        $v2Totals = [ordered]@{ eventCount=[Convert]::ToUInt32($row.code,16); coalescedCount=[Convert]::ToUInt32($row.auxiliary,16) }
        continue
    }
    $eventHighNibble = [uint32]([uint64]$row.event -band
        [uint64]::Parse('F0000000',[System.Globalization.NumberStyles]::HexNumber))
    if ($eventHighNibble -in @(2415919104,2684354560,2952790016,3221225472)) {
        if ($row.stage -eq 'FFFFFFFF' -or $v2StageRows.ContainsKey($row.stage)) { $v2Malformed=$true; continue }
        $packedCounts = [uint32]([uint64]$row.event -band 0x0FFFFFFF)
        $lastEvent = [int](($eventHighNibble -shr 28) - 8)
        if ($lastEvent -notin @(1,2,3,4)) { $v2Malformed=$true; continue }
        $v2StageRows[$row.stage] = [ordered]@{
            begin=($packedCounts -band 0x7F)
            pass=(($packedCounts -shr 7) -band 0x7F)
            fail=(($packedCounts -shr 14) -band 0x7F)
            observe=(($packedCounts -shr 21) -band 0x7F)
            lastEvent=$lastEvent; lastResult=$row.code; singleton=$row.auxiliary
        }
        $lastEventField = @('','begin','pass','fail','observe')[$lastEvent]
        if ($v2StageRows[$row.stage][$lastEventField] -eq 0) { $v2Malformed=$true }
        $v2Order.Add($row.stage)
    } else {
        $v2Malformed=$true
    }
}
$v2TracePresent = $null -ne $v2Totals -or $v2StageRows.Count -gt 0
$v2Malformed = $v2Malformed -or $v2StageRows.Count -gt 64
$v2StoredEventCount = 0
foreach ($stage in $v2StageRows.Keys) {
    $countRow = $v2StageRows[$stage]
    $v2StoredEventCount += [int]$countRow.begin + [int]$countRow.pass + [int]$countRow.fail + [int]$countRow.observe
}
if (($v2Totals -and $v2Totals.eventCount -lt $v2StoredEventCount) -or
    ($v2Totals -and $v2Totals.coalescedCount -gt $v2Totals.eventCount) -or
    ($v2Totals -and -not $v2FirstFailure -and @($v2StageRows.Keys | Where-Object { $v2StageRows[$_].fail -gt 0 }).Count -gt 0)) {
    $v2Malformed=$true
}
$overflow = if ($null -ne $traceEnd) { [uint32]$traceEnd.overflow } else { [uint32]::MaxValue }
$serialDropped = if ($null -ne $traceEnd) { [uint32]$traceEnd.serialDropped } else { [uint32]::MaxValue }
if ($v2Totals -and $overflow -ne [uint32]::MaxValue) {
    if ($v2Totals.eventCount -ne ($v2StoredEventCount + $overflow) -or
        $v2Totals.coalescedCount -lt ($v2StoredEventCount - $v2StageRows.Count) -or
        ($overflow -eq 0 -and $v2Totals.coalescedCount -ne ($v2Totals.eventCount - $v2StageRows.Count))) {
        $v2Malformed=$true
    }
    if ($v2FirstFailure -and $v2StageRows.ContainsKey($v2FirstFailure.stage) -and
        $v2StageRows[$v2FirstFailure.stage].fail -eq 0) { $v2Malformed=$true }
    if ($v2FirstFailure -and -not $v2StageRows.ContainsKey($v2FirstFailure.stage) -and $overflow -eq 0) {
        $v2Malformed=$true
    }
}
$traceCountMatches = $null -ne $traceEnd -and $traceEnd.count -eq $v2StageRows.Count
$traceComplete = $v2TracePresent -and $null -ne $v2Totals -and $null -ne $traceEnd -and $traceCountMatches -and
    $overflow -eq 0 -and $serialDropped -eq 0 -and -not $v2Malformed -and
    $v2Totals.eventCount -eq $v2StoredEventCount
$threadStoreCurrentPresent = $null -ne $currentThreadState -and
    $currentThreadState.raw0 -ne '0000000000000000' -and $currentThreadState.raw1 -eq '0000000000000001'
$threadStorePublished = $null -ne $threadStore -and
    $threadStore.raw0 -ne '0000000000000000' -and $threadStore.raw1 -eq '0000000000000001'
$attachmentComplete = $null -ne (Get-LastThreadEvent 'pal_attach_complete') -or
    $null -ne (Get-LastThreadEvent 'transition_attach_complete')
$palStackBoundsKnown = $null -ne (Get-LastThreadEvent 'pal_stack_bounds')
$reversePInvokeFrameSeen = $null -ne (Get-LastThreadEvent 'transition_enter')
$moduleInitSeen = $null -ne (Get-LastThreadEvent 'module_initialize_complete')
$safeOrderValid = $initializationAccepted -and $moduleRegistrationAccepted -and $tlsInstalled -and
    $null -ne $runtimeInstance -and $runtimeInstance.raw1 -eq '0000000000000001' -and
    $threadStorePublished -and $null -ne $currentThreadTls -and
    $null -ne $moduleRegistrationEnter -and $safeIntentional -and -not $moduleInitSeen
$initResultLine = Get-FirstLineIndex '\[C188R12R4R5R3_INIT\] init_result '
$gateClosedLine = Get-FirstLineIndex '\[C188R12R4R5R4_THREAD\] event=managed_entry_gate_closed '
$tlsInstalledLine = Get-FirstLineIndex '\[C188R12R4R5R4_THREAD\] event=tls_installed '
$moduleEnterLine = Get-FirstLineIndex '\[C188R12R4R5R4_THREAD\](?: seq=[0-9A-Fa-f]{8}|[0-9A-Fa-f]{16})? event=module_registration_enter '
$moduleCompleteLine = Get-FirstLineIndex '\[C188R12R4R5R4_THREAD\](?: seq=[0-9A-Fa-f]{8}|[0-9A-Fa-f]{16})? event=module_registration_complete '
$safeStopLine = Get-FirstLineIndex '\[C188R12R4R5R4_THREAD\] event=diagnostic_safe_stop '
if ($safeIntentional -and ($initResultLine -lt 0 -or $gateClosedLine -le $initResultLine -or
    $tlsInstalledLine -le $gateClosedLine -or $moduleEnterLine -le $tlsInstalledLine -or
    $moduleCompleteLine -le $moduleEnterLine -or $safeStopLine -le $moduleCompleteLine)) {
    $safeOrderValid=$false
}
if ($v2TracePresent) {
    $rhIndex = $v2Order.IndexOf('00000002')
    $moduleIndex = $v2Order.IndexOf('00000061')
    $codeManagerIndex = $v2Order.IndexOf('00000062')
    if ($rhIndex -lt 0 -or $moduleIndex -lt 0 -or $codeManagerIndex -lt 0 -or
        $moduleIndex -le $rhIndex -or $codeManagerIndex -le $moduleIndex -or
        $v2StageRows['00000002'].pass -lt 1 -or
        $v2StageRows['00000061'].pass -lt 1 -or
        $v2StageRows['00000062'].pass -lt 1 -or
        $v2StageRows.ContainsKey('00000063')) { $safeOrderValid=$false }
}

$toolingFailure = $false
$toolingFailureDetail = $null
if (-not [string]::IsNullOrWhiteSpace($RunnerResultPath) -and (Test-Path -LiteralPath $RunnerResultPath -PathType Leaf)) {
    $runnerResult = Get-Content -LiteralPath $RunnerResultPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $runnerClass = [string]$runnerResult.result
    if ($runnerClass -match 'TOOL|POSTPROCESS|CLASSIFICATION|ERROR') {
        $toolingFailure=$true
        $toolingFailureDetail=$runnerClass
    }
}
if ($raw -match '\[nativeaot-safe-handoff\] toolingFailure=([^\r\n ]+)') {
    $toolingFailure=$true
    $toolingFailureDetail=$Matches[1]
}
$diagnosticErrorStatus = if ($safeStopIncompleteMatch.Success) {
    [ordered]@{ status=$safeStopIncompleteMatch.Groups[1].Value; code=$safeStopIncompleteMatch.Groups[2].Value; detail='safe handoff preparation did not complete' }
} elseif ($overflow -gt 0 -or $serialDropped -gt 0 -or -not $traceComplete) {
    [ordered]@{ status='evidence_incomplete'; code=$null; detail='bounded initialization trace or terminal counts are incomplete' }
} else { [ordered]@{ status='none'; code=$null; detail=$null } }
$classification = if ($toolingFailure) { 'TOOLING_OR_POSTPROCESSING_FAILURE' }
    elseif ($safeIntentional -and $safeOrderValid -and $traceComplete) { 'SAFE_HANDOFF_INTENTIONALLY_STOPPED' }
    elseif ($safeStatusMatch.Success -and -not $safeOrderValid) { 'MALFORMED_OR_CONTRADICTORY_MILESTONE_ORDER' }
    elseif ($failFastMatch.Success -and $managedDispatchAttempted) { 'MANAGED_TRANSITION_FAILED' }
    elseif ($failFastMatch.Success) { 'RUNTIME_FAIL_FAST_BEFORE_TRANSITION_ATTRIBUTION' }
    elseif ($v2Malformed -or -not $traceComplete) { 'DIAGNOSTIC_EVIDENCE_INCOMPLETE' }
    elseif ($moduleRegistrationAccepted -and -not $managedEntryReached) { 'MODULE_REGISTRATION_ACCEPTED_MANAGED_ENTRY_NOT_REACHED' }
    else { 'INCOMPLETE_OR_UNRECOGNIZED_TRACE' }

$requiredThreadEvents = @('tls_installed','runtime_instance','threadstore','current_thread_tls','current_thread_state','module_registration_enter','module_registration_complete')
$missingThreadEvents = @($requiredThreadEvents | Where-Object { $null -eq (Get-LastThreadEvent $_) })
$report = [ordered]@{
    analyzer='C188R12R4R5R2R2R4-safe-handoff-v1'
    result=$classification
    evidencePath=[System.IO.Path]::GetFullPath($SerialPath)
    evidenceSha256=(Get-FileHash -LiteralPath $SerialPath -Algorithm SHA256).Hash.ToUpperInvariant()
    initializationAccepted=[bool]$initializationAccepted
    nativeRhInitializeBoolean=if ($null -ne $initResult) { $initResult.nativeBoolean } else { $null }
    nativeRhInitializeOutcome=if ($null -ne $initResult -and $initResult.nativeBoolean -eq '00000001') { 'success' }
        elseif ($null -ne $initResult -and $initResult.nativeBoolean -eq '00000000') { 'failure' } else { 'unknown' }
    startupAdapterStatusCode=if ($null -ne $initResult) { $initResult.adapterStatus } else { $null }
    startupAdapterOutcome=if ($null -ne $initResult -and $initResult.adapterStatus -eq '00000000') { 'success' }
        elseif ($null -ne $initResult) { 'failure' } else { 'unknown' }
    rhInitializeResult=if ($initializationAccepted) { 'success' } else { 'unknown' }
    moduleRegistrationAccepted=[bool]$moduleRegistrationAccepted
    managedTransitionIntentionallyNotAttempted=[bool]$safeIntentional
    managedTransitionAttempted=[bool]$managedDispatchAttempted
    managedTransitionFailed=[bool]($failFastMatch.Success -and $managedDispatchAttempted)
    managedEntryReached=[bool]$managedEntryReached
    managedRuntimeAcceptance=$false
    threadStoreHandoffReady=$false
    failFast=[ordered]@{ present=[bool]$failFastMatch.Success; code=$failFastReason; meaning='uninterpreted diagnostic code; source PalPrintFatalError discards its message' }
    diagnosticEvidenceComplete=[bool]$traceComplete
    diagnosticErrorStatus=$diagnosticErrorStatus
    runtimeErrorStatus=if ($failFastMatch.Success) { [ordered]@{ status='fail_fast'; code=$failFastReason } } else { [ordered]@{ status='none'; code=$null } }
    toolingFailure=[bool]$toolingFailure
    toolingFailureDetail=$toolingFailureDetail
    safeStopOrderValid=[bool]$safeOrderValid
    milestoneOrderValid=[bool]$safeOrderValid
    trace=[ordered]@{
        format=if ($v2TracePresent) { 'compact_stage_counts' } else { 'event_rows_or_unrecognized' }
        stageCapacity=64; stageCount=$v2StageRows.Count; eventCount=if ($v2Totals) { $v2Totals.eventCount } else { $null }
        coalescedEventCount=if ($v2Totals) { $v2Totals.coalescedCount } else { $null }
        storedEventCount=$v2StoredEventCount; overflow=$overflow; serialDropped=$serialDropped
        firstFailure=$v2FirstFailure; malformed=[bool]$v2Malformed; terminal=$traceEnd
    }
    threadStorePrerequisites=[ordered]@{
        tlsInstalled=[bool]$tlsInstalled
        runtimeInstancePublished=[bool]($null -ne $runtimeInstance -and $runtimeInstance.raw1 -eq '0000000000000001')
        threadStorePublished=[bool]$threadStorePublished
        currentThreadTlsObserved=[bool]($null -ne $currentThreadTls -and $currentThreadTls.raw0 -ne '0000000000000000')
        currentThreadAccessorNonNull=[bool]$threadStoreCurrentPresent
        currentThreadAttached=[bool]$attachmentComplete
        palStackBoundsQualified=[bool]$palStackBoundsKnown
        reversePInvokeFrameObserved=[bool]$reversePInvokeFrameSeen
        moduleInitializationComplete=[bool]$moduleInitSeen
        missingTelemetryEvents=$missingThreadEvents
    }
    preservedEvidence=[ordered]@{ serialMutated=$false; rawLineCount=($raw.Split("`n").Count - 1) }
}
$json = $report | ConvertTo-Json -Depth 10
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $json }
else { [System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($OutputPath),$json,[System.Text.UTF8Encoding]::new($false)) }
