param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "user-background-phase1-smoke-$stamp.log"

function Assert-Contains {
    param([string]$Text, [string]$Needle, [string]$Reason)
    if (-not $Text.Contains($Needle)) {
        throw "Missing expected text for ${Reason}: $Needle"
    }
}

function Assert-NotContains {
    param([string]$Text, [string]$Needle, [string]$Reason)
    if ($Text.Contains($Needle)) {
        throw "Unexpected text for ${Reason}: $Needle"
    }
}

function Assert-Ordered {
    param([string]$Text, [string[]]$Needles, [string]$Reason)
    $previous = -1
    foreach ($needle in $Needles) {
        $current = $Text.IndexOf($needle, [StringComparison]::Ordinal)
        if ($current -lt 0) {
            throw "Missing ordered marker for ${Reason}: $needle"
        }
        if ($current -le $previous) {
            throw "Unexpected ordering for ${Reason}: $needle"
        }
        $previous = $current
    }
}

Push-Location $Root
try {
    $storeHeader = Get-Content -LiteralPath (Join-Path $Root "background_store.h") -Raw
    $storeSource = Get-Content -LiteralPath (Join-Path $Root "background_store.cpp") -Raw
    $serviceHeader = Get-Content -LiteralPath (Join-Path $Root "background_service.h") -Raw
    $serviceSource = Get-Content -LiteralPath (Join-Path $Root "background_service.cpp") -Raw
    $desktopService = Get-Content -LiteralPath (Join-Path $Root "desktop_service.cpp") -Raw
    $desktopMenu = Get-Content -LiteralPath (Join-Path $Root "right_click_menu.cpp") -Raw
    $fileExplorer = Get-Content -LiteralPath (Join-Path $Root "file_explorer.cpp") -Raw
    $imageViewer = Get-Content -LiteralPath (Join-Path $Root "image_viewer.cpp") -Raw
    $displayOptions = Get-Content -LiteralPath (Join-Path $Root "display_options.cpp") -Raw
    $compositor = Get-Content -LiteralPath (Join-Path $Root "compositor.cpp") -Raw
    $server = Get-Content -LiteralPath (Join-Path $Root "server.cpp") -Raw
    $manifestTestDir = Join-Path $Root "tmp\user-background-phase1-test"
    $manifestTestExe = Join-Path $manifestTestDir "background_store_manifest_test.exe"
    $codecTestExe = Join-Path $manifestTestDir "png_codec_test.exe"

    Assert-Contains $storeHeader "kMaxUserBackgroundRecords = 64" "record bound"
    Assert-Contains $storeHeader "kMaxUserBackgroundManifestBytes = 64u * 1024u" "manifest byte bound"
    Assert-Contains $storeHeader "kMaxUserBackgroundManifestLineBytes = 2048u" "manifest line bound"
    Assert-Contains $storeHeader "kUserBackgroundThumbnailMaxWidth = 160" "thumbnail width bound"
    Assert-Contains $storeHeader "kUserBackgroundThumbnailMaxHeight = 120" "thumbnail height bound"
    Assert-Contains $storeHeader "/user-data/backgrounds/manifest.cfg" "manifest path"
    Assert-Contains $storeSource "version != kUserBackgroundManifestVersion" "unsupported version rejection"
    Assert-Contains $storeSource "!ids.insert(record.id).second" "duplicate ID rejection"
    Assert-Contains $storeSource "manifest contains a duplicate field" "duplicate field rejection"
    Assert-Contains $storeSource "manifest has a truncated final line" "truncated manifest rejection"
    Assert-Contains $storeSource "HostStorageDirectory" "stable hosted storage mapping"
    Assert-Contains $storeSource "WallpaperRegistry::FindBackgroundById(record.id)" "built-in collision rejection"
    Assert-Contains $storeSource 'kind == "image" && owner == "user"' "ownership and kind validation"
    Assert-Contains $storeSource "CanonicalThumbnailPath(record.id)" "derived thumbnail path"
    Assert-Contains $storeSource "ValidateOwnedRecordFiles" "missing-file validation"

    Assert-Contains $serviceSource "ImageAdapter::LoadFromBytes" "independent PNG decode validation"
    Assert-Contains $serviceSource "PngCodec::ScaleNearest" "shared thumbnail scaling"
    Assert-Contains $serviceSource "PngCodec::EncodeRgba8" "shared thumbnail encoding"
    Assert-Ordered $serviceSource @(
        "HostStorageDirectory()",
        "FS::writeAll(tempFull",
        "FS::writeAll(tempThumb",
        "FS::readAll(tempFull",
        "FS::renameFile(tempFull",
        "FS::renameFile(tempThumb",
        "replaceManifest(nextRecords",
        "BackgroundStore::Reload(reloadError)",
        "persistSelection(id, error)",
        "notifySelectionChanged(id)"
    ) "import transaction"
    Assert-Contains $serviceSource "previous manifest" "manifest replacement backup"
    Assert-Contains $serviceSource "selection-persistence-failure" "selection persistence diagnostics"
    Assert-Contains $serviceSource "Never use manifest paths as" "safe removal target derivation"
    Assert-Contains $serviceSource "fallback-selection-failure" "active removal fallback diagnostics"
    Assert-Contains $serviceSource "orphaned files remain" "deterministic cleanup failure state"

    Assert-Contains $desktopService "DesktopBackgroundService::kSetAsDesktopBackgroundAction" "typed shell action identity"
    Assert-Contains $desktopMenu "DesktopService::DispatchSetAsDesktopBackground" "Desktop dispatch"
    Assert-Contains $fileExplorer "DesktopService::DispatchSetAsDesktopBackground" "File Explorer dispatch"
    Assert-Contains $imageViewer "DesktopService::DispatchSetAsDesktopBackground" "Image Viewer dispatch"
    Assert-Contains $server "DesktopBackgroundService::ImportAndSetDesktopBackground" "legacy console import redirection"
    Assert-Contains $server "DesktopBackgroundService::RemoveBackground" "diagnostic removal redirection"
    Assert-NotContains $server 'm.type=(uint32_t)gui::MsgType::MT_DesktopWallpaperSet' "legacy direct compositor wallpaper bypass"
    Assert-Contains $desktopMenu "IsSetAsDesktopBackgroundEligible" "Desktop PNG filtering"
    Assert-Contains $fileExplorer "IsSetAsDesktopBackgroundEligible" "File Explorer PNG filtering"
    Assert-Contains $serviceSource "pathHasPngExtension" "case-insensitive extension validation"
    Assert-NotContains $imageViewer "MT_DesktopWallpaperSet" "arbitrary Image Viewer compositor bypass"

    Assert-Contains $displayOptions "BackgroundStore::MergedImageBackgrounds" "merged Display Options inventory"
    Assert-Contains $displayOptions "Remove Background" "removal control"
    Assert-Contains $displayOptions "BackgroundOwner::UserImported" "owner-based removal enablement"
    Assert-Contains $compositor "BackgroundStore::FindById" "registered compositor lookup"
    Assert-Contains $compositor "BackgroundStore::ResolveIdOrDefault" "persisted selection fallback"
    Assert-Contains $compositor "marker=active-fallback" "runtime missing-user fallback persistence"
    Assert-NotContains $compositor "ImageAdapter::LoadFromFile(idOrPath)" "arbitrary compositor path loading"

    New-Item -ItemType Directory -Force -Path $manifestTestDir | Out-Null
    $manifestTestBuild = & cmd.exe /c "g++ -std=c++17 -O2 -DGXOS_BARE_METAL -iquote . tests\background_store_manifest_test.cpp background_store.cpp wallpaper_registry.cpp -o tmp\user-background-phase1-test\background_store_manifest_test.exe 2>&1"
    if ($LASTEXITCODE -ne 0) {
        throw "Manifest test build failed with exit code $LASTEXITCODE`n$($manifestTestBuild -join [Environment]::NewLine)"
    }
    $manifestTestOutput = & $manifestTestExe
    if ($LASTEXITCODE -ne 0 -or -not (($manifestTestOutput -join [Environment]::NewLine).Contains("BackgroundStore manifest tests PASS"))) {
        throw "Manifest test failed`n$($manifestTestOutput -join [Environment]::NewLine)"
    }
    $manifestTestOutputAgain = & $manifestTestExe
    if ($LASTEXITCODE -ne 0) {
        throw "Second manifest test process failed`n$($manifestTestOutputAgain -join [Environment]::NewLine)"
    }
    $firstHash = ($manifestTestOutput | Where-Object { $_ -like "BackgroundStore hash=*" } | Select-Object -First 1)
    $secondHash = ($manifestTestOutputAgain | Where-Object { $_ -like "BackgroundStore hash=*" } | Select-Object -First 1)
    if ([string]::IsNullOrEmpty($firstHash) -or $firstHash -ne $secondHash) {
        throw "Content hash was not stable across independent processes: '$firstHash' vs '$secondHash'"
    }

    $codecTestBuild = & cmd.exe /c "g++ -std=c++17 -O2 -iquote . tests\png_codec_test.cpp png_codec.cpp image.cpp image_adapter.cpp image_renderer.cpp png_loader.cpp jpeg_loader.cpp vfs.cpp logger.cpp -lgdi32 -lmsimg32 -o tmp\user-background-phase1-test\png_codec_test.exe 2>&1"
    if ($LASTEXITCODE -ne 0) {
        throw "PNG codec test build failed with exit code $LASTEXITCODE`n$($codecTestBuild -join [Environment]::NewLine)"
    }
    $codecTestOutput = & $codecTestExe
    if ($LASTEXITCODE -ne 0 -or -not (($codecTestOutput -join [Environment]::NewLine).Contains("PngCodec tests PASS"))) {
        throw "PNG codec test failed`n$($codecTestOutput -join [Environment]::NewLine)"
    }

    $buildCheck = & cmd.exe /c "g++ -std=c++17 -fsyntax-only -iquote . background_store.cpp background_service.cpp png_codec.cpp image_viewer.cpp desktop_service.cpp display_options.cpp file_explorer.cpp right_click_menu.cpp compositor.cpp server.cpp wallpaper_registry.cpp 2>&1"
    if ($LASTEXITCODE -ne 0) {
        throw "Hosted syntax check failed with exit code $LASTEXITCODE`n$($buildCheck -join [Environment]::NewLine)"
    }

    $report = @(
        "[UserBackgroundPhase1Smoke]",
        "result=PASS",
        "manifest-bounds-and-validation=PASS",
        "import-transaction-order=PASS",
        "removal-safety-contract=PASS",
        "manifest-parser-tests=PASS",
        "stable-hash-and-png-codec-tests=PASS",
        "desktop-file-explorer-image-viewer-shared-dispatch=PASS",
        "display-options-merged-inventory-and-owner-removal=PASS",
        "compositor-registered-id-resolution=PASS",
        "hosted-new-translation-unit-syntax=PASS"
    ) -join [Environment]::NewLine

    Set-Content -LiteralPath $SmokeLog -Value ($report + [Environment]::NewLine) -Encoding ASCII
    Write-Output $report
    Write-Host "Smoke log: $SmokeLog"
    exit 0
} finally {
    if ($manifestTestExe -and (Test-Path -LiteralPath $manifestTestExe)) {
        Remove-Item -LiteralPath $manifestTestExe -Force -ErrorAction SilentlyContinue
    }
    if ($codecTestExe -and (Test-Path -LiteralPath $codecTestExe)) {
        Remove-Item -LiteralPath $codecTestExe -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}

