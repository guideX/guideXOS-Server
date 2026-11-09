param(
    [Parameter(Mandatory=$true)][string]$SerialPath,
    [Parameter(Mandatory=$true)][string]$CallSiteMapPath,
    [string]$TransitionAnalyzerPath = (Join-Path $PSScriptRoot 'analyze-c188r12r4r5r2r3-transition-attribution.ps1'),
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
foreach ($path in @($SerialPath,$CallSiteMapPath,$TransitionAnalyzerPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required analyzer input is missing: $path" }
}
$serialFull = [System.IO.Path]::GetFullPath($SerialPath)
$mapFull = [System.IO.Path]::GetFullPath($CallSiteMapPath)
$transitionFull = [System.IO.Path]::GetFullPath($TransitionAnalyzerPath)
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
$serialHashBefore = (Get-FileHash -LiteralPath $serialFull -Algorithm SHA256).Hash.ToUpperInvariant()
$transitionPath = $outputFull + '.transition.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $transitionFull `
    -SerialPath $serialFull -CallSiteMapPath $mapFull -OutputPath $transitionPath | Out-Null
$transitionExitCode = $LASTEXITCODE
if ($transitionExitCode -ne 0 -or -not (Test-Path -LiteralPath $transitionPath -PathType Leaf)) {
    throw "R6 transition analysis failed with exit code $transitionExitCode."
}
$transition = Get-Content -LiteralPath $transitionPath -Raw | ConvertFrom-Json -ErrorAction Stop
$raw = [System.IO.File]::ReadAllText($serialFull)

function Get-R12Fields([string]$Line,[string]$Prefix) {
    if (-not $Line.StartsWith($Prefix,[StringComparison]::Ordinal)) { throw "Marker prefix mismatch: $Prefix" }
    $fields = [ordered]@{}
    foreach ($part in $Line.Substring($Prefix.Length).Trim() -split '\s+') {
        if ($part -notmatch '^(?<name>[A-Za-z][A-Za-z0-9]*)=(?<value>\S+)$') { throw "Malformed marker field: $part" }
        if ($fields.Contains($Matches.name)) { throw "Duplicate marker field: $($Matches.name)" }
        $fields[$Matches.name] = $Matches.value
    }
    return $fields
}

$entryPrefix = '[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY] '
$allocationPrefix = '[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] '
$entryLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY\] [^\r\n]*') | ForEach-Object { $_.Value })
$attemptLines = @($entryLines | Where-Object { $_ -match '^\[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY\] event=attempt ' })
$returnedLines = @($entryLines | Where-Object { $_ -match '^\[C188R12R4R5R4R5R2R2R4R2R3R12_ENTRY\] event=returned ' })
$stateLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_ENTRY_STATE\] [^\r\n]*') | ForEach-Object { $_.Value })
$allocationLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC\] [^\r\n]*') | ForEach-Object { $_.Value })
$attempt = $null; $returned = $null; $state = $null; $allocation = $null
$managedEntryAttempted = $attemptLines.Count -eq 1
$managedEntryReturned = $false
$runtimeStateReady = $false
$allocationSucceeded = $false
$allocationEvidenceValid = $false
$parseError = $null
$classification = 'MANAGED_SETTINGS_OBJECT_INCOMPLETE'

try {
    if ($attemptLines.Count -gt 1 -or $returnedLines.Count -gt 1 -or $stateLines.Count -gt 1 -or $allocationLines.Count -gt 1) {
        throw 'duplicate_r12_probe_marker'
    }
    if ($attemptLines.Count -eq 1) { $attempt = Get-R12Fields $attemptLines[0] $entryPrefix }
    if ($returnedLines.Count -eq 1) { $returned = Get-R12Fields $returnedLines[0] ($entryPrefix + 'event=returned') }
    if ($stateLines.Count -eq 1) { $state = Get-R12Fields $stateLines[0] '[C188R12R4R5R2R3_ENTRY_STATE]' }
    if ($allocationLines.Count -eq 1) { $allocation = Get-R12Fields $allocationLines[0] $allocationPrefix }

    if ($null -ne $attempt -and
        ($attempt.abi -cne 'UnmanagedCallersOnly-Cdecl' -or $attempt.seed -cne '0000000E' -or
         $attempt.symbol -cne 'ManagedMain' -or $attempt.model -cne 'DisplaySettingsProbe' -or
         $attempt.collection -cne 'not_requested')) {
        throw 'r12_entry_attempt_contract_mismatch'
    }
    if ($null -ne $returned) {
        $managedEntryReturned = $true
        $entryOrder = $raw.IndexOf($returnedLines[0],[StringComparison]::Ordinal)
        $attemptOrder = if ($managedEntryAttempted) { $raw.IndexOf($attemptLines[0],[StringComparison]::Ordinal) } else { -1 }
        $allocationOrder = if ($allocationLines.Count -eq 1) { $raw.IndexOf($allocationLines[0],[StringComparison]::Ordinal) } else { -1 }
        $stateOrder = if ($stateLines.Count -eq 1) { $raw.IndexOf($stateLines[0],[StringComparison]::Ordinal) } else { -1 }
        if (-not $managedEntryAttempted -or $attemptOrder -lt 0 -or $attemptOrder -ge $entryOrder -or
            $returned.entry -cne '52313245' -or $returned.input -cne '0000000E' -or
            $returned.computed -cne '00000462' -or $returned.completion -cne '52313244' -or
            $returned.return -cne '00000462' -or $returned.expected -cne '00000462' -or
            $returned.record -cne 'PASS' -or $returned.collection -cne 'not_requested' -or
            $returned.model -cne 'DisplaySettingsProbe' -or $allocationOrder -le $attemptOrder -or
            $allocationOrder -ge $entryOrder -or $stateOrder -le $entryOrder) {
            throw 'r12_managed_entry_return_contract_mismatch'
        }
        $allocationSucceeded = $null -ne $allocation
        if ($null -ne $allocation) {
            $numericFields = @('requests','completed','objectSize','contextReadyBefore','contextReadyAfter',
                'contextBytesBefore','contextBytesAfter','contextAdvanced','methodTable','gcHeapOwned',
                'threadAttached','gcCountBefore','gcCountAfter','status','collectionRequestedByProbe')
            foreach ($field in $numericFields) {
                if (-not $allocation.Contains($field) -or $allocation[$field] -notmatch '^[0-9A-Fa-f]{1,16}$') {
                    throw "r12_allocation_field_invalid_$field"
                }
            }
            $allocationEvidenceValid = $allocation.helper -ceq 'RhpNewFast' -and
                $allocation.stock -ceq 'guideXosStockRhpNewFast' -and
                $allocation.requests -ceq '00000001' -and $allocation.completed -ceq '00000001' -and
                $allocation.objectSize -match '^(?!0+$)[0-9A-Fa-f]+$' -and
                $allocation.contextReadyBefore -ceq '00000001' -and
                $allocation.contextReadyAfter -ceq '00000001' -and
                $allocation.methodTable -ceq '00000001' -and $allocation.gcHeapOwned -ceq '00000001' -and
                $allocation.threadAttached -ceq '00000001' -and
                $allocation.status -ceq '00000000' -and
                $allocation.collectionRequestedByProbe -ceq '00000000'
            $allocationSucceeded = $allocationEvidenceValid
        }
        if ($null -ne $state) {
            $thread = [Convert]::ToUInt64([string]$state.currentThread,16)
            $fls = [Convert]::ToUInt64([string]$state.palFlsThread,16)
            $threadId = [Convert]::ToUInt64([string]$state.threadId,16)
            $observedThreadId = [Convert]::ToUInt64([string]$state.observedThreadId,16)
            $stackLow = [Convert]::ToUInt64([string]$state.stackLow,16)
            $stackHigh = [Convert]::ToUInt64([string]$state.stackHigh,16)
            $stackCurrent = [Convert]::ToUInt64([string]$state.stackCurrent,16)
            $flags = [Convert]::ToUInt32([string]$state.flags,16)
            $runtimeStateReady = $state.status -ceq '00000000' -and $state.bytes -ceq '00000070' -and
                $state.membership -ceq '00000001' -and ($flags -band 1u) -ne 0u -and
                $state.cooperativeState -ceq '00000001' -and $state.preemptiveFrame -ceq '00000001' -and
                $state.flsMatch -ceq '00000001' -and $thread -ne 0u -and $thread -eq $fls -and
                $threadId -ne 0u -and $threadId -eq $observedThreadId -and
                $state.stackReady -ceq '00000001' -and $state.stackInBounds -ceq '00000001' -and
                $stackLow -lt $stackCurrent -and $stackCurrent -lt $stackHigh -and
                $state.modules -ceq '00000001' -and $state.codeManager -ceq '00000001' -and
                $state.frame -ceq 'FFFFFFFFFFFFFFFF' -and $state.result -ceq 'PASS'
        }
        if ([string]$transition.classification -eq 'RUNTIME_FAIL_FAST_ATTRIBUTED' -or
            [string]$transition.classification -eq 'RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED') {
            $classification = 'MANAGED_SETTINGS_OBJECT_RUNTIME_FAIL_FAST_AFTER_ENTRY'
        } elseif ($allocationSucceeded -and $runtimeStateReady -and
            [string]$transition.classification -eq 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED' -and
            [string]$transition.recordIntegrity -ceq '1' -and
            [string]::IsNullOrWhiteSpace([string]$transition.parseError)) {
            $classification = 'MANAGED_SETTINGS_OBJECT_RETURNED_RUNTIME_STATE_READY'
        } else {
            $classification = 'MANAGED_SETTINGS_OBJECT_RETURNED_MISMATCH'
        }
    } elseif ($null -ne $transition -and [string]$transition.classification -eq 'TRANSITION_GATE_BLOCKED_SAFE_STOP' -and
        [string]$transition.recordIntegrity -ceq '1' -and [string]::IsNullOrWhiteSpace([string]$transition.parseError)) {
        $classification = 'MANAGED_SETTINGS_OBJECT_BLOCKED_BEFORE_CALL'
    } elseif ($null -ne $transition -and [string]$transition.classification -in @('RUNTIME_FAIL_FAST_ATTRIBUTED','RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED')) {
        $classification = 'MANAGED_SETTINGS_OBJECT_RUNTIME_FAIL_FAST_BEFORE_ENTRY'
    }
} catch {
    $parseError = $_.Exception.Message
    $classification = 'MANAGED_SETTINGS_OBJECT_INCOMPLETE'
}

$serialHashAfter = (Get-FileHash -LiteralPath $serialFull -Algorithm SHA256).Hash.ToUpperInvariant()
if ($serialHashBefore -cne $serialHashAfter) {
    $parseError = 'raw_serial_changed_during_analysis'
    $classification = 'MANAGED_SETTINGS_OBJECT_INCOMPLETE'
}
$report = [ordered]@{
    analyzer='C188R12R4R5R4R5R2R2R4R2R3R12-managed-settings-object-v1'
    classification=$classification
    evidencePath=$serialFull
    evidenceSha256Before=$serialHashBefore
    evidenceSha256After=$serialHashAfter
    evidencePreserved=($serialHashBefore -ceq $serialHashAfter)
    transitionClassification=[string]$transition.classification
    transitionRecord=$transition.transitionRecord
    threadAndRuntimeState=$transition.threadAndRuntimeState
    callSite=$transition.callSite
    callSiteMap=[ordered]@{path=$mapFull;sha256=(Get-FileHash -LiteralPath $mapFull -Algorithm SHA256).Hash.ToUpperInvariant()}
    managedEntryAttempted=$managedEntryAttempted
    managedEntryReturned=$managedEntryReturned
    managedEntryReturnValue=if ($null -ne $returned) { $returned.return } else { $null }
    managedEntryRecord=$returned
    runtimeStateReady=$runtimeStateReady
    runtimeState=$state
    allocationSucceeded=$allocationSucceeded
    allocationEvidenceValid=$allocationEvidenceValid
    allocationEvidence=$allocation
    implicitCollectionObserved=if ($null -ne $allocation) { $allocation.gcCountBefore -cne $allocation.gcCountAfter } else { $null }
    managedCollectionRequested=$false
    parseError=$parseError
    transitionAnalysisPath=$transitionPath
    qemuLaunched=$false
    qacReserved=$false
}
$parent = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
[System.IO.File]::WriteAllText($outputFull,($report | ConvertTo-Json -Depth 10),[System.Text.UTF8Encoding]::new($false))
$report | ConvertTo-Json -Depth 10
