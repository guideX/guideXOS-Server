param(
    [Parameter(Mandatory=$true)][string]$SerialPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [switch]$GuestTimedOut
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$serialFull = [System.IO.Path]::GetFullPath($SerialPath)
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
if ($serialFull -eq $outputFull) { throw 'Derived initialization analysis must use a separate path from raw serial.' }
if (-not (Test-Path -LiteralPath $serialFull -PathType Leaf)) { throw "Raw serial is missing: $serialFull" }
if (Test-Path -LiteralPath $outputFull) { throw "Refusing to overwrite derived analysis: $outputFull" }

$raw = [System.IO.File]::ReadAllBytes($serialFull)
$sha = [System.Security.Cryptography.SHA256]::Create()
try { $serialSha256 = ([BitConverter]::ToString($sha.ComputeHash($raw)) -replace '-', '').ToUpperInvariant() }
finally { $sha.Dispose() }
$hasTerminalNewline = $raw.Length -gt 0 -and $raw[$raw.Length - 1] -eq 0x0A
$decodeError = $null
try {
    $text = [System.Text.Encoding]::UTF8.GetString($raw)
    if ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) { $text = $text.Substring(1) }
} catch {
    $text = ''
    $decodeError = $_.Exception.Message
}

$stageNames = @{
    '00000001'='adapter'; '00000002'='RhInitialize'; '00000010'='PalInit'
    '00000011'='PalFls'; '00000012'='GCConfig'; '00000013'='GCToOS'
    '00000020'='InitDLL'; '00000021'='interfaceDispatch'; '00000022'='gcEventLock'
    '00000023'='restrictedCallouts'; '00000024'='RuntimeInstance'
    '00000025'='moduleIdentity'; '00000026'='startupImportLookup'
    '00000027'='debugHeaders'; '00000030'='GCInitialize'
    '00000028'='RuntimeInstanceAllocation'; '00000029'='ThreadStoreCreate'
    '0000002A'='RuntimeInstancePublish'
    '00000031'='GCHeapPublication'; '00000032'='GCHeapInitialize'
    '00000033'='GCFinalization'; '00000034'='GCHandleManager'
    '00000040'='CPUFeatures'; '00000050'='GCEventCreate'
    '00000051'='GCVMReserve'; '00000052'='GCVMCommit'
    '00000053'='GCVMDecommit'; '00000054'='GCVMRelease'
    '00000055'='GCVMAdapterInit'; '00000060'='managedReversePInvoke'
    '00000061'='moduleRegistration'; '00000062'='codeManagerRegistration'
    '00000063'='moduleInitialize'
}
$eventNames = @{ '00000001'='begin'; '00000002'='pass'; '00000003'='fail'; '00000004'='observe' }
$events = [System.Collections.Generic.List[object]]::new()
$traceEnds = [System.Collections.Generic.List[object]]::new()
$managedGates = [System.Collections.Generic.List[string]]::new()
$managedCalls = [System.Collections.Generic.List[object]]::new()
$managedReturns = [System.Collections.Generic.List[object]]::new()
$sequence = 0
foreach ($line in ($text -split "`r?`n")) {
    if ($line -match '^\[C188R12R4R5R1_INIT\] invocation=([0-9A-F]{8}) stage=([0-9A-F]{8}) event=([0-9A-F]{8}) code=([0-9A-F]{8}) singleton=([0-9A-F]{8})$') {
        $sequence++
        $invocation = $Matches[1].ToUpperInvariant()
        $stage = $Matches[2].ToUpperInvariant()
        $event = $Matches[3].ToUpperInvariant()
        $events.Add([pscustomobject]@{
            sequence=$sequence; invocation=$invocation; stage=$stage
            stageName=if ($stageNames.ContainsKey($stage)) { $stageNames[$stage] } else { 'unknown' }
            event=$event; eventName=if ($eventNames.ContainsKey($event)) { $eventNames[$event] } else { 'unknown' }
            code=$Matches[4].ToUpperInvariant(); singletonBits=$Matches[5].ToUpperInvariant()
        })
        continue
    }
    if ($line -match '^\[C188R12R4R5R1_INIT\] trace_end invocation=([0-9A-F]{8}) count=([0-9A-F]{8}) overflow=([0-9A-F]{8}) serialDropped=([0-9A-F]{8}) rhResult=([0-9A-F]{8})$') {
        $traceEnds.Add([pscustomobject]@{
            invocation=$Matches[1].ToUpperInvariant(); recordCount=$Matches[2].ToUpperInvariant()
            overflow=$Matches[3].ToUpperInvariant(); serialDropped=$Matches[4].ToUpperInvariant()
            rhResult=$Matches[5].ToUpperInvariant()
        })
        continue
    }
    if ($line -match '^\[C188R12R4R5R1_INIT\] managed_main_gate=(\S+)(?: reason=(\S+))?(?: singleton=([0-9A-F]{8}))?(?: gcProof=(\S+))?(?: rhResult=([0-9A-F]{8}))?$') {
        $managedGates.Add($line)
        continue
    }
    if ($line -match '^\[C188R12R4R5R1_INIT\] managed_main_call ordinal=([0-9A-F]{8})$') {
        $managedCalls.Add([pscustomobject]@{ordinal=$Matches[1].ToUpperInvariant()})
        continue
    }
    if ($line -match '^\[C188R12R4R5R1_INIT\] managed_main_return ordinal=([0-9A-F]{8}) result=([0-9A-F]{8})$') {
        $managedReturns.Add([pscustomobject]@{ordinal=$Matches[1].ToUpperInvariant(); result=$Matches[2].ToUpperInvariant()})
    }
}

$pairedStages = [System.Collections.Generic.List[object]]::new()
$failures = [System.Collections.Generic.List[object]]::new()
for ($index = 0; $index -lt $events.Count; $index++) {
    $entry = $events[$index]
    if ($entry.event -eq '00000003') { $failures.Add($entry) }
    if ($entry.event -ne '00000001') { continue }
    $completion = $null
    for ($next = $index + 1; $next -lt $events.Count; $next++) {
        $candidate = $events[$next]
        if ($candidate.invocation -ne $entry.invocation -or $candidate.stage -ne $entry.stage) { continue }
        if ($candidate.event -eq '00000001') { break }
        if ($candidate.event -in @('00000002','00000003')) { $completion = $candidate; break }
    }
    $status = if ($null -eq $completion) { 'begin_without_completion' }
        elseif ($completion.event -eq '00000002') { 'returned_success' }
        else { 'returned_failure' }
    $pairedStages.Add([pscustomobject]@{
        invocation=$entry.invocation; stage=$entry.stage; stageName=$entry.stageName
        beginSequence=$entry.sequence; status=$status
        completionSequence=if ($null -ne $completion) { $completion.sequence } else { $null }
        result=if ($null -ne $completion) { $completion.code } else { $null }
        singletonBitsAtBegin=$entry.singletonBits
        singletonBitsAtCompletion=if ($null -ne $completion) { $completion.singletonBits } else { $null }
    })
}
$rhTerminal = @($events | Where-Object { $_.stage -eq '00000002' -and $_.event -in @('00000002','00000003') } | Select-Object -Last 1)
$rhInitializeResult = if ($rhTerminal.Count -eq 0) { $null } elseif ($rhTerminal[0].event -eq '00000002') { 'success' } else { 'failure' }
$traceEnd = if ($traceEnds.Count -eq 1) { $traceEnds[0] } else { $null }
$serialEventCount = [uint32]$events.Count
$traceRecordCountMatches = $null -ne $traceEnd -and ([Convert]::ToUInt32($traceEnd.recordCount,16) -eq $serialEventCount)
$traceRingOverflow = if ($null -ne $traceEnd) { [Convert]::ToUInt32($traceEnd.overflow,16) } else { $null }
$serialSinkDropped = if ($null -ne $traceEnd) { [Convert]::ToUInt32($traceEnd.serialDropped,16) } else { $null }
$initTraceComplete = $null -ne $traceEnd -and $traceEnds.Count -eq 1 -and
    $hasTerminalNewline -and $null -eq $decodeError -and $traceRecordCountMatches -and
    $traceRingOverflow -eq 0 -and $serialSinkDropped -eq 0 -and
    @($events | Where-Object { $_.invocation -eq $traceEnd.invocation }).Count -eq $serialEventCount
$transportAvailable = @($events | Where-Object { $_.stage -eq '00000001' }).Count -gt 0
$runtimeInstanceStage = @($pairedStages | Where-Object { $_.stage -eq '00000024' -and $_.status -eq 'returned_success' } | Select-Object -Last 1)
$runtimeSingletonReady = $null
$runtimeSingletonMissingBits = $null
if ($runtimeInstanceStage.Count -gt 0 -and $runtimeInstanceStage[0].singletonBitsAtCompletion -match '^[0-9A-F]{8}$') {
    $bits = [Convert]::ToUInt32($runtimeInstanceStage[0].singletonBitsAtCompletion,16)
    $runtimeSingletonReady = ($bits -band 3) -eq 3
    $runtimeSingletonMissingBits = 0
    if (($bits -band 1) -eq 0) { $runtimeSingletonMissingBits = $runtimeSingletonMissingBits -bor 1 }
    if (($bits -band 2) -eq 0) { $runtimeSingletonMissingBits = $runtimeSingletonMissingBits -bor 2 }
}
$gcPublicationStage = @($pairedStages | Where-Object { $_.stage -eq '00000031' -and $_.status -eq 'returned_success' } | Select-Object -Last 1)
$gcSingletonReady = $null
$gcSingletonMissingBits = $null
if ($gcPublicationStage.Count -gt 0 -and $gcPublicationStage[0].singletonBitsAtCompletion -match '^[0-9A-F]{8}$') {
    $bits = [Convert]::ToUInt32($gcPublicationStage[0].singletonBitsAtCompletion,16)
    $gcSingletonReady = ($bits -band 12) -eq 12
    $gcSingletonMissingBits = 0
    if (($bits -band 4) -eq 0) { $gcSingletonMissingBits = $gcSingletonMissingBits -bor 4 }
    if (($bits -band 8) -eq 0) { $gcSingletonMissingBits = $gcSingletonMissingBits -bor 8 }
}
$managedMainDispatched = $managedCalls.Count -gt 0
$managedMainReturned = $managedReturns.Count -gt 0
$managedMainSuppressedOnInitializationFailure = $rhInitializeResult -eq 'failure' -and -not $managedMainDispatched
$firstFailure = if ($failures.Count -gt 0) {
    $failure = $failures[0]
    [ordered]@{sequence=$failure.sequence;stage=$failure.stage;stageName=$failure.stageName;code=$failure.code;singletonBits=$failure.singletonBits}
} else { $null }
$analysis = [ordered]@{
    schemaVersion=1
    marker='C188R12R4R5R1_INIT'
    rawSerialPath=$serialFull
    rawSerialSha256=$serialSha256
    rawSerialBytes=$raw.Length
    rawSerialHasTerminalNewline=$hasTerminalNewline
    utf8DecodeError=$decodeError
    traceTransportAvailable=$transportAvailable
    traceRecordCount=$serialEventCount
    traceEndCount=$traceEnds.Count
    traceEnd=$traceEnd
    traceRecordCountMatches=$traceRecordCountMatches
    traceRingOverflow=$traceRingOverflow
    traceSerialSinkDropped=$serialSinkDropped
    guestTimedOut=[bool]$GuestTimedOut
    initTraceComplete=[bool]$initTraceComplete
    rhInitializeResult=$rhInitializeResult
    earliestExplicitFailure=$firstFailure
    runtimeInstanceSingletonsReady=$runtimeSingletonReady
    runtimeInstanceMissingSingletonBits=$runtimeSingletonMissingBits
    gcHeapAndHandlePublicationReady=$gcSingletonReady
    gcPublicationMissingSingletonBits=$gcSingletonMissingBits
    runtimeThreadStoreAttachmentQualified=$false
    managedMainGateLines=@($managedGates)
    managedMainDispatched=$managedMainDispatched
    managedMainReturned=$managedMainReturned
    managedMainSuppressedOnInitializationFailure=$managedMainSuppressedOnInitializationFailure
    stages=@($pairedStages)
    events=@($events)
}
$parent = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$json = $analysis | ConvertTo-Json -Depth 10
[System.IO.File]::WriteAllText($outputFull,$json,[System.Text.UTF8Encoding]::new($false))
Write-Output "C188R12R4R5R1_INIT_ANALYSIS=$outputFull COMPLETE=$initTraceComplete RHINITIALIZE=$rhInitializeResult"
