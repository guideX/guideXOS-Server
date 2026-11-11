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
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
$serialHashBefore = (Get-FileHash -LiteralPath $serialFull -Algorithm SHA256).Hash.ToUpperInvariant()
$transitionPath = $outputFull + '.transition.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $TransitionAnalyzerPath `
    -SerialPath $serialFull -CallSiteMapPath $mapFull -OutputPath $transitionPath | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $transitionPath -PathType Leaf)) {
    throw "Transition analysis failed with exit code $LASTEXITCODE."
}
$transition = Get-Content -LiteralPath $transitionPath -Raw | ConvertFrom-Json
$raw = [System.IO.File]::ReadAllText($serialFull)

function Get-R13Fields([string]$Line,[string]$Prefix) {
    if (-not $Line.StartsWith($Prefix,[StringComparison]::Ordinal)) { throw "Marker prefix mismatch: $Prefix" }
    $fields = [ordered]@{}
    foreach ($part in $Line.Substring($Prefix.Length).Trim() -split '\s+') {
        if ($part -notmatch '^(?<name>[A-Za-z][A-Za-z0-9]*)=(?<value>\S+)$') { throw "Malformed marker field: $part" }
        if ($fields.Contains($Matches.name)) { throw "Duplicate marker field: $($Matches.name)" }
        $fields[$Matches.name] = $Matches.value
    }
    return $fields
}

$entryPrefix = '[C188R13_GC_ROOT_ENTRY] '
$allocationPrefix = '[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC] '
$nativePrefix = '[C188R13_GC_ROOT_NATIVE] '
$statePrefix = '[C188R12R4R5R2R3_ENTRY_STATE] '
$entryLines = @([regex]::Matches($raw,'(?m)^\[C188R13_GC_ROOT_ENTRY\] [^\r\n]*') | ForEach-Object { $_.Value })
$attemptLines = @($entryLines | Where-Object { $_ -match '^\[C188R13_GC_ROOT_ENTRY\] event=attempt ' })
$returnedLines = @($entryLines | Where-Object { $_ -match '^\[C188R13_GC_ROOT_ENTRY\] event=returned ' })
$allocationLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R4R5R2R2R4R2R3R12_ALLOC\] [^\r\n]*') | ForEach-Object { $_.Value })
$nativeLines = @([regex]::Matches($raw,'(?m)^\[C188R13_GC_ROOT_NATIVE\] [^\r\n]*') | ForEach-Object { $_.Value })
$stateLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_ENTRY_STATE\] [^\r\n]*') | ForEach-Object { $_.Value })
$attempt=$null; $returned=$null; $allocation=$null; $native=$null; $state=$null
$parseError=$null; $managedEntryReturned=$false; $runtimeStateReady=$false
$allocationSucceeded=$false; $gcCollectionStarted=$false; $gcCollectionCompleted=$false
$liveObjectSurvived=$false; $classification='MANAGED_GC_ROOT_INCOMPLETE'

try {
    if ($attemptLines.Count -gt 1 -or $returnedLines.Count -gt 1 -or
        $allocationLines.Count -gt 1 -or $nativeLines.Count -gt 1 -or $stateLines.Count -gt 1) {
        throw 'duplicate_r13_probe_marker'
    }
    if ($attemptLines.Count -eq 1) { $attempt=Get-R13Fields $attemptLines[0] $entryPrefix }
    if ($returnedLines.Count -eq 1) { $returned=Get-R13Fields $returnedLines[0] ($entryPrefix+'event=returned') }
    if ($allocationLines.Count -eq 1) { $allocation=Get-R13Fields $allocationLines[0] $allocationPrefix }
    if ($nativeLines.Count -eq 1) { $native=Get-R13Fields $nativeLines[0] $nativePrefix }
    if ($stateLines.Count -eq 1) { $state=Get-R13Fields $stateLines[0] $statePrefix }

    if ($null -ne $attempt -and ($attempt.abi -cne 'UnmanagedCallersOnly-Cdecl' -or
        $attempt.seed -cne '0000000E' -or $attempt.symbol -cne 'ManagedMain' -or
        $attempt.model -cne 'DisplaySettingsProbe' -or
        $attempt.collection -cne 'one_synchronous_full_request')) {
        throw 'r13_entry_attempt_contract_mismatch'
    }
    if ($null -ne $returned) {
        $managedEntryReturned=$true
        $attemptOrder=if ($attemptLines.Count -eq 1) { $raw.IndexOf($attemptLines[0],[StringComparison]::Ordinal) } else { -1 }
        $allocationOrder=if ($allocationLines.Count -eq 1) { $raw.IndexOf($allocationLines[0],[StringComparison]::Ordinal) } else { -1 }
        $returnedOrder=$raw.IndexOf($returnedLines[0],[StringComparison]::Ordinal)
        $nativeOrder=if ($nativeLines.Count -eq 1) { $raw.IndexOf($nativeLines[0],[StringComparison]::Ordinal) } else { -1 }
        $stateOrder=if ($stateLines.Count -eq 1) { $raw.IndexOf($stateLines[0],[StringComparison]::Ordinal) } else { -1 }
        if ($attemptOrder -lt 0 -or $attemptOrder -ge $allocationOrder -or
            $allocationOrder -ge $returnedOrder -or $returnedOrder -ge $nativeOrder -or
            $nativeOrder -ge $stateOrder) {
            throw 'r13_event_order_contract_mismatch'
        }
        foreach ($name in @('entry','input','initial','request','counterStatusBefore','countBefore',
            'countAfter','counterStatusAfter','completion','final','returnMarker','return','expected')) {
            if (-not $returned.Contains($name) -or $returned[$name] -notmatch '^[0-9A-Fa-f]{1,16}$') {
                throw "r13_return_field_invalid_$name"
            }
        }
        if ($returned.entry -cne '52313345' -or $returned.input -cne '0000000E' -or
            $returned.initial -cne '00000462' -or $returned.request -cne '52313351' -or
            $returned.counterStatusBefore -cne '00000000' -or
            $returned.counterStatusAfter -cne '00000000' -or
            $returned.completion -cne '52313343' -or $returned.final -cne '00000463' -or
            $returned.returnMarker -cne '52313352' -or $returned.return -cne '00000463' -or
            $returned.expected -cne '00000463' -or $returned.record -cne 'PASS' -or
            $returned.collection -cne 'requested') {
            throw 'r13_managed_return_contract_mismatch'
        }
        $before=[Convert]::ToUInt32([string]$returned.countBefore,16)
        $after=[Convert]::ToUInt32([string]$returned.countAfter,16)
        $gcCollectionStarted=$after -gt $before
        $liveObjectSurvived=$returned.final -ceq '00000463'

        foreach ($field in @('requests','completed','objectSize','contextReadyBefore','contextReadyAfter',
            'contextAdvanced','methodTable','gcHeapOwned','threadAttached','status','collectionRequestedByProbe')) {
            if (-not $allocation.Contains($field) -or $allocation[$field] -notmatch '^[0-9A-Fa-f]{1,16}$') {
                throw "r13_allocation_field_invalid_$field"
            }
        }
        $allocationSucceeded=$allocation.helper -ceq 'RhpNewFast' -and
            $allocation.stock -ceq 'guideXosStockRhpNewFast' -and
            $allocation.requests -ceq '00000001' -and $allocation.completed -ceq '00000001' -and
            $allocation.objectSize -match '^(?!0+$)[0-9A-Fa-f]+$' -and
            $allocation.contextReadyBefore -ceq '00000001' -and
            $allocation.contextReadyAfter -ceq '00000001' -and
            $allocation.methodTable -ceq '00000001' -and $allocation.gcHeapOwned -ceq '00000001' -and
            $allocation.threadAttached -ceq '00000001' -and $allocation.status -ceq '00000000'

        if ($null -ne $native) {
            foreach ($field in @('request','counterStatusBefore','countBefore','countAfter',
                'counterStatusAfter','completed','delta','result')) {
                if (-not $native.Contains($field)) { throw "r13_native_field_missing_$field" }
            }
            $gcCollectionCompleted=$native.request -ceq '00000001' -and
                $native.counterStatusBefore -ceq '00000000' -and
                $native.counterStatusAfter -ceq '00000000' -and
                $native.countBefore -ceq $returned.countBefore -and
                $native.countAfter -ceq $returned.countAfter -and
                $native.delta -ceq ($after-$before).ToString('X8') -and
                $native.completed -ceq '00000001' -and $native.result -ceq 'PASS' -and $gcCollectionStarted
        }

        if ($null -ne $state) {
            $thread=[Convert]::ToUInt64([string]$state.currentThread,16)
            $fls=[Convert]::ToUInt64([string]$state.palFlsThread,16)
            $threadId=[Convert]::ToUInt64([string]$state.threadId,16)
            $observedThreadId=[Convert]::ToUInt64([string]$state.observedThreadId,16)
            $stackLow=[Convert]::ToUInt64([string]$state.stackLow,16)
            $stackHigh=[Convert]::ToUInt64([string]$state.stackHigh,16)
            $stackCurrent=[Convert]::ToUInt64([string]$state.stackCurrent,16)
            $flags=[Convert]::ToUInt32([string]$state.flags,16)
            $runtimeStateReady=$state.status -ceq '00000000' -and $state.bytes -ceq '00000070' -and
                $state.membership -ceq '00000001' -and ($flags -band 1u) -ne 0u -and
                $state.cooperativeState -ceq '00000001' -and $state.preemptiveFrame -ceq '00000001' -and
                $state.flsMatch -ceq '00000001' -and $thread -ne 0u -and $thread -eq $fls -and
                $threadId -ne 0u -and $threadId -eq $observedThreadId -and
                $state.stackReady -ceq '00000001' -and $state.stackInBounds -ceq '00000001' -and
                $stackLow -lt $stackCurrent -and $stackCurrent -lt $stackHigh -and
                $state.modules -ceq '00000001' -and $state.codeManager -ceq '00000001' -and
                $state.frame -ceq 'FFFFFFFFFFFFFFFF' -and $state.result -ceq 'PASS'
        }

        $transitionReady=[string]$transition.classification -ceq 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED' -and
            [string]$transition.recordIntegrity -ceq '1' -and
            [string]::IsNullOrWhiteSpace([string]$transition.parseError)
        if ([string]$transition.classification -in @('RUNTIME_FAIL_FAST_ATTRIBUTED','RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED')) {
            $classification='MANAGED_GC_ROOT_RUNTIME_FAIL_FAST_AFTER_ENTRY'
        } elseif ($allocationSucceeded -and $gcCollectionStarted -and $gcCollectionCompleted -and
            $liveObjectSurvived -and $runtimeStateReady -and $transitionReady) {
            $classification='MANAGED_GC_ROOT_COLLECTION_VERIFIED'
        } else {
            $classification='MANAGED_GC_ROOT_RETURNED_MISMATCH'
        }
    } elseif ([string]$transition.classification -eq 'TRANSITION_GATE_BLOCKED_SAFE_STOP' -and
        [string]$transition.recordIntegrity -ceq '1' -and [string]::IsNullOrWhiteSpace([string]$transition.parseError)) {
        $classification='MANAGED_GC_ROOT_BLOCKED_BEFORE_CALL'
    }
} catch {
    $parseError=$_.Exception.Message
    $classification='MANAGED_GC_ROOT_INCOMPLETE'
}

$serialHashAfter=(Get-FileHash -LiteralPath $serialFull -Algorithm SHA256).Hash.ToUpperInvariant()
if ($serialHashBefore -cne $serialHashAfter) { $parseError='raw_serial_changed_during_analysis'; $classification='MANAGED_GC_ROOT_INCOMPLETE' }
$report=[ordered]@{
    analyzer='C188R13-managed-gc-root-v1'
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
    managedEntryAttempted=($attemptLines.Count -eq 1)
    managedEntryReturned=$managedEntryReturned
    managedEntryReturnValue=if ($null -ne $returned) { $returned.return } else { $null }
    managedEntryRecord=$returned
    allocationSucceeded=$allocationSucceeded
    allocationEvidence=$allocation
    gcCollectionRequested=($null -ne $returned -and $returned.request -ceq '52313351')
    gcCollectionStarted=$gcCollectionStarted
    gcCollectionCompleted=$gcCollectionCompleted
    collectionCountBefore=if ($null -ne $returned) { $returned.countBefore } else { $null }
    collectionCountAfter=if ($null -ne $returned) { $returned.countAfter } else { $null }
    liveObjectSurvived=$liveObjectSurvived
    postCollectionResult=if ($null -ne $returned) { $returned.final } else { $null }
    runtimeStateReady=$runtimeStateReady
    runtimeState=$state
    parseError=$parseError
    transitionAnalysisPath=$transitionPath
    qemuLaunched=$false
    qacReserved=$false
}
$parent=Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
[System.IO.File]::WriteAllText($outputFull,($report | ConvertTo-Json -Depth 12),[System.Text.UTF8Encoding]::new($false))
$report | ConvertTo-Json -Depth 12
