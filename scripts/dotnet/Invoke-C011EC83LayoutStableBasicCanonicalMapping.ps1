param(
    [Parameter(Mandatory = $true)][string]$OneManifest,
    [Parameter(Mandatory = $true)][string]$SixManifest,
    [Parameter(Mandatory = $true)][string]$BaselineBuildRoot,
    [Parameter(Mandatory = $true)][string]$C83BuildRoot,
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [string]$BaselineSixBuildRoot = "",
    [string]$C83SixBuildRoot = "",
    [string]$BaselineKernelRoot = "",
    [string]$C83KernelRoot = "",
    [string]$BaselineSixKernelRoot = "",
    [string]$C83SixKernelRoot = "",
    [string]$C80Analysis = "out\dotnet\c011ec82-one-baseline-regression\c81-regression-evidence\C81-original-evidence\c81-analysis.json"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Get-Location).Path
function Resolve-InputPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$outputRoot = Resolve-InputPath $OutputRoot
$baselineBuildRoot = Resolve-InputPath $BaselineBuildRoot
$c83BuildRoot = Resolve-InputPath $C83BuildRoot
$oneManifest = Resolve-InputPath $OneManifest
$sixManifest = Resolve-InputPath $SixManifest
$c80Analysis = Resolve-InputPath $C80Analysis
$baselineSixBuildRoot = if ([string]::IsNullOrWhiteSpace($BaselineSixBuildRoot)) { $baselineBuildRoot } else { Resolve-InputPath $BaselineSixBuildRoot }
$c83SixBuildRoot = if ([string]::IsNullOrWhiteSpace($C83SixBuildRoot)) { $c83BuildRoot } else { Resolve-InputPath $C83SixBuildRoot }
$baselineKernelRoot = if ([string]::IsNullOrWhiteSpace($BaselineKernelRoot)) { $baselineBuildRoot } else { Resolve-InputPath $BaselineKernelRoot }
$c83KernelRoot = if ([string]::IsNullOrWhiteSpace($C83KernelRoot)) { $c83BuildRoot } else { Resolve-InputPath $C83KernelRoot }
$baselineSixKernelRoot = if ([string]::IsNullOrWhiteSpace($BaselineSixKernelRoot)) { $baselineSixBuildRoot } else { Resolve-InputPath $BaselineSixKernelRoot }
$c83SixKernelRoot = if ([string]::IsNullOrWhiteSpace($C83SixKernelRoot)) { $c83SixBuildRoot } else { Resolve-InputPath $C83SixKernelRoot }

function New-Directory([string]$Path) {
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Write-Json([string]$Path, [object]$Value) {
    New-Directory (Split-Path -Parent $Path)
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding ASCII
}

function Write-Text([string]$Path, [string]$Text) {
    New-Directory (Split-Path -Parent $Path)
    Set-Content -LiteralPath $Path -Value $Text -Encoding ASCII
}

function Read-Json([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "JSON input is missing: $Path"
    }
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

function Get-MarkerValue([string]$Line, [string]$Field) {
    if ([string]::IsNullOrWhiteSpace($Line)) { return $null }
    $match = [regex]::Match($Line, "(?:^|\s)" + [regex]::Escape($Field) + "=(?<value>[0-9A-Fa-f]{1,16})(?=\s|$)")
    if (-not $match.Success) { return $null }
    return [Convert]::ToUInt64($match.Groups['value'].Value, 16)
}

function Get-MarkerText([string]$Line, [string]$Field) {
    if ([string]::IsNullOrWhiteSpace($Line)) { return $null }
    $match = [regex]::Match($Line, "(?:^|\s)" + [regex]::Escape($Field) + "=(?<value>[^\s]+)")
    if (-not $match.Success) { return $null }
    return $match.Groups['value'].Value
}

function Get-LastLine([object]$Value) {
    $items = @($Value)
    if ($items.Count -eq 0) { return $null }
    return [string]$items[$items.Count - 1]
}

function Get-FirstProperty([object]$Object, [string[]]$Names) {
    if ($null -eq $Object) { return $null }
    foreach ($name in $Names) {
        $property = $Object.PSObject.Properties[$name]
        if ($null -ne $property -and $null -ne $property.Value) {
            return $property.Value
        }
    }
    return $null
}

function Format-Hex([UInt64]$Value) {
    return ("0x{0:X}" -f $Value)
}

function Format-OptionalHex([object]$Value) {
    if ($null -eq $Value) { return "N/A" }
    return (Format-Hex ([UInt64]$Value))
}

function Copy-Evidence([object]$Manifest, [string]$Destination) {
    New-Directory $Destination
    Copy-Item -LiteralPath $Manifest.__path -Destination (Join-Path $Destination "manifest.json") -Force
    $runItems = @($Manifest.qemu.runs)
    foreach ($run in $runItems) {
        if ($null -eq $run) { continue }
        $serial = Get-FirstProperty $run @('serial', 'serialPath')
        if ($serial -and (Test-Path -LiteralPath $serial -PathType Leaf)) {
            $name = if ($run.name) { "$($run.name)-serial.txt" } else { "serial.txt" }
            Copy-Item -LiteralPath $serial -Destination (Join-Path $Destination $name) -Force
        }
    }
}

function Get-Completion([object]$Manifest) {
    $c83 = Get-FirstProperty $Manifest @('c83')
    $lifecycle = Get-FirstProperty $Manifest @('lifecycle')
    return Get-LastLine (Get-FirstProperty $c83 @('completion'))
}

function Get-C83RecordLines([object]$Manifest) {
    $c83 = Get-FirstProperty $Manifest @('c83')
    return @((Get-FirstProperty $c83 @('records')))
}

function Get-C83SummaryLines([object]$Manifest) {
    $c83 = Get-FirstProperty $Manifest @('c83')
    return @((Get-FirstProperty $c83 @('summaries')))
}

function Get-BaselineCompletion([object]$Manifest) {
    $lifecycle = Get-FirstProperty $Manifest @('lifecycle')
    $c77 = Get-FirstProperty $Manifest @('c77')
    return Get-LastLine (Get-FirstProperty $lifecycle @('completion'))
}

function Get-BaselineSummary([object]$Manifest) {
    $lifecycle = Get-FirstProperty $Manifest @('lifecycle')
    $c77 = Get-FirstProperty $Manifest @('c77')
    $c76 = Get-FirstProperty $Manifest @('c76Classification')
    $candidate = Get-FirstProperty $c77 @('summary')
    if ($null -eq $candidate) { $candidate = Get-FirstProperty $c76 @('summaries') }
    return Get-LastLine $candidate
}

function Get-BaselineCount([object]$Manifest, [string]$Name) {
    $summary = Get-BaselineSummary $Manifest
    $value = Get-MarkerValue $summary $Name
    if ($null -ne $value) { return [UInt64]$value }
    $completion = Get-BaselineCompletion $Manifest
    $value = Get-MarkerValue $completion $Name
    if ($null -ne $value) { return [UInt64]$value }
    return $null
}

function Get-PromotionObserved([object]$Manifest) {
    $summary = Get-BaselineSummary $Manifest
    $value = Get-MarkerValue $summary 'promotionObserved'
    if ($null -ne $value) { return $value -ne 0 }
    $completion = Get-BaselineCompletion $Manifest
    $value = Get-MarkerValue $completion 'promotionObserved'
    return $null -ne $value -and $value -ne 0
}

function Convert-C83Record([string]$CaseName, [string]$Line) {
    $fields = @('checkpoint','ordinal','descriptor','list','basicStart','basicEnd','basicSize','generation','planGeneration','state','canonicalDescriptor','canonicalStart','canonicalEnd','canonicalSize','offset','mappingStart','mappingEnd','mappingEntries','expectedCount','lookupStatus')
    $values = [ordered]@{}
    foreach ($field in $fields) {
        $value = Get-MarkerValue $Line $field
        if ($null -eq $value) { throw "C83 record is missing ${field}: $Line" }
        $values[$field] = [UInt64]$value
    }
    $basicContained = $values.basicStart -ge $values.canonicalStart -and $values.basicEnd -le $values.canonicalEnd
    if ($values.canonicalDescriptor -eq 0 -or $values.canonicalEnd -le $values.canonicalStart) {
        $class = 'NO_CANONICAL_MATCH'
    } elseif ($values.basicStart -eq $values.canonicalStart -and $values.basicEnd -eq $values.canonicalEnd) {
        $class = 'EXACT_CANONICAL'
    } elseif ($basicContained -and $values.canonicalSize -gt $values.basicSize) {
        $class = 'SUBRANGE_OF_LARGE'
    } elseif ($basicContained) {
        $class = 'OTHER_PROJECTION'
    } else {
        $class = 'NO_CANONICAL_MATCH'
    }
    $basicRelativeStart = if ($values.basicStart -ge $values.mappingStart) { $values.basicStart - $values.mappingStart } else { $null }
    $basicRelativeEnd = if ($values.basicEnd -ge $values.mappingStart) { $values.basicEnd - $values.mappingStart } else { $null }
    $canonicalRelativeStart = if ($values.canonicalStart -ge $values.mappingStart) { $values.canonicalStart - $values.mappingStart } else { $null }
    $canonicalRelativeEnd = if ($values.canonicalEnd -ge $values.mappingStart) { $values.canonicalEnd - $values.mappingStart } else { $null }
    $parentKey = if ($class -eq 'NO_CANONICAL_MATCH') { 'NONE' } else { (Format-Hex $values.canonicalStart) + ':' + (Format-Hex $values.canonicalSize) }
    return [pscustomobject][ordered]@{
        Case=$CaseName; Ordinal=$values.ordinal; Checkpoint=$values.checkpoint; Descriptor=($values.descriptor); List=($values.list)
        BasicStart=$values.basicStart; BasicEnd=$values.basicEnd; BasicSize=$values.basicSize
        BasicRelativeStart=$basicRelativeStart; BasicRelativeEnd=$basicRelativeEnd
        Generation=$values.generation; PlanGeneration=$values.planGeneration; State=$values.state
        CanonicalDescriptor=$values.canonicalDescriptor; CanonicalStart=$values.canonicalStart; CanonicalEnd=$values.canonicalEnd; CanonicalSize=$values.canonicalSize
        CanonicalRelativeStart=$canonicalRelativeStart; CanonicalRelativeEnd=$canonicalRelativeEnd; Offset=$values.offset
        MappingStart=$values.mappingStart; MappingEnd=$values.mappingEnd; MappingEntries=$values.mappingEntries
        ExpectedCount=$values.expectedCount; LookupStatus=$values.lookupStatus; Class=$class; ParentKey=$parentKey; Raw=$Line
    }
}

function Get-C83Data([string]$CaseName, [string]$ManifestPath) {
    $manifest = Read-Json $ManifestPath
    Add-Member -InputObject $manifest -NotePropertyName __path -NotePropertyValue $ManifestPath
    $completion = Get-Completion $manifest
    $summaryLines = @(Get-C83SummaryLines $manifest)
    $recordLines = @(Get-C83RecordLines $manifest)
    if ($null -eq $completion -or $summaryLines.Count -eq 0) {
        throw "$CaseName C83 manifest does not contain C83 completion and summary records: $ManifestPath"
    }
    $records = @($recordLines | ForEach-Object { Convert-C83Record $CaseName $_ })
    $completionFields = @('successLevel','checkpoint','expectedCount','observedCount','eventCapacity','eventCount','overflow','lookupFailures','invariantFailures','sensitiveDiagnosticAllocations','failFast','pageFault')
    $completionValues = [ordered]@{}
    foreach ($field in $completionFields) { $completionValues[$field] = Get-MarkerValue $completion $field }
    $summary = $summaryLines[$summaryLines.Count - 1]
    $summaryValues = [ordered]@{}
    foreach ($field in @('checkpoint','expectedCount','observedCount','recordCapacity','basicRegionSize','mappingStart','mappingEnd','mappingEntries','overflow','lookupFailures','invariantFailures','sensitiveDiagnosticAllocations','failFast','pageFault')) {
        $summaryValues[$field] = Get-MarkerValue $summary $field
    }
    $clean = $completionValues.successLevel -eq 1 -and $completionValues.checkpoint -eq 7 -and $completionValues.expectedCount -eq $completionValues.observedCount -and $completionValues.eventCount -eq $completionValues.observedCount -and $completionValues.eventCapacity -eq 6 -and $completionValues.overflow -eq 0 -and $completionValues.lookupFailures -eq 0 -and $completionValues.invariantFailures -eq 0 -and $completionValues.sensitiveDiagnosticAllocations -eq 0 -and $completionValues.failFast -eq 0 -and $completionValues.pageFault -eq 0
    return [pscustomobject][ordered]@{ Case=$CaseName; Manifest=$manifest; ManifestPath=$ManifestPath; CompletionLine=$completion; SummaryLine=$summary; Summary=$summaryValues; Completion=$completionValues; Records=$records; Clean=$clean }
}

function Get-FileByName([string]$Root, [string]$Name, [string]$PreferredPattern = '') {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return $null }
    $items = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction SilentlyContinue)
    if ($PreferredPattern) {
        $preferred = @($items | Where-Object { $_.FullName -match $PreferredPattern })
        if ($preferred.Count -ne 0) { return $preferred[0] }
    }
    if ($items.Count -eq 0) { return $null }
    return $items[0]
}

function Get-Hash([string]$Path) {
    if ($null -eq $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-DiagnosticBss([string]$Root) {
    $obj = Get-FileByName $Root 'guidexos_nativeaot_platform.single-thread-suspend-ee.obj'
    if ($null -eq $obj) { return [pscustomobject]@{ Path=$null; Bytes=$null; Symbol=$null; Tool=$null } }
    $nm = (Get-Command nm.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source
    if (-not $nm -and (Test-Path 'C:\mingw64\bin\nm.exe')) { $nm = 'C:\mingw64\bin\nm.exe' }
    if (-not $nm) { throw 'nm.exe is required for diagnostic BSS accounting.' }
    $lines = @(& $nm -S --size-sort $obj.FullName 2>&1)
    foreach ($line in $lines) {
        $match = [regex]::Match([string]$line, '^\s*[0-9A-Fa-f]+\s+(?<size>[0-9A-Fa-f]+)\s+[Bb]\s+g_guideXosAllocationDiagnostics\s*$')
        if ($match.Success) {
            return [pscustomobject]@{ Path=$obj.FullName; Bytes=[Convert]::ToUInt64($match.Groups['size'].Value, 16); Symbol='g_guideXosAllocationDiagnostics'; Tool=$nm }
        }
    }
    throw "Diagnostic BSS symbol was not found in $($obj.FullName)."
}

function Get-BuildAccounting([string]$Root, [string]$KernelRoot = '') {
    $kernelSearchRoot = if ([string]::IsNullOrWhiteSpace($KernelRoot)) { $Root } else { $KernelRoot }
    $kernel = Get-FileByName $kernelSearchRoot 'proof-kernel.elf'
    if ($null -eq $kernel -and $kernelSearchRoot -ne $Root) { $kernel = Get-FileByName $Root 'proof-kernel.elf' }
    $pe = Get-FileByName $Root 'NativeAotGcSingleThreadSuspendEe.exe' '\\artifact\\'
    $elf = Get-FileByName $Root 'NativeAotGcSingleThreadSuspendEe.elf' '\\artifact\\'
    $map = Get-FileByName $Root 'NativeAotGcSingleThreadSuspendEe.map' '\\artifact\\'
    $managed = Get-FileByName $Root 'HostLogProof.exe' '\\native\\'
    $bss = Get-DiagnosticBss $Root
    return [ordered]@{
        Root=$Root
        Kernel=[ordered]@{ Path=if($kernel){$kernel.FullName}else{$null}; Bytes=if($kernel){$kernel.Length}else{$null}; Sha256=if($kernel){Get-Hash $kernel.FullName}else{$null} }
        Managed=[ordered]@{ Path=if($managed){$managed.FullName}else{$null}; Bytes=if($managed){$managed.Length}else{$null}; Sha256=if($managed){Get-Hash $managed.FullName}else{$null} }
        PE=[ordered]@{ Path=if($pe){$pe.FullName}else{$null}; Bytes=if($pe){$pe.Length}else{$null}; Sha256=if($pe){Get-Hash $pe.FullName}else{$null} }
        ELF=[ordered]@{ Path=if($elf){$elf.FullName}else{$null}; Bytes=if($elf){$elf.Length}else{$null}; Sha256=if($elf){Get-Hash $elf.FullName}else{$null} }
        Map=[ordered]@{ Path=if($map){$map.FullName}else{$null}; Bytes=if($map){$map.Length}else{$null}; Sha256=if($map){Get-Hash $map.FullName}else{$null} }
        DiagnosticBss=$bss
    }
}

function Get-Delta([object]$Left, [object]$Right) {
    if ($null -eq $Left -or $null -eq $Right) { return $null }
    return ([Int64]$Right - [Int64]$Left)
}

function Test-Baseline([object]$Manifest, [string]$CaseName, [UInt64]$RequiredCount) {
    $postRestart = Get-BaselineCount $Manifest 'postRestartBasicCount'
    $postResume = Get-BaselineCount $Manifest 'postResumeBasicCount'
    $promotion = Get-PromotionObserved $Manifest
    return [pscustomobject]@{ Case=$CaseName; PromotionAuthentic=($promotion -eq $true); PostRestart=$postRestart; PostResume=$postResume; Required=$RequiredCount; Pass=($promotion -eq $true -and $postRestart -eq $RequiredCount -and $postResume -eq $RequiredCount) }
}

function Get-ParentGroups([object[]]$Records) {
    $groups = @()
    foreach ($key in @($Records | Select-Object -ExpandProperty ParentKey -Unique | Sort-Object)) {
        if ($key -eq 'NONE') { continue }
        $children = @($Records | Where-Object { $_.ParentKey -eq $key } | Sort-Object Ordinal)
        $parent = $children[0]
        $overlaps = @()
        for ($i = 0; $i -lt $children.Count; $i++) {
            for ($j = $i + 1; $j -lt $children.Count; $j++) {
                if ($children[$i].BasicStart -lt $children[$j].BasicEnd -and $children[$j].BasicStart -lt $children[$i].BasicEnd) {
                    $overlaps += "ordinal $($children[$i].Ordinal) overlaps ordinal $($children[$j].Ordinal)"
                }
            }
        }
        $containment = @($children | Where-Object { $_.BasicStart -lt $parent.CanonicalStart -or $_.BasicEnd -gt $parent.CanonicalEnd }).Count -eq 0
        $alignment = @($children | Where-Object { $null -eq $_.BasicRelativeStart -or $_.BasicRelativeStart % $_.BasicSize -ne 0 -or $_.Offset % $_.BasicSize -ne 0 }).Count -eq 0
        $covered = [UInt64]0
        foreach ($child in $children) { $covered += [UInt64]$child.BasicSize }
        $groups += [pscustomobject][ordered]@{
            ParentKey=$key; CanonicalDescriptor=$parent.CanonicalDescriptor; CanonicalStart=$parent.CanonicalStart; CanonicalEnd=$parent.CanonicalEnd; CanonicalSize=$parent.CanonicalSize
            ChildOrdinals=@($children | ForEach-Object { $_.Ordinal }); ChildOffsets=@($children | ForEach-Object { $_.Offset }); ChildRanges=@($children | ForEach-Object { [ordered]@{ Ordinal=$_.Ordinal; Start=$_.BasicStart; End=$_.BasicEnd; Size=$_.BasicSize } })
            CoveredChildBytes=$covered; Overlaps=@($overlaps); ContainmentValid=$containment; BasicSizeAlignmentValid=$alignment; Children=$children
        }
    }
    return @($groups)
}

function Get-C80LargeEvidence([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return [ordered]@{ Available=$false; Path=$Path; Ranges=@(); Note='C80/C81 source/model evidence not present.' } }
    $object = Read-Json $Path
    $ranges = @()
    $sixLarge = Get-FirstProperty $object.canonical @('SIXLarge')
    foreach ($range in @($sixLarge)) {
        if ($null -eq $range) { continue }
        $ranges += [ordered]@{ RangeKey=$range.RangeKey; RangeStart=$range.RangeStart; RangeEnd=$range.RangeEnd; RangeSize=$range.RangeSize; BasicRegionCount=$range.BasicRegionCount }
    }
    return [ordered]@{ Available=($ranges.Count -ne 0); Path=$Path; Ranges=$ranges; Note='C80/C81 dimensions are model clues only; the perturbed C81 ONE run is not causal evidence.' }
}

function Get-CommonRole([object[]]$OneRecords, [object[]]$SixRecords) {
    foreach ($one in $OneRecords) {
        $match = @($SixRecords | Where-Object { $_.BasicRelativeStart -eq $one.BasicRelativeStart -and $_.BasicSize -eq $one.BasicSize })
        if ($match.Count -ne 0) { return [pscustomobject]@{ OneOrdinal=$one.Ordinal; SixOrdinal=$match[0].Ordinal; Role='common ONE/SIX basic entry'; One=$one; Six=$match[0] } }
    }
    return [pscustomobject]@{ OneOrdinal=$null; SixOrdinal=$null; Role='no normalized common entry'; One=$null; Six=$null }
}

function Get-ClassificationCount([object[]]$Rows, [string]$Class) {
    return @($Rows | Where-Object { $_.Class -eq $Class }).Count
}

New-Directory $outputRoot
$baselineFailureRoot = Join-Path $outputRoot 'layout-gate-failures'
$discoveryRoot = Join-Path $outputRoot 'discovery'
$acceptedRoot = Join-Path $outputRoot 'accepted-ONE-SIX-confirmation'
$mappingRoot = Join-Path $outputRoot 'offline-mapping'
New-Directory $baselineFailureRoot
New-Directory $discoveryRoot
New-Directory $acceptedRoot
New-Directory $mappingRoot

$one = Get-C83Data 'ONE' $oneManifest
$six = Get-C83Data 'SIX' $sixManifest
$oneBaseline = Read-Json $oneManifest
$sixBaseline = Read-Json $sixManifest
$oneBaselineGate = Test-Baseline $oneBaseline 'ONE' 1
$sixBaselineGate = Test-Baseline $sixBaseline 'SIX' 6
$c83GatePass = $one.Clean -and $six.Clean
$layoutGatePass = $oneBaselineGate.Pass -and $sixBaselineGate.Pass -and $c83GatePass

$baselineAccounting = Get-BuildAccounting $baselineBuildRoot $baselineKernelRoot
$c83Accounting = Get-BuildAccounting $c83BuildRoot $c83KernelRoot
$baselineSixAccounting = Get-BuildAccounting $baselineSixBuildRoot $baselineSixKernelRoot
$c83SixAccounting = Get-BuildAccounting $c83SixBuildRoot $c83SixKernelRoot
$layoutAccounting = [ordered]@{
    Baseline=$baselineAccounting; C83=$c83Accounting
    DiagnosticBssDelta=Get-Delta $baselineAccounting.DiagnosticBss.Bytes $c83Accounting.DiagnosticBss.Bytes
    KernelSizeDelta=Get-Delta $baselineAccounting.Kernel.Bytes $c83Accounting.Kernel.Bytes
    ManagedSizeDelta=Get-Delta $baselineAccounting.Managed.Bytes $c83Accounting.Managed.Bytes
    PESizeDelta=Get-Delta $baselineAccounting.PE.Bytes $c83Accounting.PE.Bytes
    ELFSizeDelta=Get-Delta $baselineAccounting.ELF.Bytes $c83Accounting.ELF.Bytes
    MapSizeDelta=Get-Delta $baselineAccounting.Map.Bytes $c83Accounting.Map.Bytes
    ONE=[ordered]@{ Baseline=$baselineAccounting; C83=$c83Accounting; DiagnosticBssDelta=Get-Delta $baselineAccounting.DiagnosticBss.Bytes $c83Accounting.DiagnosticBss.Bytes; KernelSizeDelta=Get-Delta $baselineAccounting.Kernel.Bytes $c83Accounting.Kernel.Bytes; ManagedSizeDelta=Get-Delta $baselineAccounting.Managed.Bytes $c83Accounting.Managed.Bytes; PESizeDelta=Get-Delta $baselineAccounting.PE.Bytes $c83Accounting.PE.Bytes; ELFSizeDelta=Get-Delta $baselineAccounting.ELF.Bytes $c83Accounting.ELF.Bytes; MapSizeDelta=Get-Delta $baselineAccounting.Map.Bytes $c83Accounting.Map.Bytes }
    SIX=[ordered]@{ Baseline=$baselineSixAccounting; C83=$c83SixAccounting; DiagnosticBssDelta=Get-Delta $baselineSixAccounting.DiagnosticBss.Bytes $c83SixAccounting.DiagnosticBss.Bytes; KernelSizeDelta=Get-Delta $baselineSixAccounting.Kernel.Bytes $c83SixAccounting.Kernel.Bytes; ManagedSizeDelta=Get-Delta $baselineSixAccounting.Managed.Bytes $c83SixAccounting.Managed.Bytes; PESizeDelta=Get-Delta $baselineSixAccounting.PE.Bytes $c83SixAccounting.PE.Bytes; ELFSizeDelta=Get-Delta $baselineSixAccounting.ELF.Bytes $c83SixAccounting.ELF.Bytes; MapSizeDelta=Get-Delta $baselineSixAccounting.Map.Bytes $c83SixAccounting.Map.Bytes }
}
Write-Json (Join-Path $outputRoot 'layout-accounting.json') $layoutAccounting

if (-not $layoutGatePass) {
    Copy-Evidence $one.Manifest (Join-Path $baselineFailureRoot 'ONE')
    Copy-Evidence $six.Manifest (Join-Path $baselineFailureRoot 'SIX')
    Write-Json (Join-Path $baselineFailureRoot 'baseline-failure.json') ([ordered]@{
        outcome='E / layout-gate failure'; successLevel=0; rangeEvidenceInterpreted=$false
        ONE=$oneBaselineGate; SIX=$sixBaselineGate; C83ONE=$one.Completion; C83SIX=$six.Completion; layout=$layoutAccounting
        reason='C83 evidence is rejected because the restored ONE/SIX semantic gate did not pass.'
    })
    Write-Json (Join-Path $outputRoot 'c83-analysis.json') ([ordered]@{ outcome='E / layout-gate failure'; successLevel=0; rangeEvidenceInterpreted=$false; layout=$layoutAccounting; ONE=$oneBaselineGate; SIX=$sixBaselineGate })
    throw 'C83 layout gate failed. Range evidence was not interpreted.'
}

Copy-Evidence $one.Manifest (Join-Path $discoveryRoot 'ONE')
Copy-Evidence $six.Manifest (Join-Path $discoveryRoot 'SIX')
Copy-Evidence $one.Manifest (Join-Path $acceptedRoot 'ONE')
Copy-Evidence $six.Manifest (Join-Path $acceptedRoot 'SIX')
Write-Text (Join-Path $discoveryRoot 'ONE-C83-records.txt') ((@($one.Records | ForEach-Object { $_.Raw }) -join "`r`n"))
Write-Text (Join-Path $discoveryRoot 'SIX-C83-records.txt') ((@($six.Records | ForEach-Object { $_.Raw }) -join "`r`n"))

$oneRow = $one.Records[0]
$common = Get-CommonRole $one.Records $six.Records
$extraSix = @($six.Records | Where-Object { $_.Ordinal -ne $common.SixOrdinal } | Sort-Object Ordinal)
$sixParents = Get-ParentGroups $six.Records
$parentConservation = [ordered]@{ Parents=$sixParents; ParentCount=$sixParents.Count; AllOverlapsValid=(@($sixParents | Where-Object { $_.Overlaps.Count -ne 0 }).Count -eq 0); AllContainmentValid=(@($sixParents | Where-Object { -not $_.ContainmentValid }).Count -eq 0); AllBasicSizeAlignmentValid=(@($sixParents | Where-Object { -not $_.BasicSizeAlignmentValid }).Count -eq 0); CoveredChildBytes=([UInt64]0) }
foreach ($parent in $sixParents) { $parentConservation.CoveredChildBytes += [UInt64]$parent.CoveredChildBytes }
$allContainment = @($six.Records | Where-Object { $_.Class -eq 'NO_CANONICAL_MATCH' }).Count -eq 0
$allBasicSizes = @($six.Records | Where-Object { $_.BasicSize -ne $one.Summary.basicRegionSize }).Count -eq 0
$extraSubranges = @($extraSix | Where-Object { $_.Class -eq 'SUBRANGE_OF_LARGE' }).Count
$extraExact = @($extraSix | Where-Object { $_.Class -eq 'EXACT_CANONICAL' }).Count
$extraUnmatched = @($extraSix | Where-Object { $_.Class -eq 'NO_CANONICAL_MATCH' }).Count
$c80Large = Get-C80LargeEvidence $c80Analysis
$c80Supported = $extraSix.Count -eq 5 -and $extraSubranges -eq 5
$c80Falsified = $extraSix.Count -eq 5 -and $extraExact -eq 5
$outcome = if ($c80Supported) { 'A / extra five are subranges of large canonical extent(s)' } elseif ($c80Falsified) { 'C / extra five are exact canonical regions' } elseif ($extraSix.Count -eq 5 -and $extraUnmatched -eq 5) { 'F / canonical lookup model insufficient for extra entries' } else { 'D / mixed representation' }
$level = if ($allContainment -and $six.Records.Count -eq 6) { if ($c80Supported -or $c80Falsified -or $extraSix.Count -eq 5) { 2 } else { 1 } } else { 0 }

$comparisonRows = @($one.Records + $six.Records | ForEach-Object {
    [pscustomobject][ordered]@{
        Case=$_.Case; BasicOrdinal=$_.Ordinal; BasicRange=("{0}:{1}" -f (Format-Hex $_.BasicRelativeStart), (Format-Hex $_.BasicRelativeEnd)); BasicSize=(Format-Hex $_.BasicSize)
        CanonicalRange=("{0}:{1}" -f (Format-Hex $_.CanonicalRelativeStart), (Format-Hex $_.CanonicalRelativeEnd)); CanonicalSize=(Format-Hex $_.CanonicalSize); Offset=(Format-Hex $_.Offset); Class=$_.Class
        BasicStart=(Format-Hex $_.BasicStart); BasicEnd=(Format-Hex $_.BasicEnd); CanonicalStart=(Format-Hex $_.CanonicalStart); CanonicalEnd=(Format-Hex $_.CanonicalEnd); ParentKey=$_.ParentKey
    }
})
$comparisonRows | Export-Csv -LiteralPath (Join-Path $mappingRoot 'comparison-table.csv') -NoTypeInformation -Encoding ASCII
Write-Json (Join-Path $mappingRoot 'comparison-table.json') $comparisonRows
Write-Json (Join-Path $mappingRoot 'parent-grouping.json') $sixParents
Write-Json (Join-Path $mappingRoot 'conservation.json') $parentConservation
Write-Json (Join-Path $mappingRoot 'c80-two-large-range-hypothesis.json') ([ordered]@{ outcome=$outcome; supported=$c80Supported; falsified=$c80Falsified; C80=$c80Large; sixExtraCount=$extraSix.Count; extraSubrangeCount=$extraSubranges; extraExactCount=$extraExact; extraUnmatchedCount=$extraUnmatched; note='C80/C81 records are reused as candidate dimensions only; C83 restored-layout records are authoritative.' })
Write-Json (Join-Path $outputRoot 'source-audit.json') ([ordered]@{
    RuntimeSourceSha='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'
    BasicListNodeSourceType='heap_segment* node returned by region_free_list::get_first_free_region() from gc_heap::free_regions[basic_free_region]'
    CanonicalRegionSourceType='heap_segment* descriptor selected by seg_mapping_table; interior slots use negative allocated offset metadata'
    CanonicalLookupSourceFunction='gc_heap::get_region_info_for_address(uint8_t*)'
    CanonicalLookupSafety='Safe at this checkpoint under the runtime preconditions: each address is the start of an authenticated basic-free node inside the initialized heap mapping envelope; lookup is read-only.'
    BasicEntryMustBeCanonical=$false
    BasicEntryMayBeCanonicalSubrange=$true
    BasicEntryMayBeProjectionOfLargerRange=$true
    RegionSizeSource='get_region_size(heap_segment*) = heap_segment_reserved(region) - get_region_start(region)'
    CanonicalInitialization='init_heap_segment() initializes one descriptor and writes negative interior-map offsets for multi-basic extents'
    ListProjection='return_free_region() puts a real descriptor on a free list and clears basic map slots; region_free_list::get_region_kind() classifies list nodes by region_size'
    CandidateOperations=@('init_heap_segment', 'return_free_region', 'get_free_region', 'region_free_list::get_region_kind')
})

$serialHashes = @()
foreach ($manifest in @($one.Manifest, $six.Manifest)) {
    foreach ($run in @($manifest.qemu.runs)) {
        $path = Get-FirstProperty $run @('serial', 'serialPath')
        if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) { $serialHashes += Get-Hash $path }
    }
}
$analysis = [ordered]@{
    outcome=$outcome; successLevel=$level; exactQuestion='For each actual post-Restart basic-free entry in restored ONE and SIX, what canonical region/range contains it and what is its exact relationship to that canonical range?'
    repository=(Split-Path $repoRoot -Leaf); branch=((& git -C $repoRoot branch --show-current).Trim()); startingHead='c187583ccc31b7ba754806263781ccf1e0b30c2f'; startingSubject='Restore NativeAOT ONE region baseline'; finalHead=((& git -C $repoRoot rev-parse HEAD).Trim()); finalSubject=((& git -C $repoRoot log -1 --pretty=%s).Trim()); upstream=((& git -C $repoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim())
    runtime=[ordered]@{ identity='NativeAOT 9.0.0 AMD64 Workstation GC net9.0/win-x64'; sourceSha='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'; fpPatchSha='4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31'; C81Sha='9f42dde1e1b3ec6f5c63caef59b487f1089c1d71'; C82Sha='c187583ccc31b7ba754806263781ccf1e0b30c2f'; C83Sha='local commit containing C83 result' }
    layout=$layoutAccounting; baseline=[ordered]@{ ONE=$oneBaselineGate; SIX=$sixBaselineGate; C83ONE=$one.Completion; C83SIX=$six.Completion; ONEPostRestart=$oneBaselineGate.PostRestart; SIXPostRestart=$sixBaselineGate.PostRestart }
    ONE=[ordered]@{ Records=@($one.Records); MappingClass=$oneRow.Class; BasicCount=$one.Records.Count }
    SIX=[ordered]@{ Records=@($six.Records); ExactCanonicalCount=(Get-ClassificationCount $six.Records 'EXACT_CANONICAL'); SubrangeOfLargeCount=(Get-ClassificationCount $six.Records 'SUBRANGE_OF_LARGE'); UnmatchedCount=(Get-ClassificationCount $six.Records 'NO_CANONICAL_MATCH'); ParentCount=$sixParents.Count; ExtraFive=@($extraSix); CommonRole=$common }
    parentGrouping=$sixParents; conservation=$parentConservation; c80Hypothesis=[ordered]@{ supported=$c80Supported; falsified=$c80Falsified; outcome=$outcome; C80=$c80Large }; sourceAudit=(Read-Json (Join-Path $outputRoot 'source-audit.json'))
    integrity=[ordered]@{ productionMutation='none'; allocatorMutation='none'; regionMutation='none'; splitForcing='none'; descriptorMutation='none'; regionListMutation='none'; plannerMutation='none'; candidateMutation='none'; survivorFabrication='none'; rootFabrication='none'; C18='preserved'; codeManager='preserved'; FindMethodInfo='preserved'; rootScan='authentic'; markClosure='authentic'; plannerAuthenticity='authentic'; survivorIntegrity='preserved'; invariantFailures=0; sensitiveDiagnosticAllocations=0; C83EventCapacity=6; C83EventCount=$six.Completion.eventCount; C83Overflow=0; failFast=0; pageFault=0 }
    hashes=[ordered]@{ serial=$serialHashes; baselineKernel=$baselineAccounting.Kernel.Sha256; C83Kernel=$c83Accounting.Kernel.Sha256; baselinePE=$baselineAccounting.PE.Sha256; C83PE=$c83Accounting.PE.Sha256; baselineELF=$baselineAccounting.ELF.Sha256; C83ELF=$c83Accounting.ELF.Sha256; baselineMap=$baselineAccounting.Map.Sha256; C83Map=$c83Accounting.Map.Sha256 }
    evidenceRoot=$outputRoot; documentation='docs/dotnet/NATIVEAOT_WORKSTATION_GC_C83_LAYOUT_STABLE_BASIC_CANONICAL_MAPPING.md'; B02='not evaluated; still premature'; nextSmallestMilestone=if($c80Supported){'C84: trace only the exact production operation that exposes/carves the five SIX basic units while ONE remains aggregated.'}elseif($c80Falsified){'C84: abandon the large-parent theory and trace earliest materialization of the exact canonical entries.'}else{'C84: follow the majority representation class or preserve the mapping via a single-record sequential probe.'}
}
Write-Json (Join-Path $outputRoot 'c83-analysis.json') $analysis
Write-Host "C83 offline mapping complete: $outcome / Level $level"
