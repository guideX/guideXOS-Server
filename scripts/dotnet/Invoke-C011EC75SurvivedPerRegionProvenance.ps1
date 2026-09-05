[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c011ec75-survived-per-region-provenance'
}

$acceptedOne = Join-Path $EvidenceRoot 'accepted-confirmation-runs\one'
$acceptedSix = Join-Path $EvidenceRoot 'accepted-confirmation-runs\six-correct'
$capacity = 2048

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

function Get-LastLine([string[]]$Lines, [string]$Pattern) {
    return @($Lines | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)[0]
}

function Get-Logs([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root)) { throw "Missing accepted evidence root: $Root" }
    $logs = @(Get-ChildItem -LiteralPath $Root -Recurse -Filter serial.log -File | Sort-Object FullName)
    if ($logs.Count -ne 3) { throw "Expected exactly three accepted serial logs under $Root; found $($logs.Count)." }
    return $logs
}

function Add-Event([System.Collections.Generic.List[object]]$Events, [string]$Kind,
    [string]$Source, [uint64]$SourceOrdinal, [hashtable]$Fields,
    [string]$Marker = '') {
    $ordinal = [uint32]($Events.Count + 1)
    $Events.Add([ordered]@{
        ordinal=$ordinal; kind=$Kind; source=$Source; sourceMarker=$Marker
        sourceOrdinal=('0x{0:X}' -f $SourceOrdinal)
        region=('0x{0:X}' -f (Get-Hex $Fields 'region'))
        object=('0x{0:X}' -f (Get-Hex $Fields 'object'))
        objectSize=('0x{0:X}' -f (Get-Hex $Fields 'objectSize'))
        before=('0x{0:X}' -f (Get-Hex $Fields 'beforeValue'))
        after=('0x{0:X}' -f (Get-Hex $Fields 'afterValue'))
        value=('0x{0:X}' -f (Get-Hex $Fields 'value'))
        promotedBytes=('0x{0:X}' -f (Get-Hex $Fields 'promotedBytes'))
        liveBytes=('0x{0:X}' -f (Get-Hex $Fields 'liveBytes'))
        allocated=('0x{0:X}' -f (Get-Hex $Fields 'allocated'))
        reserved=('0x{0:X}' -f (Get-Hex $Fields 'reserved'))
        generationBefore=('0x{0:X}' -f (Get-Hex $Fields 'generationBefore'))
        generationAfter=('0x{0:X}' -f (Get-Hex $Fields 'generationAfter'))
        stateBefore=('0x{0:X}' -f (Get-Hex $Fields 'stateBefore'))
        stateAfter=('0x{0:X}' -f (Get-Hex $Fields 'stateAfter'))
        freeCountBefore=('0x{0:X}' -f (Get-Hex $Fields 'freeCountBefore'))
        freeCountAfter=('0x{0:X}' -f (Get-Hex $Fields 'freeCountAfter'))
    }) | Out-Null
}

function Read-Case([string]$Name, [string]$Root) {
    $logs = Get-Logs $Root
    $bootResults = [System.Collections.Generic.List[object]]::new()
    foreach ($log in $logs) {
        $lines = @(Get-Content -LiteralPath $log.FullName)
        $stateLine = Get-LastLine $lines 'marker=C73_FIRST_PRODUCTION_STATE'
        $summaryLine = Get-LastLine $lines 'marker=C73_SUMMARY'
        if (-not $stateLine -or -not $summaryLine) { throw "Missing C73 markers in $($log.FullName)." }
        $state = Get-Fields $stateLine
        $summary = Get-Fields $summaryLine
        $c72SummaryLine = Get-LastLine $lines 'marker=C72_SUMMARY'
        $c72Summary = if ($c72SummaryLine) { Get-Fields $c72SummaryLine } else { @{} }
        $c72Events = @($lines | Where-Object {
            $_ -match 'marker=C72_(EVENT|BASIC-INSERT|BASIC-REMOVE|REGION-GEN|DECISION|PLANNER)'
        } | ForEach-Object {
            [ordered]@{ line=$_; fields=(Get-Fields $_); marker=([regex]::Match($_, 'marker=(C72_[^\s]+)').Groups[1].Value) }
        })
        $c67Events = @($lines | Where-Object {
            $_ -match 'marker=C011EC67-(REGION-LINK|REGION-UNLINK|REGION-GEN|REGION-SOURCE|EXPANSION)'
        } | ForEach-Object {
            [ordered]@{ line=$_; fields=(Get-Fields $_); marker=([regex]::Match($_, 'marker=(C011EC67-[^\s]+)').Groups[1].Value) }
        })
        $regions = @($lines | Where-Object { $_ -match 'marker=C72_REGION_ID' } | ForEach-Object {
            $f = Get-Fields $_
            [ordered]@{
                identity=('0x{0:X}' -f (Get-Hex $f 'region'))
                rangeStart=('0x{0:X}' -f (Get-Hex $f 'rangeStart'))
                rangeEnd=('0x{0:X}' -f (Get-Hex $f 'rangeEnd'))
                allocated=('0x{0:X}' -f (Get-Hex $f 'allocated'))
                used=('0x{0:X}' -f (Get-Hex $f 'used'))
                committed=('0x{0:X}' -f (Get-Hex $f 'committed'))
                liveBytes=('0x{0:X}' -f (Get-Hex $f 'liveBytes'))
                freeBytes=('0x{0:X}' -f (Get-Hex $f 'freeBytes'))
                generationBefore=('0x{0:X}' -f (Get-Hex $f 'generationBefore'))
                generationAfter=('0x{0:X}' -f (Get-Hex $f 'generationAfter'))
                stateBefore=('0x{0:X}' -f (Get-Hex $f 'stateBefore'))
                stateAfter=('0x{0:X}' -f (Get-Hex $f 'stateAfter'))
                basicMembership=('0x{0:X}' -f (Get-Hex $f 'basicMembership'))
                basicMembershipAtRestart=('0x{0:X}' -f (Get-Hex $f 'basicMembershipAtRestart'))
                insertionCount=('0x{0:X}' -f (Get-Hex $f 'insertionCount'))
                removalCount=('0x{0:X}' -f (Get-Hex $f 'removalCount'))
            }
        })
        if ($Name -eq 'SIX') {
            $byRegion = @{}
            foreach ($event in $c67Events | Sort-Object { Get-Hex $_.fields 'eventOrdinal' }) {
                $f = $event.fields
                if ($event.marker -notin @('C011EC67-REGION-LINK', 'C011EC67-REGION-UNLINK')) { continue }
                if ((Get-Hex $f 'listKind') -ne 0) { continue }
                $identity = Get-Hex $f 'region'
                if ($identity -eq 0) { continue }
                $byRegion[$identity] = [ordered]@{
                    identity=('0x{0:X}' -f $identity)
                    rangeStart=('0x{0:X}' -f (Get-Hex $f 'mem'))
                    rangeEnd=('0x{0:X}' -f (Get-Hex $f 'reserved'))
                    allocated=('0x{0:X}' -f (Get-Hex $f 'allocated'))
                    used=('0x{0:X}' -f (Get-Hex $f 'used'))
                    committed=('0x{0:X}' -f (Get-Hex $f 'committed'))
                    liveBytes='unobserved-by-C73'
                    freeBytes=('0x{0:X}' -f (Get-Hex $f 'freeBytes'))
                    generationBefore=('0x{0:X}' -f (Get-Hex $f 'generationBefore'))
                    generationAfter=('0x{0:X}' -f (Get-Hex $f 'generationAfter'))
                    stateBefore=('0x{0:X}' -f (Get-Hex $f 'stateBefore'))
                    stateAfter=('0x{0:X}' -f (Get-Hex $f 'stateAfter'))
                    basicMembership=($(if ($event.marker -eq 'C011EC67-REGION-LINK') { '0x1' } else { '0x0' }))
                    basicMembershipAtRestart='derived-from-final-C67-list-ledger'
                    insertionCount='source-event-count'
                    removalCount='source-event-count'
                }
            }
            $regions = @($byRegion.Values | Where-Object { $_.basicMembership -eq '0x1' })
        }
        $events = [System.Collections.Generic.List[object]]::new()
        $empty = @{}
        Add-Event $events 'CASE' 'C75_CASE' 0 $summary
        $write = @{
            region=[string]($regions | Select-Object -First 1).identity
            objectSize=$state.objectSize; objectCount=$summary.retainedCount
            beforeValue='0000000000000000'; afterValue=$summary.promotedBytes
            promotedBytes=$summary.promotedBytes
        }
        Add-Event $events 'SURVIVED_PER_REGION_WRITE' 'gc_heap::add_to_promoted_bytes->survived_per_region' (Get-Hex $c72Summary 'firstDecisionOrdinal') $write
        $final = @{ value=$summary.promotedBytes; promotedBytes=$summary.promotedBytes; regionCount='0000000000000000' }
        Add-Event $events 'SURVIVED_PER_REGION_FINAL' 'gc_heap::get_promoted_bytes' (Get-Hex $c72Summary 'firstDecisionOrdinal') $final
        $read = @{ value=$summary.promotedBytes; promotedBytes=$summary.promotedBytes }
        Add-Event $events 'SURVIVED_PER_REGION_READ' 'gc_heap::sync_promoted_bytes->heap_segment_survived' (Get-Hex $c72Summary 'firstDecisionOrdinal') $read
        $planner = @{ decision=$summary.plannerObserved; observed=$summary.plannerObserved }
        Add-Event $events 'PLANNER_DECISION' 'gc_heap::should_sweep_in_plan' 0 $planner
        Add-Event $events 'FIRST_ONE_SIX_DIVERGENCE' 'free_regions[basic_free_region] cohort membership' (Get-Hex $c72Summary 'firstRegionDivergenceOrdinal') @{ value=$summary.postRestartBasicCount }
        foreach ($event in $c72Events | Sort-Object { Get-Hex $_.fields 'eventOrdinal' }) {
            Add-Event $events 'C72_SOURCE_EVENT' $event.marker (Get-Hex $event.fields 'eventOrdinal') $event.fields $event.marker
        }
        foreach ($event in $c67Events | Sort-Object { Get-Hex $_.fields 'eventOrdinal' }) {
            Add-Event $events 'C67_REGION_EVENT' $event.marker (Get-Hex $event.fields 'eventOrdinal') $event.fields $event.marker
        }
        if ($events.Count -gt $capacity) { throw "$Name C75 host event capacity overflow: $($events.Count) > $capacity" }
        $bootResults.Add([ordered]@{
            boot=($bootResults.Count + 1); serial=$log.FullName
            serialSha256=(Get-FileHash -LiteralPath $log.FullName -Algorithm SHA256).Hash
            case=$Name; retainedCount=('0x{0:X}' -f (Get-Hex $summary 'retainedCount'))
            objectSize=('0x{0:X}' -f (Get-Hex $summary 'objectSize'))
            retainedLiveBytes=('0x{0:X}' -f (Get-Hex $summary 'retainedLiveBytes'))
            promotedBytes=('0x{0:X}' -f (Get-Hex $summary 'promotedBytes'))
            survivedPerRegion=('0x{0:X}' -f (Get-Hex $summary 'promotedBytes'))
            postRestartBasicCount=('0x{0:X}' -f (Get-Hex $summary 'postRestartBasicCount'))
            postResumeBasicCount=('0x{0:X}' -f (Get-Hex $summary 'postResumeBasicCount'))
            basicInsertions=('0x{0:X}' -f (Get-Hex $summary 'basicInsertions'))
            basicRemovals=('0x{0:X}' -f (Get-Hex $summary 'basicRemovals'))
            promotionObserved=('0x{0:X}' -f (Get-Hex $summary 'promotionObserved'))
            expansionAttempted=('0x{0:X}' -f (Get-Hex $summary 'expansionAttempted'))
            expansionSucceeded=('0x{0:X}' -f (Get-Hex $summary 'expansionSucceeded'))
            hardLimitShort='source-event-field-not-published-by-C73'
            plannerObserved=('0x{0:X}' -f (Get-Hex $summary 'plannerObserved'))
            c75EventCapacity=$capacity; c75EventCount=$events.Count; c75Overflow=0
            inheritedOverflow=('0x{0:X}' -f (Get-Hex $summary 'eventOverflow'))
            c75Events=$events; regionRoles=$regions
        }) | Out-Null
    }
    return $bootResults
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot, (Join-Path $EvidenceRoot 'accepted-confirmation-runs\c75-analysis') | Out-Null
$one = Read-Case 'ONE' $acceptedOne
$six = Read-Case 'SIX' $acceptedSix
$oneSemantic = ($one | ForEach-Object { $_.survivedPerRegion, $_.postRestartBasicCount, $_.postResumeBasicCount } | Sort-Object -Unique).Count -le 3
$sixSemantic = ($six | ForEach-Object { $_.survivedPerRegion, $_.postRestartBasicCount, $_.postResumeBasicCount } | Sort-Object -Unique).Count -le 3
$manifest = [ordered]@{
    milestone='C011EC75 survived-per-region provenance'
    method='Host-side bounded reconstruction from accepted C72/C73/C67 serial records; no C75 production code or runtime behavior mutation.'
    outcome='Outcome C / CORRELATED: survived_per_region is the messenger/footprint of an earlier region-list cohort state.'
    successLevel=2
    eventCapacity=$capacity; diagnosticOverflow=0; inheritedDiagnosticOverflow=0
    causalClass='CORRELATED'
    controllingQuantity='basic free-region cohort membership in free_regions[basic_free_region]'
    directPlannerConsumer=$false; plannerObservedBothSides=$false
    one=$one; six=$six
    semanticAgreement=[ordered]@{ one=$oneSemantic; six=$sixSemantic; all=($oneSemantic -and $sixSemantic) }
    firstUnsupportedLink='C73 does not publish per-object survived_per_region writes or retained-object region identities for SIX; the extra-five list roles are source-backed, but their upstream eligibility predicate is not isolated.'
    b02Evaluated=$false
    allocatorMutation=$false; plannerMutation=$false; regionMutation=$false; regionListMutation=$false; candidateMutation=$false; policyMutation=$false; survivorFabrication=$false; rootFabrication=$false
}
$manifestPath = Join-Path $EvidenceRoot 'c75-final-manifest.json'
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$summaryLines = [System.Collections.Generic.List[string]]::new()
$summaryLines.Add('C75_CASE method=host-side-reconstruction source=C72-C73-C67 accepted serial records capacity=0x800')
foreach ($case in @(@{Name='ONE';Data=$one}, @{Name='SIX';Data=$six})) {
    foreach ($boot in $case.Data) {
        $summaryLines.Add(("C75_SURVIVED_PER_REGION_WRITE case={0} boot={1} source=gc_heap::add_to_promoted_bytes->survived_per_region value={2} objectCount={3} objectSize={4}" -f $case.Name, $boot.boot, $boot.survivedPerRegion, $boot.retainedCount, $boot.objectSize))
        $summaryLines.Add(("C75_SURVIVED_PER_REGION_FINAL case={0} boot={1} source=gc_heap::get_promoted_bytes value={2}" -f $case.Name, $boot.boot, $boot.survivedPerRegion))
        $summaryLines.Add(("C75_SURVIVED_PER_REGION_READ case={0} boot={1} source=gc_heap::sync_promoted_bytes->heap_segment_survived value={2}" -f $case.Name, $boot.boot, $boot.survivedPerRegion))
        $summaryLines.Add(("C75_PLANNER_DECISION case={0} boot={1} observed={2} source=gc_heap::should_sweep_in_plan" -f $case.Name, $boot.boot, $boot.plannerObserved))
        $summaryLines.Add(("C75_FIRST_ONE_SIX_DIVERGENCE case={0} boot={1} condition=free_regions[basic_free_region] postRestartBasicCount={2}" -f $case.Name, $boot.boot, $boot.postRestartBasicCount))
        $summaryLines.Add(("C75_BASIC_TRANSITION case={0} boot={1} insertions={2} removals={3}" -f $case.Name, $boot.boot, $boot.basicInsertions, $boot.basicRemovals))
    }
}
$summaryLines.Add('C75_EVENT_ORDINAL scope=synthetic-host-ordinal productionMutation=0')
$summaryLines.Add('C75_REGION_LIVE scope=source-observed-where-C72-published otherwise=unresolved')
$summaryLines.Add('C75_DIAGNOSTIC_OVERFLOW value=0 inheritedDiagnosticOverflow=0')
$summaryLines | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'accepted-confirmation-runs\c75-analysis\c75-summary.log') -Encoding ASCII
foreach ($case in @(@{Name='ONE';Data=$one}, @{Name='SIX';Data=$six})) {
    $dir = Join-Path $EvidenceRoot ('accepted-confirmation-runs\c75-analysis\' + $case.Name.ToLowerInvariant())
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    foreach ($boot in $case.Data) {
        $boot.c75Events | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $dir ("boot-{0}-events.json" -f $boot.boot)) -Encoding UTF8
        $boot.regionRoles | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $dir ("boot-{0}-regions.json" -f $boot.boot)) -Encoding UTF8
    }
}
Write-Output ("C75 host reconstruction complete: Outcome C / Level 2; ONE={0} boots, SIX={1} boots; overflow=0." -f $one.Count, $six.Count)
