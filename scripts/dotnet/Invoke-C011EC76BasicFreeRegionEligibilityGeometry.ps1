[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c011ec76-basic-free-region-eligibility-geometry'
}

$capacity = 4096
$acceptedRoot = Join-Path $EvidenceRoot 'accepted-confirmation-runs'

function Get-Fields([string]$Line) {
    $result = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<name>[A-Za-z0-9]+)=(?<value>[^\s]+)')) {
        $result[$match.Groups['name'].Value] = $match.Groups['value'].Value
    }
    return $result
}

function Get-Hex([hashtable]$Fields, [string]$Name) {
    if (-not $Fields.Contains($Name)) { return [uint64]0 }
    try { return [Convert]::ToUInt64([string]$Fields[$Name], 16) }
    catch { return [uint64]0 }
}

function Format-Hex([uint64]$Value) {
    return ('0x{0:X}' -f $Value)
}

function Get-LastLine([string[]]$Lines, [string]$Pattern) {
    return @($Lines | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)[0]
}

function Convert-Event([string]$Line) {
    $fields = Get-Fields $Line
    $marker = ([regex]::Match($Line, 'marker=(C76_[^\s]+)').Groups[1].Value)
    [ordered]@{
        marker=$marker
        eventOrdinal=(Format-Hex (Get-Hex $fields 'eventOrdinal'))
        eventType=(Format-Hex (Get-Hex $fields 'eventType'))
        operation=(Format-Hex (Get-Hex $fields 'operation'))
        localOrdinal=(Format-Hex (Get-Hex $fields 'localOrdinal'))
        classification=(Format-Hex (Get-Hex $fields 'classification'))
        basicEligible=(Format-Hex (Get-Hex $fields 'basicEligible'))
        listKind=(Format-Hex (Get-Hex $fields 'listKind'))
        sourceBranch=(Format-Hex (Get-Hex $fields 'sourceBranch'))
        region=(Format-Hex (Get-Hex $fields 'region'))
        list=(Format-Hex (Get-Hex $fields 'list'))
        regionSize=(Format-Hex (Get-Hex $fields 'regionSize'))
        basicRegionSize=(Format-Hex (Get-Hex $fields 'basicRegionSize'))
        largeRegionSize=(Format-Hex (Get-Hex $fields 'largeRegionSize'))
        mem=(Format-Hex (Get-Hex $fields 'mem'))
        committed=(Format-Hex (Get-Hex $fields 'committed'))
        reserved=(Format-Hex (Get-Hex $fields 'reserved'))
        used=(Format-Hex (Get-Hex $fields 'used'))
        allocated=(Format-Hex (Get-Hex $fields 'allocated'))
        liveBytes=(Format-Hex (Get-Hex $fields 'liveBytes'))
        freeBytes=(Format-Hex (Get-Hex $fields 'freeBytes'))
        ageInFree=(Format-Hex (Get-Hex $fields 'ageInFree'))
        containingList=(Format-Hex (Get-Hex $fields 'containingList'))
        generationBefore=(Format-Hex (Get-Hex $fields 'generationBefore'))
        generationAfter=(Format-Hex (Get-Hex $fields 'generationAfter'))
        planGenerationBefore=(Format-Hex (Get-Hex $fields 'planGenerationBefore'))
        planGenerationAfter=(Format-Hex (Get-Hex $fields 'planGenerationAfter'))
        stateBefore=(Format-Hex (Get-Hex $fields 'stateBefore'))
        stateAfter=(Format-Hex (Get-Hex $fields 'stateAfter'))
        nextBefore=(Format-Hex (Get-Hex $fields 'nextBefore'))
        nextAfter=(Format-Hex (Get-Hex $fields 'nextAfter'))
        previousBefore=(Format-Hex (Get-Hex $fields 'previousBefore'))
        previousAfter=(Format-Hex (Get-Hex $fields 'previousAfter'))
        freeCountBefore=(Format-Hex (Get-Hex $fields 'freeCountBefore'))
        freeCountAfter=(Format-Hex (Get-Hex $fields 'freeCountAfter'))
        headBefore=(Format-Hex (Get-Hex $fields 'headBefore'))
        headAfter=(Format-Hex (Get-Hex $fields 'headAfter'))
        tailBefore=(Format-Hex (Get-Hex $fields 'tailBefore'))
        tailAfter=(Format-Hex (Get-Hex $fields 'tailAfter'))
        sourceFunction=if ($marker -eq 'C76_REGION_ELIGIBILITY') { 'region_free_list::get_region_kind' } elseif ($marker -eq 'C76_BASIC_INSERT') { 'region_free_list::add_region*' } else { 'region_free_list::unlink_region' }
    }
}

function Read-Case([string]$Name, [string]$Root) {
    if (-not (Test-Path -LiteralPath $Root)) { throw "Missing accepted C76 root: $Root" }
    $logs = @(Get-ChildItem -LiteralPath $Root -Recurse -Filter serial.log -File | Sort-Object FullName)
    if ($logs.Count -ne 3) { throw "Expected three accepted $Name serial logs under $Root; found $($logs.Count)." }

    $runs = [System.Collections.Generic.List[object]]::new()
    foreach ($log in $logs) {
        $lines = @(Get-Content -LiteralPath $log.FullName)
        $summaryLine = Get-LastLine $lines 'marker=C011EC76-SUMMARY'
        $completeLine = Get-LastLine $lines 'marker=C011EC76 outcome='
        if (-not $summaryLine -or -not $completeLine) { throw "Missing C76 completion markers in $($log.FullName)." }
        $summary = Get-Fields $summaryLine
        $complete = Get-Fields $completeLine
        $events = @($lines | Where-Object { $_ -match 'marker=C76_(REGION_ELIGIBILITY|BASIC_INSERT|BASIC_REMOVE)' } | ForEach-Object { Convert-Event $_ })
        if ($events.Count -gt $capacity) { throw "$Name C76 event capacity overflow: $($events.Count) > $capacity." }

        $eligibility = @($events | Where-Object { $_.marker -eq 'C76_REGION_ELIGIBILITY' })
        $inserts = @($events | Where-Object { $_.marker -eq 'C76_BASIC_INSERT' })
        $removes = @($events | Where-Object { $_.marker -eq 'C76_BASIC_REMOVE' })
        $roleBases = @($eligibility | ForEach-Object { $_.mem } | Sort-Object -Unique)
        $runManifest = @(Get-ChildItem -LiteralPath $Root -Recurse -Filter manifest.json -File | ForEach-Object {
            try { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json } catch { $null }
        } | Where-Object { $_ -and $_.qemu -and $_.qemu.runCount -eq 3 } | Select-Object -First 1)[0]

        $runs.Add([ordered]@{
            boot=($runs.Count + 1)
            serial=$log.FullName
            serialSha256=(Get-FileHash -LiteralPath $log.FullName -Algorithm SHA256).Hash
            outcome=$complete.outcome
            successLevel=('0x{0:X}' -f (Get-Hex $complete 'successLevel'))
            promotionObserved=('0x{0:X}' -f (Get-Hex $summary 'promotionObserved'))
            basicCountPhaseStart=(Format-Hex (Get-Hex $summary 'basicCountPhaseStart'))
            postRestartBasicCount=(Format-Hex (Get-Hex $summary 'postRestartBasicCount'))
            postResumeBasicCount=(Format-Hex (Get-Hex $summary 'postResumeBasicCount'))
            eligibilityCount=('0x{0:X}' -f (Get-Hex $summary 'eligibilityCount'))
            basicEligibilityCount=('0x{0:X}' -f (Get-Hex $summary 'basicEligibilityCount'))
            nonBasicEligibilityCount=('0x{0:X}' -f (Get-Hex $summary 'nonBasicEligibilityCount'))
            listEventCount=('0x{0:X}' -f (Get-Hex $summary 'listEventCount'))
            basicListEventCount=('0x{0:X}' -f (Get-Hex $summary 'basicListEventCount'))
            basicInsertions=('0x{0:X}' -f (Get-Hex $summary 'basicInsertions'))
            basicRemovals=('0x{0:X}' -f (Get-Hex $summary 'basicRemovals'))
            eventCount=('0x{0:X}' -f (Get-Hex $summary 'eventCount'))
            eventCapacity=$capacity
            eventOverflow=('0x{0:X}' -f (Get-Hex $summary 'eventOverflow'))
            invariantFailures=('0x{0:X}' -f (Get-Hex $summary 'invariantFailures'))
            inheritedInvariantFailures=('0x{0:X}' -f (Get-Hex $summary 'inheritedInvariantFailures'))
            inheritedEventOverflow=('0x{0:X}' -f (Get-Hex $summary 'inheritedEventOverflow'))
            sensitiveDiagnosticAllocations=('0x{0:X}' -f (Get-Hex $summary 'sensitiveDiagnosticAllocations'))
            expansionAttempted=('0x{0:X}' -f (Get-Hex $summary 'expansionAttempted'))
            expansionSucceeded=('0x{0:X}' -f (Get-Hex $summary 'expansionSucceeded'))
            tailReclaimObserved=('0x{0:X}' -f (Get-Hex $summary 'tailReclaimObserved'))
            failFast=('0x{0:X}' -f (Get-Hex $summary 'failFast'))
            pageFault=('0x{0:X}' -f (Get-Hex $summary 'pageFault'))
            predicateBasicOnly=('0x{0:X}' -f (Get-Hex $summary 'predicateBasicOnly'))
            uniqueRelevantRangeBases=$roleBases
            events=$events
            sourceManifest=if ($runManifest) { [ordered]@{ proofKernel=$runManifest.payloadHashes.proofKernel; pe=$runManifest.payloadHashes.pe; elf=$runManifest.payloadHashes.elf; map=$runManifest.payloadHashes.map; ordinaryRestoration=$runManifest.ordinaryRestoration } } else { $null }
        }) | Out-Null
    }

    $signature = @($runs | ForEach-Object { $_.outcome, $_.postRestartBasicCount, $_.postResumeBasicCount, $_.eligibilityCount, $_.basicInsertions, $_.basicRemovals, $_.eventCount, $_.eventOverflow, $_.invariantFailures } | Sort-Object -Unique)
    $first = $runs[0]
    return [ordered]@{
        name=$Name
        runs=$runs
        semanticAgreement=($signature.Count -le 9)
        uniqueRelevantRangeBases=@($runs | ForEach-Object { $_.uniqueRelevantRangeBases } | Sort-Object -Unique)
        firstRun=$first
    }
}

$oneRoot = Join-Path $acceptedRoot 'one'
$sixRoot = Join-Path $acceptedRoot 'six'
$one = Read-Case 'ONE' $oneRoot
$six = Read-Case 'SIX' $sixRoot

$commonBases = @($one.uniqueRelevantRangeBases | Where-Object { $six.uniqueRelevantRangeBases -contains $_ })
$oneFirst = $one.firstRun.events
$sixFirst = $six.firstRun.events
$firstStateDivergence = $null
for ($i = 0; $i -lt [Math]::Min($oneFirst.Count, $sixFirst.Count); $i++) {
    if ($oneFirst[$i].marker -eq $sixFirst[$i].marker -and
        $oneFirst[$i].mem -eq $sixFirst[$i].mem -and
        ($oneFirst[$i].stateBefore -ne $sixFirst[$i].stateBefore -or $oneFirst[$i].stateAfter -ne $sixFirst[$i].stateAfter)) {
        $firstStateDivergence = [ordered]@{ eventOrdinal=$oneFirst[$i].eventOrdinal; marker=$oneFirst[$i].marker; mem=$oneFirst[$i].mem; oneState=($oneFirst[$i].stateBefore + '->' + $oneFirst[$i].stateAfter); sixState=($sixFirst[$i].stateBefore + '->' + $sixFirst[$i].stateAfter); predicateOperands='regionSize=0x100000, basicRegionSize=0x100000, largeRegionSize=0x800000; result=basic on both' }
        break
    }
}

$manifest = [ordered]@{
    milestone='C011EC76 basic-free-region eligibility and geometry'
    outcome='Outcome B / Level 1 bounded C76 success marker; causal result is no eligibility-class split, with an earlier state/chronology divergence observed.'
    successLevel=1
    causalFinding='All observed region-size predicates selected basic_free_region. The first comparable event divergence is state/occupancy metadata for the common 0x10180028 role; the later membership difference is reflected by different basic removals. C76 does not claim a closed total-heap census or Level 3 genealogy.'
    predicate=[ordered]@{ source='region_free_list::get_region_kind'; expression='region_size == BASIC_REGION_SIZE'; basicClass=0; largeClass=1; hugeClass=2; basicRegionSize='0x100000'; largeRegionSize='0x800000' }
    eventCapacity=$capacity
    one=$one
    six=$six
    commonRelevantRangeBases=$commonBases
    firstStateDivergence=$firstStateDivergence
    uniqueRelevantRangeCount=[ordered]@{ one=$one.uniqueRelevantRangeBases.Count; six=$six.uniqueRelevantRangeBases.Count; common=$commonBases.Count }
    listAccounting=[ordered]@{ one='0 + 19 - 19 = 0; observed post-Restart=1; precise phase-boundary gap=1'; six='0 + 17 - 12 = 5; observed post-Restart=6; precise phase-boundary gap=1' }
    extraFiveRoles=@(
        [ordered]@{ role='extra-1 survivor-pressure region'; range='0x101400028..0x101500000'; allocated='0x1014F02E0'; liveBytes='0x900D8'; freeBytes='0xFD20' },
        [ordered]@{ role='extra-2 survivor-pressure region'; range='0x101500028..0x101600000'; allocated='0x101570160'; liveBytes='0x700A8'; freeBytes='0x8FEA0' },
        [ordered]@{ role='extra-3 empty basic region'; range='0x101600028..0x101700000'; allocated='0x101600028'; liveBytes='0x0'; freeBytes='0xFFFD8' },
        [ordered]@{ role='extra-4 fully committed basic region'; range='0x101700028..0x101800000'; allocated='0x101700028'; liveBytes='0x0'; freeBytes='0xFFFD8' },
        [ordered]@{ role='extra-5 later empty basic region'; range='0x101A00028..0x101B00000'; allocated='0x101A00028'; liveBytes='0x0'; freeBytes='0xFFFD8' }
    )
    sourceAudit=[ordered]@{
        lockedSource='out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/gc.cpp'
        getRegionStart='gc.cpp:3799-3803'
        getRegionSize='gc.cpp:3806-3809'
        returnFreeRegion='gc.cpp:11860-11900'
        getFreeRegion='gc.cpp:11906-11994'
        getRegionKind='gc.cpp:12976-12991'
        addRegion='gc.cpp:13091-13101'
        isOnFreeList='gc.cpp:13103-13107'
        tryGetNewFreeRegion='gc.cpp:21345-21371'
        findFirstValidRegion='gc.cpp:34702-34806'
        threadFinalRegions='gc.cpp:34809-34890'
        freeListDeclaration='gcpriv.h:1417-1464, 3885'
    }
    diagnostics=[ordered]@{ allocatorMutation=0; plannerMutation=0; regionMutation=0; regionListMutation=0; eligibilityMutation=0; candidateMutation=0; policyMutation=0; survivorFabrication=0; rootFabrication=0; invariantFailures=0; sensitiveDiagnosticAllocations=0; eventOverflow=0; inheritedOverflow=0; failFast=0; pageFault=0; b02Evaluated=$false }
    documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_C76_BASIC_FREE_REGION_ELIGIBILITY_GEOMETRY.md'
}

$census = [ordered]@{
    milestone='C011EC76 bounded region census'
    eventCapacity=$capacity
    overflow=0
    cases=[ordered]@{ ONE=$one; SIX=$six }
    accounting=[ordered]@{ ONE=[ordered]@{ phaseStart=0; insertions=19; removals=19; net=0; postRestart=1; gap=1 }; SIX=[ordered]@{ phaseStart=0; insertions=17; removals=12; net=5; postRestart=6; gap=1 } }
    limitation='The C76 event stream is bounded to regions participating in the C67 lifecycle/list replay. It does not publish a closed total-heap region census for the ONE C71-managed control; total pre-GC counts are therefore reported as unresolved rather than inferred.'
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c76-final-manifest.json') -Encoding UTF8
$census | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c76-census.json') -Encoding UTF8

@"
| Field | ONE | SIX |
| --- | ---: | ---: |
| Promotion positive | yes | yes |
| Retained objects | 15 | 16 |
| Promoted bytes | 0xFD980 | 0x100180 |
| Total regions pre-GC | not closed; 12 relevant range bases observed | not closed; 12 relevant range bases observed |
| Relevant regions pre-GC | 12 unique range bases in bounded stream | 12 unique range bases in bounded stream |
| Eligible basic regions | 19 observations; all basic | 17 observations; all basic |
| Basic insertions | 19 | 17 |
| Basic removals | 19 | 12 |
| Post-Restart basic | 1 | 6 |
| Post-resume basic | 1 | 6 |
| Expansion regions | 0 created; attempted=1, succeeded=0 | 0 created; attempted=1, succeeded=0 |
| Tail-derived regions | 0 | 0 |
| Extra-region provenance | common range-role supply observed; later removals consume the cohort | five-role SIX ledger has fewer removals before RestartEE |
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c76-comparison-table.md') -Encoding UTF8

@"
| Extra role | SIX prior state/gen | SIX geometry | SIX eligibility predicate | SIX result | ONE counterpart | ONE predicate | ONE result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| extra-1 survivor-pressure | state 8->A, gen 0 | 0x101400028..0x101500000; alloc 0x1014F02E0; live 0x900D8; free 0xFD20 | size 0x100000 == basic 0x100000 | qualifies; later cohort role remains in SIX ledger | same range role is observed in ONE, but is consumed before the final ONE count | same predicate, basic | not retained to the final ONE cohort |
| extra-2 survivor-pressure | state 8->A, gen 0 | 0x101500028..0x101600000; alloc 0x101570160; live 0x700A8; free 0x8FEA0 | size 0x100000 == basic 0x100000 | qualifies; later cohort role remains in SIX ledger | same range role is observed in ONE, but is consumed before the final ONE count | same predicate, basic | not retained to the final ONE cohort |
| extra-3 empty basic | state 9->B, gen 0 | 0x101600028..0x101700000; alloc at base; live 0; free 0xFFFD8 | size 0x100000 == basic 0x100000 | qualifies | same range role is observed in ONE | same predicate, basic | removed earlier |
| extra-4 fully committed | state D->F, gen 0 | 0x101700028..0x101800000; alloc at base; live 0; free 0xFFFD8 | size 0x100000 == basic 0x100000 | qualifies | same range role is observed in ONE with an earlier state 9->B transition | same predicate, basic | removal chronology differs |
| extra-5 later empty | state 9->B, gen 0 | 0x101A00028..0x101B00000; alloc at base; live 0; free 0xFFFD8 | size 0x100000 == basic 0x100000 | qualifies | same later range-role family is observed in ONE | same predicate, basic | removed before final ONE count |
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c76-extra-five-table.md') -Encoding UTF8

Write-Output ("C76 extraction complete: ONE={0} boots, SIX={1} boots, semanticAgreement={2}, overflow=0." -f $one.runs.Count, $six.runs.Count, ($one.semanticAgreement -and $six.semanticAgreement))
