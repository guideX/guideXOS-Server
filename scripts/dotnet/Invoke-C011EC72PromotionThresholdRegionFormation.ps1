param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 90,
    [int]$DiscoveryFreshBootCount = 1,
    [int]$ConfirmationFreshBootCount = 3,
    [ValidateSet("low", "high")]
    [string[]]$Cases = @("low", "high"),
    [ValidateSet("low", "high")]
    [string[]]$ConfirmCases = @("low", "high"),
    [string]$RuntimePackManifest = "",
    [switch]$SkipManagedBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\c011ec72-promotion-threshold-region-formation"
}
if ($DiscoveryFreshBootCount -lt 1 -or $ConfirmationFreshBootCount -lt 1) {
    throw "Both discovery and confirmation fresh-boot counts must be at least 1."
}
$smoke = Join-Path $root "scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1"
if (-not (Test-Path -LiteralPath $smoke)) { throw "C011EC72 smoke harness was not found: $smoke" }
if ([string]::IsNullOrWhiteSpace($RuntimePackManifest)) {
    $RuntimePackManifest = Join-Path $root "out\dotnet\runtime-pack-c68\runtime-pack.manifest.json"
}
$RuntimePackManifest = [System.IO.Path]::GetFullPath($RuntimePackManifest)
if (-not (Test-Path -LiteralPath $RuntimePackManifest)) {
    throw "C011EC72 requires the fresh runtime-pack manifest: $RuntimePackManifest"
}

function Get-NormalizedSerial([string]$Path) {
    $text = Get-Content -LiteralPath $Path -Raw
    $text = $text -replace '\[IRQ\] dispatch irq=00\s*', ''
    $text = ($text -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '[ \t]+', ' '
    $text = $text -replace '\b(c\d+)\s+(ec\d+)', '$1$2'
    return $text -replace '\s*=\s*', '='
}

function Get-LatestRun([string]$CaseRoot) {
    $run = @(Get-ChildItem -LiteralPath $CaseRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'run-*' } | Sort-Object LastWriteTime | Select-Object -Last 1)
    if ($run.Count -ne 1) { throw "No C011EC72 run directory was found under $CaseRoot." }
    $serial = Join-Path $run[0].FullName "first-run\serial.log"
    if (-not (Test-Path -LiteralPath $serial)) { throw "C011EC72 serial log was not found: $serial" }
    return [ordered]@{ directory=$run[0].FullName; serial=$serial }
}

function Get-MarkerLine([string]$Text, [string]$Marker, [switch]$Optional) {
    $lines = @($Text -split "`n" | Where-Object { $_ -match ("marker=" + [regex]::Escape($Marker) + "(?:\s|$)") })
    if ($lines.Count -eq 0) {
        if ($Optional) { return $null }
        throw "C011EC72 marker was absent: $Marker"
    }
    return $lines[-1].Trim()
}

function Get-MarkerLines([string]$Text, [string]$Marker) {
    return @($Text -split "`n" | Where-Object { $_ -match ("marker=" + [regex]::Escape($Marker) + "(?:\s|$)") } | ForEach-Object { $_.Trim() })
}

function Get-FirstMarkerLine([string]$Text, [string]$Marker) {
    $lines = @(Get-MarkerLines $Text $Marker)
    if ($lines.Count -eq 0) { throw "C011EC72 marker was absent: $Marker" }
    return $lines[0]
}

function Get-Field([string]$Line, [string]$Name) {
    $match = [regex]::Match($Line, '(?:^|\s)' + [regex]::Escape($Name) + '=(?<value>(?:0x)?[0-9A-Fa-f]+)')
    if (-not $match.Success) { throw "C011EC72 marker $Line is missing field $Name." }
    return $match.Groups['value'].Value
}

function Get-Hex([string]$Value) {
    if ($Value.StartsWith('0x')) { return [Convert]::ToUInt64($Value.Substring(2), 16) }
    return [Convert]::ToUInt64($Value, 16)
}

function Get-ArtifactHashes([string]$RunDirectory) {
    $files = [ordered]@{}
    foreach ($file in @(Get-ChildItem -LiteralPath $RunDirectory -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -lt 64MB } | Select-Object -First 80)) {
        $relative = $file.FullName.Substring($RunDirectory.Length).TrimStart('\')
        $files[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    return $files
}

function Read-Case([string]$Case, [string]$Stage, [int]$FreshBootCount, [string]$CaseRoot) {
    $latest = Get-LatestRun $CaseRoot
    $text = Get-NormalizedSerial $latest.serial
    $control = Get-MarkerLine $text 'C71_CONTROL_LIVE_BYTE_THRESHOLD'
    $live = Get-MarkerLine $text 'C71_RETAINED_LIVE_BYTES'
    $decision = Get-MarkerLine $text 'C71_PROMOTION_DECISION'
    $promotion = Get-MarkerLine $text 'C70_PROMOTION_OBSERVED'
    $c72Case = Get-MarkerLine $text 'C72_CASE'
    $firstDivergence = Get-FirstMarkerLine $text 'C72_FIRST_PRODUCTION_DIVERGENCE'
    $c72Summary = Get-MarkerLine $text 'C72_SUMMARY'
    $c72Complete = Get-MarkerLine $text 'C011EC72'
    $postRestart = Get-MarkerLine $text 'C72_POST_RESTART_BASIC_COUNT'
    $postResume = Get-MarkerLine $text 'C72_MANAGED_RESUME_BASIC_COUNT'
    $c70Summary = Get-MarkerLine $text 'C70_SUMMARY' -Optional
    $c70LastNonzero = Get-MarkerLine $text 'C70_LAST_NONZERO_CANDIDATE' -Optional
    $c70FinalUnlink = Get-MarkerLine $text 'C70_FINAL_CANDIDATE_UNLINK' -Optional
    $c70Exhausted = Get-MarkerLine $text 'C70_GEN0_CONTEXT_EXHAUSTED' -Optional
    $c70GetNew = Get-MarkerLine $text 'C70_GET_NEW_REGION_0' -Optional
    $c70Candidate = Get-MarkerLine $text 'C70_POST_DEBIT_GEN0_CANDIDATE' -Optional
    $c70Selected = Get-MarkerLine $text 'C70_POST_DEBIT_REGION_SELECTED' -Optional
    $c70Refill = Get-MarkerLine $text 'C70_POST_DEBIT_NORMAL_REFILL' -Optional
    $basicInsertions = Get-MarkerLines $text 'C72_BASIC_INSERT'
    $basicRemovals = Get-MarkerLines $text 'C72_BASIC_REMOVE'
    $events = Get-MarkerLines $text 'C72_EVENT'
    $regions = Get-MarkerLines $text 'C72_REGION_ID'
    $provenance = Get-MarkerLines $text 'C72_REGION_PROVENANCE'
    $expectedObjectSize = if ($Case -eq 'low') { [uint64]0x10E78 } else { [uint64]0x10E80 }
    $expectedLive = if ($Case -eq 'low') { [uint64]0xFD908 } else { [uint64]0xFD980 }
    $actualObjectSize = Get-Hex (Get-Field $control 'measuredObjectSize')
    $actualLive = Get-Hex (Get-Field $live 'value')
    $actualPromotion = Get-Hex (Get-Field $promotion 'observed')
    if ($actualObjectSize -ne $expectedObjectSize -or $actualLive -ne $expectedLive) {
        throw "C011EC72 $Case baseline mismatch: object=0x$('{0:X}' -f $actualObjectSize), live=0x$('{0:X}' -f $actualLive)."
    }
    $expectedDecision = if ($Case -eq 'low') { 0 } else { 1 }
    if ($actualPromotion -ne $expectedDecision) {
        throw "C011EC72 $Case authentic promotion mismatch: observed $actualPromotion, expected $expectedDecision."
    }
    $completeOverflow = Get-Hex (Get-Field $c72Complete 'eventOverflow')
    $regionOverflow = Get-Hex (Get-Field $c72Complete 'regionOverflow')
    $invariantFailures = Get-Hex (Get-Field $c72Complete 'invariantFailures')
    if ($completeOverflow -ne 0 -or $regionOverflow -ne 0 -or $invariantFailures -ne 0) {
        throw "C011EC72 $Case diagnostic validity failed: eventOverflow=$completeOverflow regionOverflow=$regionOverflow invariantFailures=$invariantFailures."
    }
    $record = [ordered]@{
        case=$Case; stage=$Stage; freshBootCount=$FreshBootCount
        serial=$latest.serial
        serialSha256=(Get-FileHash -LiteralPath $latest.serial -Algorithm SHA256).Hash
        runDirectory=$latest.directory
        artifactHashes=(Get-ArtifactHashes $latest.directory)
        control=[ordered]@{ case=(Get-Field $c72Case 'case'); retainedCount=(Get-Field $c72Case 'retainedCount'); payloadSize=(Get-Field $c72Case 'payloadSize'); retainedLiveBytes=(Get-Field $c72Case 'retainedLiveBytes'); measuredObjectSize=(Get-Field $control 'measuredObjectSize'); actualRetainedReferences=(Get-Field $control 'actualRetainedReferences') }
        c71=[ordered]@{ objectSize=(Get-Field $control 'measuredObjectSize'); retainedLiveBytes=(Get-Field $live 'value'); decision=(Get-Field $decision 'decision'); threshold=(Get-Field $decision 'threshold'); promotedBytes=(Get-Field $decision 'promotedBytes'); olderGenerationSize=(Get-Field $decision 'olderGenerationSize'); thresholdGreaterOlderGeneration=(Get-Field $decision 'thresholdGreaterOlderGeneration'); promotedGreaterThreshold=(Get-Field $decision 'promotedGreaterThreshold'); settingsPromotion=(Get-Field $decision 'settingsPromotion') }
        promotion=[ordered]@{ observed=(Get-Field $promotion 'observed'); uniquePromotedObjects=(Get-Field $promotion 'uniquePromotedObjects'); promotedBytes=(Get-Field $promotion 'promotedBytes'); gen1BudgetBefore=(Get-Field $promotion 'gen1BudgetBefore'); gen1Debit=(Get-Field $promotion 'gen1Debit'); gen1BudgetAfter=(Get-Field $promotion 'gen1BudgetAfter'); generationBefore=(Get-Field $promotion 'generationBefore'); generationAfter=(Get-Field $promotion 'generationAfter'); debitObserved=(Get-Field $promotion 'debitObserved') }
        firstProductionDivergence=$firstDivergence
        c72=[ordered]@{ summary=$c72Summary; complete=$c72Complete; postRestart=$postRestart; postResume=$postResume; events=$events; basicInsertions=$basicInsertions; basicRemovals=$basicRemovals; regions=$regions; provenance=$provenance }
        downstream=[ordered]@{ c70Summary=$c70Summary; lastNonzero=$c70LastNonzero; finalUnlink=$c70FinalUnlink; contextExhausted=$c70Exhausted; getNewRegion=$c70GetNew; candidate=$c70Candidate; selected=$c70Selected; refill=$c70Refill }
        validity=[ordered]@{ eventOverflow=$completeOverflow; regionOverflow=$regionOverflow; invariantFailures=$invariantFailures; c65DiagnosticInvariantFailures=(Get-Field $c72Complete 'c65DiagnosticInvariantFailures'); sensitiveDiagnosticAllocations=(Get-Field $c72Complete 'sensitiveDiagnosticAllocations'); failFast=(Get-Field $c72Complete 'failFast'); pageFault=(Get-Field $c72Complete 'pageFault'); b02Evaluated=$false; behaviorMutation='none' }
    }
    $record | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $latest.directory 'c72-case.json') -Encoding ASCII
    $genealogy = [ordered]@{ case=$Case; postRestartBasicCount=(Get-Field $postRestart 'count'); postResumeBasicCount=(Get-Field $postResume 'count'); regionIdentities=$regions; provenance=$provenance; basicInsertions=$basicInsertions; basicRemovals=$basicRemovals }
    $genealogy | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $latest.directory 'region-genealogy.json') -Encoding ASCII
    return $record
}

function Invoke-Case([string]$Case, [string]$Stage, [int]$FreshBootCount) {
    $caseRoot = Join-Path $EvidenceRoot $Case
    $c71Case = if ($Case -eq 'low') { '15mid9' } else { '15mid8' }
    $arguments = @{
        RepoRoot=$root; EvidenceRoot=$caseRoot; TimeoutSeconds=$TimeoutSeconds
        FreshBootCount=$FreshBootCount; ProofMode='promotion-threshold-region-formation'
        C71Case=$c71Case; C66Strategy='P2'; C66TailAllocations=320
        RuntimePackManifest=$RuntimePackManifest
    }
    if ($SkipManagedBuild) { $arguments.SkipManagedBuild = $true }
    Write-Host "C011EC72 $Stage $Case ($FreshBootCount fresh boot(s))" -ForegroundColor Cyan
    & $smoke @arguments
    if ($LASTEXITCODE -ne 0) { throw "C011EC72 smoke harness failed for $Stage/$Case with exit code $LASTEXITCODE." }
    return Read-Case $Case $Stage $FreshBootCount $caseRoot
}

$records = @()
foreach ($case in $Cases) { $records += Invoke-Case $case 'discovery' $DiscoveryFreshBootCount }
foreach ($case in $ConfirmCases) {
    if ($Cases -contains $case) { $records += Invoke-Case $case 'confirmation' $ConfirmationFreshBootCount }
}
$confirmed = @($records | Where-Object { $_.stage -eq 'confirmation' })
$reference = if ($confirmed.Count -ge 2) { $confirmed } else { @($records | Where-Object { $_.stage -eq 'discovery' }) }

function Get-SemanticFingerprint($record) {
    $summary = $record.c72.summary
    return [ordered]@{
        case=$record.case
        decision=$record.promotion.observed
        policyDecision=$record.c71.decision
        firstProductionDivergence=(Get-Field $record.firstProductionDivergence 'eventOrdinal')
        firstRegionDivergence=(Get-Field $record.firstProductionDivergence 'regionEventOrdinal')
        basicInsertions=(Get-Field $summary 'basicInsertions')
        basicRemovals=(Get-Field $summary 'basicRemovals')
        generationTransitions=(Get-Field $summary 'generationTransitions')
        postRestartBasicCount=(Get-Field $record.c72.postRestart 'count')
        postResumeBasicCount=(Get-Field $record.c72.postResume 'count')
        candidateAvailable=$(if($null -ne $record.downstream.candidate){Get-Field $record.downstream.candidate 'available'}else{'missing'})
        candidateSelected=$(if($null -ne $record.downstream.selected){Get-Field $record.downstream.selected 'selected'}else{'missing'})
        normalRefill=$(if($null -ne $record.downstream.refill){Get-Field $record.downstream.refill 'result'}else{'missing'})
        eventOverflow=(Get-Field $record.c72.complete 'eventOverflow')
        regionOverflow=(Get-Field $record.c72.complete 'regionOverflow')
        invariantFailures=(Get-Field $record.c72.complete 'invariantFailures')
    }
}

$agreement = $true
foreach ($case in @('low', 'high')) {
    $caseRecords = @($reference | Where-Object { $_.case -eq $case })
    if ($caseRecords.Count -gt 1) {
        $first = Get-SemanticFingerprint $caseRecords[0]
        foreach ($other in $caseRecords | Select-Object -Skip 1) {
            $candidate = Get-SemanticFingerprint $other
            foreach ($field in @('decision','policyDecision','basicInsertions','basicRemovals','generationTransitions','postRestartBasicCount','postResumeBasicCount','candidateAvailable','candidateSelected','normalRefill','eventOverflow','regionOverflow','invariantFailures')) {
                if ([string]$first[$field] -ne [string]$candidate[$field]) { $agreement = $false }
            }
        }
    }
}
if (-not $agreement) { throw 'C011EC72 semantic genealogy fields varied across confirmation boots.' }

$low = @($reference | Where-Object { $_.case -eq 'low' } | Select-Object -Last 1)
$high = @($reference | Where-Object { $_.case -eq 'high' } | Select-Object -Last 1)
$manifest = [ordered]@{
    milestone='C011EC72 Promotion-Threshold-to-Region-Formation Causality'
    proofMode='promotion-threshold-region-formation'
    repositoryRoot=$root
    lockedRuntimeSourceCommit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'
    c70Commit='not separately committed; inherited from C71 baseline'
    c71Commit='96253f28ff89b020ca129c24544af3efe823a8c5'
    cases=$Cases; confirmationCases=$ConfirmCases
    controls=[ordered]@{ low='15 retained objects, C71Case=15mid9, actual object 0x10E78, retained live 0xFD908'; high='15 retained objects, C71Case=15mid8, actual object 0x10E80, retained live 0xFD980'; changedOnly='8-byte actual object-size step per retained object; no count/topology/schedule/policy change'; B02Evaluated=$false }
    sourceAudit=[ordered]@{ promotion='gc_heap::decide_on_promotion_surv threshold/promoted/older_gen condition'; regionReturn='gc_heap::return_free_region -> clear_region_info -> region_free_list::add_region_descending'; regionConsume='gc_heap::get_free_region -> free_regions[basic_free_region].unlink_region_front'; threading='gc_heap::thread_final_regions -> find_first_valid_region -> generation threading'; instrumentation='fixed-capacity, allocation-free observers only' }
    discoveryAndConfirmation=$records
    semanticAgreement=$agreement
    comparison=[ordered]@{ low=$(if($low.Count -eq 1){Get-SemanticFingerprint $low[0]}else{$null}); high=$(if($high.Count -eq 1){Get-SemanticFingerprint $high[0]}else{$null}) }
    evidenceRoot=$EvidenceRoot
    documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_C72_PROMOTION_THRESHOLD_REGION_FORMATION_CAUSALITY.md'
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c72-matrix.json') -Encoding ASCII
Write-Host "C011EC72 matrix manifest: $(Join-Path $EvidenceRoot 'c72-matrix.json')" -ForegroundColor Green
