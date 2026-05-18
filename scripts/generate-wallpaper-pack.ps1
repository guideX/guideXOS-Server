param(
    [string]$InputDir = "assets/Backgrounds",
    [string]$OutputDir = "out/wallpaper-pack",
    [string]$OutputImage = "ESP/ramdisk.img",
    [int]$ImageSizeMB = 64
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$InputDir = if ([System.IO.Path]::IsPathRooted($InputDir)) { $InputDir } else { Join-Path $RootDir $InputDir }
$OutputDir = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $RootDir $OutputDir }
$OutputImage = if ([System.IO.Path]::IsPathRooted($OutputImage)) { $OutputImage } else { Join-Path $RootDir $OutputImage }

$WallpaperNames = @(
    "blueflower",
    "dinos",
    "flower",
    "guidexosspace",
    "redflower",
    "ameoba",
    "ameobagx",
    "tronporche",
    "Wallpaper2"
)

$BareMetalAliases = @{
    "blueflower"    = @{ Full = "blueflwr.gxi"; Thumb = "bluef_t.gxi" }
    "dinos"         = @{ Full = "dinos.gxi";    Thumb = "dinos_t.gxi" }
    "flower"        = @{ Full = "flower.gxi";   Thumb = "flower_t.gxi" }
    "guidexosspace" = @{ Full = "gspace.gxi";   Thumb = "gspace_t.gxi" }
    "redflower"     = @{ Full = "redflwr.gxi";  Thumb = "redf_t.gxi" }
    "ameoba"        = @{ Full = "ameoba.gxi";   Thumb = "ameoba_t.gxi" }
    "ameobagx"      = @{ Full = "ameobagx.gxi"; Thumb = "amebgx_t.gxi" }
    "tronporche"    = @{ Full = "tronpor.gxi";  Thumb = "tronp_t.gxi" }
    "Wallpaper2"    = @{ Full = "wallp2.gxi";   Thumb = "wallp2_t.gxi" }
}

Add-Type -AssemblyName System.Drawing

function Write-U16LE([byte[]]$Buffer, [int]$Offset, [int]$Value) {
    $Buffer[$Offset] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
}

function Write-U32LE([byte[]]$Buffer, [int]$Offset, [uint32]$Value) {
    $Buffer[$Offset] = [byte]($Value -band 0xFF)
    $Buffer[$Offset + 1] = [byte](($Value -shr 8) -band 0xFF)
    $Buffer[$Offset + 2] = [byte](($Value -shr 16) -band 0xFF)
    $Buffer[$Offset + 3] = [byte](($Value -shr 24) -band 0xFF)
}

function Write-Ascii([byte[]]$Buffer, [int]$Offset, [string]$Text, [int]$Length) {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text)
    for ($i = 0; $i -lt $Length; $i++) {
        $Buffer[$Offset + $i] = if ($i -lt $bytes.Length) { $bytes[$i] } else { [byte]0x20 }
    }
}

function Get-ShortName([string]$Name, [hashtable]$Used) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Name).ToUpperInvariant() -replace '[^A-Z0-9]', ''
    $ext = ([System.IO.Path]::GetExtension($Name).TrimStart('.').ToUpperInvariant() -replace '[^A-Z0-9]', '')
    if ($ext.Length -gt 3) { $ext = $ext.Substring(0, 3) }
    if ($base.Length -eq 0) { $base = "FILE" }
    $candidateBase = if ($base.Length -le 8) { $base } else { $base.Substring(0, 6) + "~1" }
    $n = 1
    while ($true) {
        $rawBase = $candidateBase
        if ($Used.ContainsKey(($rawBase.PadRight(8).Substring(0, 8) + $ext.PadRight(3).Substring(0, 3)))) {
            $suffix = "~$n"
            $take = [Math]::Min(8 - $suffix.Length, $base.Length)
            $rawBase = $base.Substring(0, $take) + $suffix
            $n++
        }
        $raw = $rawBase.PadRight(8).Substring(0, 8) + $ext.PadRight(3).Substring(0, 3)
        if (-not $Used.ContainsKey($raw)) {
            $Used[$raw] = $true
            return $raw
        }
    }
}

function Get-LfnChecksum([string]$ShortRaw) {
    $sum = 0
    foreach ($b in [System.Text.Encoding]::ASCII.GetBytes($ShortRaw)) {
        $sum = ((($sum -band 1) -shl 7) + ($sum -shr 1) + $b) -band 0xFF
    }
    return [byte]$sum
}

function New-LfnEntries([string]$LongName, [string]$ShortRaw) {
    $chars = [System.Text.Encoding]::Unicode.GetBytes($LongName)
    $ucs = New-Object System.Collections.Generic.List[UInt16]
    for ($i = 0; $i -lt $chars.Length; $i += 2) {
        $ucs.Add([BitConverter]::ToUInt16($chars, $i))
    }
    $entryCount = [Math]::Ceiling(($ucs.Count + 1) / 13)
    $checksum = Get-LfnChecksum $ShortRaw
    $entries = New-Object System.Collections.Generic.List[byte[]]
    for ($seq = $entryCount; $seq -ge 1; $seq--) {
        $entry = New-Object byte[] 32
        for ($i = 0; $i -lt 32; $i++) { $entry[$i] = 0xFF }
        $entry[0] = [byte]$seq
        if ($seq -eq $entryCount) { $entry[0] = $entry[0] -bor 0x40 }
        $entry[11] = 0x0F
        $entry[12] = 0
        $entry[13] = $checksum
        $entry[26] = 0
        $entry[27] = 0
        $positions = @(1,3,5,7,9,14,16,18,20,22,24,28,30)
        for ($i = 0; $i -lt 13; $i++) {
            $charIndex = (($seq - 1) * 13) + $i
            $value = if ($charIndex -lt $ucs.Count) { $ucs[$charIndex] } elseif ($charIndex -eq $ucs.Count) { 0 } else { 0xFFFF }
            Write-U16LE $entry $positions[$i] $value
        }
        $entries.Add($entry)
    }
    Write-Output -NoEnumerate $entries.ToArray()
}

function New-DirectoryEntry([string]$ShortRaw, [byte]$Attr, [uint32]$Cluster, [uint32]$Size) {
    $entry = New-Object byte[] 32
    Write-Ascii $entry 0 $ShortRaw 11
    $entry[11] = $Attr
    Write-U16LE $entry 20 (($Cluster -shr 16) -band 0xFFFF)
    Write-U16LE $entry 26 ($Cluster -band 0xFFFF)
    Write-U32LE $entry 28 $Size
    return $entry
}

function Write-GximgFile([string]$SourcePath, [string]$TargetPath, [int]$MaxWidth = 0, [int]$MaxHeight = 0) {
    $sourceBitmap = [System.Drawing.Bitmap]::FromFile($SourcePath)
    $bitmap = $sourceBitmap
    if ($MaxWidth -gt 0 -and $MaxHeight -gt 0 -and ($sourceBitmap.Width -gt $MaxWidth -or $sourceBitmap.Height -gt $MaxHeight)) {
        $scaleX = [double]$MaxWidth / [double]$sourceBitmap.Width
        $scaleY = [double]$MaxHeight / [double]$sourceBitmap.Height
        $scale = [Math]::Min($scaleX, $scaleY)
        $targetWidth = [Math]::Max(1, [int][Math]::Round($sourceBitmap.Width * $scale))
        $targetHeight = [Math]::Max(1, [int][Math]::Round($sourceBitmap.Height * $scale))
        $resized = New-Object System.Drawing.Bitmap $targetWidth, $targetHeight
        $graphics = [System.Drawing.Graphics]::FromImage($resized)
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($sourceBitmap, 0, 0, $targetWidth, $targetHeight)
        } finally {
            $graphics.Dispose()
        }
        $bitmap = $resized
    }
    try {
        $fs = [System.IO.File]::Open($TargetPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
        try {
            $bw = New-Object System.IO.BinaryWriter($fs)
            $bw.Write([System.Text.Encoding]::ASCII.GetBytes("GXIMG001"))
            $bw.Write([uint32]$bitmap.Width)
            $bw.Write([uint32]$bitmap.Height)
            $bw.Write([uint32]1)
            for ($y = 0; $y -lt $bitmap.Height; $y++) {
                for ($x = 0; $x -lt $bitmap.Width; $x++) {
                    $bw.Write([uint32]([int64]$bitmap.GetPixel($x, $y).ToArgb() -band 0xFFFFFFFFL))
                }
            }
            $bw.Flush()
        } finally {
            $fs.Dispose()
        }
    } finally {
        if ($bitmap -ne $sourceBitmap) { $bitmap.Dispose() }
        $sourceBitmap.Dispose()
    }
}

function Add-DirectoryRecord([System.Collections.Generic.List[byte[]]]$Entries, [string]$LongName, [string]$ShortRaw, [byte]$Attr, [uint32]$Cluster, [uint32]$Size) {
    foreach ($lfn in (New-LfnEntries $LongName $ShortRaw)) { $Entries.Add($lfn) }
    $Entries.Add((New-DirectoryEntry $ShortRaw $Attr $Cluster $Size))
}

function Write-Fat32Image([string]$ImagePath, [string]$WallpaperDir, [array]$Files, [int]$SizeMB) {
    $bytesPerSector = 512
    $sectorsPerCluster = 8
    $reservedSectors = 32
    $fatCount = 2
    $totalSectors = [int](($SizeMB * 1024 * 1024) / $bytesPerSector)
    $fatSectors = 256
    $dataStartSector = $reservedSectors + ($fatCount * $fatSectors)
    $clusterBytes = $bytesPerSector * $sectorsPerCluster
    $nextCluster = 2
    $fat = New-Object uint32[] ([int](($fatSectors * $bytesPerSector) / 4))
    $fat[0] = 0x0FFFFFF8
    $fat[1] = 0x0FFFFFFF

    $rootCluster = $nextCluster++
    $wallpaperCluster = $nextCluster++
    $fat[$rootCluster] = 0x0FFFFFFF
    $fat[$wallpaperCluster] = 0x0FFFFFFF

    $fileRecords = @()
    foreach ($file in $Files) {
        $length = (Get-Item $file.FullName).Length
        $clusters = [Math]::Max(1, [Math]::Ceiling($length / $clusterBytes))
        $start = $nextCluster
        for ($i = 0; $i -lt $clusters; $i++) {
            $cluster = $nextCluster++
            $fat[$cluster] = if ($i -eq $clusters - 1) { 0x0FFFFFFF } else { [uint32]($cluster + 1) }
        }
        $fileRecords += [pscustomobject]@{ Name = $file.Name; FullName = $file.FullName; Size = [uint32]$length; Cluster = [uint32]$start }
    }

    $stream = [System.IO.File]::Open($ImagePath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::ReadWrite)
    try {
        $stream.SetLength($SizeMB * 1024 * 1024)
        $sector = New-Object byte[] $bytesPerSector
        $sector[0] = 0xEB; $sector[1] = 0x58; $sector[2] = 0x90
        Write-Ascii $sector 3 "GUIDEXOS" 8
        Write-U16LE $sector 11 $bytesPerSector
        $sector[13] = [byte]$sectorsPerCluster
        Write-U16LE $sector 14 $reservedSectors
        $sector[16] = [byte]$fatCount
        Write-U32LE $sector 32 $totalSectors
        Write-U32LE $sector 36 $fatSectors
        Write-U32LE $sector 44 $rootCluster
        Write-U16LE $sector 48 1
        Write-U16LE $sector 50 6
        $sector[64] = 0x80
        $sector[66] = 0x29
        Write-U32LE $sector 67 0x47585750
        Write-Ascii $sector 71 "GXWALLPAPER" 11
        Write-Ascii $sector 82 "FAT32   " 8
        $sector[510] = 0x55; $sector[511] = 0xAA
        $stream.Write($sector, 0, $sector.Length)

        $fsInfo = New-Object byte[] $bytesPerSector
        Write-U32LE $fsInfo 0 0x41615252
        Write-U32LE $fsInfo 484 0x61417272
        Write-U32LE $fsInfo 488 ([uint32]::MaxValue)
        Write-U32LE $fsInfo 492 $nextCluster
        $fsInfo[510] = 0x55; $fsInfo[511] = 0xAA
        $stream.Position = $bytesPerSector
        $stream.Write($fsInfo, 0, $fsInfo.Length)

        for ($fatIndex = 0; $fatIndex -lt $fatCount; $fatIndex++) {
            $stream.Position = ($reservedSectors + ($fatIndex * $fatSectors)) * $bytesPerSector
            $fatBytes = New-Object byte[] ($fatSectors * $bytesPerSector)
            for ($i = 0; $i -lt $fat.Length; $i++) { Write-U32LE $fatBytes ($i * 4) $fat[$i] }
            $stream.Write($fatBytes, 0, $fatBytes.Length)
        }

        $rootEntries = New-Object 'System.Collections.Generic.List[byte[]]'
        $usedRoot = @{}
        Add-DirectoryRecord $rootEntries "wall" (Get-ShortName "wall" $usedRoot) 0x10 $wallpaperCluster 0

        $wallEntries = New-Object 'System.Collections.Generic.List[byte[]]'
        $usedWall = @{}
        foreach ($record in $fileRecords) {
            Add-DirectoryRecord $wallEntries $record.Name (Get-ShortName $record.Name $usedWall) 0x20 $record.Cluster $record.Size
        }

        foreach ($pair in @(@($rootCluster, $rootEntries), @($wallpaperCluster, $wallEntries))) {
            $cluster = [uint32]$pair[0]
            $entries = $pair[1]
            $dirBytes = New-Object byte[] $clusterBytes
            $offset = 0
            foreach ($entry in $entries) {
                [Array]::Copy($entry, 0, $dirBytes, $offset, 32)
                $offset += 32
            }
            $stream.Position = ($dataStartSector + (($cluster - 2) * $sectorsPerCluster)) * $bytesPerSector
            $stream.Write($dirBytes, 0, $dirBytes.Length)
        }

        foreach ($record in $fileRecords) {
            $data = [System.IO.File]::ReadAllBytes($record.FullName)
            $stream.Position = ($dataStartSector + (($record.Cluster - 2) * $sectorsPerCluster)) * $bytesPerSector
            $stream.Write($data, 0, $data.Length)
            Write-Host "      added /system/wall/$($record.Name) ($([Math]::Round($record.Size / 1KB, 1)) KB)" -ForegroundColor Gray
        }
    } finally {
        $stream.Dispose()
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$wallpaperDir = Join-Path $OutputDir "wall"
if (Test-Path $wallpaperDir) { Remove-Item -Recurse -Force $wallpaperDir }
New-Item -ItemType Directory -Force -Path $wallpaperDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputImage) | Out-Null

$staged = @()
foreach ($name in $WallpaperNames) {
    foreach ($suffix in @("", "_thumb")) {
        $pngName = "$name$suffix.png"
        $source = Join-Path $InputDir $pngName
        if (-not (Test-Path $source)) {
            throw "Missing expected wallpaper asset: $source"
        }
        $targetPng = Join-Path $wallpaperDir $pngName
        Copy-Item $source $targetPng -Force
        $staged += Get-Item $targetPng

        $aliases = $BareMetalAliases[$name]
        if (-not $aliases) {
            throw "Missing 8.3 bare-metal alias for wallpaper: $name"
        }
        $gximgName = if ($suffix -eq "_thumb") { $aliases.Thumb } else { $aliases.Full }
        $targetGximg = Join-Path $wallpaperDir $gximgName
        if ($suffix -eq "_thumb") {
            Write-GximgFile $source $targetGximg
        } else {
            Write-GximgFile $source $targetGximg -MaxWidth 800 -MaxHeight 600
        }
        $staged += Get-Item $targetGximg
    }
}

$totalBytes = ($staged | Measure-Object -Property Length -Sum).Sum
$minimumMB = [Math]::Ceiling(($totalBytes + (4MB)) / 1MB)
if ($ImageSizeMB -lt $minimumMB) {
    Write-Host "      Requested image size ${ImageSizeMB}MB is too small; using ${minimumMB}MB" -ForegroundColor Yellow
    $ImageSizeMB = [int]$minimumMB
}

Write-Host "      Building wallpaper runtime filesystem: $OutputImage" -ForegroundColor Cyan
Write-Fat32Image $OutputImage $wallpaperDir ($staged | Sort-Object Name) $ImageSizeMB
Write-Host "      Wallpaper runtime filesystem ready at /system/wall/" -ForegroundColor Green
