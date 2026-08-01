# Run the opt-in trash-folder smoke image and retain bounded QEMU diagnostics.

param(
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "trash-folder-$stamp.serial.log"
$qemuStdout = Join-Path $LogDir "trash-folder-$stamp.qemu.stdout.log"
$qemuStderr = Join-Path $LogDir "trash-folder-$stamp.qemu.stderr.log"
$qemuDebug = Join-Path $LogDir "trash-folder-$stamp.qemu.debug.log"
$imageRoot = Join-Path $LogDir "trash-folder-image-$stamp"

function Find-Qemu {
    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }
$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
if (-not (Test-Path -LiteralPath $ovmf)) { throw "OVMF code image not found: $ovmf" }
if (-not (Test-Path -LiteralPath (Join-Path $Root "ESP\kernel.elf"))) {
    throw "ESP/kernel.elf is missing; stage the trash-folder smoke kernel first."
}
Copy-Item -LiteralPath (Join-Path $Root "ESP") -Destination $imageRoot -Recurse -Force

$args = @(
    "-machine", "pc",
    "-drive", "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
    "-drive", "file=fat:rw:`"$imageRoot`",format=raw,if=ide,index=0",
    "-m", "512M",
    "-vga", "std",
    "-display", "none",
    "-serial", "file:`"$serialLog`"",
    "-d", "guest_errors,int,cpu_reset",
    "-D", $qemuDebug,
    "-no-reboot",
    "-no-shutdown"
)

$process = Start-Process -FilePath $qemu -ArgumentList $args -RedirectStandardOutput $qemuStdout -RedirectStandardError $qemuStderr -PassThru -WindowStyle Hidden
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$guestMarker = $false
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    $process.Refresh()
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $serialLog) {
        $output = Get-Content -LiteralPath $serialLog -Raw
        if ($output -match "\[FILE-OPS-TRASH-SMOKE\] result=(PASS|FAIL)") {
            $guestMarker = $true
            break
        }
    }
}

$timedOut = -not $process.HasExited -and -not $guestMarker
$harnessStopped = $false
if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
    $harnessStopped = $true
}
$process.Refresh()
$exitCode = if ($process.HasExited) { $process.ExitCode } else { "unknown" }
$serialText = if (Test-Path -LiteralPath $serialLog) { Get-Content -LiteralPath $serialLog -Raw } else { "" }
$qemuErrorText = if (Test-Path -LiteralPath $qemuStderr) { Get-Content -LiteralPath $qemuStderr -Raw } else { "" }
$finalMarker = ($serialText -split "`r?`n" |
    Where-Object { $_ -match "TRASH_DIR_|FILE-OPS-TRASH-SMOKE|KERNEL-EXCEPTION|NATIVE-ELF-FAULT|KERNEL-FAULT|PANIC|RESET|SHUTDOWN" } |
    Select-Object -Last 1)
$guestResult = if ($serialText -match "\[FILE-OPS-TRASH-SMOKE\] result=(PASS|FAIL)") { $Matches[1] } else { "NONE" }

Write-Host "TRASH_FOLDER_QEMU_SERIAL=$serialLog"
Write-Host "TRASH_FOLDER_QEMU_IMAGE=$imageRoot"
Write-Host "TRASH_FOLDER_QEMU_STDOUT=$qemuStdout"
Write-Host "TRASH_FOLDER_QEMU_STDERR=$qemuStderr"
Write-Host "TRASH_FOLDER_QEMU_DEBUG=$qemuDebug"
Write-Host "TRASH_FOLDER_QEMU_EXIT_CODE=$exitCode"
Write-Host "TRASH_FOLDER_QEMU_HARNESS_STOPPED=$harnessStopped"
Write-Host "TRASH_FOLDER_QEMU_TIMED_OUT=$timedOut"
Write-Host "TRASH_FOLDER_QEMU_GUEST_RESULT=$guestResult"
Write-Host "TRASH_FOLDER_QEMU_FINAL_MARKER=$finalMarker"
$classification = if ($timedOut) { "TIMEOUT" }
elseif ($qemuErrorText -match "assertion failed|Bail out|Aborted") { "QEMU_HOST_ABORT" }
elseif ($guestResult -eq "FAIL") { "GUEST_REPORTED_FAILURE" }
elseif ($guestResult -eq "PASS") { "GUEST_PASS_HARNESS_STOP" }
elseif ($process.HasExited) { "QEMU_EXITED_BEFORE_GUEST_RESULT" }
else { "UNKNOWN" }
Write-Host "TRASH_FOLDER_QEMU_CLASSIFICATION=$classification"

if ($guestResult -ne "PASS") {
    throw "Trash-folder QEMU smoke did not report PASS. Serial log: $serialLog"
}
