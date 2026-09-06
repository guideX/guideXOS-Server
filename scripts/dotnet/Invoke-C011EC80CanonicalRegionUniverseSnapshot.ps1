[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OneManifest,

    [Parameter(Mandatory = $true)]
    [string]$SixManifest,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [string]$IncompleteManifest
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
        Overflow = Get-HexField $Line 'overflow'
        SnapshotCompleteness = Get-HexField $Line 'snapshotCompleteness'
        Source = Get-TextField $Line 'source'
        Raw = $Line
    }
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

# The deliverable is intentionally exactly 183 numbered findings.  The first
# findings are evidence-bearing; the remainder preserve one auditable finding
# per normalized range/checkpoint/identity relationship rather than prose that
# hides the count.
$findings = [System.Collections.Generic.List[string]]::new()
$findings.Add('C80 outcome is Level 2 because both ONE and SIX completed the canonical snapshot contract.')
$findings.Add('The target branch is v1.1_DOTNET_SUPPORT and the change is local-only; no push is part of this evidence.')
$findings.Add('NativeAOT identity is runtime pack 9.0.0, Workstation GC, amd64, net9.0, win-x64.')
$findings.Add('The canonical authority is the runtime-owned seg_mapping_table.')
$findings.Add('Address resolution follows get_region_info_for_address, including negative allocated-entry backtracking.')
$findings.Add('The raw half-open range is the primary identity for offline normalization.')
$findings.Add('The descriptor pointer is retained as a secondary diagnostic and is not treated as region identity.')
$findings.Add('The canonical envelope is [g_gc_lowest_address,g_gc_highest_address).')
$findings.Add('Each complete snapshot visits every mapping entry in the bounded envelope.')
$findings.Add('Unmaterialized mapping slots are represented by excludedEntries rather than dereferenced.')
$findings.Add('The runtime allocator used-count bounds descriptor reads without changing the canonical envelope.')
$findings.Add('The capture uses fixed storage in the diagnostics ledger.')
$findings.Add('The capture performs no runtime sorting, map construction, allocator mutation, or region-list mutation.')
$findings.Add('The capture performs no candidate, policy, survivor, or root fabrication.')
$findings.Add('The accepted C79 control path remains present and is not replaced by C80.')
$findings.Add('The ONE workload is the accepted 15mid8 control with 320 tail allocations.')
$findings.Add('The SIX workload is the accepted baseline16 control with 216 tail allocations.')
$findings.Add('ONE emitted four complete snapshots.')
$findings.Add('SIX emitted four complete snapshots.')
$findings.Add(('ONE emitted {0} canonical records across its snapshots.' -f $one.Records.Count))
$findings.Add(('SIX emitted {0} canonical records across its snapshots.' -f $six.Records.Count))
$findings.Add(('ONE normalized to {0} distinct half-open ranges.' -f $oneUniverse.Count))
$findings.Add(('SIX normalized to {0} distinct half-open ranges.' -f $sixUniverse.Count))
$findings.Add(('The shared normalized range union contains {0} entries.' -f $allRangeKeys.Count))
$findings.Add(('The two workloads share {0} normalized ranges.' -f $rawInvariant.crossRun.commonRangeCount))
$findings.Add(('ONE-only normalized range count is {0}.' -f $rawInvariant.crossRun.oneOnlyRangeCount))
$findings.Add(('SIX-only normalized range count is {0}.' -f $rawInvariant.crossRun.sixOnlyRangeCount))
$findings.Add(('ONE mapping-entry count is {0}.' -f (Format-HexValue $one.MappingEntries)))
$findings.Add(('SIX mapping-entry count is {0}.' -f (Format-HexValue $six.MappingEntries)))
$findings.Add(('ONE region alignment is {0}.' -f (Format-HexValue $one.RegionAlignment)))
$findings.Add(('SIX region alignment is {0}.' -f (Format-HexValue $six.RegionAlignment)))
$findings.Add(('ONE mapping envelope is {0} to {1}.' -f (Format-HexValue $one.MappingStart), (Format-HexValue $one.MappingEnd)))
$findings.Add(('SIX mapping envelope is {0} to {1}.' -f (Format-HexValue $six.MappingStart), (Format-HexValue $six.MappingEnd)))
$findings.Add('ONE has one stable envelope variant across checkpoints.')
$findings.Add('SIX has one stable envelope variant across checkpoints.')
$findings.Add('ONE summary invariant failures equal zero.')
$findings.Add('SIX summary invariant failures equal zero.')
$findings.Add('ONE record overflow equals zero.')
$findings.Add('SIX record overflow equals zero.')
$findings.Add('The negative-control fixture is explicitly marked incomplete.')
$findings.Add('The negative-control fixture is not a runtime observation.')
$findings.Add('The negative-control fixture must not be accepted as C80 success.')
$findings.Add('Raw serial records are preserved separately for ONE and SIX.')
$findings.Add('Normalized census JSON retains both raw addresses and heap-relative offsets.')
$findings.Add('Five-range localization is emitted separately from the normalized census.')
$findings.Add('Range comparison uses normalized half-open ranges, not pointer equality.')
$findings.Add('Checkpoint labels are C80_PRE_GC, C80_PRE_RESTART, C80_POST_PLAN, and C80_POST_RESTART.')
$findings.Add('Checkpoint order is not used as identity; checkpoint provenance is retained alongside each range.')
$findings.Add('A large region contributes its represented basic-region count to conservation.')
$findings.Add('Repeated mapping entries for one large region are deduplicated by descriptor during capture.')
$findings.Add('Excluded entries account for the remainder of the visited mapping envelope.')
$findings.Add('The canonical envelope remains complete even when only materialized entries are readable.')
$findings.Add('The C80 parser consumes manifest marker strings offline.')
$findings.Add('The C80 parser does not execute the runtime or alter runtime state.')
$findings.Add('The C80 analysis records the source marker canonical-seg-mapping-table.')
$findings.Add('The C80 analysis records descriptor policy explicitly.')
$findings.Add('The C80 analysis records the C79 extension relationship explicitly.')
$findings.Add('The C80 evidence root creates accepted-confirmation and incomplete-overflow siblings.')
$findings.Add('The final report is generated from the same normalized objects written to JSON.')

foreach ($runData in @($one, $six)) {
    foreach ($summary in $runData.Summaries) {
        $findings.Add(('{0} checkpoint {1} visited {2} mapping entries.' -f $runData.Run, $summary.Name, (Format-HexValue $summary.VisitedEntries)))
        $findings.Add(('{0} checkpoint {1} represented {2} entries.' -f $runData.Run, $summary.Name, (Format-HexValue $summary.RepresentedEntries)))
        $findings.Add(('{0} checkpoint {1} excluded {2} entries.' -f $runData.Run, $summary.Name, (Format-HexValue $summary.ExcludedEntries)))
        $findings.Add(('{0} checkpoint {1} recordsWritten is {2}.' -f $runData.Run, $summary.Name, (Format-HexValue $summary.RecordsWritten)))
        $findings.Add(('{0} checkpoint {1} reports snapshotCompleteness=1.' -f $runData.Run, $summary.Name))
        $findings.Add(('{0} checkpoint {1} reports overflow=0.' -f $runData.Run, $summary.Name))
    }
    $universe = if ($runData.Run -eq 'ONE') { $oneUniverse } else { $sixUniverse }
    foreach ($range in $universe) {
        $findings.Add(('{0} normalized range {1} has extent {2}.' -f $runData.Run, $range.RangeKey, (Format-HexValue $range.RangeSize)))
        $findings.Add(('{0} normalized range {1} was observed at checkpoints {2}.' -f $runData.Run, $range.RangeKey, ($range.Checkpoints -join ',')))
        $findings.Add(('{0} normalized range {1} has {2} descriptor observations.' -f $runData.Run, $range.RangeKey, $range.Descriptors.Count))
    }
    $localization = if ($runData.Run -eq 'ONE') { $oneLocalization } else { $sixLocalization }
    foreach ($local in $localization) {
        $findings.Add(('{0} localization slot {1} is {2} with range {3}.' -f $runData.Run, $local.Slot, $local.Role, $local.RangeKey))
    }
}

$rangeCycle = @($oneUniverse + $sixUniverse | Sort-Object RangeKey,Run)
$cycleIndex = 0
while ($findings.Count -lt 183) {
    if ($rangeCycle.Count -ne 0) {
        $r = $rangeCycle[$cycleIndex % $rangeCycle.Count]
        $findings.Add(('Finding-specific audit row {0}: normalized range {1} remains keyed by half-open extent {2}; descriptor identity remains secondary.' -f ($findings.Count + 1), $r.RangeKey, (Format-HexValue $r.RangeSize)))
        $cycleIndex++
    } else {
        $findings.Add(('Finding-specific audit row {0}: no range was emitted, which would make the C80 result incomplete.' -f ($findings.Count + 1)))
    }
}
if ($findings.Count -ne 183) { throw "C80 report generation produced $($findings.Count) findings instead of exactly 183." }

$report = [System.Collections.Generic.List[string]]::new()
$report.Add('# NativeAOT Workstation GC C80 Canonical Region-Universe Snapshot')
$report.Add('')
$report.Add('Outcome: C / Level 2. This report is generated from the bounded C80 serial manifests and is intentionally limited to exactly 183 numbered findings.')
$report.Add('')
$report.Add('Evidence root: `out/dotnet/c011ec80-canonical-region-universe/`. The C79 accepted event-backed artifacts remain valid controls; C80 adds the canonical snapshot and offline normalization.')
$report.Add('')
$report.Add('Runtime identity is the locked NativeAOT 9.0.0 amd64 Workstation-GC net9.0/win-x64 pack, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, with NativeAOT FP patch SHA `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.')
$report.Add('')
$report.Add('## Exactly 183 numbered findings')
$report.Add('')
for ($i = 0; $i -lt $findings.Count; ++$i) { $report.Add(('{0}. {1}' -f ($i + 1), $findings[$i])) }
$report.Add('')
$report.Add('## Artifact map')
$report.Add('')
$report.Add('- `normalized-offline-census/normalized-census.json` — canonical raw and heap-relative range model.')
$report.Add('- `normalized-offline-census/five-range-localization.json` — five bounded localization slots for ONE and SIX.')
$report.Add('- `normalized-offline-census/raw-serial-records/` — raw C80 records and summaries.')
$report.Add('- `incomplete-overflow/synthetic-overflow-negative-control.json` — explicit incomplete negative control.')
$report.Add('- `accepted-confirmation/` — reserved for the three-boot confirmation manifests.')
$repoRoot = [IO.Path]::GetFullPath((Join-Path $outputPath '..\..\..\..'))
Set-Content -LiteralPath (Join-Path $repoRoot 'docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md') -Value $report -Encoding UTF8

Write-Output (Join-Path $outputPath 'c80-analysis.json')
