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
$serialHashBefore = (Get-FileHash -LiteralPath $serialFull -Algorithm SHA256).Hash.ToUpperInvariant()
$transitionPath = [System.IO.Path]::GetFullPath($OutputPath) + '.transition.json'
& pwsh -NoProfile -ExecutionPolicy Bypass -File $transitionFull `
    -SerialPath $serialFull -CallSiteMapPath $mapFull -OutputPath $transitionPath | Out-Null
$transitionExitCode = $LASTEXITCODE
if ($transitionExitCode -ne 0 -or -not (Test-Path -LiteralPath $transitionPath -PathType Leaf)) {
    throw "R6 transition analysis failed with exit code $transitionExitCode."
}
$transition = Get-Content -LiteralPath $transitionPath -Raw | ConvertFrom-Json -ErrorAction Stop
$raw = [System.IO.File]::ReadAllText($serialFull)
$serialHashAfter = (Get-FileHash -LiteralPath $serialFull -Algorithm SHA256).Hash.ToUpperInvariant()
$evidencePreserved = $serialHashBefore -ceq $serialHashAfter -and [bool]$transition.evidencePreserved
$parseError = $null
$attemptLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_ENTRY\] event=attempt [^\r\n]*') |
    ForEach-Object { $_.Value })
$returnedLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_ENTRY\] event=returned [^\r\n]*') |
    ForEach-Object { $_.Value })
$stateLines = @([regex]::Matches($raw,'(?m)^\[C188R12R4R5R2R3_ENTRY_STATE\] [^\r\n]*') |
    ForEach-Object { $_.Value })

function Get-MarkerFields([string]$Line,[string]$Prefix) {
    if (-not $Line.StartsWith($Prefix,[StringComparison]::Ordinal)) { throw "Marker prefix mismatch: $Prefix" }
    $fields = [ordered]@{}
    foreach ($part in $Line.Substring($Prefix.Length).Trim() -split '\s+') {
        if ($part -notmatch '^(?<name>[A-Za-z][A-Za-z0-9]*)=(?<value>\S+)$') {
            throw "Malformed marker field: $part"
        }
        if ($fields.Contains($Matches.name)) { throw "Duplicate marker field: $($Matches.name)" }
        $fields[$Matches.name] = $Matches.value
    }
    return $fields
}

$classification = 'MANAGED_ENTRY_PROBE_INCOMPLETE'
$managedEntryAttempted = $false
$managedEntryReturned = $false
$runtimeStateReady = $false
$attempt = $null
$returned = $null
$state = $null
if (-not $evidencePreserved) {
    $parseError = 'raw_serial_changed_during_analysis'
} elseif ([string]$transition.classification -eq 'TRANSITION_GATE_BLOCKED_SAFE_STOP' -and
    [string]$transition.recordIntegrity -eq '1' -and [string]::IsNullOrWhiteSpace([string]$transition.parseError) -and
    $attemptLines.Count -eq 0 -and $returnedLines.Count -eq 0 -and $stateLines.Count -eq 0) {
    $classification = 'MANAGED_ENTRY_PROBE_BLOCKED_BEFORE_CALL'
} elseif ([string]$transition.classification -in @('RUNTIME_FAIL_FAST_ATTRIBUTED','RUNTIME_FAIL_FAST_CALLSITE_UNRESOLVED') -and
    [string]$transition.recordIntegrity -eq '1' -and [string]::IsNullOrWhiteSpace([string]$transition.parseError) -and
    $attemptLines.Count -eq 0 -and $returnedLines.Count -eq 0 -and $stateLines.Count -eq 0) {
    $classification = [string]$transition.classification
} elseif ([string]$transition.classification -eq 'INITIALIZEMODULES_RETURNED_MANAGED_ENTRY_NOT_REQUESTED' -and
    [string]$transition.recordIntegrity -eq '1' -and [string]::IsNullOrWhiteSpace([string]$transition.parseError)) {
    if ($attemptLines.Count -ne 1 -or $returnedLines.Count -ne 1 -or $stateLines.Count -ne 1) {
        $parseError = 'managed_entry_marker_count_mismatch'
    } else {
        try {
            $attempt = Get-MarkerFields $attemptLines[0] '[C188R12R4R5R2R3_ENTRY] event=attempt'
            $returned = Get-MarkerFields $returnedLines[0] '[C188R12R4R5R2R3_ENTRY] event=returned'
            $state = Get-MarkerFields $stateLines[0] '[C188R12R4R5R2R3_ENTRY_STATE]'
            $managedEntryAttempted = $true
            $managedEntryReturned = $true
            $entryOrder = $raw.IndexOf($attemptLines[0],[StringComparison]::Ordinal)
            $returnOrder = $raw.IndexOf($returnedLines[0],[StringComparison]::Ordinal)
            $stateOrder = $raw.IndexOf($stateLines[0],[StringComparison]::Ordinal)
            $requiredAttempt = $attempt.abi -ceq 'UnmanagedCallersOnly-Cdecl' -and
                $attempt.seed -ceq '0000000E' -and $attempt.symbol -ceq 'ManagedMain' -and
                $attempt.collection -ceq 'not_requested'
            $requiredRecord = $returned.entry -ceq '5237454E' -and
                $returned.input -ceq '0000000E' -and $returned.computed -ceq '00000031' -and
                $returned.completion -ceq '5237444E' -and $returned.return -ceq '00000031' -and
                $returned.expected -ceq '00000031' -and $returned.record -ceq 'PASS' -and
                $returned.collection -ceq 'not_requested'
            $attachedFlags = [Convert]::ToUInt32([string]$state.flags,16) -band 1u
            $currentThread = [Convert]::ToUInt64([string]$state.currentThread,16)
            $palFlsThread = [Convert]::ToUInt64([string]$state.palFlsThread,16)
            $threadId = [Convert]::ToUInt64([string]$state.threadId,16)
            $observedThreadId = [Convert]::ToUInt64([string]$state.observedThreadId,16)
            $stackLow = [Convert]::ToUInt64([string]$state.stackLow,16)
            $stackHigh = [Convert]::ToUInt64([string]$state.stackHigh,16)
            $stackCurrent = [Convert]::ToUInt64([string]$state.stackCurrent,16)
            $runtimeStateReady = $state.status -ceq '00000000' -and $state.bytes -ceq '00000070' -and
                $state.membership -ceq '00000001' -and $attachedFlags -ne 0u -and
                $state.cooperativeState -ceq '00000001' -and $state.preemptiveFrame -ceq '00000001' -and
                $state.flsMatch -ceq '00000001' -and $threadId -ne 0u -and $threadId -eq $observedThreadId -and
                $state.stackReady -ceq '00000001' -and $state.stackInBounds -ceq '00000001' -and
                $stackLow -lt $stackCurrent -and $stackCurrent -lt $stackHigh -and
                $state.modules -ceq '00000001' -and $state.codeManager -ceq '00000001' -and
                $currentThread -ne 0u -and $currentThread -eq $palFlsThread -and
                $state.frame -ceq 'FFFFFFFFFFFFFFFF' -and $state.result -ceq 'PASS'
            if ($entryOrder -ge 0 -and $entryOrder -lt $returnOrder -and $returnOrder -lt $stateOrder -and
                $requiredAttempt -and $requiredRecord -and $runtimeStateReady) {
                $classification = 'MANAGED_ENTRY_PROBE_RETURNED_RUNTIME_STATE_READY'
            } else {
                $classification = 'MANAGED_ENTRY_PROBE_RETURNED_MISMATCH'
            }
        } catch {
            $parseError = $_.Exception.Message
        }
    }
}
if (-not [string]::IsNullOrWhiteSpace($parseError)) { $classification = 'MANAGED_ENTRY_PROBE_INCOMPLETE' }
$transitionRecord = $transition.transitionRecord
$callSiteMapHash = (Get-FileHash -LiteralPath $mapFull -Algorithm SHA256).Hash.ToUpperInvariant()
$report = [ordered]@{
    analyzer='C188R12R4R5R2R3-managed-entry-probe-v1'
    classification=$classification
    evidencePath=$serialFull
    evidenceSha256Before=$serialHashBefore
    evidenceSha256After=$serialHashAfter
    evidencePreserved=$evidencePreserved
    recordIntegrity=$transition.recordIntegrity
    transitionRecord=$transitionRecord
    threadAndRuntimeState=$transition.threadAndRuntimeState
    callSite=$transition.callSite
    callSiteMap=[ordered]@{path=$mapFull;sha256=$callSiteMapHash}
    transitionClassification=$transition.classification
    transitionAnalysisPath=$transitionPath
    managedEntryAttempted=$managedEntryAttempted
    managedEntryReturned=$managedEntryReturned
    managedEntryReturnValue=if ($null -ne $returned) { $returned.return } else { $null }
    managedEntryRecord=if ($null -ne $returned) { $returned } else { $null }
    runtimeStateReady=$runtimeStateReady
    runtimeState=if ($null -ne $state) { $state } else { $null }
    managedCollectionRequested=$false
    managedEntryRequested=$managedEntryAttempted
    parseError=$parseError
    qemuLaunched=$false
    qacReserved=$false
}
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
$parent = Split-Path -Parent $outputFull
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
[System.IO.File]::WriteAllText($outputFull,($report | ConvertTo-Json -Depth 10),[System.Text.UTF8Encoding]::new($false))
$report | ConvertTo-Json -Depth 10
