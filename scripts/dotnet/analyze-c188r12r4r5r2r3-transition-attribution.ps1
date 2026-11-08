param(
    [Parameter(Mandatory=$true)][string]$SerialPath,
    [Parameter(Mandatory=$true)][string]$CallSiteMapPath,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
foreach ($path in @($SerialPath,$CallSiteMapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Transition analyzer input not found: $path" }
}
$serialFullPath = [System.IO.Path]::GetFullPath($SerialPath)
$mapFullPath = [System.IO.Path]::GetFullPath($CallSiteMapPath)
$serialHashBefore = (Get-FileHash -LiteralPath $serialFullPath -Algorithm SHA256).Hash.ToUpperInvariant()
$raw = [System.IO.File]::ReadAllText($serialFullPath)
$map = Get-Content -LiteralPath $mapFullPath -Raw | ConvertFrom-Json
$siteById = @{}
foreach ($site in @($map.sites)) {
    $candidateRip = [string]$site.callerReturnAddress
    if ($candidateRip -match '^(?:0x)?[0-9A-Fa-f]{1,16}$' -and
        ($candidateRip -replace '^0x','') -notmatch '^0+$') {
        $siteById[[string]$site.id] = $site
    }
}

function Get-Fields([string]$Line,[string]$Marker) {
    $prefix = "[$Marker] "
    if (-not $Line.StartsWith($prefix,[System.StringComparison]::Ordinal)) { return $null }
    $fields = [ordered]@{}
    foreach ($token in ($Line.Substring($prefix.Length).Trim() -split '\s+')) {
        if ($token -notmatch '^(?<key>[A-Za-z][A-Za-z0-9]*)=(?<value>[^\s]+)$') { return $null }
        $key = $Matches.key
        if ($fields.Contains($key)) { return $null }
        $fields[$key] = $Matches.value
    }
    return $fields
}
function Get-UInt64Hex([string]$Value) {
    if ($Value -notmatch '^(?:0x)?[0-9A-Fa-f]{1,16}$') { return $null }
    $token = $Value -replace '^0x',''
    try {
        return [ordered]@{
            wireToken=$Value
            value=[UInt64]::Parse($token,[System.Globalization.NumberStyles]::HexNumber,
                [System.Globalization.CultureInfo]::InvariantCulture)
            widthBits=$token.Length * 4
        }
    } catch { return $null }
}
function Get-UInt32Hex([string]$Value) {
    if ($Value -notmatch '^(?:0x)?[0-9A-Fa-f]{1,8}$') { return $null }
    $token = $Value -replace '^0x',''
    try {
        return [UInt32]::Parse($token,[System.Globalization.NumberStyles]::HexNumber,
            [System.Globalization.CultureInfo]::InvariantCulture)
    } catch { return $null }
}
function Normalize-UInt64Hex([string]$Value) {
    $parsed = Get-UInt64Hex $Value
    if ($null -eq $parsed) { return $null }
    return ([UInt64]$parsed.value).ToString('X16',[System.Globalization.CultureInfo]::InvariantCulture)
}

$fatalLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_FATAL\] [^\r\n]*') |
    ForEach-Object { $_.Value })
$stateLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_STATE\] [^\r\n]*') |
    ForEach-Object { $_.Value })
$failFastLines = @([regex]::Matches($raw,'(?m)^\[nativeaot-pal-qemu-test\] FAIL_FAST reason=[0-9A-Fa-f]{8} detail=[0-9A-Fa-f]{16}\r?$') |
    ForEach-Object { $_.Value })
$transitionLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_TRANSITION\] [^\r\n]*') |
    ForEach-Object { $_.Value })
$recordInvalid = $raw.Contains('[C188R12R4R5R2R3_FATAL] record=invalid')
$recordMissing = $raw.Contains('[C188R12R4R5R2R3_FATAL] record=missing')
$record = $null
$state = $null
$records = @()
$states = @()
$parseError = $null

$requiredFatalFields = @('valid','seq','snapshot','attempted','completed','gate','fatal','reason',
    'callSite','callerRip','msgState','msgBytes','msgTruncated','originalStatusAvailable',
    'originalStatus','firstFailureStage','firstFailureStatusAvailable','firstFailureStatus',
    'lastSuccess','initOverflow','threadOverflow')
$requiredStateFields = @('runtime','rhInitialize','threadStore','rawThread','currentThread','attachment',
    'nativeThreadId','coop','tlsReady','tlsIndex','palFlsIndex','palFlsValue','stackReady',
    'stackCurrent','stackLow','stackHigh','frame','frameReady','frameAligned','frameInStack',
    'stockWillAttach','gcReady','module','codeManager','flsState','modulesNotInitialized')
if ($recordInvalid) { $parseError = 'record_integrity_marker_invalid' }
elseif ($recordMissing) { $parseError = 'record_missing' }
elseif ($fatalLines.Count -eq 0) { $parseError = 'fatal_record_missing' }
elseif ($fatalLines.Count -ne $stateLines.Count) { $parseError = 'fatal_state_snapshot_count_mismatch' }
else {
    for ($i = 0; $i -lt $fatalLines.Count; $i++) {
        $currentRecord = Get-Fields $fatalLines[$i] 'C188R12R4R5R2R3_FATAL'
        $currentState = Get-Fields $stateLines[$i] 'C188R12R4R5R2R3_STATE'
        if ($null -eq $currentRecord) { $parseError = 'fatal_record_malformed'; break }
        if ($null -eq $currentState) { $parseError = 'state_record_malformed'; break }
        foreach ($field in $requiredFatalFields) {
            if (-not $currentRecord.Contains($field)) {
                $parseError = "missing_fatal_field_$field"
                break
            }
        }
        if ($null -ne $parseError) { break }
        if ($currentRecord.valid -cne '1') { $parseError = 'record_integrity_marker_invalid'; break }
        foreach ($field in @('seq','callerRip')) {
            if ($null -eq (Get-UInt64Hex ([string]$currentRecord[$field]))) {
                $parseError = "malformed_fatal_field_$field"
                break
            }
        }
        if ($null -ne $parseError) { break }
        foreach ($field in @('snapshot','attempted','completed','gate','fatal','reason','callSite',
                'msgState','msgBytes','msgTruncated','originalStatusAvailable','originalStatus',
                'firstFailureStage','firstFailureStatusAvailable','firstFailureStatus','lastSuccess',
                'initOverflow','threadOverflow')) {
            if ($null -eq (Get-UInt32Hex ([string]$currentRecord[$field]))) {
                $parseError = "malformed_fatal_field_$field"
                break
            }
        }
        if ($null -ne $parseError) { break }
        foreach ($field in $requiredStateFields) {
            if (-not $currentState.Contains($field)) {
                $parseError = "missing_state_field_$field"
                break
            }
            if ($null -eq (Get-UInt64Hex ([string]$currentState[$field]))) {
                $parseError = "malformed_state_field_$field"
                break
            }
        }
        if ($null -ne $parseError) { break }
        $attempted = Get-UInt32Hex ([string]$currentRecord.attempted)
        $completed = Get-UInt32Hex ([string]$currentRecord.completed)
        $fatal = Get-UInt32Hex ([string]$currentRecord.fatal)
        if ($attempted -gt 1 -or $completed -gt 1 -or $fatal -gt 1 -or $completed -gt $attempted) {
            $parseError = 'transition_record_boolean_or_order_invalid'
            break
        }
        $records += ,$currentRecord
        $states += ,$currentState
    }
    if ($null -eq $parseError -and $records.Count -gt 0) {
        for ($i = 1; $i -lt $records.Count; $i++) {
            $previous = $records[$i - 1]
            $current = $records[$i]
            $previousSeq = (Get-UInt64Hex ([string]$previous.seq)).value
            $currentSeq = (Get-UInt64Hex ([string]$current.seq)).value
            $previousAttempted = Get-UInt32Hex ([string]$previous.attempted)
            $currentAttempted = Get-UInt32Hex ([string]$current.attempted)
            $previousCompleted = Get-UInt32Hex ([string]$previous.completed)
            $currentCompleted = Get-UInt32Hex ([string]$current.completed)
            $previousFatal = Get-UInt32Hex ([string]$previous.fatal)
            $currentFatal = Get-UInt32Hex ([string]$current.fatal)
            if ($currentSeq -lt $previousSeq -or $currentAttempted -lt $previousAttempted -or
                $currentCompleted -lt $previousCompleted -or $currentFatal -lt $previousFatal) {
                $parseError = 'transition_snapshot_progression_regressed'
                break
            }
            if ($currentSeq -eq $previousSeq -and $current.snapshot -cne $previous.snapshot) {
                $parseError = 'same_sequence_snapshot_status_disagrees'
                break
            }
            if ($previousFatal -eq 1 -and $currentFatal -eq 1 -and
                ($current.reason -cne $previous.reason -or $current.callSite -cne $previous.callSite -or
                    $current.callerRip -cne $previous.callerRip)) {
                $parseError = 'fatal_snapshot_attribution_disagrees'
                break
            }
        }
    }
    if ($null -eq $parseError -and $records.Count -gt 0) {
        $record = $records[$records.Count - 1]
        $state = $states[$states.Count - 1]
    }
}

$resolvedSite = $null
$resolvedAttributionMethod = $null
$callerRip = $null
if ($null -ne $record -and $null -eq $parseError) {
    for ($i = 0; $i -lt $records.Count; $i++) {
        $candidateRecord = $records[$i]
        if ((Get-UInt32Hex ([string]$candidateRecord.fatal)) -ne 1) { continue }
        $candidateRip = Get-UInt64Hex ([string]$candidateRecord.callerRip)
        if ($null -eq $candidateRip) { $parseError = 'caller_rip_malformed'; break }
        if ($candidateRecord.callSite -ne 'FFFFFFFE') {
            $candidateId = [string][Convert]::ToUInt32($candidateRecord.callSite,16)
            if (-not $siteById.ContainsKey($candidateId)) { $parseError = 'unknown_callsite_id'; break }
            $candidateSite = $siteById[$candidateId]
            $expectedRip = ([string]$candidateSite.callerReturnAddress -replace '^0x','').ToUpperInvariant()
            $observedRip = ([string]$candidateRip.wireToken -replace '^0x','').ToUpperInvariant()
            if ($expectedRip -cne $observedRip) { $parseError = 'callsite_id_rip_mismatch'; break }
            $resolvedSite = $candidateSite
            $resolvedAttributionMethod = 'stable-call-site-id'
            $callerRip = $candidateRip
        } else {
            $callerRip = $candidateRip
            $observedRip = Normalize-UInt64Hex ([string]$candidateRip.wireToken)
            $exactMatches = @($map.sites | Where-Object {
                [bool]$_.linkedInCandidate -and
                [string]$_.resolutionStatus -ceq 'exact-direct-call-mapped' -and
                (Normalize-UInt64Hex ([string]$_.callerReturnAddress)) -ceq $observedRip
            })
            if ($exactMatches.Count -gt 1) {
                $parseError = 'captured_rip_ambiguous_callsite'
                break
            }
            if ($exactMatches.Count -eq 1) {
                if ($null -ne $resolvedSite -and [string]$resolvedSite.id -cne [string]$exactMatches[0].id) {
                    $parseError = 'fatal_callsite_attribution_disagrees'
                    break
                }
                $resolvedSite = $exactMatches[0]
                $resolvedAttributionMethod = 'exact-captured-return-rip'
            }
        }
    }
}

$runtimeFailFast = $null
if ($failFastLines.Count -eq 1) {
    $runtimeFailFast = [regex]::Match($failFastLines[0],'reason=(?<reason>[0-9A-Fa-f]{8})').Groups['reason'].Value.ToUpperInvariant()
} elseif ($failFastLines.Count -gt 1 -and $null -eq $parseError) { $parseError = 'duplicate_runtime_failfast' }
$transition = if ($transitionLines.Count -eq 1) {
    Get-Fields $transitionLines[0] 'C188R12R4R5R2R3_TRANSITION'
} else { $null }
$transitionSnapshots = @(
    for ($i = 0; $i -lt [Math]::Min($records.Count,$states.Count); $i++) {
        [ordered]@{ record=$records[$i]; state=$states[$i] }
    }
)
$snapshotValue = if ($null -ne $record) { Get-UInt32Hex ([string]$record.snapshot) } else { $null }
$attemptedValue = if ($null -ne $record) { Get-UInt32Hex ([string]$record.attempted) } else { $null }
$completedValue = if ($null -ne $record) { Get-UInt32Hex ([string]$record.completed) } else { $null }
$fatalValue = if ($null -ne $record) { Get-UInt32Hex ([string]$record.fatal) } else { $null }
$recordReason = if ($null -ne $record) { [string]$record.reason } else { $null }

$classification = 'TRANSITION_ATTRIBUTION_INCOMPLETE'
$meaning = 'No trustworthy complete transition record was available.'
if ($null -ne $parseError -or $recordInvalid -or $recordMissing) {
    $meaning = if ($null -ne $parseError) { $parseError } elseif ($recordInvalid) { 'record_integrity_marker_invalid' } else { 'record_missing' }
} elseif ($null -ne $record -and $null -ne $state) {
    if ($null -ne $runtimeFailFast) {
        if ($fatalValue -eq [uint32]1 -and $recordReason -eq $runtimeFailFast) {
            $classification = if ($null -ne $resolvedSite) {
                'RUNTIME_FAIL_FAST_ATTRIBUTED'
            } else { 'RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED' }
            $meaning = '0x50414C06 is the PAL fail-fast code; the call-site ID may identify the source condition, while the underlying runtime defect remains an inference.'
        } else { $meaning = 'runtime_failfast_and_fatal_record_disagree' }
    } elseif ($snapshotValue -eq [uint32]2 -and $attemptedValue -eq [uint32]0 -and
        $fatalValue -eq [uint32]0 -and $null -ne $transition -and $transition.result -eq 'safe_stop') {
        $classification = 'TRANSITION_GATE_BLOCKED_SAFE_STOP'
        $meaning = 'The preflight rejected a prerequisite before entering InitializeModules.'
    } elseif ($completedValue -eq [uint32]1 -and $fatalValue -eq [uint32]0 -and
        $null -ne $transition -and $transition.result -eq 'initialize_modules_returned') {
        $classification = 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED'
        $meaning = 'InitializeModules returned; this analyzer does not claim managed entry or GC acceptance.'
    } else { $meaning = 'transition state did not match a safe-stop, attributed-failfast, or returned-transition form' }
}

$serialHashAfter = (Get-FileHash -LiteralPath $serialFullPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($serialHashBefore -cne $serialHashAfter -and $null -eq $parseError) {
    $parseError = 'raw_serial_changed_during_analysis'
}
$classification = if ($null -ne $parseError) { 'TRANSITION_ATTRIBUTION_INCOMPLETE' } else { $classification }
$report = [ordered]@{
    analyzer='C188R12R4R5R2R3-transition-attribution-v1'
    classification=$classification
    meaning=$meaning
    evidencePath=$serialFullPath
    evidenceSha256Before=$serialHashBefore
    evidenceSha256After=$serialHashAfter
    evidencePreserved=($serialHashBefore -eq $serialHashAfter)
    failFast=[ordered]@{ present=($null -ne $runtimeFailFast); code=$runtimeFailFast; underlyingDefectInferred=$false }
    recordIntegrity=if ($null -ne $record) { $record.valid } else { $null }
    transitionRecord=$record
    threadAndRuntimeState=$state
    transitionSnapshots=$transitionSnapshots
    callSite=if ($null -ne $resolvedSite) { [ordered]@{
        id=[int]$resolvedSite.id; key=[string]$resolvedSite.key; name=[string]$resolvedSite.name
        message=[string]$resolvedSite.message; callerReturnAddress=[string]$resolvedSite.callerReturnAddress
    } } else { $null }
    callSiteResolutionMethod=$resolvedAttributionMethod
    callSiteMap=[ordered]@{ path=$mapFullPath; sha256=(Get-FileHash -LiteralPath $mapFullPath -Algorithm SHA256).Hash.ToUpperInvariant() }
    parseError=$parseError
    qemuLaunched=$false
    qacReserved=$false
    managedEntryRequested=$false
    managedCollectionRequested=$false
}
if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
    $parent = Split-Path -Parent $outputFullPath
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    [System.IO.File]::WriteAllText($outputFullPath,($report | ConvertTo-Json -Depth 8),[System.Text.UTF8Encoding]::new($false))
}
$report | ConvertTo-Json -Depth 8
