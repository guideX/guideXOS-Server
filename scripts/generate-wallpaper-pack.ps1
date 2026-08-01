param(
    [string]$InputDir = "assets/Backgrounds",
    [string]$OutputDir = "out/wallpaper-pack",
    [string]$OutputImage = "ESP/ramdisk.img",
    [int]$ImageSizeMB = 64,
    [switch]$SmokeCaFixture,
    [string]$ImageViewerRuntimeSmokePath,
    [string]$NativePackageDir
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$InputDir = if ([System.IO.Path]::IsPathRooted($InputDir)) { $InputDir } else { Join-Path $RootDir $InputDir }
$OutputDir = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $RootDir $OutputDir }
$OutputImage = if ([System.IO.Path]::IsPathRooted($OutputImage)) { $OutputImage } else { Join-Path $RootDir $OutputImage }
$NativePackageDir = if ([string]::IsNullOrWhiteSpace($NativePackageDir)) { "" } elseif ([System.IO.Path]::IsPathRooted($NativePackageDir)) { $NativePackageDir } else { Join-Path $RootDir $NativePackageDir }
$TrackedOutputRootCaBundlePath = Join-Path $OutputDir "certs\ca-bundle.pem"
$TrackedOutputRootCaManifestCompatPath = Join-Path $OutputDir "certs\CABUNDLE.MAN"
$NavigatorCaBundleManifestScript = Join-Path $ScriptDir "validate-navigator-ca-bundle.ps1"
. (Join-Path $ScriptDir "navigator-public-https-reviewed-targets.ps1")
$ImageViewerRuntimeSmokePath = if ([string]::IsNullOrWhiteSpace($ImageViewerRuntimeSmokePath)) {
    $env:GXOS_IMAGEVIEWER_RUNTIME_SMOKE_PNG_PATH
} else {
    $ImageViewerRuntimeSmokePath
}
$ImageViewerRuntimeSmokePath = if ([string]::IsNullOrWhiteSpace($ImageViewerRuntimeSmokePath)) { "" } else { $ImageViewerRuntimeSmokePath.Trim() }
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

if (-not ("GxosPngCrc32" -as [type])) {
    Add-Type -TypeDefinition @"
using System;

public static class GxosPngCrc32
{
    private static readonly uint[] Table = CreateTable();

    private static uint[] CreateTable()
    {
        uint[] table = new uint[256];
        for (uint i = 0; i < table.Length; ++i)
        {
            uint c = i;
            for (int j = 0; j < 8; ++j)
            {
                c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        return table;
    }

    public static uint Compute(byte[] data, int offset, int count)
    {
        uint c = 0xFFFFFFFFu;
        int end = offset + count;
        for (int i = offset; i < end; ++i)
        {
            c = Table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
        }
        return c ^ 0xFFFFFFFFu;
    }
}
"@
}

function ConvertTo-PngUInt32Be {
    param([Parameter(Mandatory = $true)][uint32]$Value)

    $bytes = [System.BitConverter]::GetBytes($Value)
    [Array]::Reverse($bytes)
    return $bytes
}

function Read-PngUInt32Be {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset
    )

    return ([uint32]$Bytes[$Offset] -shl 24) -bor
        ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
        [uint32]$Bytes[$Offset + 3]
}

function New-PngSmokeFixtureWithPadding {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][int]$PaddingBytes
    )

    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "PNG smoke source not found: $SourcePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $SourcePath))
    if ($bytes.Length -lt 33) {
        throw "PNG smoke source is too small to be valid: $SourcePath"
    }

    $signature = 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    for ($i = 0; $i -lt $signature.Length; ++$i) {
        if ($bytes[$i] -ne $signature[$i]) {
            throw "PNG smoke source is not a valid PNG: $SourcePath"
        }
    }

    $iendOffset = -1
    for ($offset = 8; $offset + 12 -le $bytes.Length; ) {
        $chunkLength = Read-PngUInt32Be -Bytes $bytes -Offset $offset
        $chunkType = [System.Text.Encoding]::ASCII.GetString($bytes, $offset + 4, 4)
        $chunkEnd = $offset + 12 + [int]$chunkLength
        if ($chunkEnd -gt $bytes.Length) {
            throw "PNG smoke source has a truncated chunk: $SourcePath"
        }
        if ($chunkType -eq "IEND") {
            $iendOffset = $offset
            break
        }
        $offset = $chunkEnd
    }

    if ($iendOffset -lt 0) {
        throw "PNG smoke source does not contain an IEND chunk: $SourcePath"
    }

    $padding = New-Object byte[] $PaddingBytes
    for ($i = 0; $i -lt $padding.Length; ++$i) {
        $padding[$i] = [byte]0x41
    }

    $chunkTypeBytes = [System.Text.Encoding]::ASCII.GetBytes("tEXt")
    $keywordBytes = [System.Text.Encoding]::ASCII.GetBytes("Comment")
    $chunkData = New-Object byte[] ($keywordBytes.Length + 1 + $padding.Length)
    [Array]::Copy($keywordBytes, 0, $chunkData, 0, $keywordBytes.Length)
    $chunkData[$keywordBytes.Length] = 0
    [Array]::Copy($padding, 0, $chunkData, $keywordBytes.Length + 1, $padding.Length)

    $crcInput = New-Object byte[] ($chunkTypeBytes.Length + $chunkData.Length)
    [Array]::Copy($chunkTypeBytes, 0, $crcInput, 0, $chunkTypeBytes.Length)
    [Array]::Copy($chunkData, 0, $crcInput, $chunkTypeBytes.Length, $chunkData.Length)
    $crc = [GxosPngCrc32]::Compute($crcInput, 0, $crcInput.Length)

    $chunkBytes = New-Object byte[] (12 + $chunkData.Length)
    [Array]::Copy((ConvertTo-PngUInt32Be -Value ([uint32]$chunkData.Length)), 0, $chunkBytes, 0, 4)
    [Array]::Copy($chunkTypeBytes, 0, $chunkBytes, 4, 4)
    [Array]::Copy($chunkData, 0, $chunkBytes, 8, $chunkData.Length)
    [Array]::Copy((ConvertTo-PngUInt32Be -Value $crc), 0, $chunkBytes, 8 + $chunkData.Length, 4)

    $outputBytes = New-Object byte[] ($iendOffset + $chunkBytes.Length + ($bytes.Length - $iendOffset))
    [Array]::Copy($bytes, 0, $outputBytes, 0, $iendOffset)
    [Array]::Copy($chunkBytes, 0, $outputBytes, $iendOffset, $chunkBytes.Length)
    [Array]::Copy($bytes, $iendOffset, $outputBytes, $iendOffset + $chunkBytes.Length, $bytes.Length - $iendOffset)

    $outputDir = Split-Path -Parent $TargetPath
    if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    }
    [System.IO.File]::WriteAllBytes($TargetPath, $outputBytes)
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
    "Wallpaper2",
    "merlin",
    "merlin2",
    "greenmedow",
    "cpu",
    "mountains"
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
    "merlin"        = @{ Full = "merlin.gxi";   Thumb = "merlin_t.gxi" }
    "merlin2"       = @{ Full = "merlin2.gxi";  Thumb = "merlin2_t.gxi" }
    "greenmedow"    = @{ Full = "greenmedow.gxi"; Thumb = "greenmedow_t.gxi" }
    "cpu"           = @{ Full = "cpu.gxi";      Thumb = "cpu_t.gxi" }
    "mountains"     = @{ Full = "mountains.gxi"; Thumb = "mountains_t.gxi" }
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

function Write-ResizedPngFile([string]$SourcePath, [string]$TargetPath, [int]$MaxWidth = 0, [int]$MaxHeight = 0) {
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
        $bitmap.Save($TargetPath, [System.Drawing.Imaging.ImageFormat]::Png)
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
    # The wallpaper pack stages a fairly large number of image + thumbnail
    # files, so use a slightly larger cluster here to keep the fixed directory
    # block from overflowing as the asset list grows.
    $sectorsPerCluster = 16
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
    $hasApps = $false
    $hasPacman = $false
    $hasPacmanBin = $false
    $hasPacmanAmd64 = $false
    $hasPacmanResources = $false
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
            '^Apps$' {
                $hasApps = $true
                break
            }
            '^Apps/PacMan$' {
                $hasApps = $true
                $hasPacman = $true
                break
            }
            '^Apps/PacMan/bin$' {
                $hasApps = $true
                $hasPacman = $true
                $hasPacmanBin = $true
                break
            }
            '^Apps/PacMan/bin/amd64$' {
                $hasApps = $true
                $hasPacman = $true
                $hasPacmanBin = $true
                $hasPacmanAmd64 = $true
                break
            }
            '^Apps/PacMan/resources$' {
                $hasApps = $true
                $hasPacman = $true
                $hasPacmanResources = $true
                break
            }
            default {
                throw "Unexpected staged file path for ramdisk image: $relativePath"
            }
        }
        $readDeadline = (Get-Date).AddSeconds(10)
        do {
            try {
                $bytes = [System.IO.File]::ReadAllBytes($fullPath)
                break
            } catch [System.IO.IOException] {
                if ((Get-Date) -ge $readDeadline) {
                    throw
                }
                Start-Sleep -Milliseconds 200
            }
        } while ($true)
        $pendingFiles += [pscustomobject]@{
            Name = $file.Name
            FullName = $fullPath
            RelativePath = $relativePath
            Directory = $directory
            # Keep the staged file metadata from the original enumeration so the
            # generator does not depend on a second filesystem lookup during smoke.
            Size = [uint32]$bytes.Length
            Bytes = $bytes
        }
    }

    $certsCluster = $null
    $configCluster = $null
    $configCertsCluster = $null
    $configNavigatorCluster = $null
    $appsCluster = $null
    $pacmanCluster = $null
    $pacmanBinCluster = $null
    $pacmanAmd64Cluster = $null
    $pacmanResourcesCluster = $null
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
    if ($hasApps) {
        $appsCluster = $nextCluster++
        $fat[$appsCluster] = 0x0FFFFFFF
    }
    if ($hasPacman) {
        $pacmanCluster = $nextCluster++
        $fat[$pacmanCluster] = 0x0FFFFFFF
    }
    if ($hasPacmanBin) {
        $pacmanBinCluster = $nextCluster++
        $fat[$pacmanBinCluster] = 0x0FFFFFFF
    }
    if ($hasPacmanAmd64) {
        $pacmanAmd64Cluster = $nextCluster++
        $fat[$pacmanAmd64Cluster] = 0x0FFFFFFF
    }
    if ($hasPacmanResources) {
        $pacmanResourcesCluster = $nextCluster++
        $fat[$pacmanResourcesCluster] = 0x0FFFFFFF
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
            Bytes = $pending.Bytes
            Cluster = [uint32]$start
        }
    }

    $imageOpenDeadline = (Get-Date).AddSeconds(10)
    do {
        try {
            $stream = [System.IO.File]::Open($ImagePath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::ReadWrite)
            break
        } catch [System.IO.IOException] {
            if ((Get-Date) -ge $imageOpenDeadline) {
                throw
            }
            Start-Sleep -Milliseconds 200
        }
    } while ($true)
    try {
        $stream.SetLength($SizeMB * 1024 * 1024)
        $sector = New-Object byte[] $bytesPerSector
        $sector[0] = 0xEB; $sector[1] = 0x58; $sector[2] = 0x90
        Write-Ascii $sector 3 "GUIDEXOS" 8
        Write-U16LE $sector 11 $bytesPerSector
        $sector[13] = [byte]$sectorsPerCluster
        Write-U16LE $sector 14 $reservedSectors
        $sector[16] = [byte]$fatCount
        $sector[21] = 0xF8
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
        if ($null -ne $appsCluster) {
            Add-DirectoryRecord $rootEntries "Apps" (Get-ShortName "Apps" $usedRoot) 0x10 $appsCluster 0
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
        $appsEntries = if ($null -ne $appsCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedApps = @{}
        $pacmanEntries = if ($null -ne $pacmanCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedPacman = @{}
        $pacmanBinEntries = if ($null -ne $pacmanBinCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedPacmanBin = @{}
        $pacmanAmd64Entries = if ($null -ne $pacmanAmd64Cluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedPacmanAmd64 = @{}
        $pacmanResourcesEntries = if ($null -ne $pacmanResourcesCluster) { New-Object 'System.Collections.Generic.List[byte[]]' } else { $null }
        $usedPacmanResources = @{}

        if ($null -ne $configEntries) {
            if ($null -ne $configCertsCluster) {
                Add-DirectoryRecord $configEntries "certs" (Get-ShortName "certs" $usedConfig) 0x10 $configCertsCluster 0
            }
            if ($null -ne $configNavigatorCluster) {
                Add-DirectoryRecord $configEntries "navigator" (Get-ShortName "navigator" $usedConfig) 0x10 $configNavigatorCluster 0
            }
        }
        if ($null -ne $appsEntries -and $null -ne $pacmanCluster) {
            Add-DirectoryRecord $appsEntries "PacMan" (Get-ShortName "PacMan" $usedApps) 0x10 $pacmanCluster 0
        }
        if ($null -ne $pacmanEntries) {
            if ($null -ne $pacmanBinCluster) {
                Add-DirectoryRecord $pacmanEntries "bin" (Get-ShortName "bin" $usedPacman) 0x10 $pacmanBinCluster 0
            }
            if ($null -ne $pacmanResourcesCluster) {
                Add-DirectoryRecord $pacmanEntries "resources" (Get-ShortName "resources" $usedPacman) 0x10 $pacmanResourcesCluster 0
            }
        }
        if ($null -ne $pacmanBinEntries -and $null -ne $pacmanAmd64Cluster) {
            Add-DirectoryRecord $pacmanBinEntries "amd64" (Get-ShortName "amd64" $usedPacmanBin) 0x10 $pacmanAmd64Cluster 0
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
                "Apps/PacMan" {
                    Add-DirectoryRecord $pacmanEntries $record.Name (Get-ShortName $record.Name $usedPacman) 0x20 $record.Cluster $record.Size
                    break
                }
                "Apps/PacMan/bin/amd64" {
                    Add-DirectoryRecord $pacmanAmd64Entries $record.Name (Get-ShortName $record.Name $usedPacmanAmd64) 0x20 $record.Cluster $record.Size
                    break
                }
                "Apps/PacMan/resources" {
                    Add-DirectoryRecord $pacmanResourcesEntries $record.Name (Get-ShortName $record.Name $usedPacmanResources) 0x20 $record.Cluster $record.Size
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
            @($configNavigatorCluster, $configNavigatorEntries),
            @($appsCluster, $appsEntries),
            @($pacmanCluster, $pacmanEntries),
            @($pacmanBinCluster, $pacmanBinEntries),
            @($pacmanAmd64Cluster, $pacmanAmd64Entries),
            @($pacmanResourcesCluster, $pacmanResourcesEntries)
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
            $data = $record.Bytes
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
$appsDir = Join-Path $OutputDir "Apps"
foreach ($stagingDir in @($wallpaperDir, $certsDir, $configDir, $appsDir)) {
    if (-not (Test-Path -LiteralPath $stagingDir)) { continue }

    $removeDeadline = (Get-Date).AddSeconds(10)
    do {
        try {
            Remove-Item -LiteralPath $stagingDir -Recurse -Force -ErrorAction Stop
            break
        } catch [System.IO.IOException] {
            if ((Get-Date) -ge $removeDeadline) {
                throw
            }
            Start-Sleep -Milliseconds 200
        }
    } while ($true)
}
New-Item -ItemType Directory -Force -Path $wallpaperDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputImage) | Out-Null

if (-not [string]::IsNullOrWhiteSpace($NativePackageDir)) {
    $nativePackageFiles = @(
        @{ Relative = "app.json"; Label = "manifest" },
        @{ Relative = "bin\amd64\pacman.elf"; Label = "AMD64 Native ELF" },
        @{ Relative = "resources\level1.gximg"; Label = "level asset" },
        @{ Relative = "resources\pacpics.gximg"; Label = "sprite asset" }
    )
    $nativeStageRoot = Join-Path $appsDir "PacMan"
    foreach ($nativeFile in $nativePackageFiles) {
        $source = Join-Path $NativePackageDir $nativeFile.Relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Missing PacMan $($nativeFile.Label) for ramdisk package: $source"
        }
        $target = Join-Path $nativeStageRoot $nativeFile.Relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target -Force
        $staged += Get-Item -LiteralPath $target
        Write-Host "      staged /Apps/PacMan/$($nativeFile.Relative.Replace('\', '/')) in ramdisk" -ForegroundColor Yellow
    }
}

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

$configNavigatorDir = Join-Path $configDir "navigator"
New-Item -ItemType Directory -Force -Path $configNavigatorDir | Out-Null

$navigatorChromeAssets = [ordered]@{
    "nav-back.png"      = "assets\Images\NuoveXT\PNG\32\above_thearrow_10194.png"
    "nav-next.png"      = "assets\Images\NuoveXT\PNG\32\Next_arrow_10211.png"
    "reload.png"        = "assets\Images\NuoveXT\PNG\32\refresh_arrow_10190.png"
    "nav-home.png"      = "assets\Images\NuoveXT\PNG\32\gohome_action_ir_10235.png"
    "marks.png"         = "assets\Images\NuoveXT\PNG\32\markers_list_add_favorites_10275.png"
    "nav-add.png"       = "assets\Images\NuoveXT\PNG\32\edit_add_10261.png"
}
for ($frame = 0; $frame -lt 12; $frame++) {
    $navigatorChromeAssets[("surfer-{0:D2}.png" -f $frame)] =
        ("assets\Images\SurfThrobber\PNG\surfer_{0:D2}.png" -f $frame)
}
foreach ($asset in $navigatorChromeAssets.GetEnumerator()) {
    $source = Join-Path $RootDir $asset.Value
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        Write-Host "      Navigator chrome asset missing; graceful fallback will be used: $($asset.Value)" -ForegroundColor DarkYellow
        continue
    }
    $target = Join-Path $configNavigatorDir $asset.Key
    Copy-Item -LiteralPath $source -Destination $target -Force
    $staged += Get-Item $target
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
if (-not [string]::IsNullOrWhiteSpace($ImageViewerRuntimeSmokePath)) {
    $targetRuntimeSmokePath = Join-Path $configNavigatorDir "imageviewer-runtime-smoke-path.txt"
    [System.IO.File]::WriteAllText($targetRuntimeSmokePath, $ImageViewerRuntimeSmokePath, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetRuntimeSmokePath
    $targetRuntimeSmokePathCompat = Join-Path $configNavigatorDir "IMGRTPTH.TXT"
    [System.IO.File]::WriteAllText($targetRuntimeSmokePathCompat, $ImageViewerRuntimeSmokePath, [System.Text.Encoding]::ASCII)
    $staged += Get-Item $targetRuntimeSmokePathCompat
    Write-Host "      staged Image Viewer runtime smoke path at /config/navigator/imageviewer-runtime-smoke-path.txt ($ImageViewerRuntimeSmokePath)" -ForegroundColor Yellow
}

foreach ($name in $WallpaperNames) {
    $fullSource = Join-Path $InputDir "$name.png"
    if (-not (Test-Path $fullSource)) {
        throw "Missing expected wallpaper asset: $fullSource"
    }
    $thumbSource = Join-Path $InputDir "${name}_thumb.png"
    $hasDedicatedThumb = Test-Path $thumbSource
    if (-not $hasDedicatedThumb) {
        $thumbSource = $fullSource
    }

    foreach ($suffix in @("", "_thumb")) {
        $pngName = "$name$suffix.png"
        $source = if ($suffix -eq "_thumb") { $thumbSource } else { $fullSource }
        $targetPng = Join-Path $wallpaperDir $pngName
        if ($suffix -eq "_thumb" -and -not $hasDedicatedThumb) {
            Write-ResizedPngFile $source $targetPng -MaxWidth 160 -MaxHeight 120
        } else {
            Copy-Item $source $targetPng -Force
        }
        $staged += Get-Item $targetPng

        $aliases = $BareMetalAliases[$name]
        if (-not $aliases) {
            throw "Missing 8.3 bare-metal alias for wallpaper: $name"
        }
        $gximgName = if ($suffix -eq "_thumb") { $aliases.Thumb } else { $aliases.Full }
        $targetGximg = Join-Path $wallpaperDir $gximgName
        if ($suffix -eq "_thumb") {
            if ($hasDedicatedThumb) {
                Write-GximgFile $source $targetGximg
            } else {
                Write-GximgFile $source $targetGximg -MaxWidth 160 -MaxHeight 120
            }
        } else {
            Write-GximgFile $source $targetGximg -MaxWidth 800 -MaxHeight 600
        }
        $staged += Get-Item $targetGximg
    }
}

# Deterministic large PNG fixture for the bare-metal Image Viewer smoke.
# Keep it above the old 512 KiB cap, but base it on a known-good PNG and pad it
# with harmless metadata so the bare-metal decoder sees the same image content.
$largeSmokeFixtureSource = Join-Path $RootDir "assets\Images\BlueVelvet\16\image.png"
if (-not (Test-Path -LiteralPath $largeSmokeFixtureSource -PathType Leaf)) {
    throw "Missing expected large Image Viewer smoke PNG fixture source: $largeSmokeFixtureSource"
}
$largeSmokeFixtureTarget = Join-Path $wallpaperDir "arrowbgx.png"
New-PngSmokeFixtureWithPadding -SourcePath $largeSmokeFixtureSource -TargetPath $largeSmokeFixtureTarget -PaddingBytes 700KB
$staged += Get-Item $largeSmokeFixtureTarget
Write-Host "      staged large Image Viewer smoke PNG fixture at /system/wall/arrowbgx.png (padded valid PNG)" -ForegroundColor Yellow

$imageViewerSmokeFixtureSource = Join-Path $RootDir "assets\Images\BlueVelvet\16\image.png"
if (-not (Test-Path -LiteralPath $imageViewerSmokeFixtureSource -PathType Leaf)) {
    throw "Missing expected Image Viewer smoke PNG fixture source: $imageViewerSmokeFixtureSource"
}
# Keep the runtime smoke on a short 8.3-style PNG path so the bare-metal VFS sees it consistently.
$imageViewerSmokeFixtureTarget = Join-Path $wallpaperDir "ivsmoke.png"
Copy-Item -LiteralPath $imageViewerSmokeFixtureSource -Destination $imageViewerSmokeFixtureTarget -Force
$staged += Get-Item $imageViewerSmokeFixtureTarget
Write-Host "      staged Image Viewer smoke PNG fixture at /system/wall/ivsmoke.png" -ForegroundColor Yellow

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
