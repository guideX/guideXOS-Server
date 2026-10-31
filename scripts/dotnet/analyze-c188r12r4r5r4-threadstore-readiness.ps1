param(
    [Parameter(Mandatory = $true)][string]$SerialPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not (Test-Path -LiteralPath $SerialPath -PathType Leaf)) {
    throw "ThreadStore-readiness serial input is missing: $SerialPath"
}

$events = [System.Collections.Generic.List[object]]::new()
$lineNumber = 0
foreach ($line in Get-Content -LiteralPath $SerialPath) {
    $lineNumber++
    if ($line -match '^\[C188R12R4R5R4_THREAD\]\s+(?:seq=([0-9A-Fa-f]{8}|[0-9A-Fa-f]{16})\s+)?event=([a-z_]+)\s+raw0=(?:0x)?([0-9A-Fa-f]{1,16})\s+raw1=(?:0x)?([0-9A-Fa-f]{1,16})\s*$') {
        $events.Add([pscustomobject]@{
            line = $lineNumber
            sequence = $Matches[1]
            event = $Matches[2]
            raw0 = $Matches[3].PadLeft(16, '0').ToUpperInvariant()
            raw1 = $Matches[4].PadLeft(16, '0').ToUpperInvariant()
        })
    }
}

function Find-EventIndex([string]$Name) {
    for ($index = 0; $index -lt $events.Count; $index++) {
        if ($events[$index].event -ceq $Name) { return $index }
    }
    return -1
}
function First-Event([string]$Name) {
    $index = Find-EventIndex $Name
    if ($index -lt 0) { return $null }
    return $events[$index]
}
function Has-OrderedEvents([string[]]$Names) {
    $prior = -1
    foreach ($name in $Names) {
        $index = Find-EventIndex $name
        if ($index -le $prior) { return $false }
        $prior = $index
    }
    return $true
}

$adapterEnter = First-Event 'adapter_enter'
$nativeReturn = First-Event 'rhinitialize_return'
$kernelResult = First-Event 'kernel_init_result'
$gateBlocked = First-Event 'managed_entry_blocked_init_failure'
$gateOpen = First-Event 'managed_entry_gate_open'
$managedEntry = First-Event 'managed_entry'
$outcome = 'INCOMPLETE_TRACE'
$failureBoundary = $null
$mappingConsistent = $null
$managedEntryReached = $false
$threadStoreReady = $false
$traceOrderConsistent = $false

if ($null -ne $nativeReturn) {
    $nativeBoolean = $nativeReturn.raw0
    $adapterStatus = $nativeReturn.raw1
    $mappingConsistent = ($nativeBoolean -eq '0000000000000001' -and $adapterStatus -eq '0000000000000000') -or
        ($nativeBoolean -eq '0000000000000000' -and $adapterStatus -eq '0000000000000001')
    if (-not $mappingConsistent) {
        $outcome = 'INIT_RESULT_INCONSISTENT'
        $failureBoundary = 'RhInitialize adapter mapping'
    } elseif ($null -eq $adapterEnter -or $adapterEnter.line -ge $nativeReturn.line) {
        $outcome = 'ADAPTER_ENTRY_INCOMPLETE'
        $failureBoundary = 'Native adapter entry'
    } elseif ($nativeBoolean -eq '0000000000000000') {
        if ($null -ne $gateBlocked -and $gateBlocked.line -gt $nativeReturn.line -and
            $null -eq $gateOpen -and $null -eq (First-Event 'tls_installed') -and
            $null -eq (First-Event 'module_registration_enter') -and
            $null -eq (First-Event 'transition_enter') -and $null -eq $managedEntry) {
            $outcome = 'INIT_FAILURE_BLOCKED'
            $failureBoundary = 'RhInitialize'
            $traceOrderConsistent = $true
        } else {
            $outcome = 'INIT_FAILURE_GATE_INCOMPLETE'
            $failureBoundary = 'RhInitialize / managed-entry failure gate'
        }
    } elseif ($null -eq $kernelResult -or
        $kernelResult.raw0 -ne $nativeReturn.raw0 -or
        $kernelResult.raw1 -ne $nativeReturn.raw1 -or
        $kernelResult.line -le $nativeReturn.line) {
        $outcome = 'KERNEL_INIT_RESULT_INCOMPLETE'
        $failureBoundary = 'Kernel startup result handoff'
    } elseif ($null -eq $gateOpen -or $gateOpen.line -le $kernelResult.line) {
        $outcome = 'SUCCESS_GATE_INCOMPLETE'
        $failureBoundary = 'Successful RhInitialize gate'
    } elseif ((Find-EventIndex 'tls_installed') -lt 0 -or
        (Find-EventIndex 'tls_installed') -le (Find-EventIndex 'managed_entry_gate_open')) {
        $outcome = 'CURRENT_THREAD_TLS_INCOMPLETE'
        $failureBoundary = 'NativeAOT TLS installation'
    } elseif ((Find-EventIndex 'runtime_instance') -lt 0 -or
        (Find-EventIndex 'threadstore') -lt 0 -or
        (Find-EventIndex 'current_thread_tls') -lt 0 -or
        (Find-EventIndex 'current_thread_state') -lt 0) {
        $outcome = 'THREADSTORE_OBSERVATION_INCOMPLETE'
        $failureBoundary = 'RuntimeInstance / ThreadStore / current-thread TLS observation'
    } elseif ((Find-EventIndex 'runtime_instance') -le (Find-EventIndex 'tls_installed') -or
        (Find-EventIndex 'threadstore') -le (Find-EventIndex 'runtime_instance') -or
        (Find-EventIndex 'current_thread_tls') -le (Find-EventIndex 'threadstore') -or
        (Find-EventIndex 'current_thread_state') -le (Find-EventIndex 'current_thread_tls') -or
        (First-Event 'runtime_instance').raw1 -ne '0000000000000001' -or
        (First-Event 'threadstore').raw1 -ne '0000000000000001' -or
        (First-Event 'current_thread_tls').raw0 -eq '0000000000000000') {
        $outcome = 'THREADSTORE_STATE_INVALID'
        $failureBoundary = 'RuntimeInstance / ThreadStore / current-thread TLS readiness'
    } elseif ((Find-EventIndex 'module_registration_enter') -lt 0 -or
        (Find-EventIndex 'module_registration_enter') -le (Find-EventIndex 'current_thread_state') -or
        (Find-EventIndex 'module_registration_complete') -le (Find-EventIndex 'module_registration_enter')) {
        $outcome = 'MODULE_REGISTRATION_INCOMPLETE'
        $failureBoundary = 'RhRegisterOSModule'
    } elseif ((Find-EventIndex 'module_initialize_enter') -lt 0 -or
        (Find-EventIndex 'module_initialize_enter') -le (Find-EventIndex 'module_registration_complete') -or
        (Find-EventIndex 'module_initialize_complete') -le (Find-EventIndex 'module_initialize_enter')) {
        $outcome = 'MODULE_INITIALIZATION_INCOMPLETE'
        $failureBoundary = 'InitializeModules'
    } elseif ((Find-EventIndex 'transition_enter') -lt 0 -or
        (Find-EventIndex 'transition_enter') -le (Find-EventIndex 'module_initialize_complete')) {
        $outcome = 'ATTACHMENT_INCOMPLETE'
        $failureBoundary = 'Stock reverse-P/Invoke attachment'
    } elseif ((Find-EventIndex 'transition_attach_complete') -lt 0 -and
        (Find-EventIndex 'pal_stack_current') -gt (Find-EventIndex 'transition_enter') -and
        (Find-EventIndex 'pal_stack_bounds') -lt 0 -and
        (First-Event 'pal_stack_current').raw1 -ne '0000000000000000') {
        $outcome = 'PAL_STACK_QUERY_FAILED'
        $failureBoundary = 'PalGetMaximumStackBounds'
    } elseif ((Find-EventIndex 'transition_attach_complete') -le (Find-EventIndex 'transition_enter')) {
        $outcome = 'ATTACHMENT_INCOMPLETE'
        $failureBoundary = 'Stock reverse-P/Invoke attachment'
    } else {
        $initialState = First-Event 'current_thread_state'
        $initiallyInitialized = $initialState.raw1 -eq '0000000000000001'
        $palEnter = First-Event 'pal_attach_enter'
        $palComplete = First-Event 'pal_attach_complete'
        if ($initialState.raw1 -notin @('0000000000000000','0000000000000001')) {
            $outcome = 'THREADSTORE_STATE_INVALID'
            $failureBoundary = 'Current ThreadStore thread state'
        } elseif (-not $initiallyInitialized -and
            ($null -eq $palEnter -or $null -eq $palComplete -or
             $palEnter.line -le (Find-EventIndex 'transition_enter') -or
             $palComplete.line -le $palEnter.line -or
             $palComplete.line -ge (Find-EventIndex 'transition_attach_complete') -or
             $palComplete.raw0 -eq '0000000000000000' -or
             $palComplete.raw1 -ne '0000000000000000')) {
            $outcome = 'PAL_ATTACHMENT_INCOMPLETE'
            $failureBoundary = 'PalAttachThread / FLS publication'
        } elseif (-not $initiallyInitialized -and
            ((Find-EventIndex 'pal_thread_id') -le (Find-EventIndex 'pal_attach_complete') -or
             (Find-EventIndex 'pal_stack_current') -le (Find-EventIndex 'pal_thread_id') -or
             (Find-EventIndex 'pal_stack_current') -ge (Find-EventIndex 'transition_attach_complete') -or
             ((Find-EventIndex 'pal_stack_bounds') -ge 0 -and
              (Find-EventIndex 'pal_stack_bounds') -le (Find-EventIndex 'pal_thread_id')))) {
            $outcome = 'PAL_THREAD_CONSTRUCTION_INCOMPLETE'
            $failureBoundary = 'Thread::Construct / PAL identity and stack bounds'
        } elseif (-not $initiallyInitialized -and
            (Find-EventIndex 'pal_stack_bounds') -lt 0 -and
            (First-Event 'pal_stack_current').raw1 -ne '0000000000000000') {
            $outcome = 'PAL_STACK_QUERY_FAILED'
            $failureBoundary = 'PalGetMaximumStackBounds'
        } elseif (-not $initiallyInitialized -and
            (Find-EventIndex 'pal_stack_bounds') -lt 0) {
            $outcome = 'PAL_THREAD_CONSTRUCTION_INCOMPLETE'
            $failureBoundary = 'Thread::Construct / missing PAL stack bounds'
        } elseif (-not $initiallyInitialized -and
            (First-Event 'pal_stack_current').raw1 -ne '0000000000000000') {
            $outcome = 'PAL_STACK_BOUNDS_INVALID'
            $failureBoundary = 'PalGetMaximumStackBounds interval validation'
        } elseif (-not $initiallyInitialized -and
            ([Convert]::ToUInt64((First-Event 'pal_stack_bounds').raw0, 16) -ge
             [Convert]::ToUInt64((First-Event 'pal_stack_bounds').raw1, 16) -or
             [Convert]::ToUInt64((First-Event 'pal_stack_current').raw0, 16) -lt
             [Convert]::ToUInt64((First-Event 'pal_stack_bounds').raw0, 16) -or
             [Convert]::ToUInt64((First-Event 'pal_stack_current').raw0, 16) -ge
             [Convert]::ToUInt64((First-Event 'pal_stack_bounds').raw1, 16))) {
            $outcome = 'PAL_STACK_BOUNDS_INVALID'
            $failureBoundary = 'Thread::Construct stack interval validation'
        } elseif ($null -eq $managedEntry -or
            $managedEntry.line -le (Find-EventIndex 'transition_attach_complete') -or
            $managedEntry.raw0 -eq '0000000000000000' -or
            $managedEntry.raw1 -ne '0000000000000001') {
            $outcome = 'MANAGED_ENTRY_INCOMPLETE'
            $failureBoundary = 'ManagedMain first statement'
        } else {
            $outcome = 'THREADSTORE_HANDOFF_TRACE_COMPLETE'
            $failureBoundary = $null
            $managedEntryReached = $true
            $threadStoreReady = $true
            $traceOrderConsistent = $true
        }
    }
}

$result = [ordered]@{
    schemaVersion = 1
    serialPath = [System.IO.Path]::GetFullPath($SerialPath)
    outcome = $outcome
    failureBoundary = $failureBoundary
    markerCount = $events.Count
    nativeRhInitializeBoolean = if ($null -ne $nativeReturn) { $nativeReturn.raw0 } else { $null }
    adapterStatus = if ($null -ne $nativeReturn) { $nativeReturn.raw1 } else { $null }
    rhInitializeAdapterMappingConsistent = $mappingConsistent
    managedEntryReached = $managedEntryReached
    threadStoreHandoffReady = $threadStoreReady
    markerOrderConsistent = $traceOrderConsistent
    markers = @($events)
}
[System.IO.File]::WriteAllText(
    [System.IO.Path]::GetFullPath($OutputPath),
    ($result | ConvertTo-Json -Depth 8),
    [System.Text.UTF8Encoding]::new($false))
if ($outcome -in @('INIT_RESULT_INCONSISTENT', 'ADAPTER_ENTRY_INCOMPLETE')) { exit 2 }
exit 0
