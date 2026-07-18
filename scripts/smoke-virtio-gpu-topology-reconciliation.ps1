param(
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogRoot = Join-Path $Root 'logs'
$RunRoot = Join-Path $LogRoot ("qemu-topology-reconciliation-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

function Find-Qemu {
    $command = Get-Command 'qemu-system-x86_64' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        'C:\qemu\qemu-system-x86_64.exe',
        'D:\qemu\qemu-system-x86_64.exe')) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Stop-ProcessTree([System.Diagnostics.Process]$Process) {
    if ($null -eq $Process -or $Process.HasExited) { return }
    try { & taskkill.exe /T /F /PID $Process.Id | Out-Null } catch { try { $Process.Kill() } catch { } }
}

function Assert-Log([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Remove-TopologySensitiveObjects {
    foreach ($name in @(
        'virtio_gpu.o', 'virtio_gpu.d', 'mmio.o', 'mmio.d', 'main.o', 'main.d',
        'display_configuration_service.o', 'display_configuration_service.d',
        'kernel_apps.o', 'kernel_apps.d', 'kernel_compositor.o', 'kernel_compositor.d',
        'qemu_display_events_proof.o', 'qemu_display_events_proof.d')) {
        Remove-Item -LiteralPath (Join-Path $Root ("kernel\build\amd64\obj\core\" + $name)) -ErrorAction SilentlyContinue
    }
}

$qemu = Find-Qemu
if (-not $qemu) { throw 'qemu-system-x86_64 was not found.' }
$qemuVersion = (& $qemu -version 2>&1 | Select-Object -First 1).Trim()
$serialPath = Join-Path $RunRoot 'serial.log'
$stdoutPath = Join-Path $RunRoot 'launcher.stdout.log'
$stderrPath = Join-Path $RunRoot 'launcher.stderr.log'
$launcher = $null
$buildCompleted = $false
$oldExtra = $env:EXTRA_CFLAGS
$oldProbeState = @{}
foreach ($name in @(
    'GXOS_QEMU_DISPLAY_PROBE_HEADLESS', 'GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE',
    'GXOS_QEMU_DISPLAY_PROBE_CAPTURE', 'GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG',
    'GXOS_QEMU_DISPLAY_PROBE_QMP_PORT')) {
    $oldProbeState[$name] = (Get-Item -Path "Env:$name" -ErrorAction SilentlyContinue).Value
}

try {
    $env:EXTRA_CFLAGS = '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE_BOUNDED -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_CONFIGURATION_CONTROL_ACTIVE'
    $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_CAPTURE = '0'
    $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG = $serialPath
    Remove-Item Env:\GXOS_QEMU_DISPLAY_PROBE_QMP_PORT -ErrorAction SilentlyContinue
    Write-Host ("QEMU version: {0}" -f $qemuVersion)
    Write-Host 'genuineHostHotplug=unvalidated; injected topology coordinator only'

    Remove-TopologySensitiveObjects
    Write-Host 'Building QEMU-only topology reconciliation proof kernel.'
    & (Join-Path $Root 'build-kernel.bat')
    if ($LASTEXITCODE -ne 0) { throw "topology reconciliation kernel build failed with exit code $LASTEXITCODE" }
    $buildCompleted = $true

    $batch = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
    $launcher = Start-Process -FilePath 'cmd.exe' -ArgumentList @('/c', $batch, 'virtio-gpu') `
        -PassThru -WindowStyle Hidden -WorkingDirectory $Root `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $launcher.HasExited -and (Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $serialPath) {
            $partial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
            if ($partial -match 'VirtioGPU topology reconciliation proof:.*result=success') { break }
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $launcher.HasExited) { Stop-ProcessTree $launcher }
    $launcher.WaitForExit()
    if (-not (Test-Path -LiteralPath $serialPath)) { throw "QEMU serial log missing: $serialPath" }
    $serial = Get-Content -LiteralPath $serialPath -Raw
    $normalized = $serial -replace '\[IRQ\] dispatch irq=[^\r\n]*\r?\n', ''

    Assert-Log $normalized 'Topology reconciliation capture: phase=initial-two-output' 'initial two-output capture missing'
    Assert-Log $normalized 'Topology reconciliation pending:.*injectedChangeType=preferred-geometry' 'metadata-only pending state missing'
    Assert-Log $normalized 'Topology reconciliation preview:.*gpuMutation=no result=success' 'preview no-mutation evidence missing'
    Assert-Log $normalized 'Topology reconciliation dismiss:.*activeResourcesUnchanged=yes' 'dismiss evidence missing'
    Assert-Log $normalized 'action=remove oldOutputs=2 newOutputs=1.*validation=ok' 'confirmed one-output removal evidence missing'
    Assert-Log $normalized 'phase=single-output-after-confirmed-removal.*presentation=live' 'single-output live capture missing'
    Assert-Log $normalized 'action=add oldOutputs=1 newOutputs=2.*validation=ok' 'confirmed output restoration evidence missing'
    Assert-Log $normalized 'phase=restored-two-output.*taskbar=Display 1.*presentation=live' 'restored dual-output capture missing'
    Assert-Log $normalized 'Topology reconciliation rollback proof:.*rollbackSucceeded=yes.*oldOutputsRestored=yes.*oldLayoutRestored=yes.*presentationResumed=yes' 'rollback evidence missing'
    Assert-Log $normalized 'VirtioGPU topology reconciliation proof: metadataPreview=ok metadataDismiss=ok removalPreview=ok removalApply=ok singleOutputLive=ok additionPreview=ok additionApply=ok dualOutputRestored=ok rollback=ok activeResourcesStable=yes gpuFailures=0 genuineHotplugValidated=no injectedTopologyProof=ok result=success' 'final topology reconciliation proof failed'
    if ($normalized -match 'automaticApply(?:Performed)?=yes') { throw 'topology runtime reported automatic application' }
    if ($normalized -match 'genuineDeviceEvent=yes.*injectedTestEvent=yes') { throw 'injected event was mislabeled as genuine' }
    Write-Host ("VirtioGPU topology reconciliation runtime smoke passed. injectedTopologyProof=ok genuineHotplugValidated=no serial={0}" -f $serialPath)
} finally {
    if ($launcher -and -not $launcher.HasExited) { Stop-ProcessTree $launcher }
    foreach ($entry in $oldProbeState.GetEnumerator()) {
        if ([string]::IsNullOrWhiteSpace($entry.Value)) { Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
        else { Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value }
    }
    if ($null -eq $oldExtra -or [string]::IsNullOrWhiteSpace($oldExtra)) { Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue }
    else { $env:EXTRA_CFLAGS = $oldExtra }
    if ($buildCompleted) {
        Write-Host 'Restoring normal kernel build after topology reconciliation runtime smoke.'
        Remove-TopologySensitiveObjects
        & (Join-Path $Root 'build-kernel.bat')
        if ($LASTEXITCODE -ne 0) { throw "normal kernel rebuild after topology smoke failed with exit code $LASTEXITCODE" }
    }
}
