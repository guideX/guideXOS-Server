param(
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Batch = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunRoot = Join-Path $Root ("logs\qemu-virtio-gpu-manual-readiness-" + $Stamp)
$ConfigPath = Join-Path $RunRoot 'display-config-store.img'
$SerialLog = Join-Path $RunRoot 'guest.serial.log'
$LauncherStdOut = Join-Path $RunRoot 'launcher.stdout.log'
$LauncherStdErr = Join-Path $RunRoot 'launcher.stderr.log'
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    try { $listener.Start(); return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Find-Qemu {
    $command = Get-Command 'qemu-system-x86_64' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        'C:\qemu\qemu-system-x86_64.exe',
        'D:\qemu\qemu-system-x86_64.exe'
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Invoke-NativeChecked {
    param([string]$FilePath,[string[]]$ArgumentList,[string]$WorkingDirectory=$Root)
    Push-Location $WorkingDirectory
    try {
        & $FilePath @ArgumentList
        if ($LASTEXITCODE -ne 0) { throw "$FilePath failed with exit code $LASTEXITCODE." }
    } finally { Pop-Location }
}

function Invoke-KernelBuild {
    param([string]$ExtraCFlags)
    $oldExtra = $env:EXTRA_CFLAGS
    try {
        $env:EXTRA_CFLAGS = $ExtraCFlags
        Invoke-NativeChecked 'mingw32-make' @('ARCH=amd64','clean') (Join-Path $Root 'kernel')
        Invoke-NativeChecked (Join-Path $Root 'build-kernel.bat') @()
    } finally {
        if ($null -eq $oldExtra) { Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue }
        else { $env:EXTRA_CFLAGS = $oldExtra }
    }
}

function Read-Serial {
    if (-not (Test-Path -LiteralPath $SerialLog)) { return '' }
    return Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
}

function Send-QmpQuit {
    param([int]$Port)
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $deadline = (Get-Date).AddSeconds(10)
        while ((Get-Date) -lt $deadline -and -not $client.Connected) {
            try { $client.Connect('127.0.0.1', $Port) } catch { Start-Sleep -Milliseconds 100 }
        }
        if (-not $client.Connected) { return $false }
        $writer = [IO.StreamWriter]::new($client.GetStream(), [Text.UTF8Encoding]::new($false))
        $writer.AutoFlush = $true
        $writer.WriteLine('{"execute":"qmp_capabilities"}')
        Start-Sleep -Milliseconds 100
        $writer.WriteLine('{"execute":"quit"}')
        return $true
    } finally { if ($client) { $client.Dispose() } }
}

function Stop-Launcher {
    param([System.Diagnostics.Process]$Process,[int]$QmpPort)
    if ($null -eq $Process -or $Process.HasExited) { return }
    [void](Send-QmpQuit -Port $QmpPort)
    if (-not $Process.WaitForExit(10000)) {
        & taskkill.exe /PID $Process.Id /T /F | Out-Null
    }
}

$qemu = Find-Qemu
if (-not $qemu) { throw 'qemu-system-x86_64.exe is unavailable.' }
if (-not (Test-Path -LiteralPath $Batch)) { throw "Missing shared launcher: $Batch" }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\smoke-virtio-gpu-display-configuration-persistence.ps1') -FormatOnly -ImagePath $ConfigPath
if ($LASTEXITCODE -ne 0) { throw 'Unable to create the readiness smoke FAT32 artifact.' }

$manualFlags = '-DGXOS_QEMU_VIRTIO_GPU_BACKEND_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_MANUAL_MODE -DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_PERSISTENCE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE'
$normalFlags = ''
$process = $null
$qmpPort = Get-FreeTcpPort
$ready = $false
try {
    Invoke-KernelBuild -ExtraCFlags $manualFlags

    $oldHeadless = $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS
    $oldNoPause = $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE
    $oldSerial = $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG
    $oldQmp = $env:GXOS_QEMU_DISPLAY_PROBE_QMP_PORT
    $oldDisableVnc = $env:GXOS_QEMU_DISPLAY_PROBE_DISABLE_VNC
    try {
        $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS = '1'
        $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE = '1'
        $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG = $SerialLog
        $env:GXOS_QEMU_DISPLAY_PROBE_QMP_PORT = [string]$qmpPort
        $env:GXOS_QEMU_DISPLAY_PROBE_DISABLE_VNC = '1'
        # The shared batch receives its optional config image as a plain
        # second argument.  The workspace launcher paths are normalized above
        # and contain no spaces, which avoids cmd.exe's nested /c quoting trap.
        $argumentList = @('/c', "`"$Batch`" virtio-gpu $ConfigPath")
        $process = Start-Process -FilePath 'cmd.exe' -ArgumentList $argumentList -PassThru -WindowStyle Hidden -WorkingDirectory $Root -RedirectStandardOutput $LauncherStdOut -RedirectStandardError $LauncherStdErr

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            if ($process.HasExited) { break }
            $serial = Read-Serial
            if ($serial -match 'Manual dual-monitor readiness: backend=virtio-gpu outputs=2 desktop=ready shell=ready startMenu=ready displayOptionsRegistered=yes displayOptionsLaunchable=yes presenter=live proofCoordinator=disabled persistence=ready topologyTestControls=yes realHardware=no result=ready') {
                if ($serial -match '\[QEMU-MANUAL\] DisplayMonitor count=2 DisplayRenderTarget count=2 .*logicalModes=valid') {
                    $ready = $true
                    break
                }
            }
            Start-Sleep -Milliseconds 250
        }
        if (-not $ready) {
            $serial = Read-Serial
            throw "Manual validation readiness did not become ready within $TimeoutSeconds seconds. Serial tail: $($serial.Substring([Math]::Max(0, $serial.Length - 1600)))"
        }
    } finally {
        Stop-Launcher -Process $process -QmpPort $qmpPort
        foreach ($entry in @{
            GXOS_QEMU_DISPLAY_PROBE_HEADLESS=$oldHeadless
            GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE=$oldNoPause
            GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG=$oldSerial
            GXOS_QEMU_DISPLAY_PROBE_QMP_PORT=$oldQmp
            GXOS_QEMU_DISPLAY_PROBE_DISABLE_VNC=$oldDisableVnc
        }.GetEnumerator()) {
            if ($null -eq $entry.Value -or $entry.Value -eq '') { Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
            else { Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value }
        }
    }
} finally {
    Invoke-KernelBuild -ExtraCFlags $normalFlags
}

if (-not $ready) { throw 'Manual validation readiness failed.' }
Write-Host 'Manual validation readiness: outputs=2 normalDesktop=ok displayOptions=available logicalModes=valid persistence=ready automaticProof=disabled topologyControls=available result=pass'
Write-Host "Readiness evidence: $RunRoot"
