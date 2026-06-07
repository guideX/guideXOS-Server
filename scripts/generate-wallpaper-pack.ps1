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
$TrackedOutputRootCaBundlePath = Join-Path $OutputDir "certs\ca-bundle.pem"
$TrackedOutputRootCaManifestCompatPath = Join-Path $OutputDir "certs\CABUNDLE.MAN"
$NavigatorCaBundleManifestScript = Join-Path $ScriptDir "validate-navigator-ca-bundle.ps1"
. (Join-Path $ScriptDir "navigator-public-https-reviewed-targets.ps1")
$TrackedOutputRootCaBundleBytes = if (Test-Path -LiteralPath $TrackedOutputRootCaBundlePath -PathType Leaf) {
    [System.IO.File]::ReadAllBytes($TrackedOutputRootCaBundlePath)
} else {
    $null
}
$TrackedOutputRootCaManifestCompatBytes = if (Test-Path -LiteralPath $TrackedOutputRootCaManifestCompatPath -PathType Leaf) {
    [System.IO.File]::ReadAllBytes($TrackedOutputRootCaManifestCompatPath)
} else {
    $null
}

$NavigatorCaBundleSizeCapBytes = 512KB
$NavigatorRealPublicProbeDefaultTarget = Get-NavigatorPublicHttpsDefaultTarget
$NavigatorRealPublicProbeTrustMarker = "# guideXOS Navigator real public HTTPS probe trust bundle"
$NavigatorRealPublicProbeTrustDetail = "# deterministic validated fixture roots are retained for smoke coverage; explicit public internet roots are appended below."
$NavigatorRealPublicProbeLocalBundlePath = Join-Path $ScriptDir "fixtures\public-roots\ca-bundle.pem.local"

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

function Get-NavigatorRealPublicProbeTarget {
    $urlValue = $env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL
    if (-not [string]::IsNullOrWhiteSpace($urlValue)) {
        return $urlValue.Trim()
    }

    $targetValue = $env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET
    if (-not [string]::IsNullOrWhiteSpace($targetValue)) {
        return $targetValue.Trim()
    }

    return $NavigatorRealPublicProbeDefaultTarget
}

function Get-NavigatorRealPublicProbeCaBundleSource {
    $source = Resolve-StagedSourcePath $env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE
    if ($source) {
        return $source
    }
    if (Test-Path -LiteralPath $NavigatorRealPublicProbeLocalBundlePath -PathType Leaf) {
        return [System.IO.Path]::GetFullPath($NavigatorRealPublicProbeLocalBundlePath)
    }
    return $null
}

function Get-NavigatorPemBundleInfo {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "$Label source not found: $LiteralPath"
    }

    $item = Get-Item -LiteralPath $LiteralPath
    if ($item.Length -le 0) {
        throw "$Label is empty: $LiteralPath"
    }
    if ($item.Length -gt $NavigatorCaBundleSizeCapBytes) {
        throw "$Label exceeds the 512 KiB safety cap: $LiteralPath"
    }

    $text = [System.IO.File]::ReadAllText($item.FullName, [System.Text.Encoding]::ASCII)
    $matches = [regex]::Matches(
        $text,
        '-----BEGIN CERTIFICATE-----(?<body>[\s\S]*?)-----END CERTIFICATE-----',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if ($matches.Count -le 0) {
        throw "$Label does not contain any PEM certificates: $LiteralPath"
    }

    $parsedCount = 0
    foreach ($match in $matches) {
        $base64 = ($match.Groups["body"].Value -replace '\s', '')
        if ([string]::IsNullOrWhiteSpace($base64)) {
            throw "$Label contains an empty PEM certificate block: $LiteralPath"
        }
        try {
            $bytes = [Convert]::FromBase64String($base64)
            $cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($bytes)
            $parsedCount++
            $cert.Dispose()
        } catch {
            throw "$Label contains a malformed PEM certificate that the smoke harness refused to stage: $LiteralPath"
        }
    }
    if ($parsedCount -le 0) {
        throw "$Label parsed zero certificates: $LiteralPath"
    }

    return [pscustomobject]@{
        Path = $item.FullName
        Bytes = [int64]$item.Length
        ParsedCertCount = [int]$parsedCount
        Text = $text
    }
}

function Test-NavigatorPathWithin {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$CandidatePath
    )

    $baseFull = [System.IO.Path]::GetFullPath($BasePath)
    $candidateFull = [System.IO.Path]::GetFullPath($CandidatePath)
    if (-not $baseFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFull += [System.IO.Path]::DirectorySeparatorChar
    }
    return $candidateFull.StartsWith($baseFull, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-NavigatorRepoRelativePath {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    $rootFull = [System.IO.Path]::GetFullPath($RootDir)
    $pathFull = [System.IO.Path]::GetFullPath($LiteralPath)
    if (-not $rootFull.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $rootFull += [System.IO.Path]::DirectorySeparatorChar
    }
    if ($pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $pathFull.Substring($rootFull.Length).Replace('\', '/')
    }
    return $pathFull
}

function Get-NavigatorManifestModeToken {
    param([AllowNull()][string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return "normal"
    }

    $token = $Value.Trim().ToLowerInvariant()
    switch ($token) {
        "normal" { return "normal" }
        "missing" { return "missing" }
        "hash-mismatch" { return "hash-mismatch" }
        "test-only" { return "test-only" }
        default { throw "Unsupported Navigator CA manifest mode: $Value" }
    }
}

function Get-NavigatorCaBundleManifestProfile {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("smoke", "user", "production", "candidate")]
        [string]$Role,
        [AllowNull()][string]$SourcePath,
        [bool]$ExplicitPublicProbeMaterial,
        [AllowNull()][string]$RotationId,
        [bool]$CandidateProductionReady
    )

    if ($Role -eq "smoke") {
        return [pscustomobject]@{
            BundleType = "smoke-fixture"
            SourceDescription = "repo-fixture:" + (Get-NavigatorRepoRelativePath -LiteralPath $SourcePath)
            RotationId = $null
            ProductionReady = "auto"
        }
    }
    if ($ExplicitPublicProbeMaterial) {
        return [pscustomobject]@{
            BundleType = "production-public-probe-merged"
            SourceDescription = $(if ($Role -eq "candidate") {
                    if ($SourcePath) { "merged:explicit-public-root-input+GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE" } else { "merged:explicit-public-root-input" }
                } elseif ($SourcePath) {
                    "merged:explicit-public-root-input+" + (Get-NavigatorRepoRelativePath -LiteralPath $SourcePath)
                } else {
                    "merged:explicit-public-root-input"
                })
            RotationId = $RotationId
            ProductionReady = "yes"
        }
    }
    if ($Role -eq "candidate") {
        return [pscustomobject]@{
            BundleType = "shipped-root-candidate"
            SourceDescription = "GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE"
            RotationId = $RotationId
            ProductionReady = $(if ($CandidateProductionReady) { "yes" } else { "no" })
        }
    }
    if ($Role -eq "user") {
        return [pscustomobject]@{
            BundleType = "user-dev"
            SourceDescription = "GXOS_NAVIGATOR_USER_CA_BUNDLE_SOURCE"
            RotationId = $null
            ProductionReady = "auto"
        }
    }
    return [pscustomobject]@{
        BundleType = "production-source"
        SourceDescription = "GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE"
        RotationId = $null
        ProductionReady = "auto"
    }
}

function Invoke-NavigatorCaBundleManifestValidation {
    param(
        [Parameter(Mandatory = $true)][string]$BundlePath,
        [Parameter(Mandatory = $true)][string]$BundleType,
        [Parameter(Mandatory = $true)][string]$OutputManifestPath,
        [Parameter(Mandatory = $true)][string]$SourceDescription,
        [AllowNull()][string]$RotationId,
        [string]$ProductionReady = "auto"
    )

    if (-not (Test-Path -LiteralPath $NavigatorCaBundleManifestScript -PathType Leaf)) {
        throw "Navigator CA bundle manifest helper not found: $NavigatorCaBundleManifestScript"
    }

    $manifestArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $NavigatorCaBundleManifestScript,
        "-BundlePath", $BundlePath,
        "-BundleType", $BundleType,
        "-OutputManifestPath", $OutputManifestPath,
        "-SourceDescription", $SourceDescription
    )
    if (-not [string]::IsNullOrWhiteSpace($RotationId)) {
        $manifestArgs += @("-RotationId", $RotationId.Trim())
    }
    if ($ProductionReady -ne "auto") {
        $manifestArgs += @("-ProductionReady", $ProductionReady)
    }

    $output = @(& powershell @manifestArgs 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Navigator CA bundle manifest helper failed for $BundlePath."
    }

    $manifest = Get-Content -LiteralPath $OutputManifestPath -Raw | ConvertFrom-Json
    return [pscustomobject]@{
        ManifestPath = [System.IO.Path]::GetFullPath($OutputManifestPath)
        Manifest = $manifest
        Output = @($output | ForEach-Object { "$_" })
    }
}

function Update-NavigatorCaBundleManifestMode {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$Mode
    )

    switch ($Mode) {
        "normal" { return }
        "missing" {
            if (Test-Path -LiteralPath $ManifestPath -PathType Leaf) {
                Remove-Item -LiteralPath $ManifestPath -Force
            }
            return
        }
        default {
            if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
                throw "Navigator CA manifest mutation requires an existing manifest: $ManifestPath"
            }

            $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
            switch ($Mode) {
                "hash-mismatch" {
                    $sha = [string]$manifest.sha256
                    if ([string]::IsNullOrWhiteSpace($sha) -or $sha.Length -ne 64) {
                        throw "Navigator CA manifest mutation could not read a 64-character sha256 from $ManifestPath"
                    }
                    $manifest.sha256 = $(if ($sha[0] -eq '0') { '1' } else { '0' }) + $sha.Substring(1)
                }
                "test-only" {
                    $manifest.test_only = "yes"
                }
            }

            $json = $manifest | ConvertTo-Json -Depth 4
            [System.IO.File]::WriteAllText($ManifestPath, $json + [Environment]::NewLine, [System.Text.Encoding]::ASCII)
        }
    }
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
$stagedRootCaBundle = $false
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$wallpaperDir = Join-Path $OutputDir "wall"
$certsDir = Join-Path $OutputDir "certs"
$configDir = Join-Path $OutputDir "config"
foreach ($stagingDir in @($wallpaperDir, $certsDir, $configDir)) {
    if (Test-Path $stagingDir) { Remove-Item -Recurse -Force $stagingDir }
}
New-Item -ItemType Directory -Force -Path $wallpaperDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputImage) | Out-Null

$httpsPolicyToken = if ([string]::IsNullOrWhiteSpace($env:GXOS_NAVIGATOR_HTTPS_POLICY)) { $null } else { $env:GXOS_NAVIGATOR_HTTPS_POLICY.Trim() }
$httpsFaultModeToken = if ([string]::IsNullOrWhiteSpace($env:GXOS_NAVIGATOR_HTTPS_FAULT_MODE)) { $null } else { $env:GXOS_NAVIGATOR_HTTPS_FAULT_MODE.Trim() }
$realPublicProbeEnabled = $env:GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS -eq "1"
$realPublicProbeRequired = $env:GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS -eq "1"
$realPublicProbeReviewedOverrideEnabled = $env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_REVIEWED_OVERRIDE -eq "1"
if ($realPublicProbeRequired) {
    $realPublicProbeEnabled = $true
}
$realPublicProbeTarget = Get-NavigatorRealPublicProbeTarget
$realPublicProbeCaBundleSource = if ($realPublicProbeEnabled) { Get-NavigatorRealPublicProbeCaBundleSource } else { $null }
$realPublicProbeCaBundleInfo = if ($realPublicProbeCaBundleSource) {
    Get-NavigatorPemBundleInfo -LiteralPath $realPublicProbeCaBundleSource -Label "Real public HTTPS probe CA bundle"
} else {
    $null
}
$productionCaSource = Resolve-StagedSourcePath $env:GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE
$candidateCaSource = Resolve-StagedSourcePath $env:GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE
$candidateRotationId = if ([string]::IsNullOrWhiteSpace($env:GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_ROTATION_ID)) { $null } else { $env:GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_ROTATION_ID.Trim() }
$candidateProductionReady = $env:GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_PRODUCTION_READY -eq "1"
$productionManifestMode = Get-NavigatorManifestModeToken $env:GXOS_NAVIGATOR_PRODUCTION_CA_MANIFEST_MODE
$userManifestMode = Get-NavigatorManifestModeToken $env:GXOS_NAVIGATOR_USER_CA_MANIFEST_MODE

if ($candidateCaSource -and $productionCaSource) {
    throw "Choose either GXOS_NAVIGATOR_PRODUCTION_CA_BUNDLE_SOURCE or GXOS_NAVIGATOR_SHIPPED_ROOT_CANDIDATE_CA_BUNDLE_SOURCE, not both."
}

if ($SmokeCaFixture -or $env:GXOS_NAVIGATOR_SMOKE_CA_FIXTURE -eq "1") {
    $smokeCaFixturePath = Join-Path (Split-Path -Parent $ScriptDir) "scripts\fixtures\navigator-smoke-root-ca-bundle.pem"
    if (-not (Test-Path $smokeCaFixturePath)) {
        throw "Smoke CA fixture not found: $smokeCaFixturePath"
    }
    New-Item -ItemType Directory -Force -Path $certsDir | Out-Null
    $targetCa = Join-Path $certsDir "ca-bundle.pem"
    Copy-Item -LiteralPath $smokeCaFixturePath -Destination $targetCa -Force
    $targetCaManifest = Join-Path $certsDir "ca-bundle.manifest"
    $targetCaManifestCompat = Join-Path $certsDir "CABUNDLE.MAN"
    $smokeManifestProfile = Get-NavigatorCaBundleManifestProfile -Role "smoke" -SourcePath $smokeCaFixturePath -ExplicitPublicProbeMaterial:$false
    $smokeManifest = Invoke-NavigatorCaBundleManifestValidation `
        -BundlePath $targetCa `
        -BundleType $smokeManifestProfile.BundleType `
        -OutputManifestPath $targetCaManifest `
        -SourceDescription $smokeManifestProfile.SourceDescription
    Copy-Item -LiteralPath $targetCaManifest -Destination $targetCaManifestCompat -Force
    $staged += Get-Item $targetCa
    $staged += Get-Item $targetCaManifest
    $staged += Get-Item $targetCaManifestCompat
    $stagedRootCaBundle = $true
    Write-Host "      staged smoke-only CA bundle at /certs/ca-bundle.pem" -ForegroundColor Yellow
    Write-Host "      staged CA manifest at /certs/ca-bundle.manifest (type=$($smokeManifest.Manifest.bundle_type), production_ready=$($smokeManifest.Manifest.production_ready), test_only=$($smokeManifest.Manifest.test_only))" -ForegroundColor Yellow
} else {
    $productionBundleSource = if ($candidateCaSource) { $candidateCaSource } else { $productionCaSource }
    if ($productionBundleSource -or $realPublicProbeCaBundleInfo) {
        if ($productionBundleSource -and -not (Test-Path -LiteralPath $productionBundleSource -PathType Leaf)) {
            throw "Production-side CA bundle source not found: $productionBundleSource"
        }
        New-Item -ItemType Directory -Force -Path $certsDir | Out-Null
        $targetCa = Join-Path $certsDir "ca-bundle.pem"
        if ($realPublicProbeCaBundleInfo) {
            if ($productionBundleSource) {
                $baseBundle = [System.IO.File]::ReadAllText($productionBundleSource, [System.Text.Encoding]::ASCII).Trim()
                $merged = @(
                    $NavigatorRealPublicProbeTrustMarker
                    $NavigatorRealPublicProbeTrustDetail
                    $baseBundle
                    $realPublicProbeCaBundleInfo.Text.Trim()
                    ""
                ) -join "`r`n"
            } else {
                $merged = @(
                    $NavigatorRealPublicProbeTrustMarker
                    "# explicit public-root bundle staged without deterministic validated fixture roots."
                    $realPublicProbeCaBundleInfo.Text.Trim()
                    ""
                ) -join "`r`n"
            }

            $mergedBytes = [System.Text.Encoding]::ASCII.GetByteCount($merged)
            if ($mergedBytes -gt $NavigatorCaBundleSizeCapBytes) {
                throw "Merged production/public CA bundle exceeds the 512 KiB safety cap."
            }
            [System.IO.File]::WriteAllText($targetCa, $merged, [System.Text.Encoding]::ASCII)
        } else {
            Copy-Item -LiteralPath $productionBundleSource -Destination $targetCa -Force
        }
        $targetCaManifest = Join-Path $certsDir "ca-bundle.manifest"
        $targetCaManifestCompat = Join-Path $certsDir "CABUNDLE.MAN"
        $productionRole = if ($candidateCaSource) { "candidate" } else { "production" }
        $productionManifestProfile = Get-NavigatorCaBundleManifestProfile `
            -Role $productionRole `
            -SourcePath $productionBundleSource `
            -ExplicitPublicProbeMaterial:([bool]$realPublicProbeCaBundleInfo) `
            -RotationId $candidateRotationId `
            -CandidateProductionReady:$candidateProductionReady
        $productionManifest = Invoke-NavigatorCaBundleManifestValidation `
            -BundlePath $targetCa `
            -BundleType $productionManifestProfile.BundleType `
            -OutputManifestPath $targetCaManifest `
            -SourceDescription $productionManifestProfile.SourceDescription `
            -RotationId $productionManifestProfile.RotationId `
            -ProductionReady $productionManifestProfile.ProductionReady
        Update-NavigatorCaBundleManifestMode -ManifestPath $targetCaManifest -Mode $productionManifestMode
        if ($productionManifestMode -ne "missing") {
            $productionManifest = [pscustomobject]@{
                ManifestPath = [System.IO.Path]::GetFullPath($targetCaManifest)
                Manifest = Get-Content -LiteralPath $targetCaManifest -Raw | ConvertFrom-Json
                Output = $productionManifest.Output
            }
            Copy-Item -LiteralPath $targetCaManifest -Destination $targetCaManifestCompat -Force
        }
        $staged += Get-Item $targetCa
        if (Test-Path -LiteralPath $targetCaManifest -PathType Leaf) {
            $staged += Get-Item $targetCaManifest
            $staged += Get-Item $targetCaManifestCompat
        }
        $stagedRootCaBundle = $true
        if ($realPublicProbeCaBundleInfo) {
            Write-Host "      staged production CA bundle at /certs/ca-bundle.pem with explicit public-root opt-in material" -ForegroundColor Yellow
        } elseif ($candidateCaSource) {
            Write-Host "      staged shipped-root candidate bundle at /certs/ca-bundle.pem" -ForegroundColor Yellow
        } else {
            Write-Host "      staged production CA bundle at /certs/ca-bundle.pem" -ForegroundColor Yellow
        }
        if (Test-Path -LiteralPath $targetCaManifest -PathType Leaf) {
            Write-Host "      staged CA manifest at /certs/ca-bundle.manifest (type=$($productionManifest.Manifest.bundle_type), production_ready=$($productionManifest.Manifest.production_ready), test_only=$($productionManifest.Manifest.test_only))" -ForegroundColor Yellow
            if ($productionManifestMode -ne "normal") {
                Write-Host "      applied production manifest mode override: $productionManifestMode" -ForegroundColor DarkYellow
            }
        } else {
            Write-Host "      production CA manifest intentionally absent at /certs/ca-bundle.manifest" -ForegroundColor DarkYellow
        }
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
    $targetUserManifest = Join-Path $configCertsDir "ca-bundle.manifest"
    $targetUserManifestCompat = Join-Path $configCertsDir "CABUNDLE.MAN"
    $staged += Get-Item $targetUserCa
    $targetUserCaCompat = Join-Path $configCertsDir "CABUNDLE.PEM"
    Copy-Item -LiteralPath $userCaSource -Destination $targetUserCaCompat -Force
    $staged += Get-Item $targetUserCaCompat
    Write-Host "      staged user CA bundle at /config/certs/ca-bundle.pem" -ForegroundColor Yellow
    try {
        $userManifestProfile = Get-NavigatorCaBundleManifestProfile `
            -Role "user" `
            -SourcePath $userCaSource `
            -ExplicitPublicProbeMaterial:$false `
            -RotationId $null `
            -CandidateProductionReady:$false
        $userManifest = Invoke-NavigatorCaBundleManifestValidation `
            -BundlePath $targetUserCa `
            -BundleType $userManifestProfile.BundleType `
            -OutputManifestPath $targetUserManifest `
            -SourceDescription $userManifestProfile.SourceDescription `
            -RotationId $userManifestProfile.RotationId `
            -ProductionReady $userManifestProfile.ProductionReady
        Update-NavigatorCaBundleManifestMode -ManifestPath $targetUserManifest -Mode $userManifestMode
        if (Test-Path -LiteralPath $targetUserManifest -PathType Leaf) {
            Copy-Item -LiteralPath $targetUserManifest -Destination $targetUserManifestCompat -Force
            $userManifest = [pscustomobject]@{
                ManifestPath = [System.IO.Path]::GetFullPath($targetUserManifest)
                Manifest = Get-Content -LiteralPath $targetUserManifest -Raw | ConvertFrom-Json
                Output = $userManifest.Output
            }
            $staged += Get-Item $targetUserManifest
            $staged += Get-Item $targetUserManifestCompat
            Write-Host "      staged CA manifest at /config/certs/ca-bundle.manifest (type=$($userManifest.Manifest.bundle_type), production_ready=$($userManifest.Manifest.production_ready), test_only=$($userManifest.Manifest.test_only))" -ForegroundColor Yellow
            if ($userManifestMode -ne "normal") {
                Write-Host "      applied user/dev manifest mode override: $userManifestMode" -ForegroundColor DarkYellow
            }
        } else {
            Write-Host "      user/dev CA manifest intentionally absent at /config/certs/ca-bundle.manifest" -ForegroundColor DarkYellow
        }
    } catch {
        Write-Host "      user/dev CA manifest unavailable; continuing with explicit negative-fixture staging ($($_.Exception.Message))" -ForegroundColor DarkYellow
        $global:LASTEXITCODE = 0
    }
}

if ($httpsPolicyToken -or $httpsFaultModeToken -or $realPublicProbeEnabled -or $realPublicProbeRequired -or $realPublicProbeReviewedOverrideEnabled) {
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
if ($realPublicProbeReviewedOverrideEnabled) {
    $targetReviewedOverride = Join-Path $configNavigatorDir "real-public-https-reviewed-override.txt"
    [System.IO.File]::WriteAllText($targetReviewedOverride, "enabled", [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetReviewedOverride
    $targetReviewedOverrideCompat = Join-Path $configNavigatorDir "RPUBROV.TXT"
    [System.IO.File]::WriteAllText($targetReviewedOverrideCompat, "enabled", [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetReviewedOverrideCompat
    Write-Host "      staged reviewed target override at /config/navigator/real-public-https-reviewed-override.txt" -ForegroundColor Yellow
}
if ($realPublicProbeCaBundleInfo) {
    $targetProbeCaSource = Join-Path $configNavigatorDir "real-public-https-ca-bundle-source.txt"
    [System.IO.File]::WriteAllText($targetProbeCaSource, $realPublicProbeCaBundleInfo.Path, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaSource
    $targetProbeCaSourceCompat = Join-Path $configNavigatorDir "RPUBCAS.TXT"
    [System.IO.File]::WriteAllText($targetProbeCaSourceCompat, $realPublicProbeCaBundleInfo.Path, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaSourceCompat

    $targetProbeCaBytes = Join-Path $configNavigatorDir "real-public-https-ca-bundle-bytes.txt"
    [System.IO.File]::WriteAllText($targetProbeCaBytes, $realPublicProbeCaBundleInfo.Bytes.ToString(), [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaBytes
    $targetProbeCaBytesCompat = Join-Path $configNavigatorDir "RPUBCABY.TXT"
    [System.IO.File]::WriteAllText($targetProbeCaBytesCompat, $realPublicProbeCaBundleInfo.Bytes.ToString(), [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaBytesCompat

    $targetProbeCaCerts = Join-Path $configNavigatorDir "real-public-https-ca-bundle-certs.txt"
    [System.IO.File]::WriteAllText($targetProbeCaCerts, $realPublicProbeCaBundleInfo.ParsedCertCount.ToString(), [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaCerts
    $targetProbeCaCertsCompat = Join-Path $configNavigatorDir "RPUBCART.TXT"
    [System.IO.File]::WriteAllText($targetProbeCaCertsCompat, $realPublicProbeCaBundleInfo.ParsedCertCount.ToString(), [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaCertsCompat

    $targetProbeCaEnabled = Join-Path $configNavigatorDir "real-public-https-ca-bundle-enabled.txt"
    [System.IO.File]::WriteAllText($targetProbeCaEnabled, "enabled", [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaEnabled
    $targetProbeCaEnabledCompat = Join-Path $configNavigatorDir "RPUBCAEN.TXT"
    [System.IO.File]::WriteAllText($targetProbeCaEnabledCompat, "enabled", [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetProbeCaEnabledCompat

    Write-Host "      staged real public CA metadata at /config/navigator/real-public-https-ca-bundle-*.txt" -ForegroundColor Yellow
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
if (-not $stagedRootCaBundle -and $null -ne $TrackedOutputRootCaBundleBytes) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $TrackedOutputRootCaBundlePath) | Out-Null
    [System.IO.File]::WriteAllBytes($TrackedOutputRootCaBundlePath, $TrackedOutputRootCaBundleBytes)
    Write-Host "      restored tracked output-only /certs/ca-bundle.pem after image generation" -ForegroundColor DarkGray
}
if (-not $stagedRootCaBundle -and $null -ne $TrackedOutputRootCaManifestCompatBytes) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $TrackedOutputRootCaManifestCompatPath) | Out-Null
    [System.IO.File]::WriteAllBytes($TrackedOutputRootCaManifestCompatPath, $TrackedOutputRootCaManifestCompatBytes)
    Write-Host "      restored tracked output-only /certs/CABUNDLE.MAN after image generation" -ForegroundColor DarkGray
}
Write-Host "      Wallpaper runtime filesystem ready at /system/wall/" -ForegroundColor Green
