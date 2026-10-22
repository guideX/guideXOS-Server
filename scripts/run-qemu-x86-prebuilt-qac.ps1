[CmdletBinding()]
param(
    [string]$QacCliPath,
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-i386.exe',
    [string]$KernelPath,
    [ValidateRange(1024, 65535)]
    [Nullable[int]]$QualificationQmpPort
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $PSScriptRoot 'qac-managed-qemu.psm1') -Force -ErrorAction Stop

try {
    $prebuiltKernelPath = Resolve-QacPrebuiltKernelPath -KernelPath $KernelPath -WorkspaceRoot $repoRoot
    $kernelInfo = Get-QacPrebuiltKernelInfo -Path $prebuiltKernelPath
} catch {
    [Console]::Error.WriteLine("Prebuilt kernel validation failed: $($_.Exception.Message)")
    exit 1
}

Write-Host 'Prebuilt kernel selected; this launcher does not build or replace kernel artifacts.'
Write-Host "Kernel: $($kernelInfo.Path)"
Write-Host "Format: $($kernelInfo.Format)"
Write-Host "Size: $($kernelInfo.Size) bytes"
Write-Host "SHA-256 before launch: $($kernelInfo.SHA256)"

if (-not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) {
    [Console]::Error.WriteLine("QEMU not found at '$QemuPath'. QEMU was not launched.")
    exit 1
}

if ($null -ne $QualificationQmpPort) {
    $listener = $null
    try {
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $QualificationQmpPort)
        $listener.Start()
    } catch {
        [Console]::Error.WriteLine("Qualification QMP port $QualificationQmpPort is unavailable on loopback. QEMU was not launched.")
        exit 1
    } finally {
        if ($null -ne $listener) {
            $listener.Stop()
        }
    }
    Write-Host "Test-scoped QMP endpoint: tcp:127.0.0.1:$QualificationQmpPort (omitted from normal interactive launches)."
}

try {
    $qacCli = Resolve-QacCliExecutable -ExplicitPath $QacCliPath -WorkspaceRoot $repoRoot
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

# The original batch launcher ran from the Server repository root and passed this
# root-relative artifact path to QEMU. Keep that working directory and argument.
$kernelArgument = 'kernel\build\x86\bin\kernel.elf'
if (-not [string]::IsNullOrWhiteSpace($KernelPath)) {
    $kernelArgument = $prebuiltKernelPath
}
$qemuArguments = New-QacPrebuiltQemuArguments -KernelArgument $kernelArgument -QualificationQmpPort $QualificationQmpPort

$qacProcess = $null
$exitCode = 1
try {
    $startInfo = New-QacRunProcessStartInfo `
        -QacCliPath $qacCli `
        -WorkingDirectory $repoRoot `
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
} catch {
    [Console]::Error.WriteLine("QAC-managed prebuilt QEMU launch failed: $($_.Exception.Message). No unmanaged fallback was attempted.")
} finally {
    if ($null -ne $qacProcess) {
        $qacProcess.Dispose()
    }
}

try {
    $kernelHashAfter = (Get-FileHash -LiteralPath $prebuiltKernelPath -Algorithm SHA256 -ErrorAction Stop).Hash
    Write-Host "SHA-256 after QAC run: $kernelHashAfter"
    if ($kernelHashAfter -ne $kernelInfo.SHA256) {
        [Console]::Error.WriteLine('Kernel integrity check failed: the prebuilt kernel hash changed during the run.')
        if ($exitCode -eq 0) {
            $exitCode = 1
        }
    } else {
        Write-Host 'Kernel integrity check: unchanged.'
    }
} catch {
    [Console]::Error.WriteLine("Kernel integrity check failed after the run: $($_.Exception.Message)")
    if ($exitCode -eq 0) {
        $exitCode = 1
    }
}

exit $exitCode
