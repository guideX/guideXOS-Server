param(
    [string]$InputDir = "assets/Backgrounds",
    [string]$OutputDir = "out/wallpaper-pack",
    [string]$OutputImage = "ESP/ramdisk.img",
    [int]$ImageSizeMB = 64,
    [switch]$SmokeCaFixture
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$InputDir = if ([System.IO.Path]::IsPathRooted($InputDir)) { $InputDir } else { Join-Path $RootDir $InputDir }
$OutputDir = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $RootDir $OutputDir }
$OutputImage = if ([System.IO.Path]::IsPathRooted($OutputImage)) { $OutputImage } else { Join-Path $RootDir $OutputImage }

function Resolve-StagedSourcePath([string]$PathValue) {
    if ([string]::IsNullOrWhiteSpace($PathValue)) { return $null }
    $candidate = $PathValue.Trim()
    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $RootDir $candidate
    }
    return $candidate
}

function Get-StagedRelativePath([string]$BasePath, [string]$FullPath) {
    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    $fileFull = [System.IO.Path]::GetFullPath($FullPath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFull += [System.IO.Path]::DirectorySeparatorChar
    }
    if (-not $fileFull.StartsWith($baseFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Staged file is outside the wallpaper pack output directory: $fileFull"
    }
    return $fileFull.Substring($baseFull.Length).Replace('\', '/')
}

$WallpaperNames = @(
    "blueflower",
    "dinos",
    "flower",
    "guidexosspace",
    "guidexosspace3",
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
    "guidexosspace3" = @{ Full = "gspace2.gxi"; Thumb = "gspac2_t.gxi" }
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

function Write-Fat32Image([string]$ImagePath, [string]$WallpaperDir, [array]$Files, [int]$SizeMB, [switch]$SmokeCaFixture) {
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

    $pendingFiles = @()
    $hasCerts = $false
    $hasConfig = $false
    $hasConfigCerts = $false
    $hasConfigNavigator = $false
    foreach ($file in $Files) {
        $fullPath = [System.IO.Path]::GetFullPath($file.FullName)
        $relativePath = Get-StagedRelativePath $OutputDir $fullPath
        $directory = [System.IO.Path]::GetDirectoryName($relativePath)
        if ($null -eq $directory) { $directory = "" }
        $directory = $directory.Replace('\', '/')
        switch -Regex ($directory) {
            '^wall$' {
                break
            }
            '^certs$' {
                $hasCerts = $true
                break
            }
            '^config/certs$' {
                $hasConfig = $true
                $hasConfigCerts = $true
                break
            }
            '^config/navigator$' {
                $hasConfig = $true
                $hasConfigNavigator = $true
                break
            }
            default {
                throw "Unexpected staged file path for ramdisk image: $relativePath"
            }
        }
        $pendingFiles += [pscustomobject]@{
            Name = $file.Name
            FullName = $fullPath
            RelativePath = $relativePath
            Directory = $directory
            Size = [uint32](Get-Item $fullPath).Length
        }
    }

    $certsCluster = $null
    $configCluster = $null
    $configCertsCluster = $null
    $configNavigatorCluster = $null
    if ($hasCerts) {
        $certsCluster = $nextCluster++
        $fat[$certsCluster] = 0x0FFFFFFF
    }
    if ($hasConfig) {
        $configCluster = $nextCluster++
        $fat[$configCluster] = 0x0FFFFFFF
    }
    if ($hasConfigCerts) {
        $configCertsCluster = $nextCluster++
        $fat[$configCertsCluster] = 0x0FFFFFFF
    }
    if ($hasConfigNavigator) {
        $configNavigatorCluster = $nextCluster++
        $fat[$configNavigatorCluster] = 0x0FFFFFFF
    }

    $fileRecords = @()
    foreach ($pending in $pendingFiles) {
        $clusters = [Math]::Max(1, [Math]::Ceiling($pending.Size / $clusterBytes))
        $start = $nextCluster
        for ($i = 0; $i -lt $clusters; $i++) {
            $cluster = $nextCluster++
            $fat[$cluster] = if ($i -eq $clusters - 1) { 0x0FFFFFFF } else { [uint32]($cluster + 1) }
        }
        $fileRecords += [pscustomobject]@{
            Name = $pending.Name
            FullName = $pending.FullName
            RelativePath = $pending.RelativePath
            Directory = $pending.Directory
            Size = $pending.Size
            Cluster = [uint32]$start
        }
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
        if ($null -ne $certsCluster) {
            Add-DirectoryRecord $rootEntries "certs" (Get-ShortName "certs" $usedRoot) 0x10 $certsCluster 0
        }
        if ($null -ne $configCluster) {
            Add-DirectoryRecord $rootEntries "config" (Get-ShortName "config" $usedRoot) 0x10 $configCluster 0
        }

        $wallEntries = New-Object 'System.Collections.Generic.List[byte[]]'
        $usedWall = @{}
        $certEntries = if ($null -ne $certsCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedCert = @{}
        $configEntries = if ($null -ne $configCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedConfig = @{}
        $configCertEntries = if ($null -ne $configCertsCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedConfigCert = @{}
        $configNavigatorEntries = if ($null -ne $configNavigatorCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedConfigNavigator = @{}

        if ($null -ne $configEntries) {
            if ($null -ne $configCertsCluster) {
                Add-DirectoryRecord $configEntries "certs" (Get-ShortName "certs" $usedConfig) 0x10 $configCertsCluster 0
            }
            if ($null -ne $configNavigatorCluster) {
                Add-DirectoryRecord $configEntries "navigator" (Get-ShortName "navigator" $usedConfig) 0x10 $configNavigatorCluster 0
            }
        }

        foreach ($record in $fileRecords) {
            switch ($record.Directory) {
                "wall" {
                    Add-DirectoryRecord $wallEntries $record.Name (Get-ShortName $record.Name $usedWall) 0x20 $record.Cluster $record.Size
                    break
                }
                "certs" {
                    Add-DirectoryRecord $certEntries $record.Name (Get-ShortName $record.Name $usedCert) 0x20 $record.Cluster $record.Size
                    break
                }
                "config/certs" {
                    Add-DirectoryRecord $configCertEntries $record.Name (Get-ShortName $record.Name $usedConfigCert) 0x20 $record.Cluster $record.Size
                    break
                }
                "config/navigator" {
                    Add-DirectoryRecord $configNavigatorEntries $record.Name (Get-ShortName $record.Name $usedConfigNavigator) 0x20 $record.Cluster $record.Size
                    break
                }
            }
        }

        foreach ($pair in @(
            @($rootCluster, $rootEntries),
            @($wallpaperCluster, $wallEntries),
            @($certsCluster, $certEntries),
            @($configCluster, $configEntries),
            @($configCertsCluster, $configCertEntries),
            @($configNavigatorCluster, $configNavigatorEntries)
        )) {
            if ($null -eq $pair[0] -or $null -eq $pair[1]) { continue }
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
            Write-Host "      added /$($record.RelativePath.Replace('\', '/')) ($([Math]::Round($record.Size / 1KB, 1)) KB)" -ForegroundColor Gray
        }
    } finally {
        $stream.Dispose()
    }
}

$staged = @()
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$wallpaperDir = Join-Path $OutputDir "wall"
$certsDir = Join-Path $OutputDir "certs"
$configDir = Join-Path $OutputDir "config"
foreach ($stagingDir in @($wallpaperDir, $certsDir, $configDir)) {
    if (Test-Path $stagingDir) { Remove-Item -Recurse -Force $stagingDir }
}
New-Item -ItemType Directory -Force -Path $wallpaperDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputImage) | Out-Null

if ($SmokeCaFixture -or $env:GXOS_NAVIGATOR_SMOKE_CA_FIXTURE -eq "1") {
    $smokeCaFixturePath = Join-Path (Split-Path -Parent $ScriptDir) "scripts\fixtures\navigator-smoke-root-ca-bundle.pem"
    if (-not (Test-Path $smokeCaFixturePath)) {
        throw "Smoke CA fixture not found: $smokeCaFixturePath"
    }
    New-Item -ItemType Directory -Force -Path $certsDir | Out-Null
    $targetCa = Join-Path $certsDir "ca-bundle.pem"
    Copy-Item -LiteralPath $smokeCaFixturePath -Destination $targetCa -Force
    $staged += Get-Item $targetCa
    Write-Host "      staged smoke-only CA bundle at /certs/ca-bundle.pem" -ForegroundColor Yellow
} else {
    $productionCaSource = Resolve-StagedSourcePath $env:GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE
    if ($productionCaSource) {
        if (-not (Test-Path $productionCaSource)) {
            throw "Production CA bundle source not found: $productionCaSource"
        }
        New-Item -ItemType Directory -Force -Path $certsDir | Out-Null
        $targetCa = Join-Path $certsDir "ca-bundle.pem"
        Copy-Item -LiteralPath $productionCaSource -Destination $targetCa -Force
        $staged += Get-Item $targetCa
        Write-Host "      staged production CA bundle at /certs/ca-bundle.pem" -ForegroundColor Yellow
    }
}

$userCaSource = Resolve-StagedSourcePath $env:GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE
if ($userCaSource) {
    if (-not (Test-Path $userCaSource)) {
        throw "User CA bundle source not found: $userCaSource"
    }
    $configCertsDir = Join-Path $configDir "certs"
    New-Item -ItemType Directory -Force -Path $configCertsDir | Out-Null
    $targetUserCa = Join-Path $configCertsDir "ca-bundle.pem"
    Copy-Item -LiteralPath $userCaSource -Destination $targetUserCa -Force
    $staged += Get-Item $targetUserCa
    $targetUserCaCompat = Join-Path $configCertsDir "CABUNDLE.PEM"
    Copy-Item -LiteralPath $userCaSource -Destination $targetUserCaCompat -Force
    $staged += Get-Item $targetUserCaCompat
    Write-Host "      staged user CA bundle at /config/certs/ca-bundle.pem" -ForegroundColor Yellow
}

$httpsPolicyToken = if ([string]::IsNullOrWhiteSpace($env:GXOS_NAVIGATOR_HTTPS_POLICY)) { $null } else { $env:GXOS_NAVIGATOR_HTTPS_POLICY.Trim() }
$httpsFaultModeToken = if ([string]::IsNullOrWhiteSpace($env:GXOS_NAVIGATOR_HTTPS_FAULT_MODE)) { $null } else { $env:GXOS_NAVIGATOR_HTTPS_FAULT_MODE.Trim() }
$realPublicProbeEnabled = $env:GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS -eq "1"
$realPublicProbeRequired = $env:GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS -eq "1"
$realPublicProbeTarget = if ([string]::IsNullOrWhiteSpace($env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET)) {
    "https://sha256.badssl.com/"
} else {
    $env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET.Trim()
}
if ($httpsPolicyToken -or $httpsFaultModeToken -or $realPublicProbeEnabled -or $realPublicProbeRequired) {
    $configNavigatorDir = Join-Path $configDir "navigator"
    New-Item -ItemType Directory -Force -Path $configNavigatorDir | Out-Null
}
if ($httpsPolicyToken) {
    $targetPolicy = Join-Path $configNavigatorDir "https-policy.txt"
    [System.IO.File]::WriteAllText($targetPolicy, $httpsPolicyToken, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetPolicy
    $targetPolicyCompat = Join-Path $configNavigatorDir "HTTPSPOL.TXT"
    [System.IO.File]::WriteAllText($targetPolicyCompat, $httpsPolicyToken, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetPolicyCompat
    Write-Host "      staged HTTPS policy config at /config/navigator/https-policy.txt ($httpsPolicyToken)" -ForegroundColor Yellow
}
if ($httpsFaultModeToken) {
    $targetFaultMode = Join-Path $configNavigatorDir "https-fault-mode.txt"
    [System.IO.File]::WriteAllText($targetFaultMode, $httpsFaultModeToken, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetFaultMode
    $targetFaultModeCompat = Join-Path $configNavigatorDir "HTTPSFLT.TXT"
    [System.IO.File]::WriteAllText($targetFaultModeCompat, $httpsFaultModeToken, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetFaultModeCompat
    Write-Host "      staged HTTPS smoke fault mode at /config/navigator/https-fault-mode.txt ($httpsFaultModeToken)" -ForegroundColor Yellow
}
if ($realPublicProbeEnabled) {
    $targetProbeUrl = Join-Path $configNavigatorDir "real-public-https-probe-url.txt"
    [System.IO.File]::WriteAllText($targetProbeUrl, $realPublicProbeTarget, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeUrl
    $targetProbeUrlCompat = Join-Path $configNavigatorDir "RPUBURL.TXT"
    [System.IO.File]::WriteAllText($targetProbeUrlCompat, $realPublicProbeTarget, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeUrlCompat
    Write-Host "      staged real public HTTPS probe target at /config/navigator/real-public-https-probe-url.txt ($realPublicProbeTarget)" -ForegroundColor Yellow
}
if ($realPublicProbeRequired) {
    $targetProbeRequire = Join-Path $configNavigatorDir "real-public-https-probe-required.txt"
    [System.IO.File]::WriteAllText($targetProbeRequire, "required", [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeRequire
    $targetProbeRequireCompat = Join-Path $configNavigatorDir "RPUBRQ.TXT"
    [System.IO.File]::WriteAllText($targetProbeRequireCompat, "required", [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeRequireCompat
    Write-Host "      staged real public HTTPS probe requirement at /config/navigator/real-public-https-probe-required.txt" -ForegroundColor Yellow
}

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
Write-Fat32Image $OutputImage $wallpaperDir ($staged | Sort-Object Name) $ImageSizeMB -SmokeCaFixture:$SmokeCaFixture
Write-Host "      Wallpaper runtime filesystem ready at /system/wall/" -ForegroundColor Green
