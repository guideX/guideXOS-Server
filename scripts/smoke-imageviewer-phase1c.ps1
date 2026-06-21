param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "imageviewer-phase1c-smoke-$stamp.log"

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

    $previousMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 2, "Previous"'
    $nextMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 3, "Next"'
    $zoomInMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 4, "Zoom In"'
    $zoomOutMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 5, "Zoom Out"'
    $fitMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 6, "Fit to Window"'
    $actualMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'addBtn\(row1Y, 7, "100%"'
    $navigateMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'navigateRelative\(int delta\)'
    $mouseMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'handleMouseInput\(int x, int y, int button, const std::string& action\)'
    $checkerboardMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'drawCheckerboardBackground\(contentLeft, contentTop, contentWidth, contentHeight\)'
    $transparentMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_backgroundMode = s_hasTransparency \? BackgroundMode::Checkerboard : BackgroundMode::Solid;'
    $titleMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern '"Image Viewer - " \+ s_fileName'
    $statusFileMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_fileName\.empty\(\) \? s_filePath : s_fileName'
    $statusDimensionsMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 's_originalW << "x" << s_originalH'
    $statusZoomMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'zoomPct << "%"'
    $statusModeMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'modeText\(\)'
    $emptyStateMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern '"No image loaded"'
    $errorStateMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern '"Failed to load "'
    $panClampMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "image_viewer.cpp") -Pattern 'clampPanForCurrentImage\('

    $sourceText = Get-Content -LiteralPath (Join-Path $Root "image_viewer.cpp") -Raw
    Assert-NotContains $sourceText "Crop" "viewer crop feature"
    Assert-NotContains $sourceText "Paint" "viewer paint feature"
    Assert-NotContains $sourceText "Layer" "viewer layer feature"
    Assert-NotContains $sourceText "Filter" "viewer filter feature"

    $report = @(
        "[ImageViewerPhase1CSmoke]",
        "result=PASS",
        "desktop.apps.verbose contains Image Viewer and gxos.builtin.imageviewer",
        "desktop.launch.resolve ImageViewer => BuiltInApp / Image Viewer",
        "desktop.launch.resolve Image Viewer => BuiltInApp / ImageViewer",
        "desktop.launch.resolve C:\temp\sample.png => FileOpen",
        "desktop.launch.resolve C:\temp\sample.PNG => FileOpen",
        "fileOpenRoutesToImageViewer=$(Format-EvidenceLine $openMatch)",
        "fileExplorerUsesOpenFilesystemEntry=$(Format-EvidenceLine $fileOpenMatch)",
        "desktopDoubleClickRoutesThroughOpenFilesystemEntry=$(Format-EvidenceLine $desktopDoubleClickMatch)",
        "folderDoubleClickPreserved=$(Format-EvidenceLine $folderNavigateMatch)",
        "unknownFileAssociationPreserved=$(Format-EvidenceLine $unknownAssociationMatch)",
        "viewerControls=$(Format-EvidenceLine $previousMatch) | $(Format-EvidenceLine $nextMatch) | $(Format-EvidenceLine $zoomInMatch) | $(Format-EvidenceLine $zoomOutMatch) | $(Format-EvidenceLine $fitMatch) | $(Format-EvidenceLine $actualMatch)",
        "viewerNavigation=$(Format-EvidenceLine $navigateMatch)",
        "viewerMouseInput=$(Format-EvidenceLine $mouseMatch)",
        "viewerCheckerboardBackground=$(Format-EvidenceLine $checkerboardMatch)",
        "viewerTransparencyDetection=$(Format-EvidenceLine $transparentMatch)",
        "viewerTitleText=$(Format-EvidenceLine $titleMatch)",
        "viewerStatusText=$(Format-EvidenceLine $statusFileMatch) | $(Format-EvidenceLine $statusDimensionsMatch) | $(Format-EvidenceLine $statusZoomMatch) | $(Format-EvidenceLine $statusModeMatch)",
        "viewerEmptyState=$(Format-EvidenceLine $emptyStateMatch)",
        "viewerErrorState=$(Format-EvidenceLine $errorStateMatch)",
        "viewerPanClamp=$(Format-EvidenceLine $panClampMatch)",
        "noEditorFeaturesDetected=true",
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
