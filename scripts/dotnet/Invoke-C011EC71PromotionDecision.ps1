param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 90,
    [int]$DiscoveryFreshBootCount = 1,
    [int]$ConfirmationFreshBootCount = 3,
    [ValidateSet("baseline16", "baseline15", "16below", "15above", "15adjacentbelow", "15mid", "15mid2", "15mid3", "15mid4", "15mid5", "15mid6", "15mid7", "15mid8", "15mid9")]
    [string[]]$Cases = @("baseline16", "baseline15", "16below", "15above"),
    [ValidateSet("baseline16", "baseline15", "16below", "15above", "15adjacentbelow", "15mid", "15mid2", "15mid3", "15mid4", "15mid5", "15mid6", "15mid7", "15mid8", "15mid9")]
    [string[]]$ConfirmCases = @("baseline16", "baseline15"),
    [string]$RuntimePackManifest = "",
    [switch]$SkipManagedBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ($DiscoveryFreshBootCount -lt 1 -or $ConfirmationFreshBootCount -lt 1) {
    throw "Both discovery and confirmation fresh-boot counts must be at least 1."
}
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\c011ec71-promotion-decision-live-byte-threshold"
}
$smoke = Join-Path $root "scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1"
if (-not (Test-Path -LiteralPath $smoke)) { throw "C011EC71 smoke harness was not found: $smoke" }
if ([string]::IsNullOrWhiteSpace($RuntimePackManifest)) {
    $RuntimePackManifest = Join-Path $root "out\dotnet\runtime-pack-c68\runtime-pack.manifest.json"
}
$RuntimePackManifest = [System.IO.Path]::GetFullPath($RuntimePackManifest)
if (-not (Test-Path -LiteralPath $RuntimePackManifest)) { throw "C011EC71 requires the fresh runtime-pack manifest: $RuntimePackManifest" }

function Get-NormalizedSerial([string]$Path) {
    $text = Get-Content -LiteralPath $Path -Raw
    $text = $text -replace '\[IRQ\] dispatch irq=00\s*', ''
    $text = ($text -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '[ \t]+', ' '
    $text = $text -replace '\b(c\d+)\s+(ec\d+)', '$1$2'
    return $text -replace '\s*=\s*', '='
}

function Get-LatestSerial([string]$CaseRoot) {
    $run = @(Get-ChildItem -LiteralPath $CaseRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'run-*' } | Sort-Object LastWriteTime | Select-Object -Last 1)
    if ($run.Count -ne 1) { throw "No C011EC71 run directory was found under $CaseRoot." }
    $serial = Join-Path $run[0].FullName "first-run\serial.log"
    if (-not (Test-Path -LiteralPath $serial)) { throw "C011EC71 serial log was not found: $serial" }
    return $serial
}

function Get-MarkerLine([string]$Text, [string]$Marker) {
    $lines = @($Text -split "`n" | Where-Object { $_ -match ("marker=" + [regex]::Escape($Marker) + "(?:\s|$)") })
    if ($lines.Count -eq 0) { throw "C011EC71 marker was absent: $Marker" }
    return $lines[-1].Trim()
}

function Get-Field([string]$Line, [string]$Name) {
    $match = [regex]::Match($Line, '(?:^|\s)' + [regex]::Escape($Name) + '=(?<value>(?:0x)?[0-9A-Fa-f]+)')
    if (-not $match.Success) { throw "C011EC71 marker $Line is missing field $Name." }
    return $match.Groups['value'].Value
}

function Read-Case([string]$Case, [string]$Stage, [int]$FreshBootCount, [string]$CaseRoot) {
    $serial = Get-LatestSerial $CaseRoot
    $text = Get-NormalizedSerial $serial
    $control = Get-MarkerLine $text 'C71_CONTROL_LIVE_BYTE_THRESHOLD'
    $live = Get-MarkerLine $text 'C71_RETAINED_LIVE_BYTES'
    $marked = Get-MarkerLine $text 'C71_MARKED_LIVE_BYTES'
    $decision = Get-MarkerLine $text 'C71_PROMOTION_DECISION'
    $sync = Get-MarkerLine $text 'C71_SYNC_REGION'
    $planner = Get-MarkerLine $text 'C71_PLANNER_DECISION'
    $complete = Get-MarkerLine $text 'C011EC71'
    return [ordered]@{
        case=$Case
        stage=$Stage
        freshBootCount=$FreshBootCount
        serial=$serial
        serialSha256=(Get-FileHash -LiteralPath $serial -Algorithm SHA256).Hash
        control=[ordered]@{
            case=(Get-Field $control 'case')
            configuredRetainedReferences=(Get-Field $control 'configuredRetainedReferences')
            actualRetainedReferences=(Get-Field $control 'actualRetainedReferences')
            payloadSize=(Get-Field $control 'payloadSize')
            measuredObjectSize=(Get-Field $control 'measuredObjectSize')
            requestedRetainedBytes=(Get-Field $control 'requestedRetainedBytes')
            retainedLiveBytes=(Get-Field $control 'retainedLiveBytes')
            retainedObjectSizeUniform=(Get-Field $control 'retainedObjectSizeUniform')
            allocationCount=(Get-Field $control 'allocationCount')
            totalRequestedPayloadBytes=(Get-Field $control 'totalRequestedPayloadBytes')
            totalRequestedObjectBytes=(Get-Field $control 'totalRequestedObjectBytes')
        }
        marked=[ordered]@{
            observed=(Get-Field $marked 'observed')
            objectCount=(Get-Field $marked 'objectCount')
            value=(Get-Field $marked 'value')
            allObservedBytes=(Get-Field $marked 'allObservedBytes')
        }
        decision=[ordered]@{
            observed=(Get-Field $decision 'observed')
            count=(Get-Field $decision 'count')
            threshold=(Get-Field $decision 'threshold')
            promotedBytes=(Get-Field $decision 'promotedBytes')
            olderGenerationSize=(Get-Field $decision 'olderGenerationSize')
            thresholdGreaterOlderGeneration=(Get-Field $decision 'thresholdGreaterOlderGeneration')
            promotedGreaterThreshold=(Get-Field $decision 'promotedGreaterThreshold')
            decision=(Get-Field $decision 'decision')
            condemnedGeneration=(Get-Field $decision 'condemnedGeneration')
            settingsPromotion=(Get-Field $decision 'settingsPromotion')
            markedBytesAtDecision=(Get-Field $decision 'markedBytesAtDecision')
        }
        live=[ordered]@{
            observed=(Get-Field $live 'observed')
            retainedCount=(Get-Field $live 'retainedCount')
            objectSize=(Get-Field $live 'objectSize')
            value=(Get-Field $live 'value')
            requestedValue=(Get-Field $live 'requestedValue')
        }
        sync=[ordered]@{
            observed=(Get-Field $sync 'observed')
            count=(Get-Field $sync 'count')
            region=(Get-Field $sync 'region')
            liveBytes=(Get-Field $sync 'liveBytes')
            generationBefore=(Get-Field $sync 'generationBefore')
            planGeneration=(Get-Field $sync 'planGeneration')
        }
        planner=[ordered]@{
            observed=(Get-Field $planner 'observed')
            count=(Get-Field $planner 'count')
            region=(Get-Field $planner 'region')
            liveBytes=(Get-Field $planner 'liveBytes')
            regionSize=(Get-Field $planner 'regionSize')
            generationBefore=(Get-Field $planner 'generationBefore')
            generationAfter=(Get-Field $planner 'generationAfter')
            survivorRatio=(Get-Field $planner 'survivorRatio')
            oldCardSurvivorRatio=(Get-Field $planner 'oldCardSurvivorRatio')
            decision=(Get-Field $planner 'decision')
            settingsPromotion=(Get-Field $planner 'settingsPromotion')
        }
        completion=[ordered]@{
            outcome=([regex]::Match($complete, 'outcome=(?<value>[A-Z])').Groups['value'].Value)
            successLevel=(Get-Field $complete 'successLevel')
            decisionObserved=(Get-Field $complete 'decisionObserved')
            plannerObserved=(Get-Field $complete 'plannerObserved')
            invariantFailures=(Get-Field $complete 'invariantFailures')
        }
    }
}

function Invoke-Case([string]$Case, [string]$Stage, [int]$FreshBootCount) {
    $caseRoot = Join-Path $EvidenceRoot $Case
    $arguments = @{
        RepoRoot=$root
        EvidenceRoot=$caseRoot
        TimeoutSeconds=$TimeoutSeconds
        FreshBootCount=$FreshBootCount
        ProofMode='promotion-decision-live-byte-threshold'
        C71Case=$Case
        C66Strategy='P2'
        C66TailAllocations=320
        RuntimePackManifest=$RuntimePackManifest
    }
    if ($SkipManagedBuild) { $arguments.SkipManagedBuild = $true }
    Write-Host "C011EC71 $Stage $Case ($FreshBootCount fresh boot(s))" -ForegroundColor Cyan
    & $smoke @arguments
    if ($LASTEXITCODE -ne 0) { throw "C011EC71 smoke harness failed for $Stage/$Case with exit code $LASTEXITCODE." }
    return Read-Case $Case $Stage $FreshBootCount $caseRoot
}

$records = @()
foreach ($case in $Cases) {
    $records += Invoke-Case $case 'discovery' $DiscoveryFreshBootCount
}
foreach ($case in $ConfirmCases) {
    if ($Cases -contains $case) {
        $records += Invoke-Case $case 'confirmation' $ConfirmationFreshBootCount
    }
}

$confirmed = @($records | Where-Object { $_.stage -eq 'confirmation' })
$referenceRecords = if ($confirmed.Count -ge 2) { $confirmed } else { @($records | Where-Object { $_.stage -eq 'discovery' }) }
$first16 = @($referenceRecords | Where-Object { $_.case -eq 'baseline16' } | Select-Object -Last 1)
$first15 = @($referenceRecords | Where-Object { $_.case -eq 'baseline15' } | Select-Object -Last 1)
$comparison = [ordered]@{
    referenceStage=if ($confirmed.Count -ge 2) { 'confirmation' } else { 'discovery' }
    firstProductionDivergence=[ordered]@{
        source='gc_heap::add_to_promoted_bytes'
        quantity='survived_per_region marked-live byte accumulation'
        nextPolicyConsumer='gc_heap::decide_on_promotion_surv'
        pair='baseline16 vs baseline15'
    }
    baseline16Decision=if ($first16.Count -eq 1) { $first16[0].decision.decision } else { $null }
    baseline15Decision=if ($first15.Count -eq 1) { $first15[0].decision.decision } else { $null }
    baseline16RetainedLiveBytes=if ($first16.Count -eq 1) { $first16[0].live.value } else { $null }
    baseline15RetainedLiveBytes=if ($first15.Count -eq 1) { $first15[0].live.value } else { $null }
}

$manifest = [ordered]@{
    milestone='C011EC71 Promotion-Decision Provenance and Retained-Live-Byte Threshold'
    proofMode='promotion-decision-live-byte-threshold'
    repositoryRoot=$root
    lockedRuntimeSourceCommit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'
    sourceAudit=[ordered]@{
        markedLive='gc_heap::add_to_promoted_bytes -> survived_per_region'
        promotionDecision='gc_heap::decide_on_promotion_surv -> threshold, total_promoted_bytes, older_gen_size'
        regionSync='gc_heap::sync_promoted_bytes -> heap_segment_survived'
        planner='gc_heap::should_sweep_in_plan -> 90 percent of basic region'
        instrumentation='observational callbacks only; no allocator, policy, region-list, or OOS mutation'
    }
    cases=$Cases
    confirmationCases=$ConfirmCases
    records=$records
    comparison=$comparison
    evidenceRoot=$EvidenceRoot
}
$manifestPath = Join-Path $EvidenceRoot 'c71-matrix.json'
$manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
Write-Host "C011EC71 matrix manifest: $manifestPath" -ForegroundColor Green
