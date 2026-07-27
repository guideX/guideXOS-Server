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
$qemuStdoutLog = Join-Path $LogDir "file-operations-runtime-$stamp.qemu.stdout.log"
$qemuStderrLog = Join-Path $LogDir "file-operations-runtime-$stamp.qemu.stderr.log"
$qemuEsp = $null

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
        Where-Object { $_ -match "TRASH_OP_|TRASH_MOVE_|FPASTE_|LFPASTE_|KERNEL-EXCEPTION|KERNEL-HEARTBEAT|\[FILE-OPS-RUNTIME-SMOKE\] phase=" } |
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

    $qemuEsp = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-trash-smoke-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $qemuEsp | Out-Null
    Copy-Item -Path (Join-Path $esp "*") -Destination $qemuEsp -Recurse -Force
    $qemuTrash = Join-Path $qemuEsp "trash"
    if (Test-Path -LiteralPath $qemuTrash) {
        Remove-Item -LiteralPath $qemuTrash -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $qemuTrash | Out-Null
    $qemuDesktop = Join-Path $qemuEsp "desktop"
    if (Test-Path -LiteralPath $qemuDesktop) {
        Get-ChildItem -LiteralPath $qemuDesktop -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^(DELONE|DELTWO|DELTHREE|DELF0[4-9]|DELF10)\.TXT$' } |
            ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
        foreach ($generatedDirectory in @("DELTEST", "DELNESTE", "DELNESTF", "DELMULTI", "DELMULTC", "GXSMK5", "GXSMK6")) {
            $generatedDirectoryPath = Join-Path $qemuDesktop $generatedDirectory
            if (Test-Path -LiteralPath $generatedDirectoryPath) {
                Remove-Item -LiteralPath $generatedDirectoryPath -Recurse -Force
            }
        }
    }

    $args = @(
        "-machine", "pc",
        "-drive", "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
        "-drive", "file=fat:rw:`"$qemuEsp`",format=raw,if=ide,index=0",
        "-m", "512M",
        "-vga", "std",
        "-display", "none",
        "-serial", "file:`"$serialLog`"",
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-no-reboot"
    )
    $process = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $qemuStdoutLog -RedirectStandardError $qemuStderrLog
    $harnessStoppedProcess = $false
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $serialLog) {
            $output = Get-Content -LiteralPath $serialLog -Raw
            if ($null -eq $output) { $output = "" }
            if ($output.Contains("[FILE-OPS-RUNTIME-SMOKE] result=PASS") -or
                $output.Contains("[FILE-OPS-RUNTIME-SMOKE] result=FAIL") -or
                ($TrashOnly -and $output.Contains("[FILE-OPS-TRASH-SMOKE] result=PASS")) -or
                ($TrashOnly -and $output.Contains("[FILE-OPS-TRASH-SMOKE] result=FAIL"))) { break }
        }
    }
    $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    if (-not $process.HasExited) {
        $harnessStoppedProcess = $true
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
    }

    $output = if (Test-Path -LiteralPath $serialLog) { Get-Content -LiteralPath $serialLog -Raw } else { "" }
    if ($null -eq $output) { $output = "" }
    $qemuOutput = ""
    if (Test-Path -LiteralPath $qemuStdoutLog) { $qemuOutput += Get-Content -LiteralPath $qemuStdoutLog -Raw }
    if (Test-Path -LiteralPath $qemuStderrLog) { $qemuOutput += Get-Content -LiteralPath $qemuStderrLog -Raw }
    if ($null -eq $qemuOutput) { $qemuOutput = "" }
    Write-Host $output
    $lastMarker = Get-LastPasteMarker ($output + "`n" + $qemuOutput)
    $completedGenerations = @(
        [regex]::Matches($output, "TRASH_OP_FILESYSTEM_COMPLETE gen=([^\s]+)") |
            ForEach-Object { $_.Groups[1].Value }
    ) | Sort-Object -Unique
    $sequentialPassCount = ([regex]::Matches(
        $output, "\[FILE-OPS-TRASH-SMOKE\] case=sequential-file index=.* result=PASS")).Count
    $requiredMarkers = @(
        "TRASH_OP_FILESYSTEM_COMPLETE",
        "TRASH_OP_REFRESH_COMPLETE",
        "TRASH_OP_TEMPORARIES_RELEASED",
        "TRASH_OP_STATE_IDLE",
        "TRASH_OP_RETURNED"
    )
    $missingMarkers = if ($TrashOnly) {
        @($requiredMarkers | Where-Object { -not $output.Contains($_) })
    } else {
        @()
    }
    $debugExitInvoked = $qemuOutput -match "debug-exit|isa-debug-exit|debug exit"
    $exceptionSeen = $output -match "KERNEL-EXCEPTION|NATIVE-ELF-FAULT|PageFault|GPF|Double fault|panic|assertion" -or
        $qemuOutput -match "assertion|Bail out|abort|reset|shutdown"
    $exitCode = if ($harnessStoppedProcess) { "harness-stopped" } elseif ($process.HasExited) { $process.ExitCode } else { "unknown" }
    Write-Host "[FILE-OPS-RUNTIME-SMOKE] exitCode=$exitCode lastTrashMarker=$lastMarker completedGenerations=$($completedGenerations.Count) sequentialCompleted=$sequentialPassCount debugExitInvoked=$debugExitInvoked exceptionOrResetSeen=$exceptionSeen"
    Write-Host "[FILE-OPS-RUNTIME-SMOKE] serialLog=$serialLog qemuStdout=$qemuStdoutLog qemuStderr=$qemuStderrLog"
    $topLevelPass = $output.Contains("[FILE-OPS-RUNTIME-SMOKE] result=PASS") -or
        ($TrashOnly -and $output.Contains("[FILE-OPS-TRASH-SMOKE] result=PASS"))
    $sequentialRequirement = -not $TrashOnly -or $sequentialPassCount -ge 10
    $generationRequirement = -not $TrashOnly -or $completedGenerations.Count -ge 10
    $smokePass = $topLevelPass -and
        $sequentialRequirement -and $missingMarkers.Count -eq 0 -and
        $generationRequirement -and -not $exceptionSeen
    if ($smokePass) {
        Write-Host "File-operation runtime smoke PASS. Serial log: $serialLog" -ForegroundColor Green
        exit 0
    }
    if ($missingMarkers.Count -gt 0) {
        Write-Host "[FILE-OPS-RUNTIME-SMOKE] missingMarkers=$($missingMarkers -join ',')" -ForegroundColor Red
    }
    if ($timedOut) {
        Write-Host "[FILE-OPS-RUNTIME-SMOKE] result=FAIL timeout=1 lastStage=$lastMarker" -ForegroundColor Red
    } else {
        $exitCode = if ($harnessStoppedProcess) { "harness-stopped" } elseif ($process.HasExited) { $process.ExitCode } else { "unknown" }
        Write-Host "[FILE-OPS-RUNTIME-SMOKE] result=FAIL exitCode=$exitCode lastStage=$lastMarker" -ForegroundColor Red
    }
    throw "File-operation runtime smoke did not report PASS. Serial log: $serialLog"
} finally {
    if ($qemuEsp -and (Test-Path -LiteralPath $qemuEsp)) {
        Remove-Item -LiteralPath $qemuEsp -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($activeSmokeBuild) {
        Write-Host "Restoring normal kernel build..."
        Build-KernelWithFlags ""
    }
}
