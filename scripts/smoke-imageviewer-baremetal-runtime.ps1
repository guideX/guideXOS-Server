param(
    [switch]$SkipBuild,
    [int]$TimeoutSeconds = 240,
    [string]$AssetPath = "/system/wall/ivsmoke.png",
    [string]$FallbackPath = "/system/wall/imageviewer-runtime-smoke-placeholder.png",
    [string]$SmokeLabel = "runtime",
    [switch]$CloseReopen,
    [switch]$StrictLargePng
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$smokeLabel = if ([string]::IsNullOrWhiteSpace($SmokeLabel)) { "runtime" } else { $SmokeLabel.Trim() }
$assetPath = if ([string]::IsNullOrWhiteSpace($AssetPath)) { "/system/wall/ivsmoke.png" } else { $AssetPath.Trim() }
$fallbackPath = if ([string]::IsNullOrWhiteSpace($FallbackPath)) { "/system/wall/imageviewer-runtime-smoke-placeholder.png" } else { $FallbackPath.Trim() }
$closeReopen = $CloseReopen.IsPresent
$strictLargePng = $StrictLargePng.IsPresent
$serialLog = Join-Path $LogDir "imageviewer-baremetal-$smokeLabel-$stamp.serial.log"
$evidencePath = Join-Path $LogDir "imageviewer-baremetal-$smokeLabel.evidence.txt"

function Invoke-KernelBuildForSmoke {
    param([string]$ExtraCFlags)

    $oldExtra = $env:EXTRA_CFLAGS
    if ($ExtraCFlags) {
        $env:EXTRA_CFLAGS = $ExtraCFlags
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }

    foreach ($objectName in @("main.o", "desktop.o", "kernel_apps.o")) {
        Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter $objectName -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }

    Push-Location $Root
    try {
        & (Join-Path $Root "build-kernel.bat")
        $buildCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if ($null -ne $oldExtra) {
        $env:EXTRA_CFLAGS = $oldExtra
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }

    if ($buildCode -ne 0) {
        throw "Kernel build failed with exit code $buildCode."
    }
}

$script:activeSmokeBuild = $false
function Restore-NormalKernelBuild {
    if ($script:activeSmokeBuild) {
        Write-Host "Restoring normal kernel build without imageviewer runtime smoke diagnostics..."
        Invoke-KernelBuildForSmoke ""
        $script:activeSmokeBuild = $false
    }
}

function Find-Qemu {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) { return $qemu.Source }
    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Find-Ovmf {
    foreach ($candidate in @(
        (Join-Path $Root "OVMF.fd"),
        (Join-Path $Root "ovmf.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

function Test-SerialLogContains {
    param(
        [string]$Path,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }

    $content = Get-Content -LiteralPath $Path -Raw
    if ($null -eq $content) { return $false }
    return $content.Contains($Pattern)
}

function Write-EvidenceFile {
    param(
        [string]$Result,
        [string]$AssetPath,
        [string]$SelectedMode,
        [string]$LaunchPath,
        [bool]$LaunchOk,
        [string]$LoadPath,
        [string]$LoadStatus,
        [string]$LoadDimensions,
        [string]$PaintMode,
        [string]$PaintPath,
        [string]$PaintDimensions,
        [string]$PaintStatus,
        [string]$SerialLogPath
    )

    $head = (git -C $Root rev-parse HEAD).Trim()
    $lines = @(
        "[ImageViewerBareMetalRuntimeSmoke]",
        "evidenceVersion=1",
        "repo=$Root",
        "head=$head",
        "timestampUnixMs=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())",
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "result=$Result",
        "assetPath=$AssetPath",
        "selectedMode=$SelectedMode",
        "launchPath=$LaunchPath",
        "launchResult=$(if ($LaunchOk) { 'PASS' } else { 'FAIL' })",
        "loadPath=$LoadPath",
        "loadStatus=$LoadStatus",
        "loadDimensions=$LoadDimensions",
        "paintMode=$PaintMode",
        "paintPath=$PaintPath",
        "paintDimensions=$PaintDimensions",
        "paintStatus=$PaintStatus",
        "serialLog=$SerialLogPath"
    )
    Set-Content -LiteralPath $evidencePath -Value $lines -Encoding ASCII
}

function Invoke-ImageViewerSmokeBuild {
    param(
        [switch]$BuildRamdiskIfNeeded
    )

    $bootloader = Join-Path $Root "ESP\EFI\BOOT\BOOTX64.EFI"
    $ramdisk = Join-Path $Root "ESP\ramdisk.img"

    if (-not (Test-Path -LiteralPath $bootloader) -or -not (Test-Path -LiteralPath $ramdisk)) {
        if (-not $BuildRamdiskIfNeeded) {
            throw "Bootloader or ramdisk image missing. Run .\build.bat first or omit -SkipBuild."
        }

        Write-Host "Building full project to refresh the bootloader and ramdisk image..."
        Push-Location $Root
        try {
            & cmd.exe /c "`"$Root\build.bat`""
            if ($LASTEXITCODE -ne 0) {
                throw "build.bat failed with exit code $LASTEXITCODE"
            }
        } finally {
            Pop-Location
        }
    }

    $wallpaperPackScript = Join-Path $Root "scripts\generate-wallpaper-pack.ps1"
    if (-not (Test-Path -LiteralPath $wallpaperPackScript)) {
        throw "Wallpaper pack generator not found: $wallpaperPackScript"
    }

    # Always restage the runtime wallpaper pack for the selected smoke asset so
    # the runtime config path cannot leak between smoke runs.
    Write-Host "Refreshing wallpaper pack for Image Viewer runtime smoke..."
    Push-Location $Root
    try {
        & powershell -ExecutionPolicy Bypass -File $wallpaperPackScript `
            -InputDir (Join-Path $Root "assets\Backgrounds") `
            -OutputDir (Join-Path $Root "out\wallpaper-pack") `
            -OutputImage $ramdisk `
            -ImageViewerRuntimeSmokePath $assetPath
        if ($LASTEXITCODE -ne 0) {
            throw "Wallpaper pack regeneration failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }

    Write-Host "Building kernel with active imageviewer runtime smoke diagnostics..."
    $oldSkipWallpaperBuild = $env:GXOS_SKIP_WALLPAPER_RUNTIME_IMAGE_BUILD
    # The smoke script already stages the wallpaper runtime image for the
    # selected asset, including the runtime smoke config file. Reuse that
    # staged image during build so build.ps1 does not regenerate a default
    # wallpaper pack and overwrite the custom target path.
    $env:GXOS_SKIP_WALLPAPER_RUNTIME_IMAGE_BUILD = "1"

    try {
        $kernelSmokeFlags = "-DGXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE"
        if ($smokeLabel -eq "large-png") {
            $kernelSmokeFlags += " -DGXOS_IMAGEVIEWER_RUNTIME_SMOKE_FORCE_ARROWBGX"
        }
        if ($closeReopen) {
            $kernelSmokeFlags += " -DGXOS_IMAGEVIEWER_RUNTIME_SMOKE_CLOSE_REOPEN_ACTIVE"
        }
        Invoke-KernelBuildForSmoke $kernelSmokeFlags
    } finally {
        if ($null -ne $oldSkipWallpaperBuild) {
            $env:GXOS_SKIP_WALLPAPER_RUNTIME_IMAGE_BUILD = $oldSkipWallpaperBuild
        } else {
            Remove-Item Env:\GXOS_SKIP_WALLPAPER_RUNTIME_IMAGE_BUILD -ErrorAction SilentlyContinue
        }
    }
    $script:activeSmokeBuild = $true
}

if (-not $SkipBuild) {
    Invoke-ImageViewerSmokeBuild -BuildRamdiskIfNeeded
} else {
    Invoke-ImageViewerSmokeBuild
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }

$ovmf = Find-Ovmf
if (-not $ovmf) { throw "OVMF image not found." }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
$ramdisk = Join-Path $Root "ESP\ramdisk.img"
if (-not (Test-Path -LiteralPath $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run .\build.bat first or omit -SkipBuild."
}
if (-not (Test-Path -LiteralPath $ramdisk)) {
    throw "ESP/ramdisk.img not found. Run .\build.bat first or omit -SkipBuild."
}

$nvVars = Join-Path $esp "NvVars"
if (Test-Path -LiteralPath $nvVars) {
    # Reset OVMF's persistent variable store so the smoke always boots the
    # freshly staged BOOTX64.EFI instead of inheriting stale NVRAM state.
    Remove-Item -LiteralPath $nvVars -Force
}

$proc = $null
try {
    $args = @(
        "-machine", "pc",
        "-drive", "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
        "-drive", "file=fat:rw:`"$esp`",format=raw,if=ide,index=0",
        "-m", "512M",
        "-vga", "std",
        "-display", "none",
        "-serial", "file:`"$serialLog`"",
        "-no-reboot"
    )

    $proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $launchOk = $false
    $loadSeen = $false
    $paintSeen = $false
    $resultSeen = $false
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $serialLog) {
            $observedOutput = Get-Content -LiteralPath $serialLog -Raw
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] launch app=ImageViewer path=.* result=PASS') {
                $launchOk = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] load path=.* sizeBytes=.* dims=\d+x\d+ status=\S+') {
                $loadSeen = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] paint=(png|placeholder) path=.* dims=\d+x\d+ status=\S+') {
                $paintSeen = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] result=PASS') {
                $resultSeen = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] result=FAIL') {
                break
            }
            if ($launchOk -and $loadSeen -and $paintSeen -and $resultSeen) {
                break
            }
        }
    }

    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $proc.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
} finally {
    Start-Sleep -Milliseconds 300
    Restore-NormalKernelBuild
}

$output = if (Test-Path $serialLog) { Get-Content $serialLog -Raw } else { "" }
Write-Host $output

$lines = if (Test-Path $serialLog) { Get-Content -LiteralPath $serialLog } else { @() }
$assetLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] asset path=") } | Select-Object -First 1
$loadLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] load path=") } | Select-Object -First 1
$launchLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] launch app=ImageViewer ") } | Select-Object -First 1
$paintLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] paint=") } | Select-Object -First 1
$reopenLaunchLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] reopen launch app=ImageViewer path=") } | Select-Object -First 1
$closeLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] close first app=ImageViewer result=") } | Select-Object -First 1
$loadCount = ($lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] load path=") }).Count
$paintCount = ($lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] paint=") }).Count

$assetLine = $null
$loadLine = $null
$launchLine = $null
$paintLine = $null
if ($assetLineText -and $assetLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] asset path=(?<assetPath>.*?) exists=(?<exists>PASS|FAIL) selectedMode=(?<selectedMode>png|placeholder) launchPath=(?<launchPath>.*)$') {
    $assetLine = [pscustomobject]@{
        AssetPath = $Matches.assetPath.Trim()
        Exists = $Matches.exists.Trim()
        SelectedMode = $Matches.selectedMode.Trim()
        LaunchPath = $Matches.launchPath.Trim()
    }
}
if ($loadLineText -and $loadLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] load path=(?<loadPath>.*?) sizeBytes=(?<sizeBytes>.*?) dims=(?<loadWidth>\d+)x(?<loadHeight>\d+) status=(?<loadStatus>\S+)$') {
    $loadLine = [pscustomobject]@{
        LoadPath = $Matches.loadPath.Trim()
        SizeBytes = $Matches.sizeBytes.Trim()
        LoadWidth = [int]$Matches.loadWidth.Trim()
        LoadHeight = [int]$Matches.loadHeight.Trim()
        LoadStatus = $Matches.loadStatus.Trim()
    }
}
if ($launchLineText -and $launchLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] launch app=ImageViewer path=(?<launchPath>.*?) result=(?<result>PASS|FAIL)$') {
    $launchLine = [pscustomobject]@{
        LaunchPath = $Matches.launchPath.Trim()
        Result = $Matches.result.Trim()
    }
}
if ($paintLineText -and $paintLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] paint=(?<paintMode>png|placeholder) path=(?<paintPath>.*?) dims=(?<paintWidth>\d+)x(?<paintHeight>\d+) status=(?<paintStatus>\S+)$') {
    $paintLine = [pscustomobject]@{
        PaintMode = $Matches.paintMode.Trim()
        PaintPath = $Matches.paintPath.Trim()
        PaintWidth = [int]$Matches.paintWidth.Trim()
        PaintHeight = [int]$Matches.paintHeight.Trim()
        PaintStatus = $Matches.paintStatus.Trim()
    }
}
$reopenLaunchLine = $null
if ($reopenLaunchLineText -and $reopenLaunchLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] reopen launch app=ImageViewer path=(?<reopenPath>.*?) result=(?<result>PASS|FAIL)$') {
    $reopenLaunchLine = [pscustomobject]@{
        ReopenPath = $Matches.reopenPath.Trim()
        Result = $Matches.result.Trim()
    }
}
$closeLine = $null
if ($closeLineText -and $closeLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] close first app=ImageViewer result=(?<result>PASS|FAIL)$') {
    $closeLine = [pscustomobject]@{
        Result = $Matches.result.Trim()
    }
}

$startMarker = Test-SerialLogContains -Path $serialLog -Pattern "[IMAGEVIEWER-RUNTIME-SMOKE] start"
$resultPass = Test-SerialLogContains -Path $serialLog -Pattern "[IMAGEVIEWER-RUNTIME-SMOKE] result=PASS"
$resultFail = Test-SerialLogContains -Path $serialLog -Pattern "[IMAGEVIEWER-RUNTIME-SMOKE] result=FAIL"
$launchPass = $launchLine -and $launchLine.Result -eq "PASS"
$selectedModePass = $assetLine -and $assetLine.Exists -eq "PASS" -and $assetLine.SelectedMode -eq "png" -and $assetLine.LaunchPath -eq $assetLine.AssetPath
$loadPass = $null -ne $loadLine -and $loadLine.LoadPath -eq $assetPath -and $loadLine.LoadStatus -eq "Loaded" -and $loadLine.LoadWidth -gt 0 -and $loadLine.LoadHeight -gt 0
$paintPass = $null -ne $paintLine -and $paintLine.PaintMode -eq "png" -and $paintLine.PaintStatus -eq "Loaded" -and $paintLine.PaintPath -eq $assetPath -and $paintLine.PaintWidth -gt 0 -and $paintLine.PaintHeight -gt 0
$closeReopenPass = $true
if ($closeReopen) {
    $closeReopenPass = $null -ne $closeLine -and $closeLine.Result -eq "PASS" -and
        $null -ne $reopenLaunchLine -and $reopenLaunchLine.Result -eq "PASS" -and
        $reopenLaunchLine.ReopenPath -eq "/system/wall/arrowbgx.png" -and
        $loadCount -ge 2
}
$selectedMode = if ($assetLine) { $assetLine.SelectedMode } else { "unknown" }
$expectedLaunchPath = if ($assetLine) { $assetLine.LaunchPath } else { "" }
$loadStatus = if ($loadLine) { $loadLine.LoadStatus } else { "" }
$loadDimensions = if ($loadLine) { "$($loadLine.LoadWidth)x$($loadLine.LoadHeight)" } else { "" }
$paintMode = if ($paintLine) { $paintLine.PaintMode } else { "missing" }
$paintPath = if ($paintLine) { $paintLine.PaintPath } else { "" }
$paintDimensions = if ($paintLine) { "$($paintLine.PaintWidth)x$($paintLine.PaintHeight)" } else { "" }
$paintStatus = if ($paintLine) { $paintLine.PaintStatus } else { "" }
$strictFailureReason = $null
if ($strictLargePng) {
    if ($output.Contains("TooLarge")) {
        $strictFailureReason = "The strict large-PNG smoke observed TooLarge in the runtime evidence."
    } elseif ($output.Contains("OutOfMemory")) {
        $strictFailureReason = "The strict large-PNG smoke observed OutOfMemory in the runtime evidence."
    } elseif ($output.Contains("paint=placeholder")) {
        $strictFailureReason = "The strict large-PNG smoke observed a placeholder paint path in the runtime evidence."
    } elseif ($output.Contains("status=NotFound")) {
        $strictFailureReason = "The strict large-PNG smoke observed NotFound in the runtime evidence."
    }
}
$overallPass = $startMarker -and $launchPass -and $selectedModePass -and $loadPass -and $paintPass -and $resultPass -and (-not $resultFail) -and ($null -eq $strictFailureReason) -and $closeReopenPass
if ($closeReopen) {
    $overallPass = $overallPass -and $loadCount -ge 2
}

Write-EvidenceFile `
    -Result $(if ($overallPass) { "PASS" } else { "FAIL" }) `
    -AssetPath $(if ($assetLine) { $assetLine.AssetPath } else { $assetPath }) `
    -SelectedMode $selectedMode `
    -LaunchPath $(if ($launchLine) { $launchLine.LaunchPath } else { $expectedLaunchPath }) `
    -LaunchOk $launchPass `
    -LoadStatus $loadStatus `
    -LoadDimensions $loadDimensions `
    -LoadPath $(if ($loadLine) { $loadLine.LoadPath } else { "" }) `
    -PaintMode $paintMode `
    -PaintPath $paintPath `
    -PaintDimensions $paintDimensions `
    -PaintStatus $paintStatus `
    -SerialLogPath $serialLog

if ($overallPass) {
    Write-Host "Image Viewer bare-metal $smokeLabel smoke PASS. Serial log: $serialLog"
    Write-Host "Image Viewer bare-metal $smokeLabel evidence: $evidencePath"
    exit 0
}

Write-Host "Image Viewer bare-metal $smokeLabel smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
Write-Host "Image Viewer bare-metal $smokeLabel evidence: $evidencePath" -ForegroundColor Red
if ($assetLine) {
    Write-Host "Expected PNG fixture: $assetPath; fallback placeholder: $fallbackPath"
    Write-Host "Observed selectedMode=$($assetLine.SelectedMode) launchPath=$($assetLine.LaunchPath) exists=$($assetLine.Exists)"
}
if ($closeReopen) {
    Write-Host "Close-reopen validation: loadCount=$loadCount paintCount=$paintCount closeResult=$(if ($closeLine) { $closeLine.Result } else { 'missing' }) reopenPath=$(if ($reopenLaunchLine) { $reopenLaunchLine.ReopenPath } else { 'missing' })"
}
if ($paintLine) {
    Write-Host "Observed paintMode=$($paintLine.PaintMode) paintPath=$($paintLine.PaintPath) paintDimensions=$paintDimensions paintStatus=$($paintLine.PaintStatus)"
}
if ($resultFail) {
    Write-Host "The runtime smoke reported a failure result in the kernel log." -ForegroundColor Red
} else {
    if ($strictFailureReason) {
        Write-Host $strictFailureReason -ForegroundColor Red
    } elseif ($assetLine -and $assetLine.SelectedMode -ne "png") {
        Write-Host "The runtime smoke fell back to the placeholder path instead of the guaranteed PNG fixture." -ForegroundColor Red
    } elseif ($loadLine -and $loadLine.LoadStatus -ne "Loaded") {
        Write-Host "The runtime smoke did not report a Loaded PNG load result." -ForegroundColor Red
    } elseif ($paintLine -and $paintLine.PaintStatus -ne "Loaded") {
        Write-Host "The runtime smoke did not report a Loaded PNG paint result." -ForegroundColor Red
    } else {
        Write-Host "The runtime smoke did not reach the expected launch/paint/result markers before timeout or shutdown." -ForegroundColor Red
    }
}
exit 1
