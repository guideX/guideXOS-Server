#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$PackageRoot = "assets\Fonts\Roboto-v2.138-package"
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $RepositoryRoot
$PackageRoot = if ([IO.Path]::IsPathRooted($PackageRoot)) {
    [IO.Path]::GetFullPath($PackageRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $PackageRoot))
}
$Validator = Join-Path $PSScriptRoot "approved_font_package.psm1"
Import-Module -Name $Validator -Force -ErrorAction Stop

$script:Checks = 0
$script:Failures = 0
$testParent = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot "out\font13-validation"))
$testRoot = Join-Path $testParent ("font13r-package-tests-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

function Assert-Condition([bool]$Condition, [string]$Name) {
    $script:Checks++
    if ($Condition) {
        Write-Host "PASS $Name"
    } else {
        $script:Failures++
        Write-Host "FAIL $Name" -ForegroundColor Red
    }
}

function New-PackageCopy([string]$Name) {
    $destination = Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $PackageRoot -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $destination
    }
    return $destination
}

function Write-Manifest($Root, $Manifest) {
    $json = $Manifest | ConvertTo-Json -Depth 12
    [IO.File]::WriteAllText(
        (Join-Path $Root "ApprovedFontManifest.json"),
        $json + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false)))
}

function Assert-Rejected([string]$Name, [scriptblock]$Action) {
    $script:Checks++
    try {
        & $Action | Out-Null
        $script:Failures++
        Write-Host "FAIL expected rejection: $Name" -ForegroundColor Red
    } catch {
        Write-Host "PASS rejected: $Name"
    }
}

try {
    $approved = Get-ApprovedFontPackage -PackageRoot $PackageRoot
    Assert-Condition ($approved.FontFiles.Count -eq 3) "exactly three approved TrueType faces"
    Assert-Condition ($approved.FontBytes -eq 274072) "package bytes match manifest"
    Assert-Condition ($approved.FontFiles.Count -le 3 -and $approved.FontBytes -le 576KB) "registry and storage bounds"
    Assert-Condition (@($approved.FontFiles | Where-Object { $_.Bytes -gt 192KB }).Count -eq 0) "each face is within 192 KiB"
    Assert-Condition (@(Get-ChildItem -LiteralPath $PackageRoot -File -Force).Count -eq 6) "package has exactly three TTFs and three license/manifest files"
    Assert-Condition ((Get-FileHash -LiteralPath (Join-Path $PackageRoot "LICENSE.txt") -Algorithm SHA256).Hash.ToLowerInvariant() -eq $approved.Manifest.upstream.licenseSha256) "complete upstream license hash matches"

    $copyTarget = Join-Path $testRoot "copy-positive"
    $null = Copy-ApprovedFontPackage -PackageRoot $PackageRoot -Destination $copyTarget
    $copied = @(Get-ChildItem -LiteralPath $copyTarget -File -Force)
    Assert-Condition ($copied.Count -eq 6) "approved files copy as an exact bounded set"
    foreach ($font in $approved.FontFiles) {
        $copyPath = Join-Path $copyTarget $font.FileName
        Assert-Condition ((Get-FileHash -LiteralPath $copyPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $font.Sha256) "copied hash matches: $($font.FileName)"
    }

    $missingRoot = New-PackageCopy "missing-optional-font"
    Remove-Item -LiteralPath (Join-Path $missingRoot "Roboto-v2.138-Italic.ttf") -Force
    $partial = Get-ApprovedFontPackage -PackageRoot $missingRoot -AllowMissingFonts
    Assert-Condition ($partial.FontFiles.Count -eq 2) "missing optional font is omitted without blocking package use"
    Assert-Rejected "missing font in strict qualification mode" {
        Get-ApprovedFontPackage -PackageRoot $missingRoot
    }

    $oversizedRoot = New-PackageCopy "oversized-font"
    [IO.File]::WriteAllBytes((Join-Path $oversizedRoot "Roboto-v2.138-Regular.ttf"), (New-Object byte[] (192KB + 1)))
    Assert-Rejected "oversized candidate" {
        Get-ApprovedFontPackage -PackageRoot $oversizedRoot -AllowMissingFonts
    }

    $unsupportedRoot = New-PackageCopy "unsupported-format"
    $unsupportedPath = Join-Path $unsupportedRoot "Roboto-v2.138-Regular.ttf"
    $unsupportedBytes = [IO.File]::ReadAllBytes($unsupportedPath)
    $unsupportedBytes[0] = 0x4f
    $unsupportedBytes[1] = 0x54
    $unsupportedBytes[2] = 0x54
    $unsupportedBytes[3] = 0x4f
    [IO.File]::WriteAllBytes($unsupportedPath, $unsupportedBytes)
    Assert-Rejected "unsupported OTTO/CFF signature" {
        Get-ApprovedFontPackage -PackageRoot $unsupportedRoot -AllowMissingFonts
    }

    $duplicateIdentityRoot = New-PackageCopy "duplicate-identity"
    $duplicateIdentityManifest = Get-Content -LiteralPath (Join-Path $duplicateIdentityRoot "ApprovedFontManifest.json") -Raw | ConvertFrom-Json
    $duplicateIdentityManifest.fonts[1].style = "Regular"
    Write-Manifest $duplicateIdentityRoot $duplicateIdentityManifest
    Assert-Rejected "duplicate family/style identity" {
        Get-ApprovedFontPackage -PackageRoot $duplicateIdentityRoot -AllowMissingFonts
    }

    $capacityRoot = New-PackageCopy "registry-capacity"
    $capacityManifest = Get-Content -LiteralPath (Join-Path $capacityRoot "ApprovedFontManifest.json") -Raw | ConvertFrom-Json
    $fourth = $capacityManifest.fonts[0] | ConvertTo-Json -Depth 6 | ConvertFrom-Json
    $fourth.fileName = "Roboto-v2.138-Extra.ttf"
    $capacityManifest.fonts += $fourth
    Write-Manifest $capacityRoot $capacityManifest
    Assert-Rejected "fourth registered font face" {
        Get-ApprovedFontPackage -PackageRoot $capacityRoot -AllowMissingFonts
    }

    $pathRoot = New-PackageCopy "invalid-font-path"
    $pathManifest = Get-Content -LiteralPath (Join-Path $pathRoot "ApprovedFontManifest.json") -Raw | ConvertFrom-Json
    $pathManifest.fonts[0].fileName = "../Roboto-v2.138-Regular.ttf"
    Write-Manifest $pathRoot $pathManifest
    Assert-Rejected "invalid manifest path traversal" {
        Get-ApprovedFontPackage -PackageRoot $pathRoot -AllowMissingFonts
    }

    $extraRoot = New-PackageCopy "extra-font"
    [IO.File]::WriteAllBytes((Join-Path $extraRoot "Hidden.ttf"), [byte[]](0, 1, 0, 0))
    Assert-Rejected "hidden/unapproved extra font" {
        Get-ApprovedFontPackage -PackageRoot $extraRoot -AllowMissingFonts
    }

    $boundedRoot = New-PackageCopy "directory-entry-bound"
    [IO.File]::WriteAllBytes((Join-Path $boundedRoot "unlisted-a.txt"), [byte[]](1))
    [IO.File]::WriteAllBytes((Join-Path $boundedRoot "unlisted-b.txt"), [byte[]](2))
    Assert-Rejected "bounded package directory entry count" {
        Get-ApprovedFontPackage -PackageRoot $boundedRoot -AllowMissingFonts
    }

    $licenseRoot = New-PackageCopy "missing-license"
    Remove-Item -LiteralPath (Join-Path $licenseRoot "LICENSE.txt") -Force
    Assert-Rejected "missing complete license file" {
        Get-ApprovedFontPackage -PackageRoot $licenseRoot -AllowMissingFonts
    }

    $caseCollisionRoot = New-PackageCopy "case-collision"
    $caseManifest = Get-Content -LiteralPath (Join-Path $caseCollisionRoot "ApprovedFontManifest.json") -Raw | ConvertFrom-Json
    $caseManifest.fonts[1].fileName = "roboto-v2.138-regular.ttf"
    Write-Manifest $caseCollisionRoot $caseManifest
    Assert-Rejected "duplicate case-insensitive destination identity" {
        Get-ApprovedFontPackage -PackageRoot $caseCollisionRoot -AllowMissingFonts
    }

    Write-Host ("FONT13R package validation: {0} checks, {1} failures" -f $script:Checks, $script:Failures)
    if ($script:Failures -ne 0) { throw "FONT13R package validation failed." }
} finally {
    if (Test-Path -LiteralPath $testRoot -PathType Container) {
        $resolvedParent = (Resolve-Path -LiteralPath $testParent).Path.TrimEnd('\') + '\'
        $resolvedTestRoot = (Resolve-Path -LiteralPath $testRoot).Path
        if ($resolvedTestRoot.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
        } else {
            throw "Refusing to remove test output outside its expected root: $resolvedTestRoot"
        }
    }
}
