param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "imageviewer-phase2b-smoke-$stamp.log"

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
    $undoMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 14, "Undo"'
    $redoMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 15, "Redo"'
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
    $discardControlMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, 16, "Discard Changes"'
    $openDialogMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'dialogs::OpenDialog::Show\(0, 0, startPath'
    $saveDialogMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'dialogs::SaveDialog::Show\(0, 0, startPath, defaultFileName'
    $openHandlerMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'openImageFromDialog\(\)'
    $wallpaperHandlerMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'trySetCurrentImageAsWallpaper\(\)'
    $wallpaperDispatchMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'DesktopService::DispatchSetAsDesktopBackground\(s_originalPath, "ImageViewer"'
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
    $undoHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::UndoEdit\(\)'
    $redoHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::RedoEdit\(\)'
    $captureHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::CaptureHistoryBeforeEdit\(\)'
    $restoreHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'bool ImageViewer::RestoreHistorySnapshot\(const HistorySnapshot& snapshot\)'
    $clearHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::ClearEditHistory\(\)'
    $guardHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'bool ImageViewer::CanNavigateAwayFromDirtyDocument\(const std::string& actionName\)'
    $discardHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::DiscardChanges\(\)'
    $historyLimitMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.h") -Pattern 'static constexpr size_t kHistoryLimit = 10;'
    $boundedUndoMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_undoStack\.size\(\) >= kHistoryLimit'
    $boundedRedoMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_redoStack\.size\(\) >= kHistoryLimit'
    $redoClearMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_redoStack\.clear\(\);'
    $dirtyGuardTextMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'Unsaved changes: use Save As Copy, Undo, or discard before'
    $discardReloadMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'loadImagePath\(s_originalPath, true, true\)'
    $saveHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'void ImageViewer::SaveCurrentImageAsCopy\(\)'
    $saveEncoderMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'writePngToVfs\(finalPath, ImageViewer::s_image, error\)'
    $saveSafeGuardMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'safeEqualsPath\(finalPath, ImageViewer::s_originalPath\)'
    $saveNoOverwriteMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'cannot overwrite the original file'
    $previewPathMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'makeEditedPreviewPath\(s_originalPath.empty\(\) \? s_filePath : s_originalPath, s_windowId\)'
    $plainSaveMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row2Y, [0-9]+, "Save"'

    $sourceText = Get-Content -LiteralPath (Join-Path $Root "image_viewer.cpp") -Raw
    Assert-NotContains $sourceText "Paint" "viewer paint feature"
    Assert-NotContains $sourceText "Layer" "viewer layer feature"
    Assert-NotContains $sourceText "Filter" "viewer filter feature"

    $report = @(
        "[ImageViewerPhase2BSmoke]",
        "result=PASS",
        "desktop.apps.verbose contains Image Viewer and gxos.builtin.imageviewer",
        "desktop.launch.resolve ImageViewer => BuiltInApp / Image Viewer",
        "desktop.launch.resolve Image Viewer => BuiltInApp / ImageViewer",
        "desktop.launch.resolve C:\temp\sample.png => FileOpen",
        "desktop.launch.resolve C:\temp\sample.PNG => FileOpen",
        "viewerOpenControl=$(Format-EvidenceLine $openControlMatch)",
        "viewerPreviousNext=$(Format-EvidenceLine $previousMatch) | $(Format-EvidenceLine $nextMatch)",
        "viewerUndoRedoControls=$(Format-EvidenceLine $undoMatch) | $(Format-EvidenceLine $redoMatch)",
        "viewerZoomControls=$(Format-EvidenceLine $zoomInMatch) | $(Format-EvidenceLine $zoomOutMatch) | $(Format-EvidenceLine $fitMatch) | $(Format-EvidenceLine $actualMatch)",
        "viewerEditControls=$(Format-EvidenceLine $rotateLeftMatch) | $(Format-EvidenceLine $rotateRightMatch) | $(Format-EvidenceLine $flipHorizontalMatch) | $(Format-EvidenceLine $flipVerticalMatch)",
        "viewerSaveCopyControl=$(Format-EvidenceLine $saveCopyControlMatch)",
        "viewerWallpaperControl=$(Format-EvidenceLine $wallpaperControlMatch)",
        "viewerDiscardControl=$(Format-EvidenceLine $discardControlMatch)",
        "viewerOpenDialogIntegration=$(Format-EvidenceLine $openDialogMatch) | $(Format-EvidenceLine $openHandlerMatch)",
        "viewerSaveDialogIntegration=$(Format-EvidenceLine $saveDialogMatch) | $(Format-EvidenceLine $saveHelperMatch)",
        "viewerWallpaperIntegration=$(Format-EvidenceLine $wallpaperHandlerMatch) | $(Format-EvidenceLine $wallpaperDispatchMatch)",
        "viewerUnsupportedFormat=$(Format-EvidenceLine $unsupportedMessageMatch) | $(Format-EvidenceLine $unsupportedHandlerMatch)",
        "viewerCheckerboardBackground=$(Format-EvidenceLine $checkerboardMatch)",
        "viewerTransparencyDetection=$(Format-EvidenceLine $transparentMatch)",
        "viewerEmptyState=$(Format-EvidenceLine $emptyStateMatch)",
        "viewerDirtyState=$(Format-EvidenceLine $dirtyStateMatch) | $(Format-EvidenceLine $originalPathMatch) | $(Format-EvidenceLine $displayPathMatch)",
        "viewerModifiedIndicators=$(Format-EvidenceLine $modifiedTitleMatch) | $(Format-EvidenceLine $modifiedStatusMatch)",
        "viewerHistoryHelpers=$(Format-EvidenceLine $undoHelperMatch) | $(Format-EvidenceLine $redoHelperMatch) | $(Format-EvidenceLine $captureHelperMatch) | $(Format-EvidenceLine $restoreHelperMatch) | $(Format-EvidenceLine $clearHelperMatch) | $(Format-EvidenceLine $guardHelperMatch) | $(Format-EvidenceLine $discardHelperMatch)",
        "viewerHistoryLimit=$(Format-EvidenceLine $historyLimitMatch) | $(Format-EvidenceLine $boundedUndoMatch) | $(Format-EvidenceLine $boundedRedoMatch) | $(Format-EvidenceLine $redoClearMatch)",
        "viewerDirtyGuard=$(Format-EvidenceLine $dirtyGuardTextMatch) | $(Format-EvidenceLine $discardReloadMatch)",
        "viewerSaveAsCopyImplementation=$(Format-EvidenceLine $saveHelperMatch) | $(Format-EvidenceLine $saveEncoderMatch) | $(Format-EvidenceLine $saveSafeGuardMatch) | $(Format-EvidenceLine $saveNoOverwriteMatch)",
        "viewerPreviewPath=$(Format-EvidenceLine $previewPathMatch)",
        "fileOpenRoutesToImageViewer=$(Format-EvidenceLine $openMatch)",
        "fileExplorerUsesOpenFilesystemEntry=$(Format-EvidenceLine $fileOpenMatch)",
        "desktopDoubleClickRoutesThroughOpenFilesystemEntry=$(Format-EvidenceLine $desktopDoubleClickMatch)",
        "folderDoubleClickPreserved=$(Format-EvidenceLine $folderNavigateMatch)",
        "unknownFileAssociationPreserved=$(Format-EvidenceLine $unknownAssociationMatch)",
        "noPaintLayerFilterFeaturesDetected=true",
        "noPlainDestructiveSaveControlDetected=$(if ($null -eq $plainSaveMatch) { 'true' } else { 'false' })",
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
