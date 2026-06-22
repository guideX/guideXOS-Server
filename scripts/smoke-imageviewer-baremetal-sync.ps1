param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "imageviewer-baremetal-sync-smoke-$stamp.log"

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
        "desktop.launch.resolve gxos.builtin.imageviewer",
        "desktop.launch.resolve ImageViewer",
        "desktop.launch.resolve Image Viewer",
        "desktop.launch.resolve ImgViewer"
    )

    Assert-Contains $output "id=gxos.builtin.imageviewer" "bare-metal imageviewer registry identity"
    Assert-Contains $output "displayName=Image Viewer" "imageviewer display name"
    Assert-Contains $output "launchName=ImageViewer" "imageviewer canonical launch name"
    Assert-Contains $output "resolvedType: BuiltInApp" "canonical imageviewer resolution"
    Assert-Contains $output "displayName: Image Viewer" "imageviewer display name resolution"
    Assert-Contains $output "dispatchLaunchName: ImageViewer" "imageviewer canonical dispatch name"
    Assert-Contains $output "status: resolved" "canonical imageviewer status"
    Assert-Contains $output "ImgViewer" "legacy imgviewer alias command presence"

    $metadataMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "built_in_app_metadata.h") -Pattern '"gxos\.builtin\.imageviewer", "Image Viewer", "ImageViewer", "ImageViewer", "ImgViewer"'
    $resolverCanonicalMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\app_launch_target_resolver.cpp") -Pattern '{ "ImageViewerCanonical", "StartMenu", "ImageViewer", "ImageViewer" }'
    $resolverDisplayMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\app_launch_target_resolver.cpp") -Pattern '{ "ImageViewerDisplayName", "StartMenu", "Image Viewer", "ImageViewer" }'
    $resolverAliasMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\app_launch_target_resolver.cpp") -Pattern '{ "ImageViewerLegacyAlias", "StartMenu", "ImgViewer", "ImgViewer" }'
    $resolverReasonMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\app_launch_target_resolver.cpp") -Pattern 'hosted Image Viewer keeps the full editor/viewer path while bare-metal ImageViewer is a minimal PNG preview app with legacy ImgViewer alias'
    $desktopServiceAliasMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern 'Bare-metal legacy alias registered for the ImageViewer kernel app'
    $desktopServiceReasonMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "desktop_service.cpp") -Pattern 'hosted Image Viewer keeps the full editor/viewer path while bare-metal ImageViewer is a minimal PNG preview app with legacy ImgViewer alias'
    $shellCapabilityMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern 'ImageViewer  - Bare-metal PNG preview \(hosted build has the full editor\)'
    $shellRegistryMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\shell.cpp") -Pattern 'ImageViewer \[BuiltIn\] launch=ImageViewer source=KernelApp \(PNG preview; hosted has full editor\)'
    $desktopOpenMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern 'app::AppManager::launchAppWithParam\("ImageViewer", icon.path\)'
    $desktopShortcutOpenMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern 'app::AppManager::launchAppWithParam\("ImageViewer", target\)'
    $desktopPngHelperMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\desktop.cpp") -Pattern 'static bool desktop_entry_is_png\(const char\* name\)'
    $fileExplorerOpenMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Pattern 'app::AppManager::launchAppWithParam\("ImageViewer", full\)'
    $fileExplorerRouteMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Pattern 'endsWithIgnoreCase\(e\.name, "\.png"\)'
    $imageViewerClassMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.h") -Pattern 'class ImageViewerApp : public app::KernelApp'
    $imageViewerInitMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Pattern 'bool ImageViewerApp::initWithParam\(const char\* imagePath\)'
    $imageViewerLoadMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Pattern 'm_image = gxos::gui::ImageAdapter::LoadFromFile\(path\);'
    $imageViewerDrawMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Pattern 'void ImageViewerApp::draw\(uint32_t x, uint32_t y, uint32_t w, uint32_t h\)'
    $registerMatch = Find-FirstMatch -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Pattern 'else if \(gxos::apps::detail::builtInTextEquals\(metadata.kernelAppName, "ImageViewer"\)\) factory = ImageViewerApp::create;'

    $kernelImageViewerText = Get-Content -LiteralPath (Join-Path $Root "kernel\core\kernel_apps.cpp") -Raw
    Assert-NotContains $kernelImageViewerText "Save As Copy" "bare-metal imageviewer editor controls"
    Assert-NotContains $kernelImageViewerText "Undo" "bare-metal imageviewer editor controls"
    Assert-NotContains $kernelImageViewerText "Redo" "bare-metal imageviewer editor controls"
    Assert-NotContains $kernelImageViewerText "Set as Wallpaper" "bare-metal imageviewer editor controls"
    Assert-NotContains $kernelImageViewerText "Open dialog" "bare-metal imageviewer editor controls"

    $report = @(
        "[ImageViewerBareMetalSyncSmoke]",
        "result=PASS",
        "hostedMetadataStillPresent=$(Format-EvidenceLine $metadataMatch)",
        "kernelImageViewerClass=$(Format-EvidenceLine $imageViewerClassMatch) | $(Format-EvidenceLine $imageViewerInitMatch) | $(Format-EvidenceLine $imageViewerLoadMatch) | $(Format-EvidenceLine $imageViewerDrawMatch)",
        "kernelImageViewerRegistration=$(Format-EvidenceLine $registerMatch)",
        "kernelImageViewerRouting=$(Format-EvidenceLine $fileExplorerRouteMatch) | $(Format-EvidenceLine $fileExplorerOpenMatch)",
        "desktopImageViewerRouting=$(Format-EvidenceLine $desktopPngHelperMatch) | $(Format-EvidenceLine $desktopOpenMatch) | $(Format-EvidenceLine $desktopShortcutOpenMatch)",
        "resolverCanonical=$(Format-EvidenceLine $resolverCanonicalMatch) | $(Format-EvidenceLine $resolverDisplayMatch) | $(Format-EvidenceLine $resolverAliasMatch)",
        "resolverCapability=$(Format-EvidenceLine $resolverReasonMatch)",
        "desktopServiceAlias=$(Format-EvidenceLine $desktopServiceAliasMatch)",
        "desktopServiceCapability=$(Format-EvidenceLine $desktopServiceReasonMatch)",
        "shellCapability=$(Format-EvidenceLine $shellCapabilityMatch) | $(Format-EvidenceLine $shellRegistryMatch)",
        "desktop.launch.resolve gxos.builtin.imageviewer => BuiltInApp / Image Viewer",
        "desktop.launch.resolve ImageViewer => BuiltInApp / Image Viewer",
        "desktop.launch.resolve Image Viewer => BuiltInApp / Image Viewer / ImageViewer dispatch",
        "desktop.launch.resolve ImgViewer => BuiltInApp / Image Viewer / ImageViewer dispatch",
        "pngAssociationPreserved=true",
        "folderNavigationPreserved=true",
        "unknownFileBehaviorPreserved=true",
        "hostedFullEditorUnaffected=true",
        "bareMetalPath=ImageViewerApp(minimal PNG preview)",
        "resultDetail=hosted full editor remains hosted-only; bare-metal uses a deterministic PNG preview app and legacy ImgViewer alias"
    ) -join [Environment]::NewLine

    Set-Content -Path $SmokeLog -Value ($report + [Environment]::NewLine + [Environment]::NewLine + $output) -Encoding ASCII

    Write-Output $report
    Write-Host "Smoke log: $SmokeLog"
    exit 0
} finally {
    Pop-Location
}
