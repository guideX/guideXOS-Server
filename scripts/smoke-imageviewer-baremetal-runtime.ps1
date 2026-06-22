param(
    [switch]$SkipBuild,
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "imageviewer-baremetal-runtime-$stamp.serial.log"
$evidencePath = Join-Path $LogDir "imageviewer-baremetal-runtime.evidence.txt"
$assetPath = "/system/wall/blueflower.png"

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
        [string]$PaintMode,
        [string]$PaintPath,
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
        "paintMode=$PaintMode",
        "paintPath=$PaintPath",
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

    Write-Host "Building kernel with active imageviewer runtime smoke diagnostics..."
    Invoke-KernelBuildForSmoke "-DGXOS_IMAGEVIEWER_BARE_METAL_RUNTIME_SMOKE_ACTIVE"
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
    $paintSeen = $false
    $resultSeen = $false
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $serialLog) {
            $observedOutput = Get-Content -LiteralPath $serialLog -Raw
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] launch app=ImageViewer path=.* result=PASS') {
                $launchOk = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] paint=(png|placeholder) path=.* status=\S+') {
                $paintSeen = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] result=PASS') {
                $resultSeen = $true
            }
            if ($observedOutput -match '\[IMAGEVIEWER-RUNTIME-SMOKE\] result=FAIL') {
                break
            }
            if ($launchOk -and $paintSeen -and $resultSeen) {
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
$launchLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] launch app=ImageViewer ") } | Select-Object -First 1
$paintLineText = $lines | Where-Object { $_.StartsWith("[IMAGEVIEWER-RUNTIME-SMOKE] paint=") } | Select-Object -First 1

$assetLine = $null
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
if ($launchLineText -and $launchLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] launch app=ImageViewer path=(?<launchPath>.*?) result=(?<result>PASS|FAIL)$') {
    $launchLine = [pscustomobject]@{
        LaunchPath = $Matches.launchPath.Trim()
        Result = $Matches.result.Trim()
    }
}
if ($paintLineText -and $paintLineText -match '^\[IMAGEVIEWER-RUNTIME-SMOKE\] paint=(?<paintMode>png|placeholder) path=(?<paintPath>.*?) status=(?<paintStatus>\S+)$') {
    $paintLine = [pscustomobject]@{
        PaintMode = $Matches.paintMode.Trim()
        PaintPath = $Matches.paintPath.Trim()
        PaintStatus = $Matches.paintStatus.Trim()
    }
}

$startMarker = Test-SerialLogContains -Path $serialLog -Pattern "[IMAGEVIEWER-RUNTIME-SMOKE] start"
$resultPass = Test-SerialLogContains -Path $serialLog -Pattern "[IMAGEVIEWER-RUNTIME-SMOKE] result=PASS"
$resultFail = Test-SerialLogContains -Path $serialLog -Pattern "[IMAGEVIEWER-RUNTIME-SMOKE] result=FAIL"
$launchPass = $launchLine -and $launchLine.Result -eq "PASS"
$paintPass = $null -ne $paintLine -and ($paintLine.PaintMode -eq "png" -or $paintLine.PaintMode -eq "placeholder")
$selectedMode = if ($assetLine) { $assetLine.SelectedMode } else { "unknown" }
$expectedLaunchPath = if ($assetLine) { $assetLine.LaunchPath } else { "" }
$paintMode = if ($paintLine) { $paintLine.PaintMode } else { "missing" }
$paintPath = if ($paintLine) { $paintLine.PaintPath } else { "" }
$paintStatus = if ($paintLine) { $paintLine.PaintStatus } else { "" }
$overallPass = $startMarker -and $launchPass -and $paintPass -and $resultPass -and (-not $resultFail)

Write-EvidenceFile `
    -Result $(if ($overallPass) { "PASS" } else { "FAIL" }) `
    -AssetPath $(if ($assetLine) { $assetLine.AssetPath } else { $assetPath }) `
    -SelectedMode $selectedMode `
    -LaunchPath $(if ($launchLine) { $launchLine.LaunchPath } else { $expectedLaunchPath }) `
    -LaunchOk $launchPass `
    -PaintMode $paintMode `
    -PaintPath $paintPath `
    -PaintStatus $paintStatus `
    -SerialLogPath $serialLog

if ($overallPass) {
    Write-Host "Image Viewer bare-metal runtime smoke PASS. Serial log: $serialLog"
    Write-Host "Image Viewer bare-metal runtime evidence: $evidencePath"
    exit 0
}

Write-Host "Image Viewer bare-metal runtime smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
Write-Host "Image Viewer bare-metal runtime evidence: $evidencePath" -ForegroundColor Red
if ($resultFail) {
    Write-Host "The runtime smoke reported a failure result in the kernel log." -ForegroundColor Red
} else {
    Write-Host "The runtime smoke did not reach the expected launch/paint/result markers before timeout or shutdown." -ForegroundColor Red
}
exit 1
