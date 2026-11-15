[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceImage,
    [Parameter(Mandatory)][ValidateSet('credential-corrupt', 'credential-unsupported',
        'credential-newer-corrupt', 'credential-truncated',
        'identity-corrupt', 'identity-unsupported')][string]$Case
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$scratchRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out\security3a'))
$markerPath = Join-Path $scratchRoot '.security3a-owned'
if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
    throw "Refusing SECURITY3A mutation without its owner marker: $scratchRoot"
}
$source = (Resolve-Path -LiteralPath $SourceImage -ErrorAction Stop).Path
$scratchPrefix = $scratchRoot.TrimEnd('\') + '\'
if (-not $source.StartsWith($scratchPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'SourceImage must be inside the owned SECURITY3A scratch directory.'
}
$target = Join-Path $scratchRoot ("security3a-{0}-retry.raw" -f $Case)
$target = [IO.Path]::GetFullPath($target)
if (-not $target.StartsWith($scratchPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $target)) {
    throw "Refusing to overwrite or escape the owned corruption image path: $target"
}

Copy-Item -LiteralPath $source -Destination $target
$stream = [IO.File]::Open($target, [IO.FileMode]::Open,
    [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)

function Read-At([IO.FileStream]$Stream, [long]$Offset, [int]$Count) {
    $bytes = New-Object byte[] $Count
    $Stream.Position = $Offset
    $read = $Stream.Read($bytes, 0, $Count)
    if ($read -ne $Count) { throw "Short raw-image read at offset $Offset." }
    return ,$bytes
}
function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [uint16]($Bytes[$Offset] -bor ([uint16]$Bytes[$Offset + 1] -shl 8))
}
function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32]($Bytes[$Offset] -bor ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor ([uint32]$Bytes[$Offset + 3] -shl 24))
}
function Read-U64([byte[]]$Bytes, [int]$Offset) {
    return [uint64](Read-U32 $Bytes $Offset) -bor
        ([uint64](Read-U32 $Bytes ($Offset + 4)) -shl 32)
}
function Write-At([IO.FileStream]$Stream, [long]$Offset, [byte[]]$Bytes) {
    $Stream.Position = $Offset
    $Stream.Write($Bytes, 0, $Bytes.Length)
}
function Write-DirectoryFileSize([IO.FileStream]$Stream, $File,
                                 [uint32]$BytesPerSector, [uint32]$Size) {
    if ($File.Offset + 32 -gt $BytesPerSector) {
        throw "Directory entry for $($File.Name) crosses a FAT sector boundary."
    }
    $sectorOffset = [long]$File.Sector * $BytesPerSector
    $sector = Read-At $Stream $sectorOffset ([int]$BytesPerSector)
    $sizeBytes = [BitConverter]::GetBytes([uint32]$Size)
    [Array]::Copy($sizeBytes, 0, $sector, [int]($File.Offset + 28), 4)
    Write-At $Stream $sectorOffset $sector
}
function Read-Cluster([IO.FileStream]$Stream, [uint32]$Cluster,
                      [uint32]$BytesPerSector, [uint32]$SectorsPerCluster,
                      [uint32]$FirstDataSector) {
    $clusterBytes = [int]($BytesPerSector * $SectorsPerCluster)
    $lba = [uint64]$FirstDataSector +
        ([uint64]($Cluster - 2) * [uint64]$SectorsPerCluster)
    return Read-At $Stream ([long]($lba * $BytesPerSector)) $clusterBytes
}
function Get-ClusterChain([IO.FileStream]$Stream, [uint32]$FirstCluster,
                          [uint32]$BytesPerSector, [uint32]$ReservedSectors) {
    $clusters = [Collections.Generic.List[uint32]]::new()
    $cluster = $FirstCluster
    for ($step = 0; $step -lt 4096; ++$step) {
        if ($cluster -lt 2 -or $cluster -ge 0x0FFFFFF8) { break }
        $clusters.Add($cluster)
        $fatOffset = [long]$ReservedSectors * $BytesPerSector + [long]$cluster * 4
        $word = Read-At $Stream $fatOffset 4
        $cluster = (Read-U32 $word 0) -band 0x0FFFFFFF
    }
    if ($clusters.Count -eq 0 -or $clusters.Count -ge 4096) {
        throw "Invalid or unbounded FAT cluster chain beginning at $FirstCluster."
    }
    return ,$clusters.ToArray()
}
function Get-DirectoryFile([IO.FileStream]$Stream, [string]$WantedName,
                           [uint32]$RootCluster, [uint32]$BytesPerSector,
                           [uint32]$SectorsPerCluster, [uint32]$FirstDataSector,
                           [uint32]$ReservedSectors) {
    $clusters = Get-ClusterChain $Stream $RootCluster $BytesPerSector $ReservedSectors
    $lfnChars = New-Object char[] 260
    foreach ($cluster in $clusters) {
        $clusterBytes = Read-Cluster $Stream $cluster $BytesPerSector `
            $SectorsPerCluster $FirstDataSector
        $entries = [int]($clusterBytes.Length / 32)
        for ($entryIndex = 0; $entryIndex -lt $entries; ++$entryIndex) {
            $offset = $entryIndex * 32
            $first = $clusterBytes[$offset]
            if ($first -eq 0) { return $null }
            if ($first -eq 0xE5) { $lfnChars = New-Object char[] 260; continue }
            $attr = $clusterBytes[$offset + 11]
            if ($attr -eq 0x0F) {
                $order = $clusterBytes[$offset]
                if (($order -band 0x40) -ne 0) { $lfnChars = New-Object char[] 260 }
                $sequence = $order -band 0x1F
                if ($sequence -eq 0) { continue }
                $base = ($sequence - 1) * 13
                for ($i = 0; $i -lt 13; ++$i) {
                    if ($i -lt 5) { $charOffset = $offset + 1 + 2 * $i }
                    elseif ($i -lt 11) { $charOffset = $offset + 14 + 2 * ($i - 5) }
                    else { $charOffset = $offset + 28 + 2 * ($i - 11) }
                    $value = Read-U16 $clusterBytes $charOffset
                    if ($value -eq 0 -or $value -eq 0xFFFF) { continue }
                    $lfnChars[$base + $i] = [char]$value
                }
                continue
            }
            if (($attr -band 0x08) -ne 0) { $lfnChars = New-Object char[] 260; continue }
            $length = 0
            while ($length -lt $lfnChars.Length -and $lfnChars[$length] -ne [char]0) { ++$length }
            $displayName = if ($length -gt 0) { [string]::new($lfnChars, 0, $length) } else { '' }
            if ([string]::Equals($displayName, $WantedName,
                    [StringComparison]::OrdinalIgnoreCase)) {
                $firstCluster = ([uint32](Read-U16 $clusterBytes ($offset + 20)) -shl 16) -bor
                    [uint32](Read-U16 $clusterBytes ($offset + 26))
                $size = Read-U32 $clusterBytes ($offset + 28)
                return [pscustomobject]@{
                    Name = $displayName
                    FirstCluster = $firstCluster
                    Size = $size
                Sector = [uint32]($FirstDataSector +
                    ([uint32]($cluster - 2) * $SectorsPerCluster) +
                    [uint32][Math]::Floor($offset / $BytesPerSector))
                Offset = [uint32]($offset % $BytesPerSector)
                }
            }
            $lfnChars = New-Object char[] 260
        }
    }
    return $null
}
function Read-FileBytes([IO.FileStream]$Stream, $File,
                        [uint32]$BytesPerSector, [uint32]$SectorsPerCluster,
                        [uint32]$FirstDataSector, [uint32]$ReservedSectors) {
    if ($File.Size -lt 40 -or $File.Size -gt 4096) {
        throw "Unexpected store file length for $($File.Name): $($File.Size)."
    }
    $clusters = Get-ClusterChain $Stream $File.FirstCluster $BytesPerSector $ReservedSectors
    $output = New-Object byte[] ([int]$File.Size)
    $copied = 0
    foreach ($cluster in $clusters) {
        if ($copied -ge $output.Length) { break }
        $chunk = Read-Cluster $Stream $cluster $BytesPerSector `
            $SectorsPerCluster $FirstDataSector
        $count = [Math]::Min($chunk.Length, $output.Length - $copied)
        [Array]::Copy($chunk, 0, $output, $copied, $count)
        $copied += $count
    }
    if ($copied -ne $output.Length) { throw "Short file cluster chain for $($File.Name)." }
    return ,$output
}
function Write-FileBytes([IO.FileStream]$Stream, $File, [byte[]]$Bytes,
                         [uint32]$BytesPerSector, [uint32]$SectorsPerCluster,
                         [uint32]$FirstDataSector, [uint32]$ReservedSectors) {
    if ($Bytes.Length -ne $File.Size) { throw "Unexpected store mutation length for $($File.Name)." }
    $clusters = Get-ClusterChain $Stream $File.FirstCluster $BytesPerSector $ReservedSectors
    $copied = 0
    foreach ($cluster in $clusters) {
        if ($copied -ge $Bytes.Length) { break }
        $chunkLength = [int]($BytesPerSector * $SectorsPerCluster)
        $lba = [uint64]$FirstDataSector +
            ([uint64]($cluster - 2) * [uint64]$SectorsPerCluster)
        $offset = [long]($lba * $BytesPerSector)
        $chunk = Read-At $Stream $offset $chunkLength
        $count = [Math]::Min($chunk.Length, $Bytes.Length - $copied)
        [Array]::Copy($Bytes, $copied, $chunk, 0, $count)
        Write-At $Stream $offset $chunk
        $copied += $count
    }
    if ($copied -ne $Bytes.Length) { throw "Short store mutation write for $($File.Name)." }
}
function Get-Crc32([byte[]]$Bytes, [int]$Count) {
    [long]$crc = 0xFFFFFFFFL
    for ($i = 0; $i -lt $Count; ++$i) {
        $crc = $crc -bxor [long]$Bytes[$i]
        for ($bit = 0; $bit -lt 8; ++$bit) {
            if (($crc -band 1) -ne 0) { $crc = ($crc -shr 1) -bxor 0xEDB88320L }
            else { $crc = $crc -shr 1 }
        }
    }
    return [uint32]($crc -bxor 0xFFFFFFFFL)
}

try {
    $boot = Read-At $stream 0 512
    $bps = Read-U16 $boot 11
    $spc = [uint32]$boot[13]
    $reserved = Read-U16 $boot 14
    $fatCount = [uint32]$boot[16]
    $fatSectors = Read-U32 $boot 36
    $rootCluster = Read-U32 $boot 44
    if ($bps -ne 512 -or $spc -eq 0 -or $reserved -eq 0 -or
        $fatCount -lt 1 -or $fatSectors -eq 0 -or $rootCluster -lt 2 -or
        [Text.Encoding]::ASCII.GetString($boot, 82, 8) -ne 'FAT32   ') {
        throw 'Source is not the expected direct FAT32 qualification image.'
    }
    $firstData = [uint32]$reserved + ($fatCount * $fatSectors)
    $storeBase = if ($Case.StartsWith('credential')) { '.gxos-credentials-' } else { '.gxos-identity-' }
    $version = if ($Case.EndsWith('unsupported')) { 99u } else { 0u }
    $selectedSlots = @(0, 1)
    if ($Case -in @('credential-newer-corrupt', 'credential-truncated')) {
        $generations = @()
        foreach ($slot in @(0, 1)) {
            $name = "$storeBase$slot.dat"
            $file = Get-DirectoryFile $stream $name $rootCluster $bps $spc $firstData $reserved
            if ($null -eq $file) { throw "Could not locate store slot $name in the FAT32 root." }
            $data = Read-FileBytes $stream $file $bps $spc $firstData $reserved
            if ([Text.Encoding]::ASCII.GetString($data, 0, 8) -ne 'GXOSCRD1') {
                throw "Credential slot $name does not have the expected magic."
            }
            $generations += [pscustomobject]@{ Slot = $slot; Generation = (Read-U64 $data 16) }
        }
        if ($generations[0].Generation -eq 0 -or $generations[1].Generation -eq 0 -or
            $generations[0].Generation -eq $generations[1].Generation) {
            throw 'Expected two distinct, nonzero credential generations for newer-slot recovery.'
        }
        $newest = if ($generations[0].Generation -gt $generations[1].Generation) {
            $generations[0]
        } else {
            $generations[1]
        }
        $selectedSlots = @([int]$newest.Slot)
    }
    $files = @()
    foreach ($slot in $selectedSlots) {
        $name = "$storeBase$slot.dat"
        $file = Get-DirectoryFile $stream $name $rootCluster $bps $spc $firstData $reserved
        if ($null -eq $file) { throw "Could not locate store slot $name in the FAT32 root." }
        $data = Read-FileBytes $stream $file $bps $spc $firstData $reserved
        if ($Case -eq 'credential-truncated') {
            $truncatedSize = [uint32]20
            if ($file.Size -le $truncatedSize) {
                throw "Credential slot $name is not long enough to truncate."
            }
            Write-DirectoryFileSize $stream $file $bps $truncatedSize
            $files += [pscustomobject]@{ Name = $name; Bytes = $truncatedSize }
            continue
        }
        if ($version -ne 0) {
            $versionBytes = [BitConverter]::GetBytes([uint32]$version)
            [Array]::Copy($versionBytes, 0, $data, 8, 4)
            $crc = Get-Crc32 $data ($data.Length - 4)
            $crcBytes = [BitConverter]::GetBytes([uint32]$crc)
            [Array]::Copy($crcBytes, 0, $data, $data.Length - 4, 4)
        } else {
            $data[$data.Length - 1] = $data[$data.Length - 1] -bxor 1
        }
        Write-FileBytes $stream $file $data $bps $spc $firstData $reserved
        $files += [pscustomobject]@{ Name = $name; Bytes = $data.Length }
    }
    $stream.Flush($true)
    $stream.Dispose()
    $stream = $null
    [pscustomobject]@{
        Case = $Case
        ImagePath = $target
        MutatedSlots = ($files.Name -join ', ')
        SlotBytes = ($files.Bytes -join ', ')
        ImageSha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    } | Format-List
} finally {
    if ($null -ne $stream) { $stream.Dispose() }
}
