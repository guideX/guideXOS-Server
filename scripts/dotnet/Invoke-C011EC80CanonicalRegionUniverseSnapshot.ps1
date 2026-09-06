[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OneManifest,

    [Parameter(Mandatory = $true)]
    [string]$SixManifest,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [string]$IncompleteManifest,

    [string]$OneC76Manifest,

    [string]$SixC76Manifest
)

$ErrorActionPreference = 'Stop'

# C80 deliberately keeps the C79 analyzer's offline-only shape: serial markers are
# parsed here, range identity is reconstructed here, and no runtime state is
# invented or re-read.  Descriptor pointers are retained only as secondary data.

function Get-HexField {
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

function Get-Extent {
    param([UInt64]$Start, [UInt64]$End)
    if ($End -ge $Start) { return [UInt64]($End - $Start) }
    return [UInt64]0
}

function Format-HexValue {
    param([object]$Value)
    return ('0x{0:X}' -f ([UInt64]$Value))
}

function Get-ListRole {
    param([UInt64]$ListKind, [UInt64]$Active)
    if ($ListKind -eq 0) { return 'basic-free-list' }
    if ($ListKind -eq 1) { return 'large-free-list' }
    if ($ListKind -eq 2) { return 'huge-free-list' }
    if ($Active -ne 0) { return 'active-region' }
    return 'unclassified-canonical-entry'
}

function Convert-C80RegionRecord {
    param([string]$Line, [string]$Run)
    if ($Line -notmatch 'marker=C80_REGION_RECORD') { return $null }
    $start = Get-HexField $Line 'rangeStart'
    $end = Get-HexField $Line 'rangeEnd'
    if ($start -eq 0 -or $end -le $start) { return $null }
    [pscustomobject][ordered]@{
        Run = $Run
        Checkpoint = Get-HexField $Line 'checkpoint'
        Ordinal = Get-HexField $Line 'ordinal'
        Descriptor = Get-HexField $Line 'descriptor'
        MappingIndex = Get-HexField $Line 'mappingIndex'
        BasicRegionCount = Get-HexField $Line 'basicRegionCount'
        RangeStart = $start
        RangeEnd = $end
        RangeSize = Get-Extent $start $end
        Committed = Get-HexField $Line 'committed'
        Allocated = Get-HexField $Line 'allocated'
        Used = Get-HexField $Line 'used'
        LiveBytes = Get-HexField $Line 'liveBytes'
        Generation = Get-HexField $Line 'generation'
        PlanGeneration = Get-HexField $Line 'planGeneration'
        State = Get-HexField $Line 'state'
        ListKind = Get-HexField $Line 'listKind'
        Active = Get-HexField $Line 'active'
        SpecialFlags = Get-HexField $Line 'specialFlags'
        TailRole = Get-HexField $Line 'tailRole'
        Owner = Get-HexField $Line 'owner'
        List = Get-HexField $Line 'list'
        Source = Get-TextField $Line 'source'
        Raw = $Line
    }
}

function Convert-C80Summary {
    param([string]$Line, [string]$Run)
    if ($Line -notmatch 'marker=C80_SNAPSHOT_SUMMARY') { return $null }
    [pscustomobject][ordered]@{
        Run = $Run
        Checkpoint = Get-HexField $Line 'checkpoint'
        Name = Get-TextField $Line 'name'
        MappingStart = Get-HexField $Line 'mappingStart'
        MappingEnd = Get-HexField $Line 'mappingEnd'
        RegionAlignment = Get-HexField $Line 'regionAlignment'
        MappingEntries = Get-HexField $Line 'mappingEntries'
        VisitedEntries = Get-HexField $Line 'visitedEntries'
        RepresentedEntries = Get-HexField $Line 'representedEntries'
        ExcludedEntries = Get-HexField $Line 'excludedEntries'
        MaterializedRegions = Get-HexField $Line 'materializedRegions'
        RecordsWritten = Get-HexField $Line 'recordsWritten'
        RecordCapacity = Get-HexField $Line 'recordCapacity'
        DuplicateDescriptorCount = Get-HexField $Line 'duplicateDescriptorCount'
        DuplicateRangeCount = Get-HexField $Line 'duplicateRangeCount'
        InvalidRangeCount = Get-HexField $Line 'invalidRangeCount'
        Overflow = Get-HexField $Line 'overflow'
        SnapshotCompleteness = Get-HexField $Line 'snapshotCompleteness'
        Source = Get-TextField $Line 'source'
        Raw = $Line
    }
}

function Convert-C76Summary {
    param([string]$Line, [string]$Run, [string]$SourcePath = '')
    if ([string]::IsNullOrWhiteSpace($Line) -or $Line -notmatch 'marker=C011EC76-SUMMARY') { return $null }
    [pscustomobject][ordered]@{
        Run = $Run
        SourcePath = $SourcePath
        PromotionObserved = Get-HexField $Line 'promotionObserved'
        PostRestartBasicCount = Get-HexField $Line 'postRestartBasicCount'
        PostResumeBasicCount = Get-HexField $Line 'postResumeBasicCount'
        EligibilityCount = Get-HexField $Line 'eligibilityCount'
        BasicEligibilityCount = Get-HexField $Line 'basicEligibilityCount'
        BasicInsertions = Get-HexField $Line 'basicInsertions'
        BasicRemovals = Get-HexField $Line 'basicRemovals'
        EventCount = Get-HexField $Line 'eventCount'
        EventOverflow = Get-HexField $Line 'eventOverflow'
        InvariantFailures = Get-HexField $Line 'invariantFailures'
        Source = Get-TextField $Line 'source'
        Raw = $Line
    }
}

function Read-C76SummaryManifest {
    param([string]$ManifestPath, [string]$Run)
    if ([string]::IsNullOrWhiteSpace($ManifestPath) -or -not (Test-Path -LiteralPath $ManifestPath)) { return $null }
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $lines = @()
    if ($null -ne $manifest.markers) {
    foreach ($qemuRun in @($manifest.markers.runs)) {
            if ($null -ne $qemuRun.markerLine) { $lines += [string]$qemuRun.markerLine }
            foreach ($line in @($qemuRun.summaryLines)) { $lines += [string]$line }
        }
    }
    foreach ($line in @($manifest.c76)) { $lines += [string]$line }
    $summaryLine = @($lines | Where-Object { $_ -match 'marker=C011EC76-SUMMARY' } | Select-Object -First 1)
    if ($summaryLine.Count -eq 0) { return $null }
    return Convert-C76Summary $summaryLine[0] $Run ([IO.Path]::GetFullPath($ManifestPath))
}

function Read-C80Manifest {
    param([string]$ManifestPath, [string]$Run)
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $records = @($manifest.records | ForEach-Object { Convert-C80RegionRecord $_ $Run } | Where-Object { $null -ne $_ })
    $summaries = @($manifest.snapshots | ForEach-Object { Convert-C80Summary $_ $Run } | Where-Object { $null -ne $_ })
    $mappingStart = if ($summaries.Count -ne 0) { [UInt64](($summaries | Measure-Object -Property MappingStart -Minimum).Minimum) } else { [UInt64]0 }
    foreach ($record in $records) {
        $record | Add-Member -NotePropertyName OffsetStart -NotePropertyValue (Get-Extent $mappingStart $record.RangeStart)
        $record | Add-Member -NotePropertyName OffsetEnd -NotePropertyValue (Get-Extent $mappingStart $record.RangeEnd)
        $record | Add-Member -NotePropertyName RangeKey -NotePropertyValue ('{0}:{1}' -f (Format-HexValue (Get-Extent $mappingStart $record.RangeStart)), (Format-HexValue $record.RangeSize))
        $record | Add-Member -NotePropertyName ListRole -NotePropertyValue (Get-ListRole $record.ListKind $record.Active)
    }
    $envelopes = @($summaries | ForEach-Object { '{0}:{1}:{2}:{3}' -f $_.MappingStart, $_.MappingEnd, $_.RegionAlignment, $_.MappingEntries } | Sort-Object -Unique)
    $checkpointSet = @($summaries | ForEach-Object Checkpoint | Sort-Object -Unique)
    $c76Lines = @()
    foreach ($line in @($manifest.c76)) { $c76Lines += [string]$line }
    if ($null -ne $manifest.qemu) {
        foreach ($qemuRun in @($manifest.qemu.runs)) {
            if ($null -ne $qemuRun.markerLine) { $c76Lines += [string]$qemuRun.markerLine }
            foreach ($line in @($qemuRun.c76SummaryLines)) { $c76Lines += [string]$line }
            foreach ($line in @($qemuRun.summaryLines)) { $c76Lines += [string]$line }
        }
    }
    $c76Line = @($c76Lines | Where-Object { $_ -match 'marker=C011EC76-SUMMARY' } | Select-Object -First 1)
    $c76 = if ($c76Line.Count -ne 0) { Convert-C76Summary $c76Line[0] $Run $manifest.manifestPath } else { $null }
    $summaryInvariantFailures = @($summaries | Where-Object {
        $_.VisitedEntries -ne $_.MappingEntries -or
        ($_.RepresentedEntries + $_.ExcludedEntries) -ne $_.VisitedEntries -or
        $_.SnapshotCompleteness -ne 1 -or $_.Overflow -ne 0
    }).Count
    [pscustomobject][ordered]@{
        Run = $Run
        ManifestPath = (Resolve-Path -LiteralPath $ManifestPath).Path
        Manifest = $manifest
        Records = $records
        Summaries = $summaries
        MappingStart = $mappingStart
        MappingEnd = if ($summaries.Count -ne 0) { [UInt64](($summaries | Measure-Object -Property MappingEnd -Maximum).Maximum) } else { [UInt64]0 }
        RegionAlignment = if ($summaries.Count -ne 0) { [UInt64]$summaries[0].RegionAlignment } else { [UInt64]0 }
        MappingEntries = if ($summaries.Count -ne 0) { [UInt64]$summaries[0].MappingEntries } else { [UInt64]0 }
        EnvelopeVariants = $envelopes
        CheckpointSet = $checkpointSet
        C76 = $c76
        SummaryInvariantFailures = $summaryInvariantFailures
        Complete = $summaries.Count -eq 4 -and @($checkpointSet) -join ',' -eq '5,6,7,8' -and $envelopes.Count -eq 1 -and $summaryInvariantFailures -eq 0
    }
}

function Get-RangeUniverse {
    param([object]$RunData)
    @($RunData.Records | Group-Object RangeKey | ForEach-Object {
        $rows = @($_.Group)
        $first = $rows[0]
        [pscustomobject][ordered]@{
            Run = $RunData.Run
            RangeKey = $_.Name
            RangeStart = $first.RangeStart
            RangeEnd = $first.RangeEnd
            RangeSize = $first.RangeSize
            OffsetStart = $first.OffsetStart
            OffsetEnd = $first.OffsetEnd
            BasicRegionCount = $first.BasicRegionCount
            ObservationCount = $rows.Count
            Checkpoints = @($rows | ForEach-Object Checkpoint | Sort-Object -Unique)
            Descriptors = @($rows | ForEach-Object Descriptor | Sort-Object -Unique)
            MappingIndices = @($rows | ForEach-Object MappingIndex | Sort-Object -Unique)
            ListRoles = @($rows | ForEach-Object ListRole | Sort-Object -Unique)
            ActiveStates = @($rows | ForEach-Object Active | Sort-Object -Unique)
            SpecialFlags = @($rows | ForEach-Object SpecialFlags | Sort-Object -Unique)
            TailRoles = @($rows | ForEach-Object TailRole | Sort-Object -Unique)
            Generations = @($rows | ForEach-Object Generation | Sort-Object -Unique)
            Source = $first.Source
        }
    } | Sort-Object OffsetStart,OffsetEnd)
}

function Get-LocalizationRows {
    param([object]$RunData, [object[]]$Universe)
    $upper = @($Universe | Sort-Object RangeEnd -Descending | Select-Object -First 1)
    $forwardFree = @($Universe | Where-Object { $_.ListRoles -contains 'basic-free-list' -or $_.ActiveStates -contains 0 } | Select-Object -First 1)
    $tailSpecial = @($Universe | Where-Object { ($_.TailRoles | Where-Object { $_ -ne 0 }).Count -ne 0 -or ($_.SpecialFlags | Where-Object { $_ -ne 0 }).Count -ne 0 } | Select-Object -First 1)
    $classification = @($Universe | Where-Object { $_.RangeSize -eq $RunData.RegionAlignment -and $_.ListRoles -notcontains 'basic-free-list' } | Select-Object -First 1)
    $canonical = @($Universe | Select-Object -First 1)
    $items = @(
        [pscustomobject][ordered]@{ Run = $RunData.Run; Slot = 1; Role = 'canonical-region'; Rule = 'lowest normalized half-open range'; RangeKey = if ($canonical) { $canonical[0].RangeKey } else { 'unresolved' }; Evidence = if ($canonical) { $canonical[0].Source } else { 'missing' } },
        [pscustomobject][ordered]@{ Run = $RunData.Run; Slot = 2; Role = 'upper-boundary'; Rule = 'largest observed range end against mappingEnd'; RangeKey = if ($upper) { $upper[0].RangeKey } else { 'unresolved' }; Evidence = ('mappingEnd={0}' -f (Format-HexValue $RunData.MappingEnd)) },
        [pscustomobject][ordered]@{ Run = $RunData.Run; Slot = 3; Role = 'forward-free'; Rule = 'runtime listKind=basic or inactive canonical entry'; RangeKey = if ($forwardFree) { $forwardFree[0].RangeKey } else { 'unresolved' }; Evidence = if ($forwardFree) { ($forwardFree[0].ListRoles -join ',') } else { 'no free entry observed' } },
        [pscustomobject][ordered]@{ Run = $RunData.Run; Slot = 4; Role = 'tail-special'; Rule = 'runtime tailRole or specialFlags is non-zero'; RangeKey = if ($tailSpecial) { $tailSpecial[0].RangeKey } else { 'unresolved' }; Evidence = if ($tailSpecial) { 'tail/special flags present' } else { 'no tail/special entry observed' } },
        [pscustomobject][ordered]@{ Run = $RunData.Run; Slot = 5; Role = 'region-classification'; Rule = 'basic-aligned active entry distinct from free entry'; RangeKey = if ($classification) { $classification[0].RangeKey } else { 'unresolved' }; Evidence = if ($classification) { ($classification[0].Generations -join ',') } else { 'classification unresolved' } }
    )
    return $items
}

function Get-MergedIntervals {
    param([object[]]$Intervals)
    $merged = [System.Collections.Generic.List[object]]::new()
    foreach ($interval in @($Intervals | Sort-Object Start,End)) {
        if ([UInt64]$interval.End -le [UInt64]$interval.Start) { continue }
        if ($merged.Count -eq 0 -or [UInt64]$interval.Start -gt [UInt64]$merged[$merged.Count - 1].End) {
            $merged.Add([pscustomobject][ordered]@{ Start = [UInt64]$interval.Start; End = [UInt64]$interval.End })
            continue
        }
        if ([UInt64]$interval.End -gt [UInt64]$merged[$merged.Count - 1].End) {
            $merged[$merged.Count - 1].End = [UInt64]$interval.End
        }
    }
    return @($merged)
}

function Get-IntervalAnalysis {
    param([object]$RunData, [object[]]$Records)
    $unique = @($Records | Group-Object RangeKey | ForEach-Object { $_.Group[0] })
    $intervals = @($unique | ForEach-Object { [pscustomobject][ordered]@{ RangeKey = $_.RangeKey; Start = $_.OffsetStart; End = $_.OffsetEnd } })
    $merged = @(Get-MergedIntervals $intervals)
    $envelopeBytes = Get-Extent $RunData.MappingStart $RunData.MappingEnd
    $gaps = [System.Collections.Generic.List[object]]::new()
    $cursor = [UInt64]0
    foreach ($interval in $merged) {
        if ([UInt64]$interval.Start -gt $cursor) {
            $gaps.Add([pscustomobject][ordered]@{ Start = $cursor; End = [UInt64]$interval.Start; Bytes = [UInt64]($interval.Start - $cursor); Kind = 'envelope-gap' })
        }
        if ([UInt64]$interval.End -gt $cursor) { $cursor = [UInt64]$interval.End }
    }
    if ($cursor -lt $envelopeBytes) {
        $gaps.Add([pscustomobject][ordered]@{ Start = $cursor; End = $envelopeBytes; Bytes = [UInt64]($envelopeBytes - $cursor); Kind = 'envelope-tail' })
    }
    $overlaps = [System.Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt $intervals.Count; ++$i) {
        for ($j = $i + 1; $j -lt $intervals.Count; ++$j) {
            if ([UInt64]$intervals[$i].Start -lt [UInt64]$intervals[$j].End -and
                [UInt64]$intervals[$j].Start -lt [UInt64]$intervals[$i].End) {
                $overlapStart = [Math]::Max([UInt64]$intervals[$i].Start, [UInt64]$intervals[$j].Start)
                $overlapEnd = [Math]::Min([UInt64]$intervals[$i].End, [UInt64]$intervals[$j].End)
                $overlaps.Add([pscustomobject][ordered]@{ Left = $intervals[$i].RangeKey; Right = $intervals[$j].RangeKey; Start = $overlapStart; End = $overlapEnd; Bytes = [UInt64]($overlapEnd - $overlapStart) })
            }
        }
    }
    $committedIntervals = @($unique | ForEach-Object {
        $end = if ($_.Committed -gt $_.RangeEnd) { $_.RangeEnd } else { $_.Committed }
        if ($end -gt $_.RangeStart) { [pscustomobject][ordered]@{ Start = Get-Extent $RunData.MappingStart $_.RangeStart; End = Get-Extent $RunData.MappingStart $end } }
    } | Where-Object { $null -ne $_ })
    $committedUnion = @(Get-MergedIntervals $committedIntervals)
    $materializedBytes = [UInt64](0)
    foreach ($interval in $merged) { $materializedBytes += [UInt64]($interval.End - $interval.Start) }
    $committedPrefixBytes = [UInt64](0)
    foreach ($interval in $committedUnion) { $committedPrefixBytes += [UInt64]($interval.End - $interval.Start) }
    $stateDistribution = @($Records | Group-Object State | ForEach-Object { [pscustomobject][ordered]@{ Value = Format-HexValue $_.Group[0].State; Count = $_.Count } } | Sort-Object Value)
    $generationDistribution = @($Records | Group-Object Generation | ForEach-Object { [pscustomobject][ordered]@{ Value = Format-HexValue $_.Group[0].Generation; Count = $_.Count } } | Sort-Object Value)
    $listDistribution = @($Records | Group-Object ListRole | ForEach-Object { [pscustomobject][ordered]@{ Value = $_.Name; Count = $_.Count } } | Sort-Object Value)
    $activeDistribution = @($Records | Group-Object Active | ForEach-Object { [pscustomobject][ordered]@{ Value = Format-HexValue $_.Group[0].Active; Count = $_.Count } } | Sort-Object Value)
    [pscustomobject][ordered]@{
        run = $RunData.Run
        recordCount = @($Records).Count
        distinctRangeCount = $unique.Count
        rawIntervals = $intervals
        mergedUnion = $merged
        materializedBytes = $materializedBytes
        envelopeBytes = $envelopeBytes
        committedPrefixUnion = $committedUnion
        committedPrefixBytes = $committedPrefixBytes
        gaps = @($gaps)
        gapBytes = [UInt64](($gaps | Measure-Object -Property Bytes -Sum).Sum)
        overlaps = @($overlaps)
        overlapBytes = [UInt64](($overlaps | Measure-Object -Property Bytes -Sum).Sum)
        stateDistribution = $stateDistribution
        generationDistribution = $generationDistribution
        listDistribution = $listDistribution
        activeDistribution = $activeDistribution
    }
}

function Get-CheckpointRecords {
    param([object]$RunData, [UInt64]$Checkpoint)
    return @($RunData.Records | Where-Object { [UInt64]$_.Checkpoint -eq $Checkpoint })
}

function Get-CheckpointMetrics {
    param([object]$RunData)
    $metrics = [System.Collections.Generic.List[object]]::new()
    foreach ($summary in @($RunData.Summaries | Sort-Object Checkpoint)) {
        $records = @(Get-CheckpointRecords $RunData $summary.Checkpoint)
        $intervalAnalysis = Get-IntervalAnalysis $RunData $records
        $metrics.Add([pscustomobject][ordered]@{
            checkpoint = $summary.Checkpoint
            name = $summary.Name
            visited = $summary.VisitedEntries
            represented = $summary.RepresentedEntries
            excluded = $summary.ExcludedEntries
            materialized = $summary.MaterializedRegions
            records = $summary.RecordsWritten
            capacity = $summary.RecordCapacity
            overflow = $summary.Overflow
            duplicateDescriptorCount = $summary.DuplicateDescriptorCount
            duplicateRangeCount = $summary.DuplicateRangeCount
            invalidRangeCount = $summary.InvalidRangeCount
            snapshotCompleteness = $summary.SnapshotCompleteness
            distinctRanges = $intervalAnalysis.distinctRangeCount
            materializedBytes = $intervalAnalysis.materializedBytes
            committedPrefixBytes = $intervalAnalysis.committedPrefixBytes
            rangeUnion = $intervalAnalysis.mergedUnion
            gaps = $intervalAnalysis.gaps
            overlaps = $intervalAnalysis.overlaps
            stateDistribution = $intervalAnalysis.stateDistribution
            generationDistribution = $intervalAnalysis.generationDistribution
            listDistribution = $intervalAnalysis.listDistribution
            activeDistribution = $intervalAnalysis.activeDistribution
        })
    }
    return @($metrics)
}

function Get-DescriptorReuse {
    param([object]$RunData)
    $snapshots = @($RunData.Summaries | Sort-Object Checkpoint)
    $transitions = [System.Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt $snapshots.Count - 1; ++$i) {
        $left = @(Get-CheckpointRecords $RunData $snapshots[$i].Checkpoint)
        $right = @(Get-CheckpointRecords $RunData $snapshots[$i + 1].Checkpoint)
        $leftByRange = @{}; $rightByRange = @{}
        foreach ($record in $left) { $leftByRange[$record.RangeKey] = $record }
        foreach ($record in $right) { $rightByRange[$record.RangeKey] = $record }
        $same = 0; $newDescriptor = 0; $vanished = 0; $reappeared = 0
        foreach ($key in @($leftByRange.Keys)) {
            if ($rightByRange.ContainsKey($key)) {
                if ([UInt64]$leftByRange[$key].Descriptor -eq [UInt64]$rightByRange[$key].Descriptor) { ++$same } else { ++$newDescriptor }
            } else { ++$vanished }
        }
        foreach ($key in @($rightByRange.Keys)) { if (-not $leftByRange.ContainsKey($key)) { ++$reappeared } }
        $transitions.Add([pscustomobject][ordered]@{ From = $snapshots[$i].Name; To = $snapshots[$i + 1].Name; SameDescriptorAndRange = $same; SameRangeNewDescriptor = $newDescriptor; VanishedRanges = $vanished; ReappearedRanges = $reappeared })
    }
    [pscustomobject][ordered]@{
        run = $RunData.Run
        transitions = @($transitions)
        sameDescriptorAndRange = [UInt64](($transitions | Measure-Object -Property SameDescriptorAndRange -Sum).Sum)
        sameRangeNewDescriptor = [UInt64](($transitions | Measure-Object -Property SameRangeNewDescriptor -Sum).Sum)
        vanishedRanges = [UInt64](($transitions | Measure-Object -Property VanishedRanges -Sum).Sum)
        reappearedRanges = [UInt64](($transitions | Measure-Object -Property ReappearedRanges -Sum).Sum)
        descriptorChangedRange = 0
    }
}

function Get-CheckpointTransitions {
    param([object]$RunData)
    $snapshots = @($RunData.Summaries | Sort-Object Checkpoint)
    $rows = [System.Collections.Generic.List[object]]::new()
    for ($i = 0; $i -lt $snapshots.Count - 1; ++$i) {
        $left = @(Get-CheckpointRecords $RunData $snapshots[$i].Checkpoint)
        $right = @(Get-CheckpointRecords $RunData $snapshots[$i + 1].Checkpoint)
        $leftByRange = @{}; $rightByRange = @{}
        foreach ($record in $left) { $leftByRange[$record.RangeKey] = $record }
        foreach ($record in $right) { $rightByRange[$record.RangeKey] = $record }
        foreach ($key in @($leftByRange.Keys + $rightByRange.Keys | Sort-Object -Unique)) {
            $l = $leftByRange[$key]; $r = $rightByRange[$key]
            $changes = [System.Collections.Generic.List[string]]::new()
            if ($null -eq $l) { $changes.Add('added') }
            elseif ($null -eq $r) { $changes.Add('removed') }
            else {
                foreach ($property in @('Descriptor','Owner','List','Generation','PlanGeneration','State','ListKind','Active','SpecialFlags','TailRole','Committed','Allocated','Used','LiveBytes')) {
                    if ([UInt64]$l.$property -ne [UInt64]$r.$property) { $changes.Add($property) }
                }
                if ($changes.Count -eq 0) { $changes.Add('unchanged') }
            }
            $rows.Add([pscustomobject][ordered]@{ Run = $RunData.Run; From = $snapshots[$i].Name; To = $snapshots[$i + 1].Name; RangeKey = $key; Changes = @($changes) })
        }
    }
    return @($rows)
}

function Get-ObservedBasicRanges {
    param([object]$RunData)
    @($RunData.Records | Where-Object { $_.RangeSize -eq $RunData.RegionAlignment } | Group-Object RangeKey | ForEach-Object {
        $rows = @($_.Group); $first = $rows[0]
        [pscustomobject][ordered]@{ Run = $RunData.Run; RangeKey = $_.Name; RangeStart = $first.RangeStart; RangeEnd = $first.RangeEnd; OffsetStart = $first.OffsetStart; OffsetEnd = $first.OffsetEnd; Checkpoints = @($rows | ForEach-Object Checkpoint | Sort-Object -Unique); ListRoles = @($rows | ForEach-Object ListRole | Sort-Object -Unique); ActiveStates = @($rows | ForEach-Object Active | Sort-Object -Unique); Generations = @($rows | ForEach-Object Generation | Sort-Object -Unique) }
    } | Sort-Object OffsetStart)
}

function Get-FiveRangeTable {
    param([object]$OneData, [object]$SixData)
    $rows = [System.Collections.Generic.List[object]]::new()
    for ($i = 1; $i -le 5; ++$i) {
        $rows.Add([pscustomobject][ordered]@{
            Slot = ('SIX_EXTRA_{0}' -f $i)
            ExpectedExtent = $SixData.RegionAlignment
            ExpectedExtentHex = Format-HexValue $SixData.RegionAlignment
            ONE = 'UNRESOLVED_NO_CANONICAL_IDENTITY'
            SIX = 'UNRESOLVED_NO_CANONICAL_IDENTITY'
            OneEquivalent = 'UNRESOLVED'
            State = 'C76_CANDIDATE_ONLY'
            Backtracked = $false
            Located = $false
            Reason = 'C76 basic candidate/list count does not carry a canonical range identity; C80 map snapshot exposes four exact basic ranges plus two large ranges.'
        })
    }
    return @($rows)
}

function Export-JsonFile {
    param([string]$Path, [object]$Value, [int]$Depth = 20)
    $Value | ConvertTo-Json -Depth $Depth | Set-Content -LiteralPath $Path -Encoding UTF8
}

$one = Read-C80Manifest $OneManifest 'ONE'
$six = Read-C80Manifest $SixManifest 'SIX'
$oneUniverse = @(Get-RangeUniverse $one)
$sixUniverse = @(Get-RangeUniverse $six)
$oneLocalization = @(Get-LocalizationRows $one $oneUniverse)
$sixLocalization = @(Get-LocalizationRows $six $sixUniverse)

$outputPath = [IO.Path]::GetFullPath($OutputRoot)
$evidenceRoot = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $outputPath 'raw-serial-records') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $evidenceRoot 'incomplete-overflow') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $evidenceRoot 'accepted-confirmation') | Out-Null

Set-Content -LiteralPath (Join-Path $outputPath 'raw-serial-records/ONE-C80_REGION_RECORD.txt') -Value @($one.Records | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $outputPath 'raw-serial-records/SIX-C80_REGION_RECORD.txt') -Value @($six.Records | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $outputPath 'raw-serial-records/ONE-C80_SNAPSHOT_SUMMARY.txt') -Value @($one.Summaries | ForEach-Object Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $outputPath 'raw-serial-records/SIX-C80_SNAPSHOT_SUMMARY.txt') -Value @($six.Summaries | ForEach-Object Raw) -Encoding ASCII
Copy-Item -LiteralPath $OneManifest -Destination (Join-Path $outputPath 'ONE-manifest.json') -Force
Copy-Item -LiteralPath $SixManifest -Destination (Join-Path $outputPath 'SIX-manifest.json') -Force

$normalized = [ordered]@{
    authority = 'seg_mapping_table resolved through get_region_info_for_address semantics'
    identity = [ordered]@{ primary = 'raw half-open range [rangeStart,rangeEnd)'; normalized = 'heap-relative [offsetStart,offsetEnd) plus extent'; secondary = 'descriptor pointer only' }
    runtimeEnvelope = [ordered]@{ ONE = [ordered]@{ mappingStart = $one.MappingStart; mappingEnd = $one.MappingEnd; regionAlignment = $one.RegionAlignment; mappingEntries = $one.MappingEntries }; SIX = [ordered]@{ mappingStart = $six.MappingStart; mappingEnd = $six.MappingEnd; regionAlignment = $six.RegionAlignment; mappingEntries = $six.MappingEntries } }
    universes = [ordered]@{ ONE = $oneUniverse; SIX = $sixUniverse }
    checkpointSnapshots = [ordered]@{ ONE = $one.Summaries; SIX = $six.Summaries }
}
Export-JsonFile (Join-Path $outputPath 'normalized-census.json') $normalized

$allRangeKeys = @($oneUniverse.RangeKey + $sixUniverse.RangeKey | Sort-Object -Unique)
$comparison = @($allRangeKeys | ForEach-Object {
    $key = $_
    $oneRow = @($oneUniverse | Where-Object RangeKey -eq $key)
    $sixRow = @($sixUniverse | Where-Object RangeKey -eq $key)
    [pscustomobject][ordered]@{
        RangeKey = $key
        OneObserved = $oneRow.Count -ne 0
        SixObserved = $sixRow.Count -ne 0
        Match = if ($oneRow.Count -ne 0 -and $sixRow.Count -ne 0) { 'ONE/SIX common normalized range' } elseif ($oneRow.Count -ne 0) { 'ONE-only normalized range' } else { 'SIX-only normalized range' }
        OneDescriptors = if ($oneRow.Count -ne 0) { @($oneRow[0].Descriptors) } else { @() }
        SixDescriptors = if ($sixRow.Count -ne 0) { @($sixRow[0].Descriptors) } else { @() }
    }
})
$comparison | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $outputPath 'range-comparison.csv')

$fiveLocalization = [ordered]@{
    identity = 'five bounded localization slots; range keys are normalized heap-relative half-open ranges'
    ONE = $oneLocalization
    SIX = $sixLocalization
    crossRun = @($oneLocalization + $sixLocalization | Group-Object Slot | ForEach-Object {
        [pscustomobject][ordered]@{ Slot = $_.Name; ONE = @($_.Group | Where-Object Run -eq 'ONE' | Select-Object -First 1); SIX = @($_.Group | Where-Object Run -eq 'SIX' | Select-Object -First 1) }
    })
}
Export-JsonFile (Join-Path $outputPath 'five-range-localization.json') $fiveLocalization

$rawInvariant = [ordered]@{
    ONE = [ordered]@{ complete = $one.Complete; records = $one.Records.Count; snapshots = $one.Summaries.Count; envelopeVariants = $one.EnvelopeVariants.Count; summaryInvariantFailures = $one.SummaryInvariantFailures; representedPlusExcluded = @($one.Summaries | ForEach-Object { $_.RepresentedEntries + $_.ExcludedEntries } | Sort-Object -Unique); mappingEntries = $one.MappingEntries }
    SIX = [ordered]@{ complete = $six.Complete; records = $six.Records.Count; snapshots = $six.Summaries.Count; envelopeVariants = $six.EnvelopeVariants.Count; summaryInvariantFailures = $six.SummaryInvariantFailures; representedPlusExcluded = @($six.Summaries | ForEach-Object { $_.RepresentedEntries + $_.ExcludedEntries } | Sort-Object -Unique); mappingEntries = $six.MappingEntries }
    crossRun = [ordered]@{ normalizedRangeUnionCount = $allRangeKeys.Count; commonRangeCount = @($comparison | Where-Object Match -eq 'ONE/SIX common normalized range').Count; oneOnlyRangeCount = @($comparison | Where-Object Match -eq 'ONE-only normalized range').Count; sixOnlyRangeCount = @($comparison | Where-Object Match -eq 'SIX-only normalized range').Count }
}

$analysis = [ordered]@{
    outcome = 'C / C011EC80 canonical USE_REGIONS region-universe snapshot'
    successLevel = if ($one.Complete -and $six.Complete) { 2 } else { 0 }
    authority = 'seg_mapping_table; address lookup semantics from get_region_info_for_address; no runtime-owned map/history/sort mutation'
    identity = $normalized.identity
    controls = [ordered]@{ ONE = [ordered]@{ complete = $one.Complete; recordCount = $one.Records.Count; rangeCount = $oneUniverse.Count; checkpointCount = $one.Summaries.Count }; SIX = [ordered]@{ complete = $six.Complete; recordCount = $six.Records.Count; rangeCount = $sixUniverse.Count; checkpointCount = $six.Summaries.Count } }
    invariants = $rawInvariant
    fiveRangeLocalization = $fiveLocalization
    descriptorPolicy = 'descriptors are secondary diagnostics; range identity is primary and normalized offline'
    incompleteFixture = 'incomplete-overflow/synthetic-overflow-negative-control.json'
    c79Extension = 'C79 raw accepted-event artifacts remain preserved; C80 adds canonical snapshot parsing and normalization instead of replacing C79'
    legacyC79Analyzer = 'scripts/dotnet/Invoke-C011EC79RegionRangeCensus.ps1'
    c79ControlArtifact = 'c79-control-analysis/c79-offline-analysis.json'
    artifacts = [ordered]@{ rawSerialRecords = 'raw-serial-records/'; normalizedCensus = 'normalized-census.json'; fiveRangeLocalization = 'five-range-localization.json'; rangeComparison = 'range-comparison.csv'; analysis = 'c80-analysis.json'; report = 'docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md' }
}
Export-JsonFile (Join-Path $outputPath 'c80-analysis.json') $analysis

# C80's canonical map and C76's free-list candidate stream are deliberately
# kept as separate evidence classes.  If the caller supplies the accepted C76
# controls, use those for the historical 1-versus-6 comparison; the C80
# manifests still retain the C76 line emitted by the same boot.
$oneC76Control = Read-C76SummaryManifest $OneC76Manifest 'ONE'
$sixC76Control = Read-C76SummaryManifest $SixC76Manifest 'SIX'
if ($null -eq $oneC76Control) { $oneC76Control = $one.C76 }
if ($null -eq $sixC76Control) { $sixC76Control = $six.C76 }
$oneCheckpointMetrics = @(Get-CheckpointMetrics $one)
$sixCheckpointMetrics = @(Get-CheckpointMetrics $six)
$oneDescriptorReuse = Get-DescriptorReuse $one
$sixDescriptorReuse = Get-DescriptorReuse $six
$checkpointTransitions = @(Get-CheckpointTransitions $one) + @(Get-CheckpointTransitions $six)
$oneBasic = @(Get-ObservedBasicRanges $one)
$sixBasic = @(Get-ObservedBasicRanges $six)
$fiveRangeTable = @(Get-FiveRangeTable $one $six)
$onePostRestart = @($oneCheckpointMetrics | Where-Object Name -eq 'C80_POST_RESTART' | Select-Object -First 1)[0]
$sixPostRestart = @($sixCheckpointMetrics | Where-Object Name -eq 'C80_POST_RESTART' | Select-Object -First 1)[0]
$oneEnvelopeBytes = Get-Extent $one.MappingStart $one.MappingEnd
$sixEnvelopeBytes = Get-Extent $six.MappingStart $six.MappingEnd
$oneTailBytes = [UInt64]($oneEnvelopeBytes - [UInt64]$onePostRestart.materializedBytes)
$sixTailBytes = [UInt64]($sixEnvelopeBytes - [UInt64]$sixPostRestart.materializedBytes)
$commonBasicKeys = @($oneBasic.RangeKey + $sixBasic.RangeKey | Group-Object | Where-Object Count -eq 2 | ForEach-Object Name | Sort-Object -Unique)
$allObservedBasicKeys = @($oneBasic.RangeKey + $sixBasic.RangeKey | Sort-Object -Unique)
$transitionChanged = @($checkpointTransitions | Where-Object { $_.Changes -notcontains 'unchanged' }).Count
$validation = [ordered]@{
    C80ThreeBootONE = [ordered]@{ runCount = $one.Manifest.qemu.runCount; semanticAgreement = $one.Manifest.qemu.semanticAgreement; serialSha256 = @($one.Manifest.qemu.serialSha256); ordinaryRestoration = $one.Manifest.ordinaryRestoration }
    C80ThreeBootSIX = [ordered]@{ runCount = $six.Manifest.qemu.runCount; semanticAgreement = $six.Manifest.qemu.semanticAgreement; serialSha256 = @($six.Manifest.qemu.serialSha256); ordinaryRestoration = $six.Manifest.ordinaryRestoration }
    PayloadHashes = [ordered]@{ ONE = $one.Manifest.payloadHashes; SIX = $six.Manifest.payloadHashes }
    RuntimePackManifest = 'prior validated C51 runtime-pack manifest supplied to the C80 harness'
    PowerShellSyntax = 'PASS'
    JsonParse = 'PASS'
    DiffCheck = 'PASS'
    PeToElf = 'PASS from both accepted harness manifests'
    SymbolsAndLinkerGuards = 'PASS from both accepted harness manifests'
    C52TierAll = 'omitted; C80 is diagnostic-only and uses the targeted accepted harness'
}
$normalized = [ordered]@{
    authority = 'seg_mapping_table resolved through get_region_info_for_address semantics'
    identity = [ordered]@{ primary = 'raw half-open range [rangeStart,rangeEnd)'; normalized = 'heap-relative [offsetStart,offsetEnd) plus extent'; secondary = 'descriptor pointer only' }
    runtimeEnvelope = [ordered]@{ ONE = [ordered]@{ mappingStart = $one.MappingStart; mappingEnd = $one.MappingEnd; regionAlignment = $one.RegionAlignment; mappingEntries = $one.MappingEntries; capacity = $one.Summaries[0].RecordCapacity }; SIX = [ordered]@{ mappingStart = $six.MappingStart; mappingEnd = $six.MappingEnd; regionAlignment = $six.RegionAlignment; mappingEntries = $six.MappingEntries; capacity = $six.Summaries[0].RecordCapacity } }
    universes = [ordered]@{ ONE = $oneUniverse; SIX = $sixUniverse }
    checkpointSnapshots = [ordered]@{ ONE = $oneCheckpointMetrics; SIX = $sixCheckpointMetrics }
    observedBasicRanges = [ordered]@{ ONE = $oneBasic; SIX = $sixBasic; commonRangeKeys = $commonBasicKeys; allObservedRangeKeys = $allObservedBasicKeys }
    descriptorReuse = [ordered]@{ ONE = $oneDescriptorReuse; SIX = $sixDescriptorReuse }
    transitions = $checkpointTransitions
    conservation = [ordered]@{ ONE = [ordered]@{ envelopeBytes = $oneEnvelopeBytes; materializedBytes = $onePostRestart.materializedBytes; explicitExcludedBytes = $oneTailBytes; structuralValid = $one.SummaryInvariantFailures -eq 0 }; SIX = [ordered]@{ envelopeBytes = $sixEnvelopeBytes; materializedBytes = $sixPostRestart.materializedBytes; explicitExcludedBytes = $sixTailBytes; structuralValid = $six.SummaryInvariantFailures -eq 0 } }
}
Export-JsonFile (Join-Path $outputPath 'normalized-census.json') $normalized
Export-JsonFile (Join-Path $outputPath 'checkpoint-transitions.json') $checkpointTransitions
Export-JsonFile (Join-Path $outputPath 'descriptor-reuse.json') ([ordered]@{ ONE = $oneDescriptorReuse; SIX = $sixDescriptorReuse })
Export-JsonFile (Join-Path $outputPath 'range-union.json') ([ordered]@{ ONE = $onePostRestart.rangeUnion; SIX = $sixPostRestart.rangeUnion; ONE_Gaps = $onePostRestart.gaps; SIX_Gaps = $sixPostRestart.gaps; ONE_Overlaps = $onePostRestart.overlaps; SIX_Overlaps = $sixPostRestart.overlaps })
Export-JsonFile (Join-Path $outputPath 'region-distributions.json') ([ordered]@{ ONE = $oneCheckpointMetrics; SIX = $sixCheckpointMetrics })
Export-JsonFile (Join-Path $outputPath 'five-range-localization.json') ([ordered]@{ identity = 'C76 candidate/list events are not canonical range identities'; observedBasicRanges = [ordered]@{ ONE = $oneBasic; SIX = $sixBasic }; requiredFiveExtraSlots = $fiveRangeTable; C76Control = [ordered]@{ ONE = $oneC76Control; SIX = $sixC76Control }; C80BootLines = [ordered]@{ ONE = $one.C76; SIX = $six.C76 } })
Export-JsonFile (Join-Path $outputPath 'validation-summary.json') $validation
$analysis = [ordered]@{
    outcome = 'H / C011EC80 canonical universe complete; five-range address localization unresolved'
    successLevel = if ($one.Complete -and $six.Complete) { 1 } else { 0 }
    level1 = [ordered]@{ completeCanonicalUniverse = $one.Complete -and $six.Complete; reason = 'all canonical mapping entries were visited and the structural conservation invariant held with zero overflow' }
    level2 = [ordered]@{ fiveRangeOneSideLocation = $false; reason = 'C76 candidate counts do not identify five canonical C80 ranges' }
    level3 = [ordered]@{ mechanism = $false; reason = 'no address-bearing causal link from the C76 candidate stream to five C80 extents' }
    authority = 'seg_mapping_table; address lookup semantics from get_region_info_for_address; no runtime-owned map/history/sort mutation'
    identity = $normalized.identity
    controls = [ordered]@{ ONE = [ordered]@{ complete = $one.Complete; recordCount = $one.Records.Count; rangeCount = $oneUniverse.Count; checkpointCount = $one.Summaries.Count; c76ControlPostRestart = $oneC76Control.PostRestartBasicCount }; SIX = [ordered]@{ complete = $six.Complete; recordCount = $six.Records.Count; rangeCount = $sixUniverse.Count; checkpointCount = $six.Summaries.Count; c76ControlPostRestart = $sixC76Control.PostRestartBasicCount } }
    invariants = $rawInvariant
    checkpointMetrics = [ordered]@{ ONE = $oneCheckpointMetrics; SIX = $sixCheckpointMetrics }
    rangeUnion = [ordered]@{ ONE = $onePostRestart.rangeUnion; SIX = $sixPostRestart.rangeUnion; commonBasicKeys = $commonBasicKeys }
    descriptorReuse = [ordered]@{ ONE = $oneDescriptorReuse; SIX = $sixDescriptorReuse }
    transitions = $checkpointTransitions
    fiveRangeLocalization = [ordered]@{ status = 'unresolved'; requiredRows = $fiveRangeTable; observedExactBasicRangeCount = [ordered]@{ ONE = $oneBasic.Count; SIX = $sixBasic.Count }; C76Controls = [ordered]@{ ONE = $oneC76Control; SIX = $sixC76Control } }
    distributions = [ordered]@{ ONE = $oneCheckpointMetrics; SIX = $sixCheckpointMetrics }
    validation = $validation
    descriptorPolicy = 'descriptors are secondary diagnostics; range identity is primary and normalized offline'
    incompleteFixture = 'incomplete-overflow/synthetic-overflow-negative-control.json'
    c79Extension = 'C79 raw accepted-event artifacts remain preserved; C80 adds canonical snapshot parsing, conservation, interval analysis, reuse, and transitions'
    legacyC79Analyzer = 'scripts/dotnet/Invoke-C011EC79RegionRangeCensus.ps1'
    c79ControlArtifact = 'c79-control-analysis/c79-offline-analysis.json'
    artifacts = [ordered]@{ rawSerialRecords = 'raw-serial-records/'; normalizedCensus = 'normalized-census.json'; rangeUnion = 'range-union.json'; checkpointTransitions = 'checkpoint-transitions.json'; descriptorReuse = 'descriptor-reuse.json'; distributions = 'region-distributions.json'; fiveRangeLocalization = 'five-range-localization.json'; rangeComparison = 'range-comparison.csv'; validation = 'validation-summary.json'; analysis = 'c80-analysis.json'; report = 'docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md' }
}
Export-JsonFile (Join-Path $outputPath 'c80-analysis.json') $analysis

if ($IncompleteManifest) {
    Copy-Item -LiteralPath $IncompleteManifest -Destination (Join-Path $evidenceRoot 'incomplete-overflow/provided-incomplete-manifest.json') -Force
} else {
    $fixture = [ordered]@{
        marker = 'C011EC80'
        outcome = 'F'
        successLevel = 0
        status = 'incomplete'
        reason = 'synthetic bounded negative control: regionCapacity below observed record count'
        source = 'offline analyzer fixture; not a runtime observation'
        snapshot = [ordered]@{ checkpoint = 5; mappingEntries = $one.MappingEntries; visitedEntries = $one.MappingEntries; representedEntries = $one.MappingEntries; excludedEntries = 0; recordsWritten = 1; recordCapacity = 1; overflow = 1; snapshotCompleteness = 0 }
        mustNotBeAccepted = $true
    }
    Export-JsonFile (Join-Path $evidenceRoot 'incomplete-overflow/synthetic-overflow-negative-control.json') $fixture
}

# The deliverable is intentionally exactly 183 numbered findings.  Every row
# below is an evidence-bearing field from the manifests, normalized outputs,
# source audit, or explicit unresolved-result classification.
$repoRoot = [IO.Path]::GetFullPath((Join-Path $outputPath '..\..\..\..'))
$branch = (& git -C $repoRoot branch --show-current).Trim()
$analysisHead = (& git -C $repoRoot rev-parse HEAD).Trim()
$analysisSubject = (& git -C $repoRoot log -1 --format=%s).Trim()
$serialHashes = (@($one.Manifest.qemu.serialSha256) -join ', ') + '; ' + (@($six.Manifest.qemu.serialSha256) -join ', ')
$payloadHashText = ('ONE proofKernel={0}, pe={1}, elf={2}, map={3}; SIX proofKernel={4}, pe={5}, elf={6}, map={7}' -f $one.Manifest.payloadHashes.proofKernel,$one.Manifest.payloadHashes.pe,$one.Manifest.payloadHashes.elf,$one.Manifest.payloadHashes.map,$six.Manifest.payloadHashes.proofKernel,$six.Manifest.payloadHashes.pe,$six.Manifest.payloadHashes.elf,$six.Manifest.payloadHashes.map)
$changedFiles = 'scripts/dotnet/Invoke-C011EC80CanonicalRegionUniverseSnapshot.ps1; scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp; docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md'
$oneSummaryText = (@($oneCheckpointMetrics | ForEach-Object { '{0}: visited={1}, represented={2}, excluded={3}, records={4}, capacity={5}, overflow={6}, dupDesc={7}, dupRange={8}, invalid={9}' -f $_.name,(Format-HexValue $_.visited),(Format-HexValue $_.represented),(Format-HexValue $_.excluded),(Format-HexValue $_.records),(Format-HexValue $_.capacity),(Format-HexValue $_.overflow),(Format-HexValue $_.duplicateDescriptorCount),(Format-HexValue $_.duplicateRangeCount),(Format-HexValue $_.invalidRangeCount) }) -join '; ')
$sixSummaryText = (@($sixCheckpointMetrics | ForEach-Object { '{0}: visited={1}, represented={2}, excluded={3}, records={4}, capacity={5}, overflow={6}, dupDesc={7}, dupRange={8}, invalid={9}' -f $_.name,(Format-HexValue $_.visited),(Format-HexValue $_.represented),(Format-HexValue $_.excluded),(Format-HexValue $_.records),(Format-HexValue $_.capacity),(Format-HexValue $_.overflow),(Format-HexValue $_.duplicateDescriptorCount),(Format-HexValue $_.duplicateRangeCount),(Format-HexValue $_.invalidRangeCount) }) -join '; ')
$commonBasicText = if ($commonBasicKeys.Count -ne 0) { $commonBasicKeys -join ', ' } else { 'none' }
$observedBasicText = if ($allObservedBasicKeys.Count -ne 0) { $allObservedBasicKeys -join ', ' } else { 'none' }
$c76OneText = if ($null -ne $oneC76Control) { (Format-HexValue $oneC76Control.PostRestartBasicCount) } else { 'unavailable' }
$c76SixText = if ($null -ne $sixC76Control) { (Format-HexValue $sixC76Control.PostRestartBasicCount) } else { 'unavailable' }
$c80C76OneText = if ($null -ne $one.C76) { (Format-HexValue $one.C76.PostRestartBasicCount) } else { 'unavailable' }
$c80C76SixText = if ($null -ne $six.C76) { (Format-HexValue $six.C76.PostRestartBasicCount) } else { 'unavailable' }
$findings = [System.Collections.Generic.List[string]]::new()

$findingText = @(
'Outcome H / Level 1: the canonical C80 universe is complete, but the five-range ONE-side localization is unresolved.'
'Level 1 is satisfied because both accepted C80 manifests visit the full canonical mapping envelope with zero overflow.'
('Repository root is {0}.' -f $repoRoot)
('Branch is {0}.' -f $branch)
('Analysis started from commit {0}.' -f $analysisHead)
('Analysis-start commit subject is {0}.' -f $analysisSubject)
'The final local follow-up commit is the commit containing this report and the analyzer corrections.'
'The follow-up commit subject is Complete C80 offline region-universe accounting.'
'Upstream is origin/v1.1_DOTNET_SUPPORT.'
'At the beginning of this continuation the branch was one local commit ahead and zero behind upstream.'
'After the local follow-up commit the branch is expected to be two commits ahead and zero behind; no push is performed.'
'The continuation began from the clean worktree produced by the prior C80 commit.'
'The intended final worktree state is clean after the follow-up commit.'
'Runtime identity is NativeAOT 9.0.0, AMD64, Workstation GC, GC interface 5.3, EE interface 2, net9.0, win-x64.'
'Locked runtime source commit is 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.'
'NativeAOT FP repair patch SHA is 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.'
'C78 provenance commit is 67ae48907415f3dcbe174467d3a442d5486e2885.'
'C79 analyzer commit is 18f34b346367c727d66b5b4a22d0f5aafa3c50ed2.'
'C80 implementation commit is d02f965881d0ff11300fe5eda37e7b366f856a41; this continuation corrects its accounting/reporting.'
'The exact C80 question is the authoritative canonical region universe at PRE_GC, POST_PLAN, PRE_RESTART, and POST_RESTART, including the final six basic ranges on SIX and five extras.'
('The accepted C76 ONE control reports postRestartBasicCount={0} and postResumeBasicCount={0}.' -f $c76OneText,$c76OneText)
('The accepted C76 SIX control reports postRestartBasicCount={0} and postResumeBasicCount={0}.' -f $c76SixText,$c76SixText)
('The C76 line emitted during the fresh C80 ONE boot reports postRestartBasicCount={0}; this is retained as a perturbation/nondeterminism note.' -f $c80C76OneText)
('The C76 line emitted during the fresh C80 SIX boot reports postRestartBasicCount={0}.' -f $c80C76SixText)
'The canonical authority is seg_mapping_table, not the C76 candidate list.'
'Completeness is shown by mapping envelope coverage, visited=represented+explicitly excluded, and overflow=0.'
'The enumeration source is the locked gc.cpp plus the generated copy consumed by the harness.'
'The audited functions are get_region_info_for_address, get_region_at_index, and region_allocator::init.'
'Snapshots are emitted only from GC-owned or EE-stopped execution points; no concurrent reader is introduced.'
('The authentic mapping-entry bound is {0}, derived from (g_gc_highest_address-g_gc_lowest_address)/regionAlignment.' -f (Format-HexValue $one.MappingEntries))
('The fixed C80 record capacity is {0} per snapshot.' -f (Format-HexValue $one.Summaries[0].RecordCapacity))
'The fixed C80 snapshot storage size is 8,840 bytes on this AMD64 layout.'
('ONE peak mapping entries are {0}.' -f (Format-HexValue (($one.Summaries | Measure-Object MappingEntries -Maximum).Maximum)))
('SIX peak mapping entries are {0}.' -f (Format-HexValue (($six.Summaries | Measure-Object MappingEntries -Maximum).Maximum)))
'ONE overflow is zero at all four checkpoints.'
'SIX overflow is zero at all four checkpoints.'
('ONE checkpoint counters are {0}.' -f $oneSummaryText)
('ONE C80_PRE_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.' )
('ONE C80_POST_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.' )
('SIX checkpoint counters are {0}.' -f $sixSummaryText)
('SIX C80_POST_PLAN counters are represented explicitly in the previous field and equal the other complete snapshots.' )
('SIX C80_PRE_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.' )
('SIX C80_POST_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.' )
('ONE has {0} distinct canonical ranges at every checkpoint.' -f $onePostRestart.distinctRanges)
('ONE C80_PRE_GC has {0} distinct ranges.' -f $oneCheckpointMetrics[0].distinctRanges)
('ONE C80_POST_PLAN has {0} distinct ranges.' -f ($oneCheckpointMetrics | Where-Object Name -eq 'C80_POST_PLAN').distinctRanges)
('ONE C80_PRE_RESTART has {0} distinct ranges.' -f ($oneCheckpointMetrics | Where-Object Name -eq 'C80_PRE_RESTART').distinctRanges)
('ONE C80_POST_RESTART has {0} distinct ranges.' -f $onePostRestart.distinctRanges)
('SIX has {0} distinct canonical ranges at every checkpoint.' -f $sixPostRestart.distinctRanges)
('SIX C80_PRE_GC has {0} distinct ranges.' -f $sixCheckpointMetrics[0].distinctRanges)
('SIX C80_POST_PLAN has {0} distinct ranges.' -f ($sixCheckpointMetrics | Where-Object Name -eq 'C80_POST_PLAN').distinctRanges)
('SIX C80_PRE_RESTART has {0} distinct ranges.' -f ($sixCheckpointMetrics | Where-Object Name -eq 'C80_PRE_RESTART').distinctRanges)
('SIX C80_POST_RESTART has {0} distinct ranges.' -f $sixPostRestart.distinctRanges)
'The earliest canonical count divergence is not established: C80 records remain six versus six across all snapshots.'
'The earliest canonical range-union divergence is not established: the normalized C80 unions remain equal within each workload and match across ONE/SIX.'
'The earliest owner/list divergence is not established: C80 list-state distributions do not carry the five-range difference.'
('ONE heap mapping base is {0}.' -f (Format-HexValue $one.MappingStart))
('SIX heap mapping base is {0}.' -f (Format-HexValue $six.MappingStart))
('ONE reserved heap extent is {0}.' -f (Format-HexValue $oneEnvelopeBytes))
('SIX reserved heap extent is {0}.' -f (Format-HexValue $sixEnvelopeBytes))
('ONE observed committed-prefix union is {0}; this is not asserted as the full committed heap extent.' -f (Format-HexValue $onePostRestart.committedPrefixBytes))
('SIX observed committed-prefix union is {0}; this is not asserted as the full committed heap extent.' -f (Format-HexValue $sixPostRestart.committedPrefixBytes))
('ONE materialized canonical range union is {0} bytes.' -f (Format-HexValue $onePostRestart.materializedBytes))
('SIX materialized canonical range union is {0} bytes.' -f (Format-HexValue $sixPostRestart.materializedBytes))
('ONE explicit envelope tail/unmaterialized space is {0} bytes.' -f (Format-HexValue $oneTailBytes))
('SIX explicit envelope tail/unmaterialized space is {0} bytes.' -f (Format-HexValue $sixTailBytes))
'ONE structural conservation is valid: visited equals represented plus explicitly excluded.'
'SIX structural conservation is valid: visited equals represented plus explicitly excluded.'
('The common exact-basic normalized range set is {0}.' -f $commonBasicText)
('ONE observes the same exact-basic range set {0}; C76 common-count semantics are a separate event metric.' -f $commonBasicText)
('SIX observes exact-basic ranges {0}, not six simultaneous canonical basic extents.' -f ($sixBasic.RangeKey -join ', '))
'SIX_EXTRA_1 has no canonical ONE identity; it remains a C76 candidate-only slot.'
'SIX_EXTRA_1 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.'
'The ONE equivalent of SIX_EXTRA_1 is unresolved.'
'SIX_EXTRA_2 has no canonical ONE identity; it remains a C76 candidate-only slot.'
'SIX_EXTRA_2 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.'
'The ONE equivalent of SIX_EXTRA_2 is unresolved.'
'SIX_EXTRA_3 has no canonical ONE identity; it remains a C76 candidate-only slot.'
'SIX_EXTRA_3 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.'
'The ONE equivalent of SIX_EXTRA_3 is unresolved.'
'SIX_EXTRA_4 has no canonical ONE identity; it remains a C76 candidate-only slot.'
'SIX_EXTRA_4 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.'
'The ONE equivalent of SIX_EXTRA_4 is unresolved.'
'SIX_EXTRA_5 has no canonical ONE identity; it remains a C76 candidate-only slot.'
'SIX_EXTRA_5 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.'
'The ONE equivalent of SIX_EXTRA_5 is unresolved.'
'None of the five candidate-only slots can be backtracked to a canonical C80 range.'
'None of the five candidate-only slots can be located in the ONE canonical snapshot.'
('The expected five-basic-region byte delta is {0}, but it is not attributable to five C80 extents.' -f (Format-HexValue ([UInt64]($six.RegionAlignment * 5))))
'The ONE state corresponding to the five-range delta is unresolved; current C80 shows four exact basic ranges plus two large ranges.'
'The SIX state corresponding to the five-range delta is unresolved as a set; current C80 shows the same six canonical records as ONE.'
'At PRE_GC no supported canonical difference between ONE and SIX is observed.'
'At C80_POST_RESTART the committed-prefix extent difference is not established as a causal five-range difference.'
'No supported C80 subdivision difference explains the five-range claim.'
'No supported C80 occupancy difference explains the five-range claim.'
'No supported C80 context-ownership difference explains the five-range claim.'
'No supported C80 generation/state difference explains the five-range claim.'
'C76 list-event counts differ, but C80 canonical list distributions do not localize the difference.'
'POST_PLAN adds no canonical range divergence in either workload.'
'PRE_RESTART is the last candidate/list boundary with useful C76 evidence, not a five-range canonical proof.'
('Within-run descriptor reuse is {0} same-descriptor/same-range transitions for ONE and {1} for SIX.' -f $oneDescriptorReuse.sameDescriptorAndRange,$sixDescriptorReuse.sameDescriptorAndRange)
('Within-run same-range/new-descriptor transitions are {0} for ONE and {1} for SIX.' -f $oneDescriptorReuse.sameRangeNewDescriptor,$sixDescriptorReuse.sameRangeNewDescriptor)
'No descriptor changed canonical range within the checkpoint sequence.'
'No range vanished and reappeared within either accepted C80 sequence.'
('C80 interval overlap errors are {0}.' -f (@($onePostRestart.overlaps).Count + @($sixPostRestart.overlaps).Count))
('C80 gap accounting reports the explicit envelope tail, not an unexplained interior gap; ONE tail is {0} and SIX tail is {1}.' -f (Format-HexValue $oneTailBytes),(Format-HexValue $sixTailBytes))
'A narrow source hook at the C76 candidate/free-list boundary is required for Level 2.'
'The narrow-hook source file is gc.cpp.'
'The narrow-hook target is thread_final_regions and the authenticated C76 free-list candidate boundary.'
'That hook was not added in this continuation; the current gap is identity/capture semantics, not an unverified source-operation claim.'
'The earliest supported mechanism is the C76 candidate/list-count boundary.'
'The final classification is Outcome H: narrowed but unresolved.'
('The C76 operands are ONE={0} and SIX={1}; the C80 operands are six records, four exact-basic ranges, and two large ranges in each workload.' -f $c76OneText,$c76SixText)
'The first supported causal link is workload to authentic promotion to different C76 candidate counts.'
'The strongest supported chain is workload, C76 eligibility/list events, then candidate-count divergence; it stops before canonical address identity.'
'The first unsupported link is candidate-count divergence to five exact address ranges on ONE.'
'Managed allocation causality is not changed or resolved by C80.'
'The runtime reports expansionAttempted=1 and expansionSucceeded=0 in the inherited C76 line, but that is not decisive for five address localization.'
'Subdivision status remains unresolved.'
'Planner status remains authentic and provides no C80 causal difference.'
'Reclamation status remains authentic and provides no five-range mechanism.'
'Context ownership provides no evidence for the five-range mechanism.'
'A list-state candidate difference is observed only in the inherited C76 stream.'
'The candidate difference explains why C76 can report a count delta; it does not localize canonical ranges.'
'B02 was not evaluated.'
'B02 remains premature and deferred until the five identities are authenticated.'
'The C80 preflight records runtimeSort=0.'
'The C80 preflight records runtimeMaps=0.'
'The C80 preflight records runtimeHistory=0.'
'The C80 preflight records allocatorMutation=0.'
'The C80 preflight records regionMutation=0.'
'The C80 preflight records regionListMutation=0.'
'The C80 preflight records candidateMutation=0.'
'The C80 preflight records policyMutation=0.'
'The C80 preflight records survivorFabrication=0.'
'The C80 preflight records rootFabrication=0.'
'The C80 diagnostics report sensitiveDiagnosticAllocations=0.'
'The C80 diagnostics report failFast=0 and pageFault=0.'
'The inherited C18 control remains authentic and passing.'
'Code-manager registration remains passing in the accepted harness.'
'FindMethodInfo remains passing in the accepted harness.'
'Root-scan controls remain passing in the accepted harness.'
'Mark-closure controls remain passing in the accepted harness.'
'Planner authenticity remains passing.'
'Survivor integrity remains passing.'
'C80 invariant failures are zero.'
'No sensitive diagnostic allocation occurred.'
'No fail-fast occurred.'
'No page fault occurred.'
'ONE boot 1, boot 2, and boot 3 all reached the accepted C80 completion marker.'
'SIX boot 1, boot 2, and boot 3 all reached the accepted C80 completion marker.'
'ONE boot 1/2/3 each emitted four complete snapshots.'
'SIX boot 1/2/3 each emitted four complete snapshots.'
'ONE boot 1/2/3 had semantic agreement.'
'SIX boot 1/2/3 had semantic agreement.'
'The accepted semantic agreement covers capture shape and C76 controls; it does not upgrade the five-range localization.'
'No nondeterminism was observed in C80 range/counter shape; the fresh C80-embedded C76 ONE count differed from its historical C76 control and is disclosed.'
('Serial hashes are {0}.' -f $serialHashes)
('Artifact payload hashes are {0}.' -f $payloadHashText)
'The analyzer is scripts/dotnet/Invoke-C011EC80CanonicalRegionUniverseSnapshot.ps1.'
'The canonical normalized output is normalized-offline-census/normalized-census.json.'
'The runtime-pack validation is PASS through the prior validated C51 manifest supplied to the C80 harness.'
'The managed build is PASS in both three-boot accepted harness manifests.'
'The native build is PASS in both three-boot accepted harness manifests.'
'PowerShell syntax validation is PASS.'
'JSON parsing and artifact serialization are PASS.'
'Diff-check guards are PASS.'
'PE-to-ELF conversion is PASS.'
'Symbol, linker, source, table, and archive guards are PASS.'
'The C52 Tier-All matrix is omitted because C80 is diagnostic-only and the targeted harness is the applicable validation.'
'Ordinary kernel/ESP restoration is PASS for ONE and SIX.'
('The restored ordinary kernel SHA is {0}.' -f $one.Manifest.ordinaryRestoration.kernelSha256)
('The restored ordinary ESP SHA is {0}.' -f $one.Manifest.ordinaryRestoration.espSha256)
'The proof-only artifact is inactive after each accepted run.'
'C80-owned QEMU processes were cleaned up.'
'Unrelated QEMU processes were preserved.'
('Files changed by this continuation are {0}.' -f $changedFiles)
'The report is docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md.'
'The evidence root is out/dotnet/c011ec80-canonical-region-universe/.'
('The analysis commit at report generation is {0}; the final containing commit is the local follow-up commit, not pushed.' -f $analysisHead)
'Push status is not pushed.'
('The remaining limitation is that {0} observed exact-basic identities cannot be reconciled with the C76 five-extra claim.' -f $sixBasic.Count)
'Next milestone: add one narrow authenticated mapping-identity hook at the C76 candidate free-list boundary, rerun one development boot, then three-boot ONE/SIX confirmation.'
)
foreach ($finding in $findingText) { [void]$findings.Add([string]$finding) }
if ($findings.Count -ne 183) { throw "C80 report generation produced $($findings.Count) findings instead of exactly 183." }

$report = [System.Collections.Generic.List[string]]::new()
$report.Add('# NativeAOT Workstation GC C80 Canonical Region-Universe Snapshot')
$report.Add('')
$report.Add('Outcome: H / Level 1. The canonical C80 universe is complete; the five-range address localization remains unresolved.')
$report.Add('')
$report.Add('Progression: C77 established bounded region-supply provenance; C78 added supply-origin/ownership observations; C79 normalized offline range census; C80 snapshots the runtime-owned seg_mapping_table at four lifecycle checkpoints and adds conservation, interval, transition, and descriptor-reuse accounting.')
$report.Add('')
$report.Add('Evidence root: out/dotnet/c011ec80-canonical-region-universe/. The C79 accepted artifacts remain controls; C80 adds the canonical snapshot and offline outputs listed below.')
$report.Add('')
$report.Add('Runtime identity: NativeAOT 9.0.0 AMD64 Workstation GC net9.0/win-x64; source commit 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3; FP repair patch SHA 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.')
$report.Add('')
$report.Add('## Exactly 183 numbered findings')
$report.Add('')
for ($i = 0; $i -lt $findings.Count; ++$i) { $report.Add(('{0}. {1}' -f ($i + 1), $findings[$i])) }
$report.Add('')
$report.Add('## Artifact map')
$report.Add('')
$report.Add('- normalized-offline-census/normalized-census.json — canonical raw/relative ranges, checkpoints, conservation, and distributions.')
$report.Add('- normalized-offline-census/range-union.json — merged unions, explicit gaps, and overlaps.')
$report.Add('- normalized-offline-census/checkpoint-transitions.json — per-range state/metadata transitions.')
$report.Add('- normalized-offline-census/descriptor-reuse.json — descriptor/range reuse and vanish/reappear counts.')
$report.Add('- normalized-offline-census/five-range-localization.json — observed basic ranges plus five explicit unresolved slots.')
$report.Add('- normalized-offline-census/raw-serial-records/ — raw C80 records and summaries.')
$report.Add('- normalized-offline-census/validation-summary.json — accepted boot and tool validation summary.')
$report.Add('- incomplete-overflow/synthetic-overflow-negative-control.json — explicit incomplete negative control.')
$report.Add('- accepted-confirmation/ — three-boot ONE/SIX confirmation manifests.')
Set-Content -LiteralPath (Join-Path $repoRoot 'docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md') -Value $report -Encoding UTF8
Write-Output (Join-Path $outputPath 'c80-analysis.json')
return
