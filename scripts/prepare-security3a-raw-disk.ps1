[CmdletBinding()]
param(
    [string]$OutputPath = "out\security3a\security3a-credential-data.raw",
    [ValidateRange(64, 2048)]
    [int]$VolumeSizeMiB = 128,
    [switch]$Superfloppy
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$scratchRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out\security3a'))
$markerPath = Join-Path $scratchRoot '.security3a-owned'
if (-not (Test-Path -LiteralPath $scratchRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $scratchRoot | Out-Null
    [IO.File]::WriteAllText($markerPath,
        "Owned by SECURITY3A qualification scratch artifacts.`r`n",
        [Text.Encoding]::ASCII)
} elseif (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
    throw "Refusing to use an unmarked scratch directory: $scratchRoot"
}

$imagePath = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
$scratchPrefix = $scratchRoot.TrimEnd('\') + '\'
if (-not $imagePath.StartsWith($scratchPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputPath must resolve under the owned SECURITY3A scratch directory: $scratchRoot"
}
if ([IO.Path]::GetFileName($imagePath) -notlike 'security3a-*.raw') {
    throw 'OutputPath must have a security3a-*.raw filename.'
}
if (Test-Path -LiteralPath $imagePath) {
    throw "Refusing to overwrite an existing image: $imagePath"
}

$bytesPerSector = 512
$partitionStartLba = if ($Superfloppy) { [uint32]0 } else { [uint32]63 }
$volumeSectors = [uint32]($VolumeSizeMiB * 1024 * 1024 / $bytesPerSector)
$reservedSectors = [uint32]32
$fatCount = [uint32]2
$sectorsPerCluster = [uint32]1
$fatSectors = [uint32]1
while ($true) {
    $clusters = [uint32](($volumeSectors - $reservedSectors - ($fatCount * $fatSectors)) / $sectorsPerCluster)
    $requiredFatSectors = [uint32][Math]::Ceiling((($clusters + 2.0) * 4.0) / $bytesPerSector)
    if ($requiredFatSectors -eq $fatSectors) { break }
    $fatSectors = $requiredFatSectors
}
$dataClusters = [uint32](($volumeSectors - $reservedSectors - ($fatCount * $fatSectors)) / $sectorsPerCluster)
if ($dataClusters -lt 65525) {
    throw "The selected volume is too small for the required FAT32 cluster count: $dataClusters"
}

$mediaSectors = [uint64]$partitionStartLba + [uint64]$volumeSectors
$mediaBytes = $mediaSectors * [uint64]$bytesPerSector
$directory = Split-Path -Parent $imagePath
if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
    New-Item -ItemType Directory -Path $directory | Out-Null
}

function Set-U16([byte[]]$Buffer, [int]$Offset, [uint16]$Value) {
    $Buffer[$Offset] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
}
function Set-U32([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    $Buffer[$Offset] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 2] = [byte](($Value -shr 16) -band 0xFF)
    $Buffer[$Offset + 3] = [byte](($Value -shr 24) -band 0xFF)
}

$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
$identityBytes = New-Object byte[] 8
$rng.GetBytes($identityBytes)
$rng.Dispose()
$volumeSerial = [BitConverter]::ToUInt32($identityBytes, 0)
$diskSignature = [BitConverter]::ToUInt32($identityBytes, 4)
if ($volumeSerial -eq 0) { $volumeSerial = 1 }
if ($diskSignature -eq 0) { $diskSignature = 1 }

$mbr = New-Object byte[] $bytesPerSector
$mbr[446] = 0
$mbr[447] = 0xFE; $mbr[448] = 0xFF; $mbr[449] = 0xFF
$mbr[450] = 0x0C
$mbr[451] = 0xFE; $mbr[452] = 0xFF; $mbr[453] = 0xFF
Set-U32 $mbr 454 ([uint32]$partitionStartLba)
Set-U32 $mbr 458 $volumeSectors
Set-U32 $mbr 440 $diskSignature
$mbr[510] = 0x55; $mbr[511] = 0xAA

$boot = New-Object byte[] $bytesPerSector
$boot[0] = 0xEB; $boot[1] = 0x58; $boot[2] = 0x90
[Text.Encoding]::ASCII.GetBytes('GXOS3A  ').CopyTo($boot, 3)
Set-U16 $boot 11 $bytesPerSector
$boot[13] = [byte]$sectorsPerCluster
Set-U16 $boot 14 $reservedSectors
$boot[16] = [byte]$fatCount
Set-U16 $boot 17 0
Set-U16 $boot 19 0
$boot[21] = 0xF8
Set-U16 $boot 22 0
Set-U16 $boot 24 63
Set-U16 $boot 26 255
Set-U32 $boot 28 ([uint32]$partitionStartLba)
Set-U32 $boot 32 $volumeSectors
Set-U32 $boot 36 $fatSectors
Set-U16 $boot 40 0
Set-U16 $boot 42 0
Set-U32 $boot 44 2
Set-U16 $boot 48 1
Set-U16 $boot 50 6
$boot[64] = 0x80
$boot[66] = 0x29
Set-U32 $boot 67 $volumeSerial
[Text.Encoding]::ASCII.GetBytes('GXOS3AQUAL ').CopyTo($boot, 71)
[Text.Encoding]::ASCII.GetBytes('FAT32   ').CopyTo($boot, 82)
$boot[510] = 0x55; $boot[511] = 0xAA

$fsInfo = New-Object byte[] $bytesPerSector
Set-U32 $fsInfo 0 0x41615252
Set-U32 $fsInfo 484 0x61417272
Set-U32 $fsInfo 488 ($dataClusters - 1)
Set-U32 $fsInfo 492 3
Set-U32 $fsInfo 508 ([uint32]::Parse('AA550000',
    [Globalization.NumberStyles]::HexNumber))

$fatFirstSector = New-Object byte[] $bytesPerSector
Set-U32 $fatFirstSector 0 0x0FFFFFF8
Set-U32 $fatFirstSector 4 0x0FFFFFFF
Set-U32 $fatFirstSector 8 0x0FFFFFFF

$stream = $null
try {
    $stream = [IO.File]::Open($imagePath, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $stream.SetLength($mediaBytes)
    $stream.Position = 0
    $stream.Write($mbr, 0, $mbr.Length)
    $volumeOffset = [int64]$partitionStartLba * $bytesPerSector
    $stream.Position = $volumeOffset
    $stream.Write($boot, 0, $boot.Length)
    $stream.Position = $volumeOffset + $bytesPerSector
    $stream.Write($fsInfo, 0, $fsInfo.Length)
    $stream.Position = $volumeOffset + 6 * $bytesPerSector
    $stream.Write($boot, 0, $boot.Length)
    $stream.Position = $volumeOffset + 7 * $bytesPerSector
    $stream.Write($fsInfo, 0, $fsInfo.Length)
    $firstFatOffset = $volumeOffset + [int64]$reservedSectors * $bytesPerSector
    $stream.Position = $firstFatOffset
    $stream.Write($fatFirstSector, 0, $fatFirstSector.Length)
    $stream.Position = $firstFatOffset + [int64]$fatSectors * $bytesPerSector
    $stream.Write($fatFirstSector, 0, $fatFirstSector.Length)
    $stream.Flush($true)
} finally {
    if ($null -ne $stream) { $stream.Dispose() }
}

[pscustomobject]@{
    ImagePath = $imagePath
    ImageBytes = $mediaBytes
    PartitionStartLba = $partitionStartLba
    PartitionSectors = $volumeSectors
    PartitionType = if ($Superfloppy) { 'superfloppy FAT32' } else { '0x0C (FAT32 LBA)' }
    BytesPerSector = $bytesPerSector
    SectorsPerCluster = $sectorsPerCluster
    FatCopies = $fatCount
    FatSectorsEach = $fatSectors
    DataClusters = $dataClusters
    VolumeLabel = 'GXOS3AQUAL'
    VolumeSerial = ('{0:X8}' -f $volumeSerial)
    DiskSignature = ('{0:X8}' -f $diskSignature)
    Sha256 = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
} | Format-List
