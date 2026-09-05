[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OneManifest,

    [Parameter(Mandatory = $true)]
    [string]$SixManifest,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'

function Get-Field {
    param(
        [string]$Line,
        [string]$Name,
        [UInt64]$Default = 0
    )
    $match = [regex]::Match($Line, "(?:^|\s)$([regex]::Escape($Name))=(?<value>[0-9A-Fa-f]+)(?:\s|$)")
    if (-not $match.Success) { return $Default }
    return [Convert]::ToUInt64($match.Groups['value'].Value, 16)
}

function Get-TextField {
    param([string]$Line, [string]$Name)
    $match = [regex]::Match($Line, "(?:^|\s)$([regex]::Escape($Name))=(?<value>[^\s]+)")
    if ($match.Success) { return $match.Groups['value'].Value }
    return $null
}

function Get-FieldOrText {
    param([string]$Line, [string]$Primary, [string]$Secondary, [UInt64]$Default = 0)
    $primaryMatch = [regex]::Match($Line, "(?:^|\s)$([regex]::Escape($Primary))=(?<value>[0-9A-Fa-f]+)(?:\s|$)")
    if ($primaryMatch.Success) { return [Convert]::ToUInt64($primaryMatch.Groups['value'].Value, 16) }
    return Get-Field $Line $Secondary $Default
}

function Get-Extent {
    param([UInt64]$Start, [UInt64]$End)
    if ($End -ge $Start) { return $End - $Start }
    return [UInt64]0
}

function Get-OwnerRole {
    param([UInt64]$OwnerKind)
    if ($OwnerKind -eq 0) { return 'basic-free-list' }
    if ($OwnerKind -lt 0x100) { return "free-list-$OwnerKind" }
    return "generation-chain-$($OwnerKind - 0x100)"
}

function Format-HexValue {
    param([object]$Value)
    return ([UInt64]$Value).ToString('X')
}

function Convert-RangeRecord {
    param([string]$Line, [string]$Run)
    $isC79 = $Line -match 'marker=C79_CENSUS_RECORD'
    if ($isC79) {
        $checkpoint = Get-Field $Line 'checkpoint'
        $descriptor = Get-Field $Line 'descriptor'
        $start = Get-Field $Line 'rangeStart'
        $end = Get-Field $Line 'rangeEnd'
        $committedEnd = $start + (Get-Field $Line 'committedExtent')
        $allocatedEnd = $start + (Get-Field $Line 'allocatedExtent')
        $usedEnd = $start + (Get-Field $Line 'usedExtent')
        $owner = Get-Field $Line 'ownerKind'
        $generation = Get-Field $Line 'generation'
        $state = Get-Field $Line 'state'
        $basic = if ($owner -eq 0) { 1 } else { 0 }
        $source = 'C79_RUNTIME_RECORD'
    } elseif ($Line -match 'marker=C76_REGION_ELIGIBILITY') {
        $checkpoint = [UInt64]0
        $descriptor = Get-Field $Line 'region'
        $start = Get-Field $Line 'mem'
        $end = Get-Field $Line 'reserved'
        $committedEnd = Get-Field $Line 'committed'
        $allocatedEnd = Get-Field $Line 'allocated'
        $usedEnd = Get-Field $Line 'used'
        $owner = Get-Field $Line 'listKind'
        $generation = Get-Field $Line 'generationAfter'
        $state = Get-Field $Line 'stateAfter'
        $basic = Get-Field $Line 'basicEligible'
        $source = 'C76_ACCEPTED_EVENT'
    } else {
        return $null
    }
    if ($start -eq 0 -or $end -le $start) { return $null }
    $rangeSize = Get-Extent $start $end
    $committedExtent = Get-Extent $start $committedEnd
    $allocatedExtent = Get-Extent $start $allocatedEnd
    $usedExtent = Get-Extent $start $usedEnd
    [pscustomobject][ordered]@{
        Run = $Run
        Source = $source
        Checkpoint = $checkpoint
        CheckpointName = if ($checkpoint -eq 0) { 'C76_EVENT_UNTIMED' } else { "C79_$checkpoint" }
        Ordinal = Get-Field $Line 'ordinal' (Get-Field $Line 'eventOrdinal')
        Descriptor = $descriptor
        RangeStart = $start
        RangeEnd = $end
        RangeSize = $rangeSize
        CommittedExtent = $committedExtent
        AllocatedExtent = $allocatedExtent
        UsedExtent = $usedExtent
        LiveBytes = Get-FieldOrText $Line 'liveBytes' 'liveBytes'
        Generation = $generation
        State = $state
        OwnerKind = $owner
        OwnerRole = Get-OwnerRole $owner
        BasicEligible = [UInt64]$basic
        Active = Get-Field $Line 'active'
        ContextOwned = Get-Field $Line 'contextOwned'
        TailRole = Get-Field $Line 'tailRole'
        List = Get-FieldOrText $Line 'list' 'list'
        Raw = $Line
    }
}

function Convert-Summary {
    param([string]$Line, [string]$Run)
    [pscustomobject][ordered]@{
        Run = $Run
        Checkpoint = Get-Field $Line 'checkpoint'
        Name = Get-TextField $Line 'name'
        RegionCount = Get-Field $Line 'regionCount'
        DistinctRangeCount = Get-Field $Line 'distinctRangeCount'
        RecordCapacity = Get-Field $Line 'recordCapacity'
        RecordsWritten = Get-Field $Line 'recordsWritten'
        Overflow = Get-Field $Line 'overflow'
        HeapBase = Get-Field $Line 'heapBase'
        HeapEnd = Get-Field $Line 'heapEnd'
        ReservedHeapBytes = Get-Field $Line 'reservedHeapBytes'
        RegionCoveredBytes = Get-Field $Line 'regionCoveredBytes'
        CommittedHeapBytes = Get-Field $Line 'committedHeapBytes'
        AllocatedBytes = Get-Field $Line 'allocatedBytes'
        UsedBytes = Get-Field $Line 'usedBytes'
        FreeBytes = Get-Field $Line 'freeBytes'
        SnapshotCompleteness = Get-Field $Line 'snapshotCompleteness'
        Source = Get-TextField $Line 'source'
        Raw = $Line
    }
}

function Read-Run {
    param([string]$ManifestPath, [string]$Run)
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $rawRecords = @($manifest.records)
    $records = @($rawRecords | ForEach-Object { Convert-RangeRecord $_ $Run } | Where-Object { $null -ne $_ })
    $heapBase = if ($records.Count -ne 0) { ($records | Measure-Object -Property RangeStart -Minimum).Minimum } else { [UInt64]0 }
    foreach ($record in $records) {
        $record | Add-Member -NotePropertyName HeapBaseObserved -NotePropertyValue ([UInt64]$heapBase)
        $record | Add-Member -NotePropertyName OffsetFromHeapBase -NotePropertyValue (Get-Extent $heapBase $record.RangeStart)
        $record | Add-Member -NotePropertyName NormalizedRange -NotePropertyValue ('0x{0}:0x{1}' -f (Format-HexValue (Get-Extent $heapBase $record.RangeStart)), (Format-HexValue $record.RangeSize))
    }
    $summaries = @($manifest.checkpoints | ForEach-Object { Convert-Summary $_ $Run })
    $basic = @($records | Where-Object { $_.BasicEligible -ne 0 } | Group-Object NormalizedRange | ForEach-Object { $_.Group[0] })
    $descriptorRanges = @($records | Group-Object Descriptor | ForEach-Object {
        [pscustomobject]@{ Descriptor = $_.Name; RangeKeys = @($_.Group | Group-Object NormalizedRange | ForEach-Object Name) }
    })
    $rangeDescriptors = @($records | Group-Object NormalizedRange | ForEach-Object {
        [pscustomobject]@{ Range = $_.Name; Descriptors = @($_.Group | Group-Object Descriptor | ForEach-Object Name) }
    })
    $c76 = [string]$manifest.c76
    [pscustomobject][ordered]@{
        Run = $Run
        Manifest = (Resolve-Path $ManifestPath).Path
        ManifestObject = $manifest
        Records = $records
        Summaries = $summaries
        HeapBaseObserved = [UInt64]$heapBase
        HeapEndObserved = if ($records.Count -ne 0) { ($records | Measure-Object -Property RangeEnd -Maximum).Maximum } else { [UInt64]0 }
        BasicRanges = $basic
        DescriptorRanges = $descriptorRanges
        RangeDescriptors = $rangeDescriptors
        C76Summary = $c76
        AcceptedPostRestartBasic = Get-Field $c76 'postRestartBasicCount'
        AcceptedPostResumeBasic = Get-Field $c76 'postResumeBasicCount'
        AcceptedPromotion = Get-Field $c76 'promotionObserved'
    }
}

function Get-SetDifference {
    param([object[]]$Left, [object[]]$Right)
    $rightSet = @{}
    foreach ($item in $Right) { $rightSet[[string]$item.NormalizedRange] = $true }
    @($Left | Where-Object { -not $rightSet.ContainsKey([string]$_.NormalizedRange) })
}

function Get-RangeRole {
    param([object]$Record)
    if ($Record.BasicEligible -ne 0) { return 'basic-eligible' }
    if ($Record.Active -ne 0) { return 'active' }
    if ($Record.ContextOwned -ne 0) { return 'context-owned' }
    return $Record.OwnerRole
}

$one = Read-Run $OneManifest 'ONE'
$six = Read-Run $SixManifest 'SIX'

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot 'raw-census') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot 'accepted-manifests') | Out-Null
Copy-Item -LiteralPath $OneManifest -Destination (Join-Path $OutputRoot 'accepted-manifests/ONE-manifest.json') -Force
Copy-Item -LiteralPath $SixManifest -Destination (Join-Path $OutputRoot 'accepted-manifests/SIX-manifest.json') -Force
Set-Content -LiteralPath (Join-Path $OutputRoot 'raw-census/ONE-C76_REGION_ELIGIBILITY.txt') -Value @($one.Records | Where-Object Source -eq 'C76_ACCEPTED_EVENT' | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $OutputRoot 'raw-census/SIX-C76_REGION_ELIGIBILITY.txt') -Value @($six.Records | Where-Object Source -eq 'C76_ACCEPTED_EVENT' | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $OutputRoot 'raw-census/ONE-C79_CENSUS_RECORD.txt') -Value @($one.Records | Where-Object Source -eq 'C79_RUNTIME_RECORD' | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $OutputRoot 'raw-census/SIX-C79_CENSUS_RECORD.txt') -Value @($six.Records | Where-Object Source -eq 'C79_RUNTIME_RECORD' | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $OutputRoot 'raw-census/ONE-C79_CENSUS_SUMMARY.txt') -Value @($one.Summaries | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $OutputRoot 'raw-census/SIX-C79_CENSUS_SUMMARY.txt') -Value @($six.Summaries | ForEach-Object Raw) -Encoding ASCII

$allRecords = @($one.Records + $six.Records)
$allRecords | Sort-Object Run,Checkpoint,RangeStart,RangeEnd,Descriptor,Ordinal | Select-Object Run,Source,Checkpoint,CheckpointName,Ordinal,Descriptor,RangeStart,RangeEnd,RangeSize,CommittedExtent,AllocatedExtent,UsedExtent,LiveBytes,Generation,State,OwnerKind,OwnerRole,BasicEligible,Active,ContextOwned,TailRole,List,HeapBaseObserved,OffsetFromHeapBase,NormalizedRange | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $OutputRoot 'c79-range-records.csv')

$oneBasic = @($one.BasicRanges)
$sixBasic = @($six.BasicRanges)
$extraSix = @(Get-SetDifference $sixBasic $oneBasic)
$extraOne = @(Get-SetDifference $oneBasic $sixBasic)
$common = @($sixBasic | Where-Object { $key = $_.NormalizedRange; @($oneBasic | Where-Object NormalizedRange -eq $key).Count -ne 0 })

$transitionRows = foreach ($runData in @($one, $six)) {
    $descriptorGroups = @($runData.Records | Group-Object Descriptor)
    $rangeGroups = @($runData.Records | Group-Object NormalizedRange)
    $sameDescriptorDifferentRange = 0
    foreach ($group in $descriptorGroups) {
        $sameDescriptorDifferentRange += [Math]::Max(0, @($group.Group | Select-Object -ExpandProperty NormalizedRange -Unique).Count - 1)
    }
    $sameRangeDifferentDescriptor = 0
    foreach ($group in $rangeGroups) {
        $sameRangeDifferentDescriptor += [Math]::Max(0, @($group.Group | Select-Object -ExpandProperty Descriptor -Unique).Count - 1)
    }
    [pscustomobject][ordered]@{
        Run = $runData.Run
        Classification = if ($runData.Records.Count -eq 0) { 'no records' } else { 'event-backed sample only' }
        SameDescriptorDifferentRange = $sameDescriptorDifferentRange
        SameRangeDifferentDescriptor = $sameRangeDifferentDescriptor
        CheckpointTransitions = 'not determinable: accepted C76 records have no complete C79 checkpoint snapshots'
        RangeSubdivision = 'not determinable'
        RangeMerger = 'not determinable'
        CoverageExpansion = 'not determinable'
    }
}
$transitionRows | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $OutputRoot 'transition-classification.csv')

$oneByRange = @{}
foreach ($group in @($one.Records | Group-Object NormalizedRange)) { $oneByRange[[string]$group.Name] = $group.Group[0] }
$sixByRange = @{}
foreach ($group in @($six.Records | Group-Object NormalizedRange)) { $sixByRange[[string]$group.Name] = $group.Group[0] }
$rangeKeys = @($oneByRange.Keys + $sixByRange.Keys | Sort-Object -Unique)
$rangeMatchingRows = foreach ($key in $rangeKeys) {
    $oneRecord = if ($oneByRange.ContainsKey([string]$key)) { $oneByRange[[string]$key] } else { $null }
    $sixRecord = if ($sixByRange.ContainsKey([string]$key)) { $sixByRange[[string]$key] } else { $null }
    [pscustomobject][ordered]@{
        NormalizedRange = $key
        Match = if ($null -ne $oneRecord -and $null -ne $sixRecord) { 'ONE/SIX common sampled range' } elseif ($null -ne $oneRecord) { 'ONE-only sampled range' } else { 'SIX-only sampled range' }
        OneRole = if ($null -ne $oneRecord) { Get-RangeRole $oneRecord } else { 'absent' }
        SixRole = if ($null -ne $sixRecord) { Get-RangeRole $sixRecord } else { 'absent' }
        SemanticRoleAgreement = if ($null -ne $oneRecord -and $null -ne $sixRecord) { (Get-RangeRole $oneRecord) -eq (Get-RangeRole $sixRecord) } else { $false }
        Note = 'sampled event role; not a complete checkpoint range universe'
    }
}
$rangeMatchingRows | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $OutputRoot 'range-matching.csv')

$checkpoints = @('C79_PRE_WORKLOAD','C79_POST_RETAINED_ALLOC','C79_PRE_TARGET_GC','C79_POST_PLAN','C79_PRE_RESTART','C79_POST_RESTART')
$checkpointRows = foreach ($name in $checkpoints) {
    $oneSummary = @($one.Summaries | Where-Object Name -eq $name | Select-Object -First 1)
    $sixSummary = @($six.Summaries | Where-Object Name -eq $name | Select-Object -First 1)
    [pscustomobject][ordered]@{
        Checkpoint = $name
        OneRegionCount = if ($oneSummary.Count) { $oneSummary[0].RegionCount } else { $null }
        SixRegionCount = if ($sixSummary.Count) { $sixSummary[0].RegionCount } else { $null }
        OneDistinctRangeCount = if ($oneSummary.Count) { $oneSummary[0].DistinctRangeCount } else { $null }
        SixDistinctRangeCount = if ($sixSummary.Count) { $sixSummary[0].DistinctRangeCount } else { $null }
        OneCommittedBytes = if ($oneSummary.Count) { $oneSummary[0].CommittedHeapBytes } else { $null }
        SixCommittedBytes = if ($sixSummary.Count) { $sixSummary[0].CommittedHeapBytes } else { $null }
        OneCoverage = if ($oneSummary.Count) { $oneSummary[0].SnapshotCompleteness } else { $null }
        SixCoverage = if ($sixSummary.Count) { $sixSummary[0].SnapshotCompleteness } else { $null }
    }
}
$checkpointRows | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $OutputRoot 'checkpoint-comparison.csv')

$descriptorReuse = [ordered]@{
    ONE = [ordered]@{ sameDescriptorDifferentRange = 0; sameRangeDifferentDescriptor = 0; observedDescriptors = @($one.DescriptorRanges).Count; observedRanges = @($one.RangeDescriptors).Count; classification = 'not determinable from event-backed C76 records; pointer/range pairs are sampled, not full checkpoint snapshots' }
    SIX = [ordered]@{ sameDescriptorDifferentRange = 0; sameRangeDifferentDescriptor = 0; observedDescriptors = @($six.DescriptorRanges).Count; observedRanges = @($six.RangeDescriptors).Count; classification = 'not determinable from event-backed C76 records; pointer/range pairs are sampled, not full checkpoint snapshots' }
}
$descriptorReuse | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'descriptor-reuse.json') -Encoding ASCII

$earliestCountDivergence = 'unresolved: no complete paired checkpoint snapshots'
$earliestRangeDivergence = 'unresolved: no complete paired checkpoint range unions'
$earliestOwnershipDivergence = 'unresolved: event-backed owner samples are not checkpoint-complete'
$divergence = [ordered]@{
    earliestCountDivergence = $earliestCountDivergence
    earliestRangeUnionDivergence = $earliestRangeDivergence
    earliestOwnershipListDivergence = $earliestOwnershipDivergence
    differencePredatesManagedWorkload = 'not established'
}
$divergence | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'range-divergence.json') -Encoding ASCII

$conservation = [ordered]@{
    ONE = [ordered]@{ heapBaseObserved = $one.HeapBaseObserved; heapEndObserved = $one.HeapEndObserved; reservedHeapBytes = Get-Extent $one.HeapBaseObserved $one.HeapEndObserved; regionCoveredBytes = [UInt64](($one.Records | Measure-Object -Property RangeSize -Sum).Sum); committedBytes = [UInt64](($one.Records | Measure-Object -Property CommittedExtent -Sum).Sum); gaps = 'not determinable from event sample'; overlaps = 'event sample contains repeated observations; full range-set arithmetic deferred'; valid = $false }
    SIX = [ordered]@{ heapBaseObserved = $six.HeapBaseObserved; heapEndObserved = $six.HeapEndObserved; reservedHeapBytes = Get-Extent $six.HeapBaseObserved $six.HeapEndObserved; regionCoveredBytes = [UInt64](($six.Records | Measure-Object -Property RangeSize -Sum).Sum); committedBytes = [UInt64](($six.Records | Measure-Object -Property CommittedExtent -Sum).Sum); gaps = 'not determinable from event sample'; overlaps = 'event sample contains repeated observations; full range-set arithmetic deferred'; valid = $false }
}
$conservation | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputRoot 'range-conservation.json') -Encoding ASCII

$extraRows = for ($index = 0; $index -lt 5; ++$index) {
    $candidate = if ($index -lt $extraSix.Count) { $extraSix[$index] } else { $null }
    [pscustomobject][ordered]@{
        ExtraIndex = $index + 1
        SixNormalizedRange = if ($null -ne $candidate) { $candidate.NormalizedRange } else { 'unresolved' }
        EarliestCheckpoint = 'unresolved: no full range census was captured'
        OneSideState = 'unresolved: no corresponding full ONE range set'
        Evidence = 'C79 reused accepted C76 event records only; full live census would perturb ONE/SIX'
    }
}
$extraRows | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $OutputRoot 'extra-five-ranges.csv')

$analysis = [ordered]@{
    outcome = 'H / event-backed range evidence is valid, but full offline reconstruction remains unresolved'
    successLevel = 1
    identity = [ordered]@{ primary = 'heap address range start/end/extent'; secondary = 'descriptor pointer'; normalized = 'offset from observed low range plus extent' }
    runtime = [ordered]@{ newLiveRangeWalk = $false; newRuntimeMaps = $false; newRuntimeSort = $false; sourceRecords = 'accepted C76_REGION_ELIGIBILITY'; c79Summaries = @($one.Summaries).Count; c79SnapshotCompleteness = 0; completeCheckpointSnapshots = $false }
    controls = [ordered]@{ ONE = [ordered]@{ promotion = $one.AcceptedPromotion; postRestartBasic = $one.AcceptedPostRestartBasic; postResumeBasic = $one.AcceptedPostResumeBasic; observedRangeRecords = $one.Records.Count; distinctObservedRanges = @($one.RangeDescriptors).Count }; SIX = [ordered]@{ promotion = $six.AcceptedPromotion; postRestartBasic = $six.AcceptedPostRestartBasic; postResumeBasic = $six.AcceptedPostResumeBasic; observedRangeRecords = $six.Records.Count; distinctObservedRanges = @($six.RangeDescriptors).Count } }
    checkpointRows = $checkpointRows
    descriptorReuse = $descriptorReuse
    conservation = $conservation
    basicRangeEvidence = [ordered]@{ oneDistinctEligibilityRanges = $oneBasic.Count; sixDistinctEligibilityRanges = $sixBasic.Count; common = $common.Count; extraSix = $extraSix.Count; extraFive = $extraRows }
    causalDecision = [ordered]@{ earliestRangeSupplyDivergence = $earliestRangeDivergence; mechanism = 'unresolved'; nextSmallest = 'capture one bounded post-C76 range snapshot only after the accepted safe-stop boundary, or extend the accepted C76 records with checkpoint provenance offline' }
    artifacts = [ordered]@{ rawRecords = 'raw-census/ONE-C76_REGION_ELIGIBILITY.txt, SIX-C76_REGION_ELIGIBILITY.txt, ONE-C79_CENSUS_RECORD.txt, SIX-C79_CENSUS_RECORD.txt'; recordsCsv = 'c79-range-records.csv'; checkpointsCsv = 'checkpoint-comparison.csv'; transitionsCsv = 'transition-classification.csv'; matchingCsv = 'range-matching.csv'; descriptorReuse = 'descriptor-reuse.json'; conservation = 'range-conservation.json'; divergence = 'range-divergence.json'; extraFive = 'extra-five-ranges.csv' }
}
$analysis | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $OutputRoot 'c79-offline-analysis.json') -Encoding ASCII

$descriptorMarkdown = @"
# C79 descriptor/range reconstruction

The runtime emitted no new live range walk. C79 reused accepted C76 range-bearing records and performs all identity/reconstruction work here.

| Run | Observed descriptors | Observed normalized ranges | Same descriptor/different range | Same range/different descriptor |
| --- | ---: | ---: | ---: | ---: |
| ONE | $(@($one.DescriptorRanges).Count) | $(@($one.RangeDescriptors).Count) | 0 (not determinable) | 0 (not determinable) |
| SIX | $(@($six.DescriptorRanges).Count) | $(@($six.RangeDescriptors).Count) | 0 (not determinable) | 0 (not determinable) |

The accepted event stream is not a full checkpoint census, so descriptor appearance cannot be interpreted as descriptor birth.
"@
Set-Content -LiteralPath (Join-Path $OutputRoot 'descriptor-reuse.md') -Value $descriptorMarkdown -Encoding UTF8

$conservationMarkdown = @"
# C79 range conservation

The raw records are repeated C76 eligibility observations, not one disjoint range set per checkpoint. Summing them would double-count ranges. Heap envelope values below are observed event bounds only.

| Run | Observed heap base | Observed heap end | Envelope bytes | Distinct event ranges | Conservation |
| --- | ---: | ---: | ---: | ---: | --- |
| ONE | 0x$(Format-HexValue $one.HeapBaseObserved) | 0x$(Format-HexValue $one.HeapEndObserved) | 0x$(Format-HexValue (Get-Extent $one.HeapBaseObserved $one.HeapEndObserved)) | $(@($one.RangeDescriptors).Count) | unresolved |
| SIX | 0x$(Format-HexValue $six.HeapBaseObserved) | 0x$(Format-HexValue $six.HeapEndObserved) | 0x$(Format-HexValue (Get-Extent $six.HeapBaseObserved $six.HeapEndObserved)) | $(@($six.RangeDescriptors).Count) | unresolved |
"@
Set-Content -LiteralPath (Join-Path $OutputRoot 'range-conservation.md') -Value $conservationMarkdown -Encoding UTF8

Write-Output ((Join-Path $OutputRoot 'c79-offline-analysis.json'))
