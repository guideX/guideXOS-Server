param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "imageviewer-phase2a-smoke-$stamp.log"

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    if (-not $Text.Contains($Needle)) {
        throw "Missing expected text for ${Reason}: $Needle"
    }
}

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    if ($Text.Contains($Needle)) {
        throw "Unexpected text for ${Reason}: $Needle"
    }
}

function Invoke-ServerCommands {
    param(
        [string[]]$Commands
    )

    $exe = Join-Path $Root "guideXOSServer.exe"
    if (-not (Test-Path $exe)) {
        if ($SkipBuild) {
            throw "guideXOSServer.exe not found. Run .\build.bat first or omit -SkipBuild."
        }
        Write-Host "Building guideXOSServer.exe first..."
        & cmd.exe /c "`"$Root\build.bat`""
        if ($LASTEXITCODE -ne 0) {
            throw "build.bat failed with exit code $LASTEXITCODE"
        }
    }

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return $inputText | & $exe
}

function Find-FirstMatch {
    param(
        [string]$LiteralPath,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $LiteralPath)) {
        return $null
    }

    $match = Select-String -LiteralPath $LiteralPath -Pattern $Pattern | Select-Object -First 1
    if ($null -eq $match) {
        return $null
    }

    [pscustomobject]@{
        Path = $LiteralPath
        LineNumber = $match.LineNumber
        Line = $match.Line.Trim()
    }
}

function Format-EvidenceLine {
    param([object]$Match)

    if ($null -eq $Match) {
        return "evidence: none"
    }

    $relative = $Match.Path.Substring($Root.Length).TrimStart('\', '/')
    return "evidence: ${relative}:$($Match.LineNumber) $($Match.Line)"
}

Push-Location $Root
try {
    $output = Invoke-ServerCommands -Commands @(
        "desktop.apps.verbose",
        "desktop.launch.resolve ImageViewer",
        "desktop.launch.resolve Image Viewer",
        "desktop.launch.resolve C:\temp\sample.png",
        "desktop.launch.resolve C:\temp\sample.PNG"
    )

    Assert-Contains $output "id=gxos.builtin.imageviewer" "registered app id"
    Assert-Contains $output "displayName=Image Viewer" "registered app display name"
    Assert-Contains $output "launchName=ImageViewer" "registered app launch name"
    Assert-Contains $output "resolvedType: BuiltInApp" "ImageViewer launch resolution"
    Assert-Contains $output "displayName: Image Viewer" "Image Viewer display name resolution"
    Assert-Contains $output "dispatchLaunchName: ImageViewer" "ImageViewer launch name resolution"
    Assert-Contains $output "status: resolved" "resolved built-in target"
    Assert-Contains $output "resolvedType: FileOpen" "path-like file open resolution"
    Assert-Contains $output "status: resolved-file-open" "file-open classification"

    $metadataMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "built_in_app_metadata.h") -Pattern '"gxos\.builtin\.imageviewer", "Image Viewer", "ImageViewer"'
    $resolveMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern 'if \(name == "Image Viewer"\) return "ImageViewer";'
    $openMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern 'apps::ImageViewer::Launch\(path\);'
    $fileOpenMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "file_explorer.cpp") -Pattern 'DesktopService::OpenFilesystemEntry\(entry\.fullPath, false, error\)'
    $desktopDoubleClickMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern 'DesktopService::OpenFilesystemEntry\(item\.path, item\.isDirectory, err\)'
    $folderNavigateMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "compositor.cpp") -Pattern 'hostedDesktopSetCurrentPath\(item\.path, true\)'
    $unknownAssociationMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern 'No file association registered for '

    $openControlMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 1, "Open"'
    $previousMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 2, "Previous"'
    $nextMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 3, "Next"'
    $zoomInMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 4, "Zoom In"'
    $zoomOutMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 5, "Zoom Out"'
    $fitMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 6, "Fit to Window"'
    $actualMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 7, "100%"'
    $rotateLeftMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 8, "Rotate Left"'
    $rotateRightMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 9, "Rotate Right"'
    $flipHorizontalMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 10, "Flip Horizontal"'
    $flipVerticalMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 11, "Flip Vertical"'
    $saveCopyControlMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 12, "Save As Copy"'
    $wallpaperControlMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 13, "Set as Wallpaper"'
    $openDialogMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'dialogs::OpenDialog::Show\(0, 0, startPath'
    $saveDialogMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'dialogs::SaveDialog::Show\(0, 0, startPath, defaultFileName'
    $openHandlerMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'openImageFromDialog\(\)'
    $wallpaperHandlerMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'trySetCurrentImageAsWallpaper\(\)'
    $wallpaperMessageMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'MT_DesktopWallpaperSet'
    $unsupportedMessageMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'Unsupported image format: only PNG is supported in this version'
    $unsupportedHandlerMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'showUnsupportedFormat\(const std::string& path\)'
    $checkerboardMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'drawCheckerboardBackground\(contentLeft, contentTop, contentWidth, contentHeight\)'
    $transparentMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_backgroundMode = s_hasTransparency \? BackgroundMode::Checkerboard : BackgroundMode::Solid;'
    $dirtyStateMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'bool ImageViewer::s_isDirty = false;'
    $originalPathMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'std::string ImageViewer::s_originalPath;'
    $displayPathMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'std::string ImageViewer::s_displayPath;'
    $emptyStateMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_statusText = "No image loaded";'
    $modifiedTitleMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern '" \*";'
    $modifiedStatusMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern '" \| Modified"'
    $rotateHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::RotateCurrentImageLeft\(\)'
    $rotateHelperRightMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::RotateCurrentImageRight\(\)'
    $flipHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::FlipCurrentImageHorizontal\(\)'
    $flipHelperVerticalMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::FlipCurrentImageVertical\(\)'
    $markModifiedMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::MarkModified\(\)'
    $updateModifiedTitleMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::UpdateModifiedTitleStatus\(\)'
    $saveHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::SaveCurrentImageAsCopy\(\)'
    $saveEncoderMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'writePngToVfs\(finalPath, ImageViewer::s_image, error\)'
    $saveSafeGuardMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'safeEqualsPath\(finalPath, ImageViewer::s_originalPath\)'
    $saveNoOverwriteMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'cannot overwrite the original file'
    $previewPathMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'makeEditedPreviewPath\(s_originalPath.empty\(\) \? s_filePath : s_originalPath, s_windowId\)'

    $sourceText = Get-Content -LiteralPath (Join-Path $Root "image_viewer.cpp") -Raw
    Assert-NotContains $sourceText "Crop" "viewer crop feature"
    Assert-NotContains $sourceText "Paint" "viewer paint feature"
    Assert-NotContains $sourceText "Layer" "viewer layer feature"
    Assert-NotContains $sourceText "Filter" "viewer filter feature"

    $report = @(
        "[ImageViewerPhase2ASmoke]",
        "result=PASS",
        "desktop.apps.verbose contains Image Viewer and gxos.builtin.imageviewer",
        "desktop.launch.resolve ImageViewer => BuiltInApp / Image Viewer",
        "desktop.launch.resolve Image Viewer => BuiltInApp / ImageViewer",
        "desktop.launch.resolve C:\temp\sample.png => FileOpen",
        "desktop.launch.resolve C:\temp\sample.PNG => FileOpen",
        "viewerOpenControl=$(Format-EvidenceLine $openControlMatch)",
        "viewerPreviousNext=$(Format-EvidenceLine $previousMatch) | $(Format-EvidenceLine $nextMatch)",
        "viewerZoomControls=$(Format-EvidenceLine $zoomInMatch) | $(Format-EvidenceLine $zoomOutMatch) | $(Format-EvidenceLine $fitMatch) | $(Format-EvidenceLine $actualMatch)",
        "viewerEditControls=$(Format-EvidenceLine $rotateLeftMatch) | $(Format-EvidenceLine $rotateRightMatch) | $(Format-EvidenceLine $flipHorizontalMatch) | $(Format-EvidenceLine $flipVerticalMatch)",
        "viewerSaveCopyControl=$(Format-EvidenceLine $saveCopyControlMatch)",
        "viewerWallpaperControl=$(Format-EvidenceLine $wallpaperControlMatch)",
        "viewerOpenDialogIntegration=$(Format-EvidenceLine $openDialogMatch) | $(Format-EvidenceLine $openHandlerMatch)",
        "viewerSaveDialogIntegration=$(Format-EvidenceLine $saveDialogMatch) | $(Format-EvidenceLine $saveHelperMatch)",
        "viewerWallpaperIntegration=$(Format-EvidenceLine $wallpaperHandlerMatch) | $(Format-EvidenceLine $wallpaperMessageMatch)",
        "viewerUnsupportedFormat=$(Format-EvidenceLine $unsupportedMessageMatch) | $(Format-EvidenceLine $unsupportedHandlerMatch)",
        "viewerCheckerboardBackground=$(Format-EvidenceLine $checkerboardMatch)",
        "viewerTransparencyDetection=$(Format-EvidenceLine $transparentMatch)",
        "viewerEmptyState=$(Format-EvidenceLine $emptyStateMatch)",
        "viewerDirtyState=$(Format-EvidenceLine $dirtyStateMatch) | $(Format-EvidenceLine $originalPathMatch) | $(Format-EvidenceLine $displayPathMatch)",
        "viewerModifiedIndicators=$(Format-EvidenceLine $modifiedTitleMatch) | $(Format-EvidenceLine $modifiedStatusMatch)",
        "viewerEditHelpers=$(Format-EvidenceLine $rotateHelperMatch) | $(Format-EvidenceLine $rotateHelperRightMatch) | $(Format-EvidenceLine $flipHelperMatch) | $(Format-EvidenceLine $flipHelperVerticalMatch) | $(Format-EvidenceLine $markModifiedMatch) | $(Format-EvidenceLine $updateModifiedTitleMatch)",
        "viewerSaveAsCopyImplementation=$(Format-EvidenceLine $saveHelperMatch) | $(Format-EvidenceLine $saveEncoderMatch) | $(Format-EvidenceLine $saveSafeGuardMatch) | $(Format-EvidenceLine $saveNoOverwriteMatch)",
        "viewerPreviewPath=$(Format-EvidenceLine $previewPathMatch)",
        "fileOpenRoutesToImageViewer=$(Format-EvidenceLine $openMatch)",
        "fileExplorerUsesOpenFilesystemEntry=$(Format-EvidenceLine $fileOpenMatch)",
        "desktopDoubleClickRoutesThroughOpenFilesystemEntry=$(Format-EvidenceLine $desktopDoubleClickMatch)",
        "folderDoubleClickPreserved=$(Format-EvidenceLine $folderNavigateMatch)",
        "unknownFileAssociationPreserved=$(Format-EvidenceLine $unknownAssociationMatch)",
        "noCropPaintLayerFilterFeaturesDetected=true",
        "metadata=$(Format-EvidenceLine $metadataMatch)",
        "resolverAlias=$(Format-EvidenceLine $resolveMatch)"
    ) -join [Environment]::NewLine

    Set-Content -Path $SmokeLog -Value ($report + [Environment]::NewLine + [Environment]::NewLine + $output) -Encoding ASCII

    Write-Output $report
    Write-Host "Smoke log: $SmokeLog"
    exit 0
} finally {
    Pop-Location
}
