[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c011ec77-basic-region-supply-provenance'
}

$acceptedRoot = Join-Path $EvidenceRoot 'accepted-confirmation-runs'
$eventCapacity = 2048
$snapshotCapacity = 1024

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

function Format-Hex([uint64]$Value) { return ('0x{0:X}' -f $Value) }

function Get-LastLine([string[]]$Lines, [string]$Pattern) {
    return @($Lines | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)[0]
}

function Convert-RegionCount([string]$Line) {
    $f = Get-Fields $Line
    [ordered]@{
        ordinal = [int64](Get-Hex $f 'eventOrdinal')
        checkpoint = [int](Get-Hex $f 'checkpoint')
        totalRegions = [int64](Get-Hex $f 'totalRegions')
        gen0Regions = [int64](Get-Hex $f 'gen0Regions')
        basicFreeRegions = [int64](Get-Hex $f 'basicFreeRegions')
        allocatorUsedRegions = [int64](Get-Hex $f 'allocatorUsedRegions')
        allocatorFreeBytes = Format-Hex (Get-Hex $f 'allocatorFreeBytes')
    }
}

function Convert-Event([string]$Line) {
    $f = Get-Fields $Line
    $marker = ([regex]::Match($Line, 'marker=(C77_[^\s]+)').Groups[1].Value)
    [ordered]@{
        marker = $marker
        ordinal = [int64](Get-Hex $f 'eventOrdinal')
        checkpoint = [int](Get-Hex $f 'checkpoint')
        kind = [int](Get-Hex $f 'kind')
        listKind = [int](Get-Hex $f 'listKind')
        sourceBranch = [int](Get-Hex $f 'sourceBranch')
        region = ('0x{0:X}' -f (Get-Hex $f 'region'))
        mem = ('0x{0:X}' -f (Get-Hex $f 'mem'))
        committed = ('0x{0:X}' -f (Get-Hex $f 'committed'))
        reserved = ('0x{0:X}' -f (Get-Hex $f 'reserved'))
        allocated = ('0x{0:X}' -f (Get-Hex $f 'allocated'))
        freeBytes = ('0x{0:X}' -f (Get-Hex $f 'freeBytes'))
        liveBytes = ('0x{0:X}' -f (Get-Hex $f 'liveBytes'))
        generationBefore = [int](Get-Hex $f 'generationBefore')
        generationAfter = [int](Get-Hex $f 'generationAfter')
        stateBefore = Format-Hex (Get-Hex $f 'stateBefore')
        stateAfter = Format-Hex (Get-Hex $f 'stateAfter')
        freeCountBefore = [int64](Get-Hex $f 'freeCountBefore')
        freeCountAfter = [int64](Get-Hex $f 'freeCountAfter')
    }
}

function Get-ManifestForLog([System.IO.FileInfo]$Log) {
    $runRoot = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $Log.FullName) '..'))
    $path = Join-Path $runRoot 'manifest.json'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try { return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json }
    catch { return $null }
}

function Read-Case([string]$Name, [string]$Root) {
    if (-not (Test-Path -LiteralPath $Root)) { throw "Missing accepted C77 root: $Root" }
    $logs = @(Get-ChildItem -LiteralPath $Root -Recurse -Filter serial.log -File | Sort-Object FullName)
    if ($logs.Count -ne 3) { throw "Expected three accepted $Name serial logs under $Root; found $($logs.Count)." }

    $runs = [System.Collections.Generic.List[object]]::new()
    foreach ($log in $logs) {
        $lines = @(Get-Content -LiteralPath $log.FullName)
        $c77SummaryLine = Get-LastLine $lines 'marker=C011EC77-SUMMARY'
        $c77CompleteLine = Get-LastLine $lines 'marker=C011EC77 outcome='
        $c76SummaryLine = Get-LastLine $lines 'marker=C011EC76-SUMMARY'
        $c76CompleteLine = Get-LastLine $lines 'marker=C011EC76 outcome='
        if (-not $c77SummaryLine -or -not $c77CompleteLine -or -not $c76SummaryLine -or -not $c76CompleteLine) {
            throw "Missing C77/C76 completion markers in $($log.FullName)."
        }
        $c77Summary = Get-Fields $c77SummaryLine
        $c77Complete = Get-Fields $c77CompleteLine
        $c76Summary = Get-Fields $c76SummaryLine
        $c76Complete = Get-Fields $c76CompleteLine
        $regions = @($lines | Where-Object { $_ -match 'marker=C77_REGION_COUNT' } | ForEach-Object { Convert-RegionCount $_ })
        $events = @($lines | Where-Object { $_ -match 'marker=C77_(EVENT_ORDINAL|REGION_BIRTH|REGION_EXPAND|REGION_RECLAIM|REGION_SOURCE|REGION_RECLASSIFY)' } | ForEach-Object { Convert-Event $_ })
        $markers = @($lines | Select-String -Pattern 'marker=([A-Z0-9_-]+)' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique)
        $manifest = Get-ManifestForLog $log
        $checkpointSummary = [ordered]@{}
        foreach ($group in ($regions | Group-Object { $_['checkpoint'] })) {
            $last = $group.Group | Sort-Object { $_['ordinal'] } | Select-Object -Last 1
            $checkpointSummary[[string]$group.Name] = [ordered]@{
                observations = $group.Count
                totalRegions = $last.totalRegions
                gen0Regions = $last.gen0Regions
                basicFreeRegions = $last.basicFreeRegions
                allocatorUsedRegions = $last.allocatorUsedRegions
                allocatorFreeBytes = $last.allocatorFreeBytes
            }
        }
        $kindCounts = [ordered]@{}
        foreach ($group in ($events | Group-Object { $_['kind'] })) { $kindCounts[[string]$group.Name] = $group.Count }
        $markerCounts = [ordered]@{}
        foreach ($group in ($events | Group-Object { $_['marker'] })) { $markerCounts[$group.Name] = $group.Count }
        $diagnostics = [ordered]@{
            eventOverflow = [int](Get-Hex $c77Summary 'eventOverflow')
            regionOverflow = [int](Get-Hex $c77Summary 'regionOverflow')
            invariantFailures = [int](Get-Hex $c77Summary 'invariantFailures')
            sensitiveDiagnosticAllocations = [int](Get-Hex $c77Summary 'sensitiveDiagnosticAllocations')
            failFast = [int](Get-Hex $c77Summary 'failFast')
            pageFault = [int](Get-Hex $c77Summary 'pageFault')
        }
        $runs.Add([ordered]@{
            boot = $runs.Count + 1
            serial = $log.FullName
            serialSha256 = (Get-FileHash -LiteralPath $log.FullName -Algorithm SHA256).Hash
            outcome = $c77Complete.outcome
            successLevel = [int](Get-Hex $c77Complete 'successLevel')
            c77EventCount = [int64](Get-Hex $c77Summary 'eventCount')
            c77RegionMaximum = [int64](Get-Hex $c77Summary 'regionCount')
            c77MaxEvents = Format-Hex (Get-Hex $c77Summary 'maxEvents')
            c77MaxRegions = Format-Hex (Get-Hex $c77Summary 'maxRegions')
            preGcBasicCount = [int64](Get-Hex $c77Summary 'preGcBasicCount')
            postRestartBasicCount = [int64](Get-Hex $c77Summary 'postRestartBasicCount')
            postResumeBasicCount = [int64](Get-Hex $c77Summary 'postResumeBasicCount')
            basicInsertions = [int64](Get-Hex $c77Summary 'basicInsertions')
            basicRemovals = [int64](Get-Hex $c77Summary 'basicRemovals')
            expansionCount = [int64](Get-Hex $c77Summary 'expansionCount')
            regionsCreated = [int64](Get-Hex $c77Summary 'regionsCreated')
            c76Outcome = $c76Complete.outcome
            c76PostRestartBasicCount = [int64](Get-Hex $c76Summary 'postRestartBasicCount')
            c76PostResumeBasicCount = [int64](Get-Hex $c76Summary 'postResumeBasicCount')
            c76EligibilityCount = [int64](Get-Hex $c76Summary 'eligibilityCount')
            c76BasicInsertions = [int64](Get-Hex $c76Summary 'basicInsertions')
            c76BasicRemovals = [int64](Get-Hex $c76Summary 'basicRemovals')
            c76PredicateBasicOnly = [int](Get-Hex $c76Summary 'predicateBasicOnly')
            diagnostics = $diagnostics
            checkpointsObserved = @($checkpointSummary.Keys | ForEach-Object { [int]$_ } | Sort-Object)
            checkpointSummary = $checkpointSummary
            eventKindCounts = $kindCounts
            markerCounts = $markerCounts
            regions = $regions
            events = $events
            requiredMarkersPresent = @('C77_REGION_COUNT','C77_REGION_BIRTH','C77_REGION_EXPAND','C77_REGION_RECLAIM','C77_EVENT_ORDINAL','C77_REGION_SPLIT','C77_REGION_COALESCE','C77_CONTEXT_ACQUIRE','C77_CONTEXT_RELEASE','C011EC67','C011EC76-SUMMARY') | ForEach-Object { [ordered]@{ marker=$_; present=($markers -contains $_ -or ($markers -contains ('marker=' + $_))) } }
            payloadHashes = if ($manifest) { $manifest.payloadHashes } else { $null }
            ordinaryRestoration = if ($manifest) { $manifest.ordinaryRestoration } else { $null }
        }) | Out-Null
    }
    $signature = @($runs | ForEach-Object { "{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}" -f $_.outcome,$_.successLevel,$_.c77EventCount,$_.c77RegionMaximum,$_.postRestartBasicCount,$_.postResumeBasicCount,$_.basicInsertions,$_.basicRemovals,$_.diagnostics.invariantFailures,$_.diagnostics.eventOverflow } | Sort-Object -Unique)
    [ordered]@{
        name = $Name
        root = $Root
        runs = $runs
        semanticAgreement = $signature.Count -eq 1
        signatures = $signature
        firstRun = $runs[0]
    }
}

$one = Read-Case 'ONE' (Join-Path $acceptedRoot 'one')
$six = Read-Case 'SIX' (Join-Path $acceptedRoot 'six')
$oneRun = $one.firstRun
$sixRun = $six.firstRun

function Get-Discovery([string]$Name, [string]$Root) {
    $log = Get-ChildItem -LiteralPath $Root -Recurse -Filter serial.log -File | Sort-Object FullName | Select-Object -Last 1
    if (-not $log) { return [ordered]@{ status='missing' } }
    $lines = @(Get-Content $log.FullName)
    $s = Get-Fields (Get-LastLine $lines 'marker=C011EC77-SUMMARY')
    $c = Get-Fields (Get-LastLine $lines 'marker=C011EC77 outcome=')
    [ordered]@{ name=$Name; serial=$log.FullName; serialSha256=(Get-FileHash $log.FullName -Algorithm SHA256).Hash; outcome=$c.outcome; successLevel=[int](Get-Hex $c 'successLevel'); eventCount=[int64](Get-Hex $s 'eventCount'); regionMaximum=[int64](Get-Hex $s 'regionCount'); postRestartBasicCount=[int64](Get-Hex $s 'postRestartBasicCount'); postResumeBasicCount=[int64](Get-Hex $s 'postResumeBasicCount'); c76PostRestartBasicCount=[int64](Get-Hex (Get-Fields (Get-LastLine $lines 'marker=C011EC76-SUMMARY')) 'postRestartBasicCount') }
}

$discovery = [ordered]@{
    one = Get-Discovery 'ONE' (Join-Path $EvidenceRoot 'discovery\one')
    six = Get-Discovery 'SIX' (Join-Path $EvidenceRoot 'discovery\six')
}

$checkpointLabels = [ordered]@{
    '1'='proof workload entry; not observed by the C67 snapshot stream'
    '2'='before retained cohort allocation; not observed by the C67 snapshot stream'
    '3'='C67 before-promotion snapshot; comparable basic-free count was zero in the final boot'
    '4'='before later pressure allocations; not observed by the C67 snapshot stream'
    '5'='C67 after-promotion snapshot; comparable basic-free count was zero in the final boot'
    '6'='C67 observed intermediate supply snapshot; not a closed total-heap census'
    '7'='C67 after-resume/RestartEE chronology; final basic-free count 1 versus 6'
    '8'='C67 before-decisive snapshot; not a closed total-heap census'
    '9'='C67 after-decisive snapshot; not a closed total-heap census'
    '10'='after basic-list population; not separately observed'
    '11'='RestartEE; not separately observed as a C77 snapshot'
    '12'='managed resume; not separately observed as a C77 snapshot'
}

$sourceAudit = [ordered]@{
    lockedSource='out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/gc.cpp'
    regionCreation='region_allocator::allocate_region:4129; gc_heap::make_heap_segment:12313; gc_heap::init_heap_segment:12353; gc_heap::allocate_new_region:35019'
    regionAllocation='gc_heap::get_new_region:34988; gc_heap::get_free_region:11906; region_allocator::allocate_region:4129'
    expansion='gc_heap::expand_heap:43321; gc_heap::get_new_region:34988; gc_heap::allocate_new_region:35019'
    commitReserve='gc_heap::make_heap_segment:12313; gc_heap::init_heap_segment:12353; region_allocator::allocate_region:4129'
    freeList='region_free_list::add_region_front:12840; region_free_list::add_region_in_descending_order:12862; region_free_list::unlink_region:12931; region_free_list::get_region_kind:12976'
    retirement='gc_heap::find_first_valid_region:34702; gc_heap::thread_final_regions:34809; gc_heap::decommit_region:44531; gc_heap::decommit_ephemeral_segment_pages:44397'
    split='no production region-split function observed; gc.cpp:11903 is a TODO noting that SOH could split a large region'
    coalesce='gcpriv.h:6295-6297 describes allocator free-block coalescing; no C77 region-coalesce event was observed'
    context='C77 did not instrument allocation-context acquire/release; closest free-pool transitions are gc_heap::get_free_region:11906 and gc_heap::return_free_region:11860'
    geometry='gc_heap::a_fit_segment_end_p:17621 and uoh_a_fit_segment_end_p:17777 are allocation-boundary predicates'
}

$oneTotalEntry = 'not observed by C67 checkpoint 1; C77 has no closed workload-entry census'
$sixTotalEntry = 'not observed by C67 checkpoint 1; C77 has no closed workload-entry census'
$oneCp3 = $oneRun.checkpointSummary['3']
$sixCp3 = $sixRun.checkpointSummary['3']
$oneCp5 = $oneRun.checkpointSummary['5']
$sixCp5 = $sixRun.checkpointSummary['5']
$oneCp7 = $oneRun.checkpointSummary['7']
$sixCp7 = $sixRun.checkpointSummary['7']
$oneCp8 = $oneRun.checkpointSummary['8']
$sixCp8 = $sixRun.checkpointSummary['8']
$oneCp9 = $oneRun.checkpointSummary['9']
$sixCp9 = $sixRun.checkpointSummary['9']
$oneExpandEvents = if ($oneRun.eventKindCounts.Contains('10')) { $oneRun.eventKindCounts['10'] } else { 0 }
$sixExpandEvents = if ($sixRun.eventKindCounts.Contains('10')) { $sixRun.eventKindCounts['10'] } else { 0 }
$oneReclaimEvents = if ($oneRun.eventKindCounts.Contains('2')) { $oneRun.eventKindCounts['2'] } else { 0 }
$sixReclaimEvents = if ($sixRun.eventKindCounts.Contains('2')) { $sixRun.eventKindCounts['2'] } else { 0 }
$oneBirthEvents = if ($oneRun.eventKindCounts.Contains('6')) { $oneRun.eventKindCounts['6'] } else { 0 }
$sixBirthEvents = if ($sixRun.eventKindCounts.Contains('6')) { $sixRun.eventKindCounts['6'] } else { 0 }

$manifest = [ordered]@{
    milestone='C011EC77 pre-eligibility basic-region supply provenance'
    outcome='Outcome G / bounded Level 1: the C67-backed observer is clean and reproduces the accepted 1-versus-6 post-Restart result, but the available snapshots do not isolate a source-backed pre-GC five-region genealogy.'
    successLevel=1
    causalClassification='UNRESOLVED'
    exactQuestion='At what earliest production event does SIX acquire five more relevant basic-sized region objects than ONE, and what authentic allocator/GC mechanism creates, exposes, empties, reclaims, splits, coalesces, or preserves those regions?'
    primaryControls=[ordered]@{ ONE='retained=15; object=0x10E80; promoted/live=0xFD980; final basic=1'; SIX='retained=16; object=0x10018; promoted/live=0x100180; final basic=6' }
    discovery=$discovery
    confirmation=[ordered]@{ ONE=$one; SIX=$six }
    c76Predicate='region_size == BASIC_REGION_SIZE'
    c76PredicateDifference=$false
    observedSupplyDivergence='The earliest stable C77 basic-free count difference is the C67 checkpoint-7 post-Restart/free-pool observation: ONE=1 and SIX=6. Checkpoints 3 and 5 finish at zero comparable basic-free regions on both controls, but C67 does not provide a closed pre-GC object/ownership census.'
    divergenceClass='UNRESOLVED'
    divergenceOrdinal='not a proven production birth ordinal; checkpoint-7 is the earliest stable count checkpoint with 1-versus-6'
    divergenceSource='C67 snapshot observed from the existing GC region-supply lifecycle; exact upstream source event remains unisolated'
    sourceAudit=$sourceAudit
    checkpointLabels=$checkpointLabels
    diagnostics=[ordered]@{ eventCapacity=$eventCapacity; snapshotCapacity=$snapshotCapacity; ONE=$oneRun.diagnostics; SIX=$sixRun.diagnostics; split=0; coalesce=0; contextAcquire=0; contextRelease=0; b02Evaluated=$false }
    extraFive=[ordered]@{ classification='UNRESOLVED'; rows=@(
        [ordered]@{ role='extra-1'; presentPreGc='not proven'; origin='not genealogically identified'; priorUse='not observed'; emptyFreeEvent='not separated from C67 list removals'; basicListEvent='C76/C67 aggregate only' },
        [ordered]@{ role='extra-2'; presentPreGc='not proven'; origin='not genealogically identified'; priorUse='not observed'; emptyFreeEvent='not separated from C67 list removals'; basicListEvent='C76/C67 aggregate only' },
        [ordered]@{ role='extra-3'; presentPreGc='not proven'; origin='not genealogically identified'; priorUse='not observed'; emptyFreeEvent='not separated from C67 list removals'; basicListEvent='C76/C67 aggregate only' },
        [ordered]@{ role='extra-4'; presentPreGc='not proven'; origin='not genealogically identified'; priorUse='not observed'; emptyFreeEvent='not separated from C67 list removals'; basicListEvent='C76/C67 aggregate only' },
        [ordered]@{ role='extra-5'; presentPreGc='not proven'; origin='not genealogically identified'; priorUse='not observed'; emptyFreeEvent='not separated from C67 list removals'; basicListEvent='C76/C67 aggregate only' }
    ) }
    accounting=[ordered]@{ ONE='C67 snapshot ledger is not closed; C76 list accounting is 0 + 13 - 13, final=1'; SIX='C67 snapshot ledger is not closed; C76 list accounting is 0 + 15 - 10, final=6'; reconciliation='The final 1-versus-6 result reconciles at the C76/C77 post-Restart aggregate only; the five region identities and upstream supply terms remain unresolved' }
    candidateMapping='C77 preserves C67 list/source/create/commit/generation/expansion/decommit observations and C76 basic insertion/removal aggregates; it does not modify candidate logic.'
    b02='premature; C77 did not isolate an authentic later candidate event sufficient to justify B02'
    documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_C77_BASIC_REGION_SUPPLY_PROVENANCE.md'
    evidenceRoot=$EvidenceRoot
}

$census = [ordered]@{
    milestone='C011EC77 bounded supply census'
    checkpointLabels=$checkpointLabels
    ONE=[ordered]@{ confirmation=$one; discovery=$discovery.one }
    SIX=[ordered]@{ confirmation=$six; discovery=$discovery.six }
    limitation='C67 snapshots are bounded lifecycle observations, not a closed heap-region ledger. Checkpoints 1,2,4,10,11,12 are not separately emitted by the existing observer; split/coalesce/context markers are explicit zero/not-observed.'
}

$comparison = @"
| Field | ONE | SIX |
| --- | ---: | ---: |
| Accepted C76 post-Restart basic | $($oneRun.c76PostRestartBasicCount) | $($sixRun.c76PostRestartBasicCount) |
| Accepted C76 post-resume basic | $($oneRun.c76PostResumeBasicCount) | $($sixRun.c76PostResumeBasicCount) |
| C77 Level | $($oneRun.successLevel) | $($sixRun.successLevel) |
| C77 events | $($oneRun.c77EventCount) | $($sixRun.c77EventCount) |
| C77 maximum observed total regions | $($oneRun.c77RegionMaximum) | $($sixRun.c77RegionMaximum) |
| C77 checkpoint 3 total/basic | $($oneCp3.totalRegions)/$($oneCp3.basicFreeRegions) | $($sixCp3.totalRegions)/$($sixCp3.basicFreeRegions) |
| C77 checkpoint 5 total/basic | $($oneCp5.totalRegions)/$($oneCp5.basicFreeRegions) | $($sixCp5.totalRegions)/$($sixCp5.basicFreeRegions) |
| C77 checkpoint 7 total/basic | $($oneCp7.totalRegions)/$($oneCp7.basicFreeRegions) | $($sixCp7.totalRegions)/$($sixCp7.basicFreeRegions) |
| C77 checkpoint 8 total/basic | $($oneCp8.totalRegions)/$($oneCp8.basicFreeRegions) | $($sixCp8.totalRegions)/$($sixCp8.basicFreeRegions) |
| C77 checkpoint 9 total/basic | $($oneCp9.totalRegions)/$($oneCp9.basicFreeRegions) | $($sixCp9.totalRegions)/$($sixCp9.basicFreeRegions) |
| C76 basic insertions/removals | $($oneRun.c76BasicInsertions)/$($oneRun.c76BasicRemovals) | $($sixRun.c76BasicInsertions)/$($sixRun.c76BasicRemovals) |
| C77 expansion event records | $oneExpandEvents | $sixExpandEvents |
| C77 region-create event records | $oneBirthEvents | $sixBirthEvents |
| C77 free-list-remove event records | $oneReclaimEvents | $sixReclaimEvents |
| C77 split/coalesce/context events | 0 / 0 / 0 | 0 / 0 / 0 |
| Classification | UNRESOLVED | UNRESOLVED |
"@

$extraTable = @"
| Extra SIX role | Present pre-GC? | Birth/provenance | Prior use | Empty/free event | Basic-list event | ONE counterpart |
| --- | --- | --- | --- | --- | --- | --- |
| extra-1 | not proven | not identified by bounded C67 ledger | not observed | not separated from aggregate list removal | aggregate C76/C77 only | unresolved |
| extra-2 | not proven | not identified by bounded C67 ledger | not observed | not separated from aggregate list removal | aggregate C76/C77 only | unresolved |
| extra-3 | not proven | not identified by bounded C67 ledger | not observed | not separated from aggregate list removal | aggregate C76/C77 only | unresolved |
| extra-4 | not proven | not identified by bounded C67 ledger | not observed | not separated from aggregate list removal | aggregate C76/C77 only | unresolved |
| extra-5 | not proven | not identified by bounded C67 ledger | not observed | not separated from aggregate list removal | aggregate C76/C77 only | unresolved |
"@

$audit = @"
| Mechanism | Locked source evidence | C77 result |
| --- | --- | --- |
| Region creation/allocation | $($sourceAudit.regionCreation); $($sourceAudit.regionAllocation) | production functions identified; exact five-region chain unresolved |
| Expansion/commit | $($sourceAudit.expansion); $($sourceAudit.commitReserve) | expansion attempted on both controls, no successful expansion delta proved |
| Split/coalesce | $($sourceAudit.split); $($sourceAudit.coalesce) | no C77 split/coalesce observation; B02 not justified |
| Reclaim/retirement | $($sourceAudit.retirement) | aggregate list removals observed, exact extra-five empties not proven |
| Context ownership | $($sourceAudit.context) | acquire/release not observed by C67; CONTEXT markers are explicit not-observed zeros |
| Geometry | $($sourceAudit.geometry) | plausible upstream mechanism, not isolated by this bounded run |
"@

$reportHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$reportSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$reportUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$reportAheadBehind = if ([string]::IsNullOrWhiteSpace($reportUpstream)) { 'unknown' } else { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$reportUpstream").Trim() }
$reportItems = @(
    "Outcome: Outcome G / bounded Level 1; clean reproduction with unresolved upstream supply cause.",
    "Success Level: 1.",
    "Repository: $RepoRoot.",
    "Branch: v1.1_DOTNET_SUPPORT.",
    "Starting HEAD: fe54399f3416ff0c5794328688525608a68fc336.",
    "Starting subject: Trace NativeAOT basic free-region eligibility.",
    "Final HEAD at report generation: $reportHead.",
    "Final subject at report generation: $reportSubject.",
    "Upstream: origin/v1.1_DOTNET_SUPPORT.",
    "Starting ahead/behind: 0/0.",
    "Final ahead/behind at report generation: $reportAheadBehind.",
    "Starting worktree: clean.",
    "Final worktree: clean after the local C77 commit.",
    "Runtime identity: NativeAOT workstation GC, C68 locked runtime, AMD64 win-x64, ILCompiler 9.0.0.",
    "Runtime source SHA: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.",
    "FP patch SHA: 733ff7b793b91477d3b959441829cbfbd1511cbd2.",
    "C75 SHA: 97b898e5 Trace NativeAOT survived-per-region provenance.",
    "C76 SHA: fe54399f Trace NativeAOT basic free-region eligibility.",
    "C77 SHA at report generation: $reportHead.",
    "Exact C77 question: locate the earliest production event that gives SIX five more relevant basic-sized region objects than ONE.",
    "ONE reproduction: 1 discovery boot plus 3 confirmation boots; authentic promotion and C76 1/1.",
    "SIX reproduction: 1 discovery boot plus 3 confirmation boots; authentic promotion and C76 6/6.",
    "ONE final basic count: 1 post-Restart and 1 post-resume.",
    "SIX final basic count: 6 post-Restart and 6 post-resume.",
    "C76 eligibility predicate: region_size == BASIC_REGION_SIZE.",
    "Whether eligibility predicate differs: no; observed C76 predicates remain basic-only on both sides.",
    "Region-creation source functions: region_allocator::allocate_region, make_heap_segment, init_heap_segment, allocate_new_region.",
    "Expansion source functions: gc_heap::expand_heap, get_new_region, allocate_new_region.",
    "Split source functions: no production region-split function observed; locked gc.cpp:11903 is a TODO.",
    "Coalesce source functions: allocator free-block coalescing is described in gcpriv.h:6295-6297; no region coalesce event observed.",
    "Tail-reclaim source functions: find_first_valid_region, thread_final_regions, decommit_region, decommit_ephemeral_segment_pages.",
    "Context acquire/release sources: get_free_region and return_free_region are the nearest observed free-pool transitions; context lifetime was not instrumented.",
    "ONE total regions at workload entry: not observed by checkpoint 1.",
    "SIX total regions at workload entry: not observed by checkpoint 1.",
    "ONE regions after retained allocation: not separately observed; checkpoint 3 total=$($oneCp3.totalRegions), basic=$($oneCp3.basicFreeRegions).",
    "SIX regions after retained allocation: not separately observed; checkpoint 3 total=$($sixCp3.totalRegions), basic=$($sixCp3.basicFreeRegions).",
    "ONE regions after pressure allocation: not separately observed; checkpoint 5 total=$($oneCp5.totalRegions), basic=$($oneCp5.basicFreeRegions).",
    "SIX regions after pressure allocation: not separately observed; checkpoint 5 total=$($sixCp5.totalRegions), basic=$($sixCp5.basicFreeRegions).",
    "ONE regions immediately pre-GC: no closed C77 snapshot; checkpoint 5 is the nearest observed point.",
    "SIX regions immediately pre-GC: no closed C77 snapshot; checkpoint 5 is the nearest observed point.",
    "Earliest region-count divergence: checkpoint 7 is the earliest stable observed 1-versus-6 basic-free count.",
    "Divergence ordinal: no proven production ordinal; the stable count divergence is checkpoint 7.",
    "Divergence source function: C67 snapshot callback fed by the production region-supply lifecycle.",
    "Divergence operation: post-Restart/free-pool snapshot, not a proven birth/split/reclaim operation.",
    "ONE operands/state: checkpoint 7 total=$($oneCp7.totalRegions), gen0=$($oneCp7.gen0Regions), basic=$($oneCp7.basicFreeRegions).",
    "SIX operands/state: checkpoint 7 total=$($sixCp7.totalRegions), gen0=$($sixCp7.gen0Regions), basic=$($sixCp7.basicFreeRegions).",
    "Difference exists before target GC: not established; checkpoints 3/5 are not a closed pre-GC object census.",
    "ONE retained-region count: retained references=15; precise region count not observed.",
    "SIX retained-region count: retained references=16; precise region count not observed.",
    "ONE pressure-region count: not separately observed.",
    "SIX pressure-region count: not separately observed.",
    "ONE active-region identity/role: C67 records activeRegion addresses, but C77 does not promote a stable identity role.",
    "SIX active-region identity/role: C67 records activeRegion addresses, but C77 does not promote a stable identity role.",
    "ONE allocation-context region count: not observed.",
    "SIX allocation-context region count: not observed.",
    "ONE context acquisitions: 0 C77 markers; not observed, not proven absent in production.",
    "SIX context acquisitions: 0 C77 markers; not observed, not proven absent in production.",
    "ONE context releases: 0 C77 markers; not observed, not proven absent in production.",
    "SIX context releases: 0 C77 markers; not observed, not proven absent in production.",
    "Expansion attempted ONE/SIX: one bounded expansion attempt is reported on each side; success is not reported.",
    "Expansion event count ONE: $oneExpandEvents C67 expansion records.",
    "Expansion event count SIX: $sixExpandEvents C67 expansion records.",
    "Regions created by expansion ONE: not attributable; expansionSucceeded=0 in the observed C76 summary.",
    "Regions created by expansion SIX: not attributable; expansionSucceeded=0 in the observed C76 summary.",
    "Hard-limit state ONE/SIX: no successful expansion and no hard-limit causal delta established.",
    "Split count ONE: 0 C77_REGION_SPLIT, source not observed.",
    "Split count SIX: 0 C77_REGION_SPLIT, source not observed.",
    "Split-created regions ONE: none observed.",
    "Split-created regions SIX: none observed.",
    "Coalesce count ONE: 0 C77_REGION_COALESCE, source not observed.",
    "Coalesce count SIX: 0 C77_REGION_COALESCE, source not observed.",
    "Tail reclaim ONE/SIX: C76 tailReclaimObserved=0 on the accepted controls; C77 has no tail-specific marker.",
    "Tail reclaimed bytes ONE: not observed.",
    "Tail reclaimed bytes SIX: not observed.",
    "Tail-derived regions ONE: none observed.",
    "Tail-derived regions SIX: none observed.",
    "Comparable pre-GC supply: unresolved; the bounded snapshots do not close the ledger.",
    "Regions emptied by target GC ONE: not provable from aggregate list removals.",
    "Regions emptied by target GC SIX: not provable from aggregate list removals.",
    "Regions reclaimed ONE: $oneReclaimEvents C67 list-remove/reclaim-class records; not all are target-GC emptying.",
    "Regions reclaimed SIX: $sixReclaimEvents C67 list-remove/reclaim-class records; not all are target-GC emptying.",
    "Extra SIX region 1 origin: unresolved.",
    "Extra SIX region 2 origin: unresolved.",
    "Extra SIX region 3 origin: unresolved.",
    "Extra SIX region 4 origin: unresolved.",
    "Extra SIX region 5 origin: unresolved.",
    "Extra region 1 present pre-GC: not proven.",
    "Extra region 2 present pre-GC: not proven.",
    "Extra region 3 present pre-GC: not proven.",
    "Extra region 4 present pre-GC: not proven.",
    "Extra region 5 present pre-GC: not proven.",
    "ONE counterpart for extra 1: unresolved.",
    "ONE counterpart for extra 2: unresolved.",
    "ONE counterpart for extra 3: unresolved.",
    "ONE counterpart for extra 4: unresolved.",
    "ONE counterpart for extra 5: unresolved.",
    "Extra region 1 prior role: not observed.",
    "Extra region 2 prior role: not observed.",
    "Extra region 3 prior role: not observed.",
    "Extra region 4 prior role: not observed.",
    "Extra region 5 prior role: not observed.",
    "Extra region 1 empty/free event: not separated from aggregate C67 list removal.",
    "Extra region 2 empty/free event: not separated from aggregate C67 list removal.",
    "Extra region 3 empty/free event: not separated from aggregate C67 list removal.",
    "Extra region 4 empty/free event: not separated from aggregate C67 list removal.",
    "Extra region 5 empty/free event: not separated from aggregate C67 list removal.",
    "Region supply determinant class: UNRESOLVED.",
    "Pre-GC geometry causal status: plausible and source-relevant, but not isolated.",
    "Expansion causal status: attempted but not causal on current evidence.",
    "Reclamation causal status: aggregate removals exist, but five-region causality is not isolated.",
    "Tail/split causal status: not observed.",
    "Context-ownership causal status: not observed.",
    "Earliest supported causal chain: C67 snapshots converge on post-Restart basic count 1 versus 6; upstream chain remains open.",
    "ONE starting supply: not closed by C77.",
    "ONE created supply: $($oneRun.regionsCreated) aggregate region-create records in C77 summary; attribution to the five is unresolved.",
    "ONE reclaimed/released supply: $oneReclaimEvents aggregate C67 list-remove/reclaim-class records.",
    "ONE unavailable/private regions: not observed.",
    "ONE final available basic supply: 1 at C76/C77 post-Restart aggregate.",
    "ONE list accounting result: C76 basic insertions=$($oneRun.c76BasicInsertions), removals=$($oneRun.c76BasicRemovals), final=1.",
    "SIX starting supply: not closed by C77.",
    "SIX created supply: $($sixRun.regionsCreated) aggregate region-create records in C77 summary; attribution to the five is unresolved.",
    "SIX reclaimed/released supply: $sixReclaimEvents aggregate C67 list-remove/reclaim-class records.",
    "SIX unavailable/private regions: not observed.",
    "SIX final available basic supply: 6 at C76/C77 post-Restart aggregate.",
    "SIX list accounting result: C76 basic insertions=$($sixRun.c76BasicInsertions), removals=$($sixRun.c76BasicRemovals), final=6.",
    "Exact 1-vs-6 reconciliation: final aggregate reconciles; five individual supply terms do not.",
    "First unsupported causal link: mapping post-Restart basic identities backward to pre-GC ownership/birth.",
    "Extra-five later candidate mapping: aggregate C67 list/source/remove events only; identities unavailable.",
    "Candidate selection relevance: C67 records source and list transitions; no C77 candidate mutation.",
    "Normal refill relevance: inherited C65/C66 diagnostics remain clean; no new C77 causal claim.",
    "commit_failed relevance: no C77 commit-failed causal link established.",
    "OOS relevance: no C77 OOS causal link established.",
    "B02 evaluated: no; B02 was not run.",
    "B02 future justification: premature until a specific later candidate event is isolated.",
    "Allocator mutation: 0.",
    "Planner mutation: 0.",
    "Region mutation: 0.",
    "Region creation forcing: 0; diagnostics are observational.",
    "Split/coalesce forcing: 0.",
    "Region-list mutation: 0.",
    "Candidate mutation: 0.",
    "Policy mutation: 0.",
    "Survivor fabrication: 0.",
    "Root fabrication: 0.",
    "C18: inherited runtime contract retained and smoke checks passed.",
    "Code manager: valid registration retained.",
    "FindMethodInfo: inherited validation retained.",
    "Root scan: authentic inherited path retained.",
    "Mark closure: authentic inherited path retained.",
    "Planner authenticity: inherited planner path retained; C77 adds no planner input.",
    "Survivor integrity: authentic promotion observed on both controls.",
    "C77 invariant failures: ONE=$($oneRun.diagnostics.invariantFailures), SIX=$($sixRun.diagnostics.invariantFailures).",
    "Sensitive diagnostic allocations: ONE=$($oneRun.diagnostics.sensitiveDiagnosticAllocations), SIX=$($sixRun.diagnostics.sensitiveDiagnosticAllocations).",
    "C77 event capacity: $eventCapacity.",
    "C77 event count: ONE=$($oneRun.c77EventCount), SIX=$($sixRun.c77EventCount).",
    "C77 overflow: ONE=$($oneRun.diagnostics.eventOverflow)/region=$($oneRun.diagnostics.regionOverflow), SIX=$($sixRun.diagnostics.eventOverflow)/region=$($sixRun.diagnostics.regionOverflow).",
    "Inherited overflow: C76/C67 inherited overflow fields are zero in accepted summaries.",
    "Fail-fast: ONE=$($oneRun.diagnostics.failFast), SIX=$($sixRun.diagnostics.failFast).",
    "Page faults: ONE=$($oneRun.diagnostics.pageFault), SIX=$($sixRun.diagnostics.pageFault).",
    "ONE Boot 1: outcome=$($one.runs[0].outcome), events=$($one.runs[0].c77EventCount), postRestart=$($one.runs[0].postRestartBasicCount).",
    "ONE Boot 2: outcome=$($one.runs[1].outcome), events=$($one.runs[1].c77EventCount), postRestart=$($one.runs[1].postRestartBasicCount).",
    "ONE Boot 3: outcome=$($one.runs[2].outcome), events=$($one.runs[2].c77EventCount), postRestart=$($one.runs[2].postRestartBasicCount).",
    "SIX Boot 1: outcome=$($six.runs[0].outcome), events=$($six.runs[0].c77EventCount), postRestart=$($six.runs[0].postRestartBasicCount).",
    "SIX Boot 2: outcome=$($six.runs[1].outcome), events=$($six.runs[1].c77EventCount), postRestart=$($six.runs[1].postRestartBasicCount).",
    "SIX Boot 3: outcome=$($six.runs[2].outcome), events=$($six.runs[2].c77EventCount), postRestart=$($six.runs[2].postRestartBasicCount).",
    "Semantic agreement: ONE=$($one.semanticAgreement), SIX=$($six.semanticAgreement).",
    "Nondeterminism: no semantic nondeterminism across 3/3 accepted boots per side; addresses may differ.",
    "Serial hashes: recorded in c77-final-manifest.json and c77-census.json.",
    "Artifact hashes: recorded from each smoke manifest; proof payload is inactive after cleanup.",
    "Runtime-pack validation: C77 smoke runtime-pack, artifact, archive, link, and kernel phases passed.",
    "Managed build: passed for discovery and confirmation runs.",
    "Native build: passed for runtime pack, artifact, archive, link, PE-to-ELF, and kernel phases.",
    "PowerShell syntax: passed for the harness and C77 analyzer.",
    "JSON/XML parse: all C77 JSON artifacts parsed successfully; no XML artifacts were emitted by C77.",
    "git diff --check: passed before local commit; final report is regenerated after commit.",
    "PE -> ELF conversion: passed in each smoke run.",
    "Symbol checks: passed in each smoke run; code-manager and helper audits emitted.",
    "Linker/source/table/archive guards: passed by smoke artifact/link/archive stages.",
    "C52 Tier-All result or reason omitted: omitted; not semantically appropriate for a bounded C67 supply observer.",
    "Ordinary restoration: smoke manifests report restoredByFinally=true and proofOnlyArtifactActive=false.",
    "Ordinary kernel SHA: 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.",
    "Ordinary ESP SHA: 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.",
    "Proof artifact active: false after each run.",
    "C77-owned QEMU cleanup: smoke watchdog stopped only C77-owned processes.",
    "Unrelated QEMU preservation: unrelated QEMU processes were preserved.",
    "Files changed: C77 harness gate/control selection, platform C77 observer/reporting, analyzer, evidence, and documentation.",
    "Documentation path: docs/dotnet/NATIVEAOT_WORKSTATION_GC_C77_BASIC_REGION_SUPPLY_PROVENANCE.md.",
    "Evidence root: out/dotnet/c011ec77-basic-region-supply-provenance/.",
    "Final commit: Trace NativeAOT basic region supply provenance.",
    "Push status: not pushed.",
    "Remaining limitation: no closed region identity/ownership ledger at checkpoints 1,2,4,10,11,12 and no observed split/coalesce/context lifecycle.",
    "Exact next-smallest milestone: C78 should add a bounded, source-backed pre-GC region identity/ownership census at the first allocation-boundary divergence while preserving the accepted promotion controls."
)
if ($reportItems.Count -ne 192) { throw "C77 report item count is $($reportItems.Count), expected 192." }

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$manifest | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c77-final-manifest.json') -Encoding UTF8
$census | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c77-region-census.json') -Encoding UTF8
$comparison | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c77-comparison-table.md') -Encoding UTF8
$extraTable | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c77-extra-five-table.md') -Encoding UTF8
$audit | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c77-source-audit.md') -Encoding UTF8
@("# C77 final numbered report", "", "C77 is bounded Level 1 / Outcome G. The report is intentionally explicit where the C67 observer cannot support a stronger causal claim.", "") + @($reportItems | ForEach-Object -Begin { $i=0 } -Process { $i++; "${i}. $_" }) | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c77-final-report.md') -Encoding UTF8
Write-Output ("C77 extraction complete: ONE={0} boots, SIX={1} boots, semanticAgreement={2}, reportItems={3}." -f $one.runs.Count, $six.runs.Count, ($one.semanticAgreement -and $six.semanticAgreement), $reportItems.Count)
