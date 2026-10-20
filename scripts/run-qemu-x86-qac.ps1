[CmdletBinding()]
param(
    [string]$QacCliPath,
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-i386.exe'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$kernelRoot = Join-Path $root 'kernel'
$kernelRelativePath = 'build\x86\bin\kernel.elf'
$kernelPath = Join-Path $kernelRoot $kernelRelativePath

Import-Module (Join-Path $PSScriptRoot 'qac-managed-qemu.psm1') -Force -ErrorAction Stop

if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) {
    [Console]::Error.WriteLine("Kernel not found at '$kernelPath'. Run scripts\run-qemu-x86-with-build.bat first.")
    exit 1
}
if (-not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) {
    [Console]::Error.WriteLine("QEMU not found at '$QemuPath'. Install QEMU or pass -QemuPath.")
    exit 1
}

try {
    $qacCli = Resolve-QacCliExecutable -ExplicitPath $QacCliPath -WorkspaceRoot $root
} catch {
    [Console]::Error.WriteLine("QAC coordinator unavailable: $($_.Exception.Message)")
    exit 2
}

Write-Host 'Checking the interactive QAC coordinator...'
$statusOutput = @(& $qacCli status 2>&1)
$statusExitCode = $LASTEXITCODE
foreach ($line in $statusOutput) {
    Write-Host $line
}
if ($statusExitCode -ne 0) {
    if ($statusExitCode -eq 2) {
        [Console]::Error.WriteLine('QAC coordinator unavailable. Start the already-installed interactive coordinator, then retry. QEMU was not launched.')
    } else {
        [Console]::Error.WriteLine("QAC coordinator status failed with exit code $statusExitCode. QEMU was not launched.")
    }
    exit $statusExitCode
}

Write-Host 'QAC coordinator online.'
Write-Host 'Reservation requested: project=guideXOSServer mode=development.'
Write-Host 'Waiting for QAC admission; QAC reports queue position and cancellation status.'

$qemuArguments = @(
    '-machine', 'pc,usb=off',
    '-kernel', $kernelRelativePath,
    '-m', '128M',
    '-vga', 'std',
    '-serial', 'stdio',
    '-display', 'gtk',
    '-vnc', ':0',
    '-no-reboot',
    '-no-shutdown',
    '-d', 'guest_errors'
)

$qacProcess = $null
try {
    $startInfo = New-QacRunProcessStartInfo `
        -QacCliPath $qacCli `
        -WorkingDirectory $kernelRoot `
        -QemuPath $QemuPath `
        -QemuArguments $qemuArguments

    $qacProcess = [System.Diagnostics.Process]::Start($startInfo)
    if ($null -eq $qacProcess) {
        throw 'The QAC CLI process did not start.'
    }

    Write-Host 'QAC is managing the QEMU child. Close the QEMU window or press Ctrl+C to end this interactive development run.'
    $qacProcess.WaitForExit()
    $exitCode = $qacProcess.ExitCode
    Write-Host "QAC managed run completed with exit code $exitCode."
    exit $exitCode
} catch {
    [Console]::Error.WriteLine("QAC-managed QEMU launch failed: $($_.Exception.Message). No unmanaged fallback was attempted.")
    exit 1
} finally {
    if ($null -ne $qacProcess) {
        $qacProcess.Dispose()
    }
}
