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
foreach ($site in @($map.sites)) { $siteById[[string]$site.id] = $site }

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

$fatalLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_FATAL\] [^\r\n]*') |
    ForEach-Object { $_.Value })
$stateLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_STATE\] [^\r\n]*') |
    ForEach-Object { $_.Value })
$failFastLines = @([regex]::Matches($raw,'(?m)^\[nativeaot-pal-qemu-test\] FAIL_FAST reason=[0-9A-Fa-f]{8} detail=[0-9A-Fa-f]{16}$') |
    ForEach-Object { $_.Value })
$transitionLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_TRANSITION\] [^\r\n]*') |
    ForEach-Object { $_.Value })
$recordInvalid = $raw.Contains('[C188R12R4R5R2R3_FATAL] record=invalid')
$recordMissing = $raw.Contains('[C188R12R4R5R2R3_FATAL] record=missing')
$record = $null
$state = $null
$parseError = $null
if ($fatalLines.Count -eq 1) {
    $record = Get-Fields $fatalLines[0] 'C188R12R4R5R2R3_FATAL'
    if ($null -eq $record) { $parseError = 'fatal_record_malformed' }
} elseif ($fatalLines.Count -gt 1) { $parseError = 'duplicate_fatal_records' }
if ($stateLines.Count -eq 1) {
    $state = Get-Fields $stateLines[0] 'C188R12R4R5R2R3_STATE'
    if ($null -eq $state -and $null -eq $parseError) { $parseError = 'state_record_malformed' }
} elseif ($stateLines.Count -gt 1 -and $null -eq $parseError) { $parseError = 'duplicate_state_records' }

$requiredFatalFields = @('valid','seq','snapshot','attempted','completed','gate','fatal','reason',
    'callSite','callerRip','msgState','msgBytes','msgTruncated','originalStatusAvailable',
    'originalStatus','firstFailureStage','firstFailureStatusAvailable','firstFailureStatus',
    'lastSuccess','initOverflow','threadOverflow')
$requiredStateFields = @('runtime','rhInitialize','threadStore','rawThread','currentThread','attachment',
    'nativeThreadId','coop','tlsReady','tlsIndex','palFlsIndex','palFlsValue','stackReady',
    'stackCurrent','stackLow','stackHigh','frame','frameReady','frameAligned','frameInStack',
    'stockWillAttach','gcReady','module','codeManager','flsState','modulesNotInitialized')
if ($null -ne $record) {
    foreach ($field in $requiredFatalFields) {
        if (-not $record.Contains($field) -and $null -eq $parseError) { $parseError = "missing_fatal_field_$field" }
    }
    if ($record.valid -cne '1' -and $null -eq $parseError) { $parseError = 'record_integrity_marker_invalid' }
    foreach ($field in @('snapshot','attempted','completed','fatal','reason','callSite')) {
        if ($null -eq (Get-UInt32Hex ([string]$record[$field])) -and $null -eq $parseError) {
            $parseError = "malformed_fatal_field_$field"
        }
    }
    if ($null -ne $state) {
        foreach ($field in $requiredStateFields) {
            if (-not $state.Contains($field) -and $null -eq $parseError) { $parseError = "missing_state_field_$field" }
        }
    } elseif ($null -eq $parseError) { $parseError = 'state_record_missing' }
}

$resolvedSite = $null
$callerRip = $null
if ($null -ne $record -and $null -eq $parseError) {
    $callerRip = Get-UInt64Hex ([string]$record.callerRip)
    if ($null -eq $callerRip) { $parseError = 'caller_rip_malformed' }
    elseif ($record.callSite -ne 'FFFFFFFE') {
        if ($siteById.ContainsKey([string][Convert]::ToUInt32($record.callSite,16))) {
            $resolvedSite = $siteById[[string][Convert]::ToUInt32($record.callSite,16)]
            $expectedRip = ([string]$resolvedSite.callerReturnAddress -replace '^0x','').ToUpperInvariant()
            $observedRip = ([string]$callerRip.wireToken -replace '^0x','').ToUpperInvariant()
            if ($expectedRip -cne $observedRip) {
                $parseError = 'callsite_id_rip_mismatch'
            }
        } else { $parseError = 'unknown_callsite_id' }
    }
}

$runtimeFailFast = $null
if ($failFastLines.Count -eq 1) {
    $runtimeFailFast = [regex]::Match($failFastLines[0],'reason=(?<reason>[0-9A-Fa-f]{8})').Groups['reason'].Value.ToUpperInvariant()
} elseif ($failFastLines.Count -gt 1 -and $null -eq $parseError) { $parseError = 'duplicate_runtime_failfast' }
$transition = if ($transitionLines.Count -eq 1) {
    Get-Fields $transitionLines[0] 'C188R12R4R5R2R3_TRANSITION'
} else { $null }
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
    callSite=if ($null -ne $resolvedSite) { [ordered]@{
        id=[int]$resolvedSite.id; key=[string]$resolvedSite.key; name=[string]$resolvedSite.name
        message=[string]$resolvedSite.message; callerReturnAddress=[string]$resolvedSite.callerReturnAddress
    } } else { $null }
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
