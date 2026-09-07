[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceImage,
    [Parameter(Mandatory = $true)][string]$OutputImage
)

$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($SourceImage)
$output = [IO.Path]::GetFullPath($OutputImage)
if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Ramdisk source not found: $source" }
$parent = Split-Path -Parent $output
$null = New-Item -ItemType Directory -Path $parent -Force
Copy-Item -LiteralPath $source -Destination $output -Force
$bytes = [IO.File]::ReadAllBytes($output)

function U16([int]$offset) {
    return [uint32]$bytes[$offset] -bor ([uint32]$bytes[$offset + 1] -shl 8)
}
function U32([int]$offset) {
    return [uint32]$bytes[$offset] -bor ([uint32]$bytes[$offset + 1] -shl 8) -bor
        ([uint32]$bytes[$offset + 2] -shl 16) -bor ([uint32]$bytes[$offset + 3] -shl 24)
}
function SetU16([int]$offset, [uint32]$value) {
    $bytes[$offset] = [byte]($value -band 0xff)
    $bytes[$offset + 1] = [byte](($value -shr 8) -band 0xff)
}
function SetU32([int]$offset, [uint32]$value) {
    for ($i = 0; $i -lt 4; ++$i) { $bytes[$offset + $i] = [byte](($value -shr (8 * $i)) -band 0xff) }
}
function ClusterOffset([uint32]$cluster) {
    return [int]($dataOffset + (($cluster - 2) * $sectorsPerCluster * $bytesPerSector))
}
function FatEntryOffset([uint32]$cluster, [uint32]$fatIndex) {
    return [int](($reservedSectors + ($fatIndex * $fatSizeSectors)) * $bytesPerSector + ($cluster * 4))
}
function FatValue([uint32]$cluster) { return (U32 (FatEntryOffset $cluster 0)) -band 0x0fffffff }
function SetFatValue([uint32]$cluster, [uint32]$value) {
    for ($fat = 0; $fat -lt $numberOfFats; ++$fat) { SetU32 (FatEntryOffset $cluster $fat) (($value -band 0x0fffffff) -bor 0xf0000000) }
}
function NextFreeCluster {
    for ($cluster = 2; $cluster -lt $clusterCount + 2; ++$cluster) {
        if ((FatValue $cluster) -eq 0) { SetFatValue $cluster 0x0fffffff; return [uint32]$cluster }
    }
    throw 'Phase-4 ramdisk has no free FAT clusters'
}
function WriteClusterBytes([uint32]$cluster, [byte[]]$content) {
    if ($content.Length -gt $clusterBytes) { throw 'Phase-4 fixture exceeds one FAT cluster' }
    $offset = ClusterOffset $cluster
    for ($i = 0; $i -lt $clusterBytes; ++$i) { $bytes[$offset + $i] = 0 }
    for ($i = 0; $i -lt $content.Length; ++$i) { $bytes[$offset + $i] = $content[$i] }
}
function WriteName([int]$offset, [string]$name) {
    $ascii = [Text.Encoding]::ASCII.GetBytes($name)
    if ($ascii.Length -ne 11) { throw "FAT short name must be 11 bytes: $name" }
    for ($i = 0; $i -lt 11; ++$i) { $bytes[$offset + $i] = $ascii[$i] }
}
function WriteDirEntry([int]$offset, [string]$name, [bool]$directory, [uint32]$cluster, [uint32]$size) {
    for ($i = 0; $i -lt 32; ++$i) { $bytes[$offset + $i] = 0 }
    WriteName $offset $name
    $attribute = 0x20
    if ($directory) { $attribute = 0x10 }
    $bytes[$offset + 11] = [byte]$attribute
    SetU16 ($offset + 20) (($cluster -shr 16) -band 0xffff)
    SetU16 ($offset + 26) ($cluster -band 0xffff)
    SetU32 ($offset + 28) $size
}
function FindFreeDirSlot([uint32]$cluster) {
    $offset = ClusterOffset $cluster
    for ($i = 0; $i -lt $clusterBytes; $i += 32) {
        $first = $bytes[$offset + $i]
        if ($first -eq 0 -or $first -eq 0xe5) { return $offset + $i }
    }
    throw "Directory cluster $cluster has no free entry"
}
function AddDirEntry([uint32]$directoryCluster, [string]$name, [bool]$directory, [uint32]$cluster, [uint32]$size) {
    WriteDirEntry (FindFreeDirSlot $directoryCluster) $name $directory $cluster $size
}
function InitializeDirectory([uint32]$cluster, [uint32]$parentCluster) {
    $offset = ClusterOffset $cluster
    for ($i = 0; $i -lt $clusterBytes; ++$i) { $bytes[$offset + $i] = 0 }
    WriteDirEntry $offset '.          ' $true $cluster 0
    WriteDirEntry ($offset + 32) '..         ' $true $parentCluster 0
}

$bytesPerSector = U16 11
$sectorsPerCluster = $bytes[13]
$reservedSectors = U16 14
$numberOfFats = $bytes[16]
$fatSizeSectors = U32 36
$rootCluster = U32 44
$totalSectors = U32 32
if ($bytesPerSector -ne 512 -or $sectorsPerCluster -eq 0 -or $numberOfFats -lt 2 -or
    $fatSizeSectors -eq 0 -or $rootCluster -lt 2 -or $bytes[510] -ne 0x55 -or $bytes[511] -ne 0xaa) {
    throw 'Phase-4 staging requires a valid FAT32 image'
}
$dataOffset = ($reservedSectors + ($numberOfFats * $fatSizeSectors)) * $bytesPerSector
$dataSectors = $totalSectors - ($reservedSectors + ($numberOfFats * $fatSizeSectors))
$clusterCount = [uint32]($dataSectors / $sectorsPerCluster)
$clusterBytes = $sectorsPerCluster * $bytesPerSector
if ($clusterCount -lt 128) { throw 'Phase-4 ramdisk is unexpectedly small' }

$phase4 = NextFreeCluster
$nested = NextFreeCluster
$hello = NextFreeCluster
$proof = NextFreeCluster
$helloText = [Text.Encoding]::ASCII.GetBytes('guideXOS AARCH64 Phase 4 filesystem proof')
$nestedText = [Text.Encoding]::ASCII.GetBytes('guideXOS AARCH64 Phase 4 nested filesystem proof')
WriteClusterBytes $hello $helloText
WriteClusterBytes $proof $nestedText
InitializeDirectory $phase4 $rootCluster
InitializeDirectory $nested $phase4
AddDirEntry $rootCluster 'PHASE4     ' $true $phase4 0
AddDirEntry $phase4 'HELLO   TXT' $false $hello ([uint32]$helloText.Length)
AddDirEntry $phase4 'NESTED     ' $true $nested 0
AddDirEntry $nested 'PROOF   TXT' $false $proof ([uint32]$nestedText.Length)

[IO.File]::WriteAllBytes($output, $bytes)
Write-Host "Staged deterministic AARCH64 Phase-4 FAT32 fixture in $output" -ForegroundColor Green
