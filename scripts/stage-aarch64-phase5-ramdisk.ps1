[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputRoot,
    [Parameter(Mandatory = $true)][string]$OutputImage,
    [int]$ImageSizeMB = 64
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($InputRoot).TrimEnd('\')
$output = [IO.Path]::GetFullPath($OutputImage)
if (!(Test-Path -LiteralPath $root -PathType Container)) { throw "Phase-5 ramdisk input root not found: $root" }

function Write-U16LE([byte[]]$Buffer, [int]$Offset, [int]$Value) {
    $Buffer[$Offset] = [byte]($Value -band 0xff)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xff)
}
function Write-U32LE([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    for ($i = 0; $i -lt 4; ++$i) { $Buffer[$Offset + $i] = [byte](($Value -shr (8 * $i)) -band 0xff) }
}
function Write-Ascii([byte[]]$Buffer, [int]$Offset, [string]$Text, [int]$Length) {
    $ascii = [Text.Encoding]::ASCII.GetBytes($Text)
    for ($i = 0; $i -lt $Length; ++$i) { $Buffer[$Offset + $i] = if ($i -lt $ascii.Length) { $ascii[$i] } else { [byte]0x20 } }
}
function Get-ShortName([string]$Name, [hashtable]$Used) {
    $base = [IO.Path]::GetFileNameWithoutExtension($Name).ToUpperInvariant() -replace '[^A-Z0-9]', ''
    $ext = ([IO.Path]::GetExtension($Name).TrimStart('.').ToUpperInvariant() -replace '[^A-Z0-9]', '')
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    if ($base.Length -eq 0) { $base = 'FILE' }
    $n = 1
    while ($true) {
        $suffix = if ($base.Length -le 8 -and $n -eq 1) { '' } else { "~$n" }
        $take = [Math]::Min(8 - $suffix.Length, $base.Length)
        $candidate = $base.Substring(0, $take) + $suffix
        $raw = $candidate.PadRight(8).Substring(0, 8) + $ext.PadRight(3).Substring(0, 3)
        if (!$Used.ContainsKey($raw)) { $Used[$raw] = $true; return $raw }
        ++$n
    }
}
function Get-LfnChecksum([string]$ShortRaw) {
    $sum = 0
    foreach ($b in [Text.Encoding]::ASCII.GetBytes($ShortRaw)) { $sum = (((($sum -band 1) -shl 7) + ($sum -shr 1) + $b) -band 0xff) }
    return [byte]$sum
}
function New-LfnEntries([string]$LongName, [string]$ShortRaw) {
    $bytes = [Text.Encoding]::Unicode.GetBytes($LongName)
    $ucs = New-Object 'System.Collections.Generic.List[UInt16]'
    for ($i = 0; $i -lt $bytes.Length; $i += 2) { $ucs.Add([BitConverter]::ToUInt16($bytes, $i)) }
    $count = [int][Math]::Ceiling(($ucs.Count + 1) / 13.0)
    $checksum = Get-LfnChecksum $ShortRaw
    $entries = New-Object 'System.Collections.Generic.List[byte[]]'
    $positions = @(1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)
    for ($sequence = $count; $sequence -ge 1; --$sequence) {
        $entry = New-Object byte[] 32
        for ($i = 0; $i -lt 32; ++$i) { $entry[$i] = 0xff }
        $entry[0] = [byte]$sequence
        if ($sequence -eq $count) { $entry[0] = $entry[0] -bor 0x40 }
        $entry[11] = 0x0f; $entry[12] = 0; $entry[13] = $checksum; $entry[26] = 0; $entry[27] = 0
        for ($i = 0; $i -lt 13; ++$i) {
            $charIndex = (($sequence - 1) * 13) + $i
            $value = if ($charIndex -lt $ucs.Count) { $ucs[$charIndex] } elseif ($charIndex -eq $ucs.Count) { 0 } else { 0xffff }
            Write-U16LE $entry $positions[$i] $value
        }
        $entries.Add($entry)
    }
    Write-Output -NoEnumerate $entries.ToArray()
}
function New-DirectoryEntry([string]$ShortRaw, [byte]$Attribute, [uint32]$Cluster, [uint32]$Size) {
    $entry = New-Object byte[] 32
    Write-Ascii $entry 0 $ShortRaw 11
    $entry[11] = $Attribute
    Write-U16LE $entry 20 (($Cluster -shr 16) -band 0xffff)
    Write-U16LE $entry 26 ($Cluster -band 0xffff)
    Write-U32LE $entry 28 $Size
    return $entry
}
function Add-DirectoryRecord([System.Collections.Generic.List[byte[]]]$Entries, [string]$Name,
                             [string]$ShortRaw, [byte]$Attribute, [uint32]$Cluster, [uint32]$Size) {
    foreach ($lfn in (New-LfnEntries $Name $ShortRaw)) { $Entries.Add($lfn) }
    $Entries.Add((New-DirectoryEntry $ShortRaw $Attribute $Cluster $Size))
}

$bytesPerSector = 512
$sectorsPerCluster = 16
$reservedSectors = 32
$fatCount = 2
$fatSectors = 256
$totalSectors = [int](($ImageSizeMB * 1024 * 1024) / $bytesPerSector)
$dataStartSector = $reservedSectors + ($fatCount * $fatSectors)
$clusterBytes = $bytesPerSector * $sectorsPerCluster
$fat = New-Object uint32[] ([int](($fatSectors * $bytesPerSector) / 4))
$fat[0] = 0x0ffffff8; $fat[1] = 0x0fffffff
$nextCluster = 2
$directories = New-Object 'System.Collections.Generic.List[string]'
$directories.Add('')
$files = @()
foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File)) {
    $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
    $directory = [IO.Path]::GetDirectoryName($relative)
    if ($null -eq $directory) { $directory = '' } else { $directory = $directory.Replace('\', '/') }
    $parts = if ($directory) { $directory.Split('/') } else { @() }
    $path = ''
    foreach ($part in $parts) {
        $path = if ($path) { "$path/$part" } else { $part }
        if (!$directories.Contains($path)) { $directories.Add($path) }
    }
    $files += [pscustomobject]@{ Name = [IO.Path]::GetFileName($relative); Relative = $relative; Directory = $directory; Bytes = [IO.File]::ReadAllBytes($file.FullName) }
}
$directories = @($directories | Sort-Object { if ($_ -eq '') { 0 } else { ($_ -split '/').Length } })
$directoryClusters = @{}
foreach ($directory in $directories) { $directoryClusters[$directory] = [uint32]$nextCluster; $fat[$nextCluster] = 0x0fffffff; ++$nextCluster }
$fileRecords = @()
foreach ($file in $files) {
    $clusterCount = [Math]::Max(1, [int][Math]::Ceiling($file.Bytes.Length / [double]$clusterBytes))
    $start = [uint32]$nextCluster
    for ($i = 0; $i -lt $clusterCount; ++$i) { $fat[$nextCluster] = if ($i -eq $clusterCount - 1) { 0x0fffffff } else { [uint32]($nextCluster + 1) }; ++$nextCluster }
    $fileRecords += [pscustomobject]@{ Name = $file.Name; Relative = $file.Relative; Directory = $file.Directory; Bytes = $file.Bytes; Cluster = $start }
}
$parentEntries = @{}
$parentUsed = @{}
foreach ($directory in $directories) { $parentEntries[$directory] = New-Object 'System.Collections.Generic.List[byte[]]'; $parentUsed[$directory] = @{} }
foreach ($directory in $directories) {
    if (!$directory) { continue }
    $separator = $directory.LastIndexOf('/')
    $parent = if ($separator -lt 0) { '' } else { $directory.Substring(0, $separator) }
    $name = if ($separator -lt 0) { $directory } else { $directory.Substring($separator + 1) }
    $short = Get-ShortName $name $parentUsed[$parent]
    Add-DirectoryRecord $parentEntries[$parent] $name $short 0x10 $directoryClusters[$directory] 0
}
foreach ($file in $fileRecords) {
    $short = Get-ShortName $file.Name $parentUsed[$file.Directory]
    Add-DirectoryRecord $parentEntries[$file.Directory] $file.Name $short 0x20 $file.Cluster ([uint32]$file.Bytes.Length)
}

$parent = Split-Path -Parent $output
$null = New-Item -ItemType Directory -Path $parent -Force
$stream = [IO.File]::Open($output, [IO.FileMode]::Create, [IO.FileAccess]::ReadWrite)
try {
    $stream.SetLength($ImageSizeMB * 1024 * 1024)
    $boot = New-Object byte[] $bytesPerSector
    $boot[0] = 0xeb; $boot[1] = 0x58; $boot[2] = 0x90; Write-Ascii $boot 3 'GUIDEXOS' 8
    Write-U16LE $boot 11 $bytesPerSector; $boot[13] = $sectorsPerCluster; Write-U16LE $boot 14 $reservedSectors
    $boot[16] = $fatCount; $boot[21] = 0xf8; Write-U32LE $boot 32 $totalSectors; Write-U32LE $boot 36 $fatSectors; Write-U32LE $boot 44 $directoryClusters['']
    Write-U16LE $boot 48 1; Write-U16LE $boot 50 6; $boot[64] = 0x80; $boot[66] = 0x29
    Write-U32LE $boot 67 0x47585035; Write-Ascii $boot 71 'GXOSPHASE5' 11; Write-Ascii $boot 82 'FAT32   ' 8
    $boot[510] = 0x55; $boot[511] = 0xaa; $stream.Position = 0; $stream.Write($boot, 0, $boot.Length)
    $fsInfo = New-Object byte[] $bytesPerSector
    Write-U32LE $fsInfo 0 0x41615252; Write-U32LE $fsInfo 484 0x61417272
    Write-U32LE $fsInfo 488 ([uint32]::MaxValue); Write-U32LE $fsInfo 492 $nextCluster
    $fsInfo[510] = 0x55; $fsInfo[511] = 0xaa; $stream.Position = $bytesPerSector; $stream.Write($fsInfo, 0, $fsInfo.Length)
    for ($fatIndex = 0; $fatIndex -lt $fatCount; ++$fatIndex) {
        $stream.Position = ($reservedSectors + ($fatIndex * $fatSectors)) * $bytesPerSector
        $fatBytes = New-Object byte[] ($fatSectors * $bytesPerSector)
        for ($i = 0; $i -lt $fat.Length; ++$i) { Write-U32LE $fatBytes ($i * 4) $fat[$i] }
        $stream.Write($fatBytes, 0, $fatBytes.Length)
    }
    foreach ($directory in $directories) {
        $dirBytes = New-Object byte[] $clusterBytes
        $offset = 0
        foreach ($entry in $parentEntries[$directory]) { if ($offset + 32 -gt $dirBytes.Length) { throw "Directory too large: $directory" }; [Array]::Copy($entry, 0, $dirBytes, $offset, 32); $offset += 32 }
        $stream.Position = ($dataStartSector + (($directoryClusters[$directory] - 2) * $sectorsPerCluster)) * $bytesPerSector
        $stream.Write($dirBytes, 0, $dirBytes.Length)
    }
    foreach ($file in $fileRecords) {
        $stream.Position = ($dataStartSector + (($file.Cluster - 2) * $sectorsPerCluster)) * $bytesPerSector
        $stream.Write($file.Bytes, 0, $file.Bytes.Length)
    }
} finally { $stream.Dispose() }
Write-Host "Staged Phase-5 FAT32 ramdisk with $($fileRecords.Count) files in $output" -ForegroundColor Green
