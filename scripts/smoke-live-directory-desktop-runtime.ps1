param(
    [switch]$Build,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "live-directory-desktop-runtime-$stamp.serial.log"
$evidencePath = Join-Path $LogDir "live-directory-desktop-runtime.evidence.txt"

function Invoke-KernelBuildForSmoke {
    param([string]$ExtraCFlags)

    $oldExtra = $env:EXTRA_CFLAGS
    if ($ExtraCFlags) {
        $env:EXTRA_CFLAGS = $ExtraCFlags
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }

    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "main.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "desktop.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    & (Join-Path $Root "build-kernel.bat")
    $buildCode = $LASTEXITCODE

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
        Write-Host "Restoring normal kernel build without live-directory runtime smoke diagnostics..."
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
        [bool]$StartMarker,
        [bool]$FolderActivation,
        [bool]$CompactLayout,
        [bool]$BackNavigation,
        [bool]$ShellSync,
        [bool]$GoHome,
        [bool]$Cleanup,
        [string]$SerialLogPath
    )

    $head = (git -C $Root rev-parse HEAD).Trim()
    $lines = @(
        "[LiveDirectoryDesktopRuntimeSmoke]",
        "evidenceVersion=1",
        "repo=$Root",
        "head=$head",
        "timestampUnixMs=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())",
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "result=$Result",
        "startPathMarker=$(if ($StartMarker) { 'PASS' } else { 'FAIL' })",
        "folderActivation=$(if ($FolderActivation) { 'PASS' } else { 'FAIL' })",
        "compactLayout=$(if ($CompactLayout) { 'PASS' } else { 'FAIL' })",
        "backNavigation=$(if ($BackNavigation) { 'PASS' } else { 'FAIL' })",
        "shellCdSync=$(if ($ShellSync) { 'PASS' } else { 'FAIL' })",
        "goHome=$(if ($GoHome) { 'PASS' } else { 'FAIL' })",
        "cleanup=$(if ($Cleanup) { 'PASS' } else { 'FAIL' })",
        "serialLog=$SerialLogPath"
    )
    Set-Content -LiteralPath $evidencePath -Value $lines -Encoding ASCII
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }

$ovmf = Find-Ovmf
if (-not $ovmf) { throw "OVMF image not found." }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
if (-not (Test-Path $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run build-kernel.bat first or pass -Build."
}

Write-Host "Building kernel with live-directory runtime smoke diagnostics..."
Invoke-KernelBuildForSmoke "-DGXOS_LIVE_DIRECTORY_DESKTOP_RUNTIME_SMOKE_ACTIVE"
$script:activeSmokeBuild = $true

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

$proc = $null
try {
    $proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-SerialLogContains -Path $serialLog -Pattern "[LIVE-DIRECTORY-RUNTIME-SMOKE] result=PASS") {
            break
        }
        if (Test-SerialLogContains -Path $serialLog -Pattern "[LIVE-DIRECTORY-RUNTIME-SMOKE] result=FAIL") {
            break
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

$startMarker = $output.Contains("[LIVE-DIRECTORY-RUNTIME-SMOKE] startPath=/Desktop home=/Desktop")
$folderActivation = $output -match '\[LIVE-DIRECTORY-RUNTIME-SMOKE\] LIVE_DESKTOP_NAV from=/Desktop to=/system/wall source=folder-activation result=PASS'
$compactLayout = $output -match '\[LIVE-DIRECTORY-RUNTIME-SMOKE\] LIVE_DESKTOP_LAYOUT compact=1 path=/system/wall'
$backNavigation = $output -match '\[LIVE-DIRECTORY-RUNTIME-SMOKE\] LIVE_DESKTOP_NAV_BACK to=/Desktop result=PASS'
$shellSync = $output -match '\[LIVE-DIRECTORY-RUNTIME-SMOKE\] LIVE_DESKTOP_SHELL_CD_SYNC cwd=/system/wall result=PASS'
$goHome = $output -match '\[LIVE-DIRECTORY-RUNTIME-SMOKE\] LIVE_DESKTOP_NAV_HOME to=/Desktop result=PASS'
$cleanup = $output -match '\[LIVE-DIRECTORY-RUNTIME-SMOKE\] LIVE_DESKTOP_CLEANUP path=/system/wall result=PASS'
$resultPass = $output.Contains("[LIVE-DIRECTORY-RUNTIME-SMOKE] result=PASS")
$overallPass = $startMarker -and $folderActivation -and $compactLayout -and $backNavigation -and $shellSync -and $goHome -and $cleanup -and $resultPass

Write-EvidenceFile `
    -Result $(if ($overallPass) { "PASS" } else { "FAIL" }) `
    -StartMarker $startMarker `
    -FolderActivation $folderActivation `
    -CompactLayout $compactLayout `
    -BackNavigation $backNavigation `
    -ShellSync $shellSync `
    -GoHome $goHome `
    -Cleanup $cleanup `
    -SerialLogPath $serialLog

if ($overallPass) {
    Write-Host "Live-directory desktop runtime smoke PASS. Serial log: $serialLog"
    Write-Host "Live-directory desktop runtime evidence: $evidencePath"
    exit 0
}

Write-Host "Live-directory desktop runtime smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
Write-Host "Live-directory desktop runtime evidence: $evidencePath" -ForegroundColor Red
if ($output.Contains("[LIVE-DIRECTORY-RUNTIME-SMOKE] result=FAIL")) {
    Write-Host "The kernel reported a live-directory smoke failure." -ForegroundColor Red
} else {
    Write-Host "The runtime smoke did not reach the final PASS marker before timeout or shutdown." -ForegroundColor Red
}
exit 1
