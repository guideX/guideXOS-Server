# Runs the opt-in bare-metal file-operation smoke through the production VFS,
# clipboard, desktop-path, and folder-creation command path.

param(
    [int]$TimeoutSeconds = 120,
    [switch]$TrashOnly
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "file-operations-runtime-$stamp.serial.log"

function Find-Qemu {
    $command = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Find-Ovmf {
    foreach ($candidate in @(
        (Join-Path $Root "OVMF.fd"),
        (Join-Path $Root "ovmf.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Build-KernelWithFlags([string]$flags) {
    $oldFlags = $env:EXTRA_CFLAGS
    if ($flags) { $env:EXTRA_CFLAGS = $flags }
    else { Remove-Item Env:EXTRA_CFLAGS -ErrorAction SilentlyContinue }
    try {
        & (Join-Path $Root "build-kernel.bat")
        if ($LASTEXITCODE -ne 0) { throw "Kernel build failed with exit code $LASTEXITCODE." }
    } finally {
        if ($null -eq $oldFlags) { Remove-Item Env:EXTRA_CFLAGS -ErrorAction SilentlyContinue }
        else { $env:EXTRA_CFLAGS = $oldFlags }
    }
}

function Get-LastPasteMarker([string]$output) {
    if (-not $output) { return "none" }
    $marker = $output -split "`r?`n" |
        Where-Object { $_ -match "FPASTE_|LFPASTE_|KERNEL-HEARTBEAT|\[FILE-OPS-RUNTIME-SMOKE\] phase=" } |
        Select-Object -Last 1
    if (-not $marker) { return "none" }
    return $marker.Trim()
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }
$ovmf = Find-Ovmf
if (-not $ovmf) { throw "OVMF image not found." }

$esp = Join-Path $Root "ESP"
if (-not (Test-Path -LiteralPath (Join-Path $esp "EFI\BOOT\BOOTX64.EFI"))) {
    throw "ESP bootloader missing; run the normal build first."
}

$activeSmokeBuild = $false
try {
    $smokeFlags = "-DGXOS_FILE_OPERATIONS_RUNTIME_SMOKE_ACTIVE"
    if ($TrashOnly) {
        $smokeFlags += " -DGXOS_FILE_OPERATIONS_TRASH_RUNTIME_SMOKE_ACTIVE"
    }
    Build-KernelWithFlags $smokeFlags
    $activeSmokeBuild = $true

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
    $process = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $serialLog) {
            $output = Get-Content -LiteralPath $serialLog -Raw
            if ($null -eq $output) { $output = "" }
            if ($output.Contains("[FILE-OPS-RUNTIME-SMOKE] result=PASS") -or
                $output.Contains("[FILE-OPS-RUNTIME-SMOKE] result=FAIL")) { break }
        }
    }
    $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
    }

    $output = if (Test-Path -LiteralPath $serialLog) { Get-Content -LiteralPath $serialLog -Raw } else { "" }
    if ($null -eq $output) { $output = "" }
    Write-Host $output
    if ($output.Contains("[FILE-OPS-RUNTIME-SMOKE] result=PASS")) {
        Write-Host "File-operation runtime smoke PASS. Serial log: $serialLog" -ForegroundColor Green
        exit 0
    }
    $lastMarker = Get-LastPasteMarker $output
    if ($timedOut) {
        Write-Host "[FILE-OPS-RUNTIME-SMOKE] result=FAIL timeout=1 lastStage=$lastMarker" -ForegroundColor Red
    } else {
        $exitCode = if ($process.HasExited) { $process.ExitCode } else { "unknown" }
        Write-Host "[FILE-OPS-RUNTIME-SMOKE] result=FAIL exitCode=$exitCode lastStage=$lastMarker" -ForegroundColor Red
    }
    throw "File-operation runtime smoke did not report PASS. Serial log: $serialLog"
} finally {
    if ($activeSmokeBuild) {
        Write-Host "Restoring normal kernel build..."
        Build-KernelWithFlags ""
    }
}
