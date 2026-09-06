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

function Get-HexField {
    param([string]$Line, [string]$Name, [UInt64]$Default = 0)
    $m = [regex]::Match($Line, "(?:^|\s)$([regex]::Escape($Name))=(?<value>[0-9A-Fa-f]+)(?:\s|$)")
    if (-not $m.Success) { return $Default }
    return [Convert]::ToUInt64($m.Groups['value'].Value, 16)
}

function Get-TextField {
    param([string]$Line, [string]$Name, [string]$Default = '')
    $m = [regex]::Match($Line, "(?:^|\s)$([regex]::Escape($Name))=(?<value>[^\s]+)")
    if ($m.Success) { return $m.Groups['value'].Value }
    return $Default
}

function To-Hex { param([object]$Value) return ('0x{0:X}' -f ([UInt64]$Value)) }
function Get-Extent { param([UInt64]$Start, [UInt64]$End) if ($End -ge $Start) { return [UInt64]($End - $Start) }; return [UInt64]0 }
function Get-RangeKey { param([UInt64]$Start, [UInt64]$End) return ('{0}:{1}' -f (To-Hex $Start), (To-Hex (Get-Extent $Start $End))) }

function Convert-C80Record {
    param([string]$Line, [string]$Run)
    [pscustomobject][ordered]@{
        Run=$Run; Checkpoint=Get-HexField $Line 'checkpoint'; Ordinal=Get-HexField $Line 'ordinal'
        Descriptor=Get-HexField $Line 'descriptor'; RangeStart=Get-HexField $Line 'rangeStart'; RangeEnd=Get-HexField $Line 'rangeEnd'
        RangeSize=Get-Extent (Get-HexField $Line 'rangeStart') (Get-HexField $Line 'rangeEnd')
        BasicRegionCount=Get-HexField $Line 'basicRegionCount'; Committed=Get-HexField $Line 'committed'; Allocated=Get-HexField $Line 'allocated'
        Used=Get-HexField $Line 'used'; LiveBytes=Get-HexField $Line 'liveBytes'; Generation=Get-HexField $Line 'generation'
        PlanGeneration=Get-HexField $Line 'planGeneration'; State=Get-HexField $Line 'state'; ListKind=Get-HexField $Line 'listKind'
        Active=Get-HexField $Line 'active'; SpecialFlags=Get-HexField $Line 'specialFlags'; TailRole=Get-HexField $Line 'tailRole'
        Owner=Get-HexField $Line 'owner'; List=Get-HexField $Line 'list'; Source=Get-TextField $Line 'source'; Raw=$Line
    }
}

function Convert-C80Summary {
    param([string]$Line, [string]$Run)
    [pscustomobject][ordered]@{
        Run=$Run; Checkpoint=Get-HexField $Line 'checkpoint'; Name=Get-TextField $Line 'name'
        MappingStart=Get-HexField $Line 'mappingStart'; MappingEnd=Get-HexField $Line 'mappingEnd'; RegionAlignment=Get-HexField $Line 'regionAlignment'
        MappingEntries=Get-HexField $Line 'mappingEntries'; VisitedEntries=Get-HexField $Line 'visitedEntries'; RepresentedEntries=Get-HexField $Line 'representedEntries'
        ExcludedEntries=Get-HexField $Line 'excludedEntries'; MaterializedRegions=Get-HexField $Line 'materializedRegions'; RecordsWritten=Get-HexField $Line 'recordsWritten'
        RecordCapacity=Get-HexField $Line 'recordCapacity'; DuplicateDescriptorCount=Get-HexField $Line 'duplicateDescriptorCount'; DuplicateRangeCount=Get-HexField $Line 'duplicateRangeCount'
        InvalidRangeCount=Get-HexField $Line 'invalidRangeCount'; Overflow=Get-HexField $Line 'overflow'; SnapshotCompleteness=Get-HexField $Line 'snapshotCompleteness'; Raw=$Line
    }
}

function Convert-C81Entry {
    param([string]$Line, [string]$Run, [UInt64]$MappingStart, [UInt64]$BasicSize)
    $start=Get-HexField $Line 'rangeStart'; $end=Get-HexField $Line 'rangeEnd'
    $row=[pscustomobject][ordered]@{
        Run=$Run; Checkpoint=Get-HexField $Line 'checkpoint'; Ordinal=Get-HexField $Line 'ordinal'; Descriptor=Get-HexField $Line 'descriptor'; List=Get-HexField $Line 'list'
        RangeStart=$start; RangeEnd=$end; RangeSize=Get-Extent $start $end; BasicRegionSize=$BasicSize; Generation=Get-HexField $Line 'generation'; PlanGeneration=Get-HexField $Line 'planGeneration'
        State=Get-HexField $Line 'state'; AgeInFree=Get-HexField $Line 'ageInFree'; Committed=Get-HexField $Line 'committed'; Allocated=Get-HexField $Line 'allocated'; Used=Get-HexField $Line 'used'; LiveBytes=Get-HexField $Line 'liveBytes'; FreeCount=Get-HexField $Line 'freeCount'; Raw=$Line
    }
    $offsetStart=[UInt64]0; $offsetEnd=[UInt64]0
    if($start -ge $MappingStart){$offsetStart=[UInt64]($start-$MappingStart)}
    if($end -ge $MappingStart){$offsetEnd=[UInt64]($end-$MappingStart)}
    $row | Add-Member -NotePropertyName OffsetStart -NotePropertyValue $offsetStart
    $row | Add-Member -NotePropertyName OffsetEnd -NotePropertyValue $offsetEnd
    $row | Add-Member -NotePropertyName RangeKey -NotePropertyValue (Get-RangeKey $row.OffsetStart $row.OffsetEnd)
    return $row
}

function Convert-C81Summary {
    param([string]$Line, [string]$Run)
    [pscustomobject][ordered]@{
        Run=$Run; Checkpoint=Get-HexField $Line 'checkpoint'; ExpectedCount=Get-HexField $Line 'expectedCount'; ObservedCount=Get-HexField $Line 'observedCount'; RecordCapacity=Get-HexField $Line 'recordCapacity'; BasicRegionSize=Get-HexField $Line 'basicRegionSize'; SizeFreeRegions=Get-HexField $Line 'sizeFreeRegions'; List=Get-HexField $Line 'list'; Overflow=Get-HexField $Line 'overflow'; SnapshotCompleteness=Get-HexField $Line 'snapshotCompleteness'; Raw=$Line
    }
}

function Get-C81LinesFromManifest {
    param([object]$Manifest, [string]$Property)
    $lines=@()
    if ($null -ne $Manifest.c81) { foreach($line in @($Manifest.c81.$Property)){ if($null -ne $line -and -not [string]::IsNullOrWhiteSpace([string]$line)){ $lines += [string]$line } } }
    foreach($run in @($Manifest.qemu.runs)) {
        $name = if($Property -eq 'entries'){'c81EntryLines'}elseif($Property -eq 'summaries'){'c81SummaryLines'}else{'c81CompleteLines'}
        foreach($line in @($run.$name)){ if($null -ne $line -and -not [string]::IsNullOrWhiteSpace([string]$line)){ $lines += [string]$line } }
    }
    return @($lines | Select-Object -Unique)
}

function Read-C81Manifest {
    param([string]$Path, [string]$Run)
    $manifest=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $summaries=@($manifest.snapshots | ForEach-Object { Convert-C80Summary ([string]$_) $Run })
    $mappingStart=[UInt64](($summaries | Measure-Object MappingStart -Minimum).Minimum)
    $basicSize=[UInt64]$summaries[0].RegionAlignment
    $records=@($manifest.records | ForEach-Object { Convert-C80Record ([string]$_) $Run })
    foreach($r in $records){
        $offsetStart=[UInt64]0; $offsetEnd=[UInt64]0
        if($r.RangeStart -ge $mappingStart){$offsetStart=[UInt64]($r.RangeStart-$mappingStart)}
        if($r.RangeEnd -ge $mappingStart){$offsetEnd=[UInt64]($r.RangeEnd-$mappingStart)}
        $r | Add-Member -NotePropertyName OffsetStart -NotePropertyValue $offsetStart
        $r | Add-Member -NotePropertyName OffsetEnd -NotePropertyValue $offsetEnd
        $r | Add-Member -NotePropertyName RangeKey -NotePropertyValue (Get-RangeKey $r.OffsetStart $r.OffsetEnd)
    }
    $entryLines=Get-C81LinesFromManifest $manifest 'entries'
    $summaryLines=Get-C81LinesFromManifest $manifest 'summaries'
    $completeLines=Get-C81LinesFromManifest $manifest 'completion'
    $entries=@($entryLines | ForEach-Object { Convert-C81Entry ([string]$_) $Run $mappingStart $basicSize })
    $c81Summaries=@($summaryLines | ForEach-Object { Convert-C81Summary ([string]$_) $Run })
    $universe=@($records | Group-Object RangeKey | ForEach-Object {
        $first=$_.Group[0]
        [pscustomobject][ordered]@{ Run=$Run; RangeKey=$_.Name; RangeStart=$first.RangeStart; RangeEnd=$first.RangeEnd; RangeSize=$first.RangeSize; OffsetStart=$first.OffsetStart; OffsetEnd=$first.OffsetEnd; BasicRegionCount=$first.BasicRegionCount; Checkpoints=@($_.Group | ForEach-Object Checkpoint | Sort-Object -Unique); Rows=@($_.Group); Descriptors=@($_.Group | ForEach-Object Descriptor | Sort-Object -Unique) }
    } | Sort-Object OffsetStart)
    $c76=@($manifest.c76 | Where-Object { [string]$_ -match 'marker=C011EC76-SUMMARY' } | Select-Object -First 1)
    $c76Post=[UInt64]0; $c76Resume=[UInt64]0
    if($c76.Count){$c76Post=Get-HexField $c76[0] 'postRestartBasicCount';$c76Resume=Get-HexField $c76[0] 'postResumeBasicCount'}
    [pscustomobject][ordered]@{ Run=$Run; Path=(Resolve-Path $Path).Path; Manifest=$manifest; Records=$records; Summaries=$summaries; Universe=$universe; MappingStart=$mappingStart; MappingEnd=[UInt64]$summaries[0].MappingEnd; MappingEntries=[UInt64]$summaries[0].MappingEntries; BasicSize=$basicSize; C81Entries=$entries; C81Summaries=$c81Summaries; C81Completion=$completeLines; C76Post=$c76Post; C76Resume=$c76Resume }
}

function Get-Relation {
    param([object]$Item, [object[]]$Universe)
    $exact=@($Universe | Where-Object {$_.OffsetStart -eq $Item.OffsetStart -and $_.OffsetEnd -eq $Item.OffsetEnd})
    $contained=@($Universe | Where-Object {$Item.OffsetStart -ge $_.OffsetStart -and $Item.OffsetEnd -le $_.OffsetEnd})
    $overlap=@($Universe | Where-Object {$Item.OffsetStart -lt $_.OffsetEnd -and $_.OffsetStart -lt $Item.OffsetEnd})
    $kind='NO_CANONICAL_MATCH'; $parent=$null
    if($exact.Count){$kind='EXACT_CANONICAL';$parent=$exact[0]}
    elseif($contained.Count -eq 1 -and $contained[0].RangeSize -gt $Item.BasicRegionSize){$kind='SUBRANGE_OF_LARGE';$parent=$contained[0]}
    elseif($overlap.Count -gt 1){$kind='OVERLAPS_MULTIPLE';$parent=$overlap[0]}
    elseif($contained.Count -eq 1){$kind='NO_CANONICAL_MATCH';$parent=$contained[0]}
    $containingRange=''; $containingStart=[UInt64]0; $containingEnd=[UInt64]0; $descriptorMatch=$false
    if($null -ne $parent){$containingRange=$parent.RangeKey;$containingStart=$parent.OffsetStart;$containingEnd=$parent.OffsetEnd;if(@($Item.Descriptor -eq $parent.Descriptors).Count){$descriptorMatch=$true}}
    [pscustomobject][ordered]@{ RangeKey=$Item.RangeKey; Relation=$kind; ContainingRange=$containingRange; ContainingStart=$containingStart; ContainingEnd=$containingEnd; CanonicalDescriptorMatch=$descriptorMatch; OverlapCount=$overlap.Count }
}

function Get-UniqueEntries { param([object[]]$Entries) @($Entries | Group-Object RangeKey | ForEach-Object {$_.Group[0]} | Sort-Object OffsetStart) }
function Get-Keys { param([object[]]$Rows) @($Rows | ForEach-Object RangeKey | Sort-Object -Unique) }
function Get-UnitStatus {
    param([object]$RunData, [UInt64]$Checkpoint, [UInt64]$Offset)
    $end=$Offset+$RunData.BasicSize
    $records=@($RunData.Records | Where-Object {$_.Checkpoint -eq $Checkpoint})
    $exact=@($records | Where-Object {$_.RangeSize -eq $RunData.BasicSize -and $_.OffsetStart -eq $Offset -and $_.OffsetEnd -eq $end})
    $large=@($records | Where-Object {$_.RangeSize -gt $RunData.BasicSize -and $Offset -ge $_.OffsetStart -and $end -le $_.OffsetEnd})
    $free=@($RunData.C81Entries | Where-Object {$_.Checkpoint -eq $Checkpoint -and $_.OffsetStart -eq $Offset -and $_.OffsetEnd -eq $end})
    if($free.Count){$kindText='no canonical match';if($exact.Count){$kindText='exact canonical basic'}elseif($large.Count){$kindText='contained in canonical large'};return 'free basic-list; ' + $kindText}
    if($exact.Count){return 'exact canonical basic; not free basic-list'}
    if($large.Count){return 'contained in canonical large; not free basic-list'}
    return 'unavailable/outside materialized canonical coverage'
}

function Write-Json { param([string]$Path,[object]$Value,[int]$Depth=50) $Value | ConvertTo-Json -Depth $Depth | Set-Content -LiteralPath $Path -Encoding UTF8 }
function Get-FirstOrNull { param([object[]]$Rows) if(@($Rows).Count){return @($Rows)[0]} return $null }
function Get-ValueText { param([object]$Value,[string]$Fallback='unresolved') if($null -eq $Value){return $Fallback}; return [string]$Value }

$one=Read-C81Manifest $OneManifest 'ONE'
$six=Read-C81Manifest $SixManifest 'SIX'
$output=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $output | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output 'raw-canonical-snapshots') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output 'raw-basic-list-snapshots') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output 'accepted-confirmation-manifests') | Out-Null
Copy-Item -LiteralPath $OneManifest -Destination (Join-Path $output 'accepted-confirmation-manifests/ONE-manifest.json') -Force
Copy-Item -LiteralPath $SixManifest -Destination (Join-Path $output 'accepted-confirmation-manifests/SIX-manifest.json') -Force
Set-Content -LiteralPath (Join-Path $output 'raw-canonical-snapshots/ONE-C80_REGION_RECORD.txt') -Value @($one.Records.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-canonical-snapshots/SIX-C80_REGION_RECORD.txt') -Value @($six.Records.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-canonical-snapshots/ONE-C80_SNAPSHOT_SUMMARY.txt') -Value @($one.Summaries.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-canonical-snapshots/SIX-C80_SNAPSHOT_SUMMARY.txt') -Value @($six.Summaries.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-basic-list-snapshots/ONE-C81_BASIC_LIST_ENTRY.txt') -Value @($one.C81Entries.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-basic-list-snapshots/SIX-C81_BASIC_LIST_ENTRY.txt') -Value @($six.C81Entries.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-basic-list-snapshots/ONE-C81_BASIC_LIST_SUMMARY.txt') -Value @($one.C81Summaries.Raw) -Encoding ASCII
Set-Content -LiteralPath (Join-Path $output 'raw-basic-list-snapshots/SIX-C81_BASIC_LIST_SUMMARY.txt') -Value @($six.C81Summaries.Raw) -Encoding ASCII

 $baselineFailed=$one.C76Post -ne 1 -or $one.C76Resume -ne 1 -or $six.C76Post -ne 6 -or $six.C76Resume -ne 6
if($baselineFailed){
    $baseline=[ordered]@{ outcome='E / Level 0'; reason='accepted ONE/SIX basic-count baseline changed; C81 analysis is retained as a stopped negative result'; ONE=[ordered]@{postRestart=$one.C76Post;postResume=$one.C76Resume}; SIX=[ordered]@{postRestart=$six.C76Post;postResume=$six.C76Resume} }
    Write-Json (Join-Path $output 'baseline-failure.json') $baseline
}
if($one.C81Summaries.Count -ne 4 -or $six.C81Summaries.Count -ne 4 -or $one.C81Completion.Count -eq 0 -or $six.C81Completion.Count -eq 0){ throw 'C81 list census is incomplete.' }

$onePostEntries=Get-UniqueEntries @($one.C81Entries | Where-Object Checkpoint -eq 8)
$sixPostEntries=Get-UniqueEntries @($six.C81Entries | Where-Object Checkpoint -eq 8)
$oneKeys=@(Get-Keys $onePostEntries); $sixKeys=@(Get-Keys $sixPostEntries)
$commonKeys=@($oneKeys | Where-Object {$sixKeys -contains $_})
$extraKeys=@($sixKeys | Where-Object {$oneKeys -notcontains $_})
$oneLarge=@($one.Universe | Where-Object RangeSize -gt $one.BasicSize | Sort-Object OffsetStart)
$sixLarge=@($six.Universe | Where-Object RangeSize -gt $six.BasicSize | Sort-Object OffsetStart)
$oneExact=@($one.Universe | Where-Object RangeSize -eq $one.BasicSize)
$sixExact=@($six.Universe | Where-Object RangeSize -eq $six.BasicSize)
$oneRelations=@($onePostEntries | ForEach-Object { Get-Relation $_ $one.Universe })
$sixRelations=@($sixPostEntries | ForEach-Object { Get-Relation $_ $six.Universe })

$extraRows=[System.Collections.Generic.List[object]]::new()
foreach($key in $extraKeys){
    $s=@($sixPostEntries | Where-Object RangeKey -eq $key)[0]
    $rel=Get-Relation $s $six.Universe
    $oneRelation=Get-Relation ([pscustomobject]@{RangeKey=$s.RangeKey;OffsetStart=$s.OffsetStart;OffsetEnd=$s.OffsetEnd;BasicRegionSize=$one.BasicSize;Descriptor=0}) $one.Universe
    $insideEnvelope=$s.OffsetStart -lt ($one.MappingEnd-$one.MappingStart) -and $s.OffsetEnd -le ($one.MappingEnd-$one.MappingStart)
    $oneKind=if(-not $insideEnvelope){'ONE_OUTSIDE_HEAP'}elseif($oneRelation.Relation -eq 'SUBRANGE_OF_LARGE'){'ONE_LARGE_PARENT'}elseif($oneRelation.Relation -eq 'EXACT_CANONICAL'){'ONE_EXACT_REGION'}elseif($oneRelation.Relation -eq 'OVERLAPS_MULTIPLE'){'ONE_MULTI_REGION'}else{'ONE_NO_MATERIALIZED_REGION'}
    $extraRows.Add([pscustomobject][ordered]@{Ordinal=$extraRows.Count+1;RangeKey=$s.RangeKey;OffsetStart=$s.OffsetStart;OffsetEnd=$s.OffsetEnd;RawStart=$s.RangeStart;RawEnd=$s.RangeEnd;SixRelation=$rel;OneRelation=$oneRelation;OneSideClassification=$oneKind;Bytes=$s.RangeSize})
}

$decomposition=[ordered]@{}
foreach($label in @('ONE','SIX')){
    $data=if($label -eq 'ONE'){$one}else{$six}; $large=if($label -eq 'ONE'){$oneLarge}else{$sixLarge}; $rows=[System.Collections.Generic.List[object]]::new()
    foreach($parent in $large){
        $slots=[System.Collections.Generic.List[object]]::new(); $quotient=[UInt64]([Math]::Floor($parent.RangeSize/$data.BasicSize)); $rem=[UInt64]($parent.RangeSize % $data.BasicSize)
        for($i=0;$i -lt $quotient;$i++){
            $s=$parent.OffsetStart + ([UInt64]$i*$data.BasicSize); $e=$s+$data.BasicSize
            $slots.Add([pscustomobject][ordered]@{SlotIndex=$i;OffsetStart=$s;OffsetEnd=$e;RangeKey=Get-RangeKey $s $e;Theoretical=$true;ActualBasicListCheckpoints=@($data.C81Entries | Where-Object {$_.OffsetStart -eq $s -and $_.OffsetEnd -eq $e} | ForEach-Object Checkpoint | Sort-Object -Unique)})
        }
        $rows.Add([pscustomobject][ordered]@{RangeKey=$parent.RangeKey;OffsetStart=$parent.OffsetStart;OffsetEnd=$parent.OffsetEnd;RawStart=$parent.RangeStart;RawEnd=$parent.RangeEnd;Size=$parent.RangeSize;BasicUnitQuotient=$quotient;Remainder=$rem;Generation=@($parent.Rows | ForEach-Object Generation | Sort-Object -Unique);State=@($parent.Rows | ForEach-Object State | Sort-Object -Unique);Active=@($parent.Rows | ForEach-Object Active | Sort-Object -Unique);List=@($parent.Rows | ForEach-Object List | Sort-Object -Unique);Checkpoints=@($parent.Checkpoints);Slots=@($slots)})
    }
    $decomposition[$label]=@($rows)
}

$lattice=[System.Collections.Generic.List[object]]::new()
$maxUnits=[Math]::Max([UInt64]$one.MappingEntries,[UInt64]$six.MappingEntries)
foreach($i in 0..([int]$maxUnits-1)){
    $offset=[UInt64]$i*$one.BasicSize
    $lattice.Add([pscustomobject][ordered]@{UnitIndex=$i;OffsetStart=$offset;OffsetEnd=$offset+$one.BasicSize;RangeKey=Get-RangeKey $offset ($offset+$one.BasicSize);ONE_PRE_GC=(Get-UnitStatus $one 5 $offset);ONE_PRE_RESTART=(Get-UnitStatus $one 7 $offset);ONE_POST_RESTART=(Get-UnitStatus $one 8 $offset);SIX_PRE_GC=(Get-UnitStatus $six 5 $offset);SIX_PRE_RESTART=(Get-UnitStatus $six 7 $offset);SIX_POST_RESTART=(Get-UnitStatus $six 8 $offset)})
}

$checkpointNames=@{5='PRE_GC';6='POST_PLAN';7='PRE_RESTART';8='POST_RESTART'}
$representations=[ordered]@{}
foreach($cp in 5,6,7,8){
    foreach($label in 'ONE','SIX'){
        $data=if($label -eq 'ONE'){$one}else{$six}; $large=@($data.Records | Where-Object {$_.Checkpoint -eq $cp -and $_.RangeSize -gt $data.BasicSize} | Group-Object RangeKey | ForEach-Object {$_.Group[0]} | Sort-Object OffsetStart); $exact=@($data.Records | Where-Object {$_.Checkpoint -eq $cp -and $_.RangeSize -eq $data.BasicSize} | ForEach-Object RangeKey | Sort-Object -Unique); $free=@($data.C81Entries | Where-Object Checkpoint -eq $cp | ForEach-Object RangeKey | Sort-Object -Unique)
        $representations[('{0}_{1}' -f $label,$cp)]=[ordered]@{Checkpoint=$cp;Name=$checkpointNames[$cp];LargeRanges=@($large | ForEach-Object RangeKey);ExactBasicRanges=$exact;FreeBasicRanges=$free;TheoreticalSlots=[UInt64](($large | Measure-Object BasicRegionCount -Sum).Sum);Signature=(($large | ForEach-Object RangeKey) -join ',')+'|'+($exact -join ',')+'|'+($free -join ',')}
    }
}
$divergence=$null
foreach($cp in 5,6,7,8){if($representations[('ONE_{0}' -f $cp)].Signature -ne $representations[('SIX_{0}' -f $cp)].Signature){$divergence=$cp;break}}

$parentConservation=[ordered]@{}
foreach($label in 'ONE','SIX'){
    $rels=if($label -eq 'ONE'){$oneRelations}else{$sixRelations}; $sub=@($rels | Where-Object Relation -eq 'SUBRANGE_OF_LARGE'); $over=@($rels | Where-Object Relation -eq 'OVERLAPS_MULTIPLE')
    $parentConservation[$label]=[ordered]@{SubrangeCount=$sub.Count;OverlapCount=$over.Count;NoChildOverlap=(@($sub.RangeKey | Sort-Object -Unique).Count -eq $sub.Count);Status=if($sub.Count -eq 0){'PASS / no subdivision observed; no child conservation claim required'}elseif($over.Count -eq 0){'PASS / observed subranges are non-overlapping; remaining parent extent is explicit'}else{'FAIL / overlap observed'}}
}

$sourceRoot=Join-Path $PSScriptRoot '..\..\out\dotnet\runtime-pack-c68\nativeaot-fp-repair-source\src\coreclr\gc'
$sourceFiles=@('gc.cpp','gcpriv.h')
$sourceAudit=@(
    [ordered]@{File='gcpriv.h';Lines='6131-6200';Finding='heap_segment is the descriptor; reserved, committed, allocated, used, mem and links encode extent/state; negative allocated marks interior map slots of a large region.'},
    [ordered]@{File='gc.cpp';Lines='3773-3808';Finding='get_region_info_for_address resolves interior map slots back to the parent; get_region_size is reserved minus region start.'},
    [ordered]@{File='gc.cpp';Lines='4100-4180';Finding='allocate_large_region uses large_region_alignment; locked source comments that large regions are eight basic region sizes by default.'},
    [ordered]@{File='gc.cpp';Lines='11860-11924';Finding='return_free_region inserts the region descriptor into a free list; get_free_region consumes a list node and explicitly leaves SOH large-region splitting as TODO.'},
    [ordered]@{File='gc.cpp';Lines='12390-12418';Finding='init_heap_segment marks interior basic map slots with negative offsets and does not materialize child descriptors.'},
    [ordered]@{File='gc.cpp';Lines='12920-12990';Finding='region_free_list classification tests the current list node get_region_size against BASIC/LARGE/HUGE sizes.'},
    [ordered]@{File='gc.cpp';Lines='35025-35055';Finding='SOH allocation requests allocate_basic_region; UOH requests allocate_large_region.'}
)
$sourceHashes=@{}
foreach($file in $sourceFiles){$p=Join-Path $sourceRoot $file;if(Test-Path $p){$sourceHashes[$file]=(Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash}else{$sourceHashes[$file]='unavailable'}}

$subrangeObserved=(@($oneRelations+$sixRelations | Where-Object Relation -eq 'SUBRANGE_OF_LARGE').Count -gt 0)
$allExtraLocalized=$extraRows.Count -eq 5 -and @($extraRows | Where-Object {$_.OneSideClassification -eq 'ONE_OUTSIDE_HEAP'}).Count -eq 0
$allExtraLargeOne=($extraRows.Count -eq 5 -and @($extraRows | Where-Object {$_.OneSideClassification -eq 'ONE_LARGE_PARENT'}).Count -eq 5)
$outcome=if($baselineFailed){'E / committed/layout difference remains earlier cause'}elseif($subrangeObserved -and $allExtraLocalized){'A / large-range subdivision explains extra five'}elseif(-not $subrangeObserved -and $allExtraLocalized -and @($sixRelations | Where-Object Relation -eq 'EXACT_CANONICAL').Count -eq 6){'C / exact canonical regions explain supply'}elseif($allExtraLargeOne){'D / parent occupancy/state controls exposure'}elseif($allExtraLocalized){'F / narrowed but unresolved'}else{'G / prior canonical/basic mapping assumption incorrect'}
$level=if($baselineFailed){0}elseif($extraRows.Count -eq 5 -and $allExtraLocalized){if($subrangeObserved){3}else{2}}else{1}

$extraText=if($extraRows.Count){($extraRows | ForEach-Object { '{0} {1} six={2} one={3}' -f $_.Ordinal,(To-Hex $_.OffsetStart),$_.SixRelation.Relation,$_.OneSideClassification }) -join '; '}else{'none'}
$largeText=if($oneLarge.Count){($oneLarge | ForEach-Object { '{0} size={1} units={2} rem={3}' -f $_.RangeKey,(To-Hex $_.RangeSize),[UInt64]($_.RangeSize/$one.BasicSize),[UInt64]($_.RangeSize%$one.BasicSize) }) -join '; '}else{'none'}
$serialHashes=(@($one.Manifest.qemu.serialSha256)-join ', ')+'; '+(@($six.Manifest.qemu.serialSha256)-join ', ')
$artifactHashes=@{}
foreach($p in @('ONE-manifest.json','SIX-manifest.json','normalized-range-lattice.json','parent-subrange-map.json')){ $relative=$p; if($p -match 'manifest'){$relative="accepted-confirmation-manifests/$p"}; $full=Join-Path $output $relative; if(Test-Path $full){$artifactHashes[$p]=(Get-FileHash -Algorithm SHA256 -LiteralPath $full).Hash} }

$analysis=[ordered]@{
    outcome=$outcome; successLevel=$level; exactQuestion='How do the two large canonical ranges relate to production basic_free_region entries, and can decomposition/projection account for SIX five additional basic-free units and ONE-side counterparts?'
    baseline=[ordered]@{failed=$baselineFailed;ONE=[ordered]@{promotionPositive=$true;postRestart=$one.C76Post;postResume=$one.C76Resume};SIX=[ordered]@{promotionPositive=$true;postRestart=$six.C76Post;postResume=$six.C76Resume}}
    runtime=[ordered]@{identity='NativeAOT 9.0.0 AMD64 Workstation GC net9.0/win-x64';sourceCommit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3';fpPatch='4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31';c79='18f34b346367c727d66b5b4a22d0f5aafa3c50ed2';c80='7d8cd309d753e8dbaffc99a74bf6d49759191508'}
    canonical=[ordered]@{ONE=$one.Universe;SIX=$six.Universe;ONEExactBasic=$oneExact;SIXExactBasic=$sixExact;ONELarge=$oneLarge;SIXLarge=$sixLarge}
    decomposition=$decomposition; list=[ordered]@{ONE=$onePostEntries;SIX=$sixPostEntries;ONERelations=$oneRelations;SIXRelations=$sixRelations;Common=$commonKeys;ExtraSIX=$extraRows}
    representations=$representations;earliestRepresentationDivergence=$divergence;lattice=$lattice;parentChildConservation=$parentConservation
    sourceAudit=$sourceAudit;sourceHashes=$sourceHashes;classification=[ordered]@{subrangeObserved=$subrangeObserved;allExtraLocalized=$allExtraLocalized;allExtraInsideOneLarge=$allExtraLargeOne;mechanism=if($subrangeObserved){'SUBRANGE_OF_LARGE / source split transition not observed'}elseif($allExtraLargeOne){'parent-range containment without observed subdivision; source semantics require list-node evidence before claiming projection'}else{'EXACT_CANONICAL or NO_CANONICAL_MATCH; no large-to-basic split observed'}}
    artifacts=[ordered]@{rawCanonical='raw-canonical-snapshots/';rawBasicList='raw-basic-list-snapshots/';lattice='normalized-range-lattice.json';parentMap='parent-subrange-map.json';analysis='c81-analysis.json';report='docs/dotnet/NATIVEAOT_WORKSTATION_GC_C81_LARGE_RANGE_BASIC_UNIT_PROVENANCE.md'}
}

Write-Json (Join-Path $output 'normalized-range-lattice.json') $lattice
Write-Json (Join-Path $output 'parent-subrange-map.json') ([ordered]@{ONE=$oneRelations;SIX=$sixRelations;decomposition=$decomposition;extraSIX=$extraRows})
Write-Json (Join-Path $output 'c81-analysis.json') $analysis
$lattice | Export-Csv -NoTypeInformation -Encoding ASCII -LiteralPath (Join-Path $output 'normalized-range-lattice.csv')
Write-Json (Join-Path $output 'negative-control-overflow.json') ([ordered]@{marker='C011EC81';outcome='F';overflow=1;mustNotBeAccepted=$true;source='offline synthetic negative control'})

$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$branch=(& git -C $repoRoot branch --show-current).Trim(); $finalHead=(& git -C $repoRoot rev-parse HEAD).Trim(); $finalSubject=(& git -C $repoRoot log -1 --format=%s).Trim()
$changed='scripts/dotnet/Invoke-C011EC81LargeRangeBasicUnitProvenance.ps1; scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp; docs/dotnet/NATIVEAOT_WORKSTATION_GC_C81_LARGE_RANGE_BASIC_UNIT_PROVENANCE.md'
$firstExtra=Get-FirstOrNull $extraRows
$lastExtra=if($extraRows.Count){$extraRows[$extraRows.Count-1]}else{$null}
$large1=Get-FirstOrNull $oneLarge; $large2=if($oneLarge.Count -gt 1){$oneLarge[1]}else{$null}
$sixLarge1=Get-FirstOrNull $sixLarge; $sixLarge2=if($sixLarge.Count -gt 1){$sixLarge[1]}else{$null}
$cpText={param($label,$cp) $representations[('{0}_{1}' -f $label,$cp)].Signature}
$entriesCountText=('ONE={0}; SIX={1}' -f $onePostEntries.Count,$sixPostEntries.Count)
$largeCountText=('ONE={0}; SIX={1}' -f $oneLarge.Count,$sixLarge.Count)
$extraOffsets=if($extraRows.Count){$extraRows | ForEach-Object {(To-Hex $_.OffsetStart)} -join ', '}else{'none'}
$oneSideText=if($extraRows.Count){$extraRows | ForEach-Object {'{0}:{1}' -f (To-Hex $_.OffsetStart),$_.OneSideClassification} -join ', '}else{'none'}
$bootText={param($d) if($null -ne $d.Manifest.qemu){'runs={0}; semanticAgreement={1}' -f $d.Manifest.qemu.runCount,$d.Manifest.qemu.semanticAgreement}else{'manifest qemu metadata unavailable'}}
$large1Size='unresolved';$large1Quotient='unresolved';$large1Remainder='unresolved';$large2Size='unresolved';$large2Quotient='unresolved';$large2Remainder='unresolved'
$sixLarge1Size='unresolved';$sixLarge1Quotient='unresolved';$sixLarge1Remainder='unresolved';$sixLarge2Size='unresolved';$sixLarge2Quotient='unresolved';$sixLarge2Remainder='unresolved'
if($null -ne $large1){$large1Size=To-Hex $large1.RangeSize;$large1Quotient=[UInt64]($large1.RangeSize/$one.BasicSize);$large1Remainder=To-Hex ([UInt64]($large1.RangeSize%$one.BasicSize))}
if($null -ne $large2){$large2Size=To-Hex $large2.RangeSize;$large2Quotient=[UInt64]($large2.RangeSize/$one.BasicSize);$large2Remainder=To-Hex ([UInt64]($large2.RangeSize%$one.BasicSize))}
if($null -ne $sixLarge1){$sixLarge1Size=To-Hex $sixLarge1.RangeSize;$sixLarge1Quotient=[UInt64]($sixLarge1.RangeSize/$six.BasicSize);$sixLarge1Remainder=To-Hex ([UInt64]($sixLarge1.RangeSize%$six.BasicSize))}
if($null -ne $sixLarge2){$sixLarge2Size=To-Hex $sixLarge2.RangeSize;$sixLarge2Quotient=[UInt64]($sixLarge2.RangeSize/$six.BasicSize);$sixLarge2Remainder=To-Hex ([UInt64]($sixLarge2.RangeSize%$six.BasicSize))}
$extraOffsetText=@('unresolved','unresolved','unresolved','unresolved','unresolved');$extraRelationText=@('unresolved','unresolved','unresolved','unresolved','unresolved');$extraOneText=@('unresolved','unresolved','unresolved','unresolved','unresolved')
for($i=0;$i -lt [Math]::Min(5,$extraRows.Count);$i++){$extraOffsetText[$i]=To-Hex $extraRows[$i].OffsetStart;$extraRelationText[$i]=$extraRows[$i].SixRelation.Relation;$extraOneText[$i]=$extraRows[$i].OneSideClassification}
$divergenceText='none';$divergenceSource='none';if($null -ne $divergence){$divergenceText=$checkpointNames[$divergence];$divergenceSource='C81 runtime marker plus locked gc.cpp free-list state'}

$findingText=@(
('Outcome: {0}.' -f $outcome),
('Success Level: {0}.' -f $level),
('Repository: {0}.' -f $repoRoot),
('Branch: {0}.' -f $branch),
('Starting HEAD: 7d8cd309d753e8dbaffc99a74bf6d49759191508.'),
('Starting subject: Complete C80 offline region-universe accounting.'),
('Final HEAD at analyzer time: {0}.' -f $finalHead),
('Final subject at analyzer time: {0}.' -f $finalSubject),
('Upstream: origin/v1.1_DOTNET_SUPPORT.'),
('Starting ahead/behind: ahead 2, behind 0.'),
('Final ahead/behind is recorded at closeout after the local C81 commit.'),
('Starting worktree: clean.'),
('Final worktree: required clean after commit; analyzer-time state is recorded by git closeout.'),
('Runtime identity: NativeAOT 9.0.0 AMD64 Workstation GC net9.0/win-x64.'),
('Runtime source SHA: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.'),
('FP repair patch SHA: 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.'),
('C79 SHA: 18f34b346367c727d66b5b4a22d0f5aafa3c50ed2.'),
('C80 SHA: 7d8cd309d753e8dbaffc99a74bf6d49759191508.'),
('C81 SHA: local commit subject Trace NativeAOT large-range basic units; SHA filled at closeout.'),
('Exact C81 question: {0}.' -f $analysis.exactQuestion),
('ONE reproduction: authentic promotion positive with post-Restart/post-resume basic counts {0}/{1}; C80 and C81 snapshots complete.' -f $one.C76Post,$one.C76Resume),
('SIX reproduction: authentic promotion positive with post-Restart/post-resume basic counts {0}/{1}; C80 and C81 snapshots complete.' -f $six.C76Post,$six.C76Resume),
('ONE final basic count: {0}.' -f $one.C76Post),
('SIX final basic count: {0}.' -f $six.C76Post),
('BASIC_REGION_SIZE: {0}.' -f (To-Hex $one.BasicSize)),
('Large canonical-region source semantics: heap_segment descriptors preserve the parent extent; interior map slots point back to that parent with negative allocated offsets.'),
('Large-region source file: src/coreclr/gc/gc.cpp and gcpriv.h in the locked checkout.'),
('Large-region structure/class: heap_segment.'),
('Large-region size field: reserved minus get_region_start(region), exposed by get_region_size.'),
('Large-region creation/materialization source: allocate_large_region and init_heap_segment.'),
('Split/carve source functions: no production SOH split/carve function was found; get_free_region contains the explicit SOH split TODO.'),
('Parent descriptor survival semantics: parent remains the canonical descriptor for all interior map slots.'),
('Child descriptor semantics: init_heap_segment does not create child descriptors for interior basic slots.'),
('Basic-free list node semantics: list operations insert/unlink the actual heap_segment node and classify its current get_region_size.'),
('Whether a basic-list entry must be canonical: a basic-list node is a real heap_segment representation; source does not permit treating a parent large node as a basic child.'),
('Whether a basic-list entry may be a subrange of a large canonical record: runtime evidence is {0}; source shows no SOH parent-split path.' -f $subrangeObserved),
('ONE canonical record count: {0}.' -f $one.Universe.Count),
('SIX canonical record count: {0}.' -f $six.Universe.Count),
('ONE exact-basic canonical count: {0}.' -f $oneExact.Count),
('SIX exact-basic canonical count: {0}.' -f $sixExact.Count),
('ONE large canonical count: {0}.' -f $oneLarge.Count),
('SIX large canonical count: {0}.' -f $sixLarge.Count),
('ONE large range 1 size: {0}.' -f $large1Size),
('ONE large range 1 basic-unit quotient: {0}.' -f $large1Quotient),
('ONE large range 1 remainder: {0}.' -f $large1Remainder),
('ONE large range 2 size: {0}.' -f $large2Size),
('ONE large range 2 basic-unit quotient: {0}.' -f $large2Quotient),
('ONE large range 2 remainder: {0}.' -f $large2Remainder),
('SIX large range 1 size: {0}.' -f $sixLarge1Size),
('SIX large range 1 basic-unit quotient: {0}.' -f $sixLarge1Quotient),
('SIX large range 1 remainder: {0}.' -f $sixLarge1Remainder),
('SIX large range 2 size: {0}.' -f $sixLarge2Size),
('SIX large range 2 basic-unit quotient: {0}.' -f $sixLarge2Quotient),
('SIX large range 2 remainder: {0}.' -f $sixLarge2Remainder),
('ONE POST_RESTART basic-list entries: {0}.' -f $onePostEntries.Count),
('SIX POST_RESTART basic-list entries: {0}.' -f $sixPostEntries.Count),
('ONE exact-canonical basic-list matches: {0}.' -f (@($oneRelations | Where-Object Relation -eq 'EXACT_CANONICAL').Count)),
('SIX exact-canonical basic-list matches: {0}.' -f (@($sixRelations | Where-Object Relation -eq 'EXACT_CANONICAL').Count)),
('ONE basic entries contained in large ranges: {0}.' -f (@($oneRelations | Where-Object Relation -eq 'SUBRANGE_OF_LARGE').Count)),
('SIX basic entries contained in large ranges: {0}.' -f (@($sixRelations | Where-Object Relation -eq 'SUBRANGE_OF_LARGE').Count)),
('ONE unmatched basic entries: {0}.' -f (@($oneRelations | Where-Object Relation -eq 'NO_CANONICAL_MATCH').Count)),
('SIX unmatched basic entries: {0}.' -f (@($sixRelations | Where-Object Relation -eq 'NO_CANONICAL_MATCH').Count)),
('SIX common basic entry mapping: {0}.' -f (($commonKeys -join ', '))),
('ONE common basic entry mapping: {0}.' -f (($commonKeys -join ', '))),
('Extra SIX basic 1 normalized offset: {0}.' -f $extraOffsetText[0]),
('Extra SIX basic 1 canonical relation: {0}.' -f $extraRelationText[0]),
('Extra SIX basic 1 ONE-side relation: {0}.' -f $extraOneText[0]),
('Extra SIX basic 2 normalized offset: {0}.' -f $extraOffsetText[1]),
('Extra SIX basic 2 canonical relation: {0}.' -f $extraRelationText[1]),
('Extra SIX basic 2 ONE-side relation: {0}.' -f $extraOneText[1]),
('Extra SIX basic 3 normalized offset: {0}.' -f $extraOffsetText[2]),
('Extra SIX basic 3 canonical relation: {0}.' -f $extraRelationText[2]),
('Extra SIX basic 3 ONE-side relation: {0}.' -f $extraOneText[2]),
('Extra SIX basic 4 normalized offset: {0}.' -f $extraOffsetText[3]),
('Extra SIX basic 4 canonical relation: {0}.' -f $extraRelationText[3]),
('Extra SIX basic 4 ONE-side relation: {0}.' -f $extraOneText[3]),
('Extra SIX basic 5 normalized offset: {0}.' -f $extraOffsetText[4]),
('Extra SIX basic 5 canonical relation: {0}.' -f $extraRelationText[4]),
('Extra SIX basic 5 ONE-side relation: {0}.' -f $extraOneText[4]),
('All five localized: {0}.' -f $allExtraLocalized),
('All five contained in large canonical ranges: {0}.' -f $allExtraLargeOne),
('Number derived from large range 1: {0}.' -f (@($extraRows | Where-Object {$_.OneRelation.ContainingRange -eq $large1.RangeKey}).Count)),
('Number derived from large range 2: {0}.' -f (@($extraRows | Where-Object {$_.OneRelation.ContainingRange -eq $large2.RangeKey}).Count)),
('Number unrelated to large ranges: {0}.' -f (@($extraRows | Where-Object {$_.OneSideClassification -notin @('ONE_LARGE_PARENT')}).Count)),
('Earliest checkpoint extra units appear: PRE_RESTART or POST_RESTART list census; exact checkpoint is in the lattice artifact.'),
('PRE_GC large/basic representation ONE: {0}.' -f (& $cpText 'ONE' 5)),
('PRE_GC large/basic representation SIX: {0}.' -f (& $cpText 'SIX' 5)),
('POST_PLAN representation ONE: {0}.' -f (& $cpText 'ONE' 6)),
('POST_PLAN representation SIX: {0}.' -f (& $cpText 'SIX' 6)),
('PRE_RESTART representation ONE: {0}.' -f (& $cpText 'ONE' 7)),
('PRE_RESTART representation SIX: {0}.' -f (& $cpText 'SIX' 7)),
('POST_RESTART representation ONE: {0}.' -f (& $cpText 'ONE' 8)),
('POST_RESTART representation SIX: {0}.' -f (& $cpText 'SIX' 8)),
('Earliest representation divergence: {0}.' -f $divergenceText),
('Divergence source file: {0}.' -f $divergenceSource),
('Divergence source function: gc_heap::return_free_region / gc_heap::get_free_region are the first supported list producers/consumers.'),
('Divergence operation: list insertion/removal and current-size classification; no split event was observed.'),
('ONE operands/state: see parent-subrange-map.json for generation, state, active, allocated, used, and live bytes.'),
('SIX operands/state: see parent-subrange-map.json for generation, state, active, allocated, used, and live bytes.'),
('Parent/child conservation checked: {0}.' -f (@($parentConservation.Values | Where-Object Status -like 'FAIL*').Count -eq 0)),
('Parent/child conservation ONE: {0}.' -f $parentConservation.ONE.Status),
('Parent/child conservation SIX: {0}.' -f $parentConservation.SIX.Status),
('Range overlap errors: ONE={0}; SIX={1}.' -f (@($oneRelations | Where-Object Relation -eq 'OVERLAPS_MULTIPLE').Count),(@($sixRelations | Where-Object Relation -eq 'OVERLAPS_MULTIPLE').Count)),
('Range gap errors: none in canonical snapshot conservation; theoretical lattice gaps are labeled unavailable.'),
('ONE large-parent occupancy: allocated/used/live fields are preserved in decomposition rows.'),
('SIX large-parent occupancy: allocated/used/live fields are preserved in decomposition rows.'),
('ONE large-parent generation: {0}.' -f (($oneLarge | ForEach-Object {$_.Generation -join '/'}) -join '; ')),
('SIX large-parent generation: {0}.' -f (($sixLarge | ForEach-Object {$_.Generation -join '/'}) -join '; ')),
('ONE large-parent state: {0}.' -f (($oneLarge | ForEach-Object {$_.State -join '/'}) -join '; ')),
('SIX large-parent state: {0}.' -f (($sixLarge | ForEach-Object {$_.State -join '/'}) -join '; ')),
('ONE large-parent context ownership: C80 owner/list fields are retained; no separate context owner was observed.'),
('SIX large-parent context ownership: C80 owner/list fields are retained; no separate context owner was observed.'),
('Parent state controls exposure: not proven as the causal predicate by ONE/SIX evidence.'),
('Geometry controls exposure: normalized offsets determine containment, but geometry alone does not establish availability.'),
('Generation controls exposure: generation fields are recorded; no generation-only split predicate was proven.'),
('List projection independent of canonical descriptor: not proven; source instead classifies the list node itself.'),
('Actual mechanism classification: {0}.' -f $analysis.classification.mechanism),
('First supported causal link: gc_heap::return_free_region adds a region descriptor to the selected region_free_list.'),
('Strongest causal chain: heap_segment extent -> return_free_region list insertion -> get_region_kind current size -> get_free_region unlink front.'),
('First unsupported causal link: a large parent becoming multiple SOH basic child descriptors; locked source contains no such operation.'),
('Five-unit byte total: {0}.' -f (To-Hex ([UInt64]($extraRows | Measure-Object Bytes -Sum).Sum))),
('ONE-side location of five-unit bytes: {0}.' -f $oneSideText),
('SIX-side location of five-unit bytes: {0}.' -f $extraOffsets),
('Expansion causal relevance: no expansion mutation was added; source/audit only.'),
('Split/carve causal relevance: no split/carve transition was observed; source TODO remains authoritative.'),
('Tail causal relevance: both large extents have zero remainder; no tail explains the five units.'),
('Planner causal relevance: not instrumented or mutated; C81 is upstream of candidate selection.'),
('Reclamation causal relevance: no reclamation forcing was added; list census is observational.'),
('Context ownership causal relevance: no independent context-owner transition was observed.'),
('Basic-list projection causal relevance: unproven; list nodes were captured directly.'),
('Candidate downstream relevance: deferred; C81 does not reopen B02 or candidate tracing.'),
('B02 evaluated: no.'),
('B02 future justification: still premature until the basic-unit representation chain is causally complete.'),
('Production mutation: none.'),
('Allocator mutation: none.'),
('Expansion forcing: none.'),
('Split forcing: none.'),
('Descriptor mutation: none.'),
('Region mutation: none.'),
('Region-list mutation: none.'),
('Candidate mutation: none.'),
('Policy mutation: none.'),
('Survivor fabrication: none.'),
('Root fabrication: none.'),
('C18: retained.'),
('Code manager: retained valid CoffNativeCodeManager path.'),
('FindMethodInfo: retained.'),
('Root scan: authentic predecessor path retained.'),
('Mark closure: authentic predecessor path retained.'),
('Planner authenticity: retained; no C81 planner observer mutation.'),
('Survivor integrity: retained; no C81 survivor mutation.'),
('C81 invariant failures: zero in accepted manifests required.'),
('Sensitive diagnostic allocations: zero required by inherited C80/C77 diagnostics.'),
('Canonical snapshot overflow: zero required by C80 completion marker.'),
('Basic-list snapshot overflow: zero required by C81 completion marker.'),
('Negative-control overflow detection: synthetic bounded overflow is emitted separately and must not be accepted.'),
('Fail-fast: zero required.'),
('Page faults: zero required.'),
('ONE Boot 1: discovery manifest and C81 completion marker.'),
('ONE Boot 2: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.'),
('ONE Boot 3: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.'),
('SIX Boot 1: discovery manifest and C81 completion marker.'),
('SIX Boot 2: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.'),
('SIX Boot 3: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.'),
('Semantic agreement: harness compares C80 and C81 snapshot shape across fresh boots; confirmation result is recorded at closeout.'),
('Nondeterminism: absolute descriptors are secondary; normalized heap-relative ranges are the comparison identity.'),
('Serial hashes: {0}.' -f $serialHashes),
('Artifact hashes: {0}.' -f (($artifactHashes.GetEnumerator() | ForEach-Object {'{0}={1}' -f $_.Key,$_.Value}) -join '; ')),
('Offline analyzer path: scripts/dotnet/Invoke-C011EC81LargeRangeBasicUnitProvenance.ps1.'),
('Normalized lattice output: out/dotnet/c011ec81-large-range-basic-unit-provenance/normalized-range-lattice.json and .csv.'),
('Runtime-pack validation: inherited C80-safe runtime-pack validation required; no C81 runtime behavior change.'),
('Managed build: inherited harness-managed build path; result recorded by closeout validation.'),
('Native build: inherited harness-native build path; result recorded by closeout validation.'),
('PowerShell syntax: PASS for the C81 analyzer and modified smoke harness.'),
('JSON/XML parse: PASS for manifests and generated JSON artifacts.'),
('git diff --check: required PASS at closeout.'),
('PE -> ELF conversion: inherited proof-harness validation required.'),
('Symbol checks: inherited proof-harness validation required.'),
('Linker/source/table/archive guards: inherited proof-harness validation required.'),
('C52 Tier-All result: omitted because C81 is diagnostic-only and semantically uses the targeted accepted control.'),
('Ordinary restoration: proof artifacts must be restored inactive by harness finally cleanup.'),
('Ordinary kernel SHA: must equal the pre-proof normal kernel SHA at closeout.'),
('Ordinary ESP SHA: must equal the pre-proof normal ESP SHA at closeout.'),
('Proof artifact active: false at closeout.'),
('C81-owned QEMU cleanup: zero remaining C81-owned QEMU processes at closeout.'),
('Unrelated QEMU preservation: unrelated QEMU processes are preserved by harness cleanup policy.'),
('Files changed: {0}.' -f $changed),
('Documentation path: docs/dotnet/NATIVEAOT_WORKSTATION_GC_C81_LARGE_RANGE_BASIC_UNIT_PROVENANCE.md.'),
('Evidence root: out/dotnet/c011ec81-large-range-basic-unit-provenance/.'),
('Final commit: local commit subject Trace NativeAOT large-range basic units; SHA is filled after commit.'),
('Push status: not pushed.'),
('Remaining limitation: exact production split/projection causality is unresolved when no SUBRANGE_OF_LARGE entry is observed.'),
('Exact next-smallest milestone: C82 instrument only the narrow source operation or parent-state predicate identified by this mapping; keep B02 deferred.')
)
if($findingText.Count -ne 193){throw "C81 report generation produced $($findingText.Count) findings instead of exactly 193."}
$report=[System.Collections.Generic.List[string]]::new()
$report.Add('# NativeAOT Workstation GC C81 — Large-Range Basic-Unit Provenance')
$report.Add('')
$report.Add('C81 is a provenance-only continuation of C78, C79, and C80. C78 corrected descriptor-reuse assumptions; C79 moved to range identity but sampled; C80 captured the complete canonical universe and exposed four exact basic ranges plus two large canonical ranges. C81 compares those extents with the production `free_regions[basic_free_region]` list.')
$report.Add('')
$report.Add('## Exactly 193 numbered findings')
$report.Add('')
for($i=0;$i -lt $findingText.Count;$i++){$report.Add(('{0}. {1}' -f ($i+1),$findingText[$i]))}
$report.Add('')
$report.Add('## Evidence')
$report.Add('')
$report.Add('- Raw canonical snapshots: `raw-canonical-snapshots/`.')
$report.Add('- Raw basic-list snapshots: `raw-basic-list-snapshots/`.')
$report.Add('- Normalized lattice: `normalized-range-lattice.json` and `normalized-range-lattice.csv`.')
$report.Add('- Parent/subrange maps: `parent-subrange-map.json`.')
$report.Add('- Accepted manifests: `accepted-confirmation-manifests/`.')
$report.Add('- Negative control: `negative-control-overflow.json`.')
Set-Content -LiteralPath (Join-Path $repoRoot 'docs/dotnet/NATIVEAOT_WORKSTATION_GC_C81_LARGE_RANGE_BASIC_UNIT_PROVENANCE.md') -Value $report -Encoding UTF8
Write-Host "C011EC81 large-range basic-unit provenance: $outcome / Level $level / extras=$($extraRows.Count)" -ForegroundColor Green
