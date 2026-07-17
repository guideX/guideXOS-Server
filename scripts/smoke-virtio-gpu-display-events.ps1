param(
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogRoot = Join-Path $Root 'logs'
$RunRoot = Join-Path $LogRoot ("qemu-display-events-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

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

function Assert-Log {
    param([string]$Text, [string]$Pattern, [string]$Message)
    if ($Text -notmatch $Pattern) { throw $Message }
}

function Get-QmpCapabilityText {
    param([Parameter(Mandatory = $true)][string]$QemuPath)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $QemuPath
    $psi.Arguments = '-machine none -nodefaults -display none -qmp stdio'
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    [void]$process.Start()
    try {
        Start-Sleep -Milliseconds 150
        $process.StandardInput.WriteLine('{"execute":"qmp_capabilities"}')
        $process.StandardInput.Flush()
        Start-Sleep -Milliseconds 150
        $process.StandardInput.WriteLine('{"execute":"query-commands"}')
        $process.StandardInput.Flush()
        Start-Sleep -Milliseconds 150
        $process.StandardInput.WriteLine('{"execute":"quit"}')
        $process.StandardInput.Flush()
        $output = $process.StandardOutput.ReadToEnd()
        $error = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        return ($output + "`n" + $error)
    } finally {
        if (-not $process.HasExited) {
            try { $process.Kill() } catch { }
        }
        $process.Dispose()
    }
}

function Stop-ProcessTree {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    if ($Process.HasExited) { return }
    try { & taskkill.exe /T /F /PID $Process.Id | Out-Null } catch { try { $Process.Kill() } catch { } }
}

function Remove-EventSensitiveObjects {
    foreach ($name in @(
        'virtio_gpu.o',
        'virtio_gpu.d',
        'mmio.o',
        'mmio.d',
        'main.o',
        'main.d',
        'qemu_display_events_proof.o',
        'qemu_display_events_proof.d'
    )) {
        Remove-Item -LiteralPath (Join-Path $Root ("kernel\build\amd64\obj\core\" + $name)) -ErrorAction SilentlyContinue
    }
}

$qemu = Find-Qemu
if (-not $qemu) { throw 'qemu-system-x86_64 was not found.' }
$qemuVersion = (& $qemu -version 2>&1 | Select-Object -First 1).Trim()
$gpuHelp = (& $qemu -device virtio-gpu-pci,help 2>&1 | Out-String)
$qmpText = Get-QmpCapabilityText -QemuPath $qemu
$qmpDisplayUpdate = $qmpText -match '"name": "display-update"'
$qmpDisplayReload = $qmpText -match '"name": "display-reload"'
$headTopologyCommand = $qmpText -match 'display-(head|output|scanout)|set-display-(head|output|scanout)'

if ($headTopologyCommand) {
    $genuineEventTrigger = 'available'
    $genuineEventReason = 'QMP advertised a display-head topology command; no automatic action was issued by this smoke.'
} else {
    $genuineEventTrigger = 'unavailable'
    $genuineEventReason = 'QMP exposes display-update/display-reload only; this build exposes no documented virtio-gpu head enable/disable topology action.'
}

Write-Host ("QEMU version: {0}" -f $qemuVersion)
Write-Host ("QEMU virtio-gpu help: max_outputs={0} edidOption={1}" -f ($gpuHelp -match 'max_outputs=<uint32>'), ($gpuHelp -match 'edid=<bool>'))
Write-Host ("QMP display-update={0} display-reload={1} headTopologyCommand={2}" -f $qmpDisplayUpdate, $qmpDisplayReload, $headTopologyCommand)
Write-Host ("genuineEventTrigger={0} reason={1}" -f $genuineEventTrigger, $genuineEventReason)

$oldExtra = $env:EXTRA_CFLAGS
$oldProbeState = @{}
foreach ($name in @(
    'GXOS_QEMU_DISPLAY_PROBE_HEADLESS',
    'GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE',
    'GXOS_QEMU_DISPLAY_PROBE_CAPTURE',
    'GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG',
    'GXOS_QEMU_DISPLAY_PROBE_QMP_PORT'
)) {
    $oldProbeState[$name] = (Get-Item -Path "Env:$name" -ErrorAction SilentlyContinue).Value
}

$eventFlags = '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE_BOUNDED -DGXOS_QEMU_VIRTIO_GPU_DISPLAY_EVENTS_ACTIVE'
$serialPath = Join-Path $RunRoot 'serial.log'
$stdoutPath = Join-Path $RunRoot 'launcher.stdout.log'
$stderrPath = Join-Path $RunRoot 'launcher.stderr.log'
$launcher = $null
$eventBuildCompleted = $false
try {
    $env:EXTRA_CFLAGS = $eventFlags
    Remove-EventSensitiveObjects
    Write-Host 'Building QEMU-only display-event proof kernel.'
    & (Join-Path $Root 'build-kernel.bat')
    if ($LASTEXITCODE -ne 0) { throw "event-enabled kernel build failed with exit code $LASTEXITCODE" }
    $eventBuildCompleted = $true

    $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_CAPTURE = '0'
    $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG = $serialPath
    Remove-Item Env:\GXOS_QEMU_DISPLAY_PROBE_QMP_PORT -ErrorAction SilentlyContinue
    $batch = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
    $launcher = Start-Process -FilePath 'cmd.exe' -ArgumentList @('/c', $batch, 'virtio-gpu') `
        -PassThru -WindowStyle Hidden -WorkingDirectory $Root `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $launcher.HasExited -and (Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $serialPath) {
            $partial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
            if ($partial -match 'VirtioGPU display-event proof:.*result=success') { break }
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $launcher.HasExited) { Stop-ProcessTree -Process $launcher }
    $launcher.WaitForExit()
    if (-not (Test-Path -LiteralPath $serialPath)) { throw "QEMU serial log missing: $serialPath" }
    $serial = Get-Content -LiteralPath $serialPath -Raw
    # Timer IRQ diagnostics can interleave with a serial line at character
    # granularity. Normalize only that bounded diagnostic prefix for matching;
    # retain the original serial log as the evidence artifact.
    $normalizedSerial = $serial -replace '\[IRQ\] dispatch irq=[^\r\n]*\r?\n', ''

    Assert-Log $normalizedSerial 'VirtioGPU config snapshot: firstGeneration=' 'initial coherent config snapshot evidence missing'
    Assert-Log $normalizedSerial 'VirtioGPU display-event initial: coherent=ok' 'idle coherent config proof missing'
    Assert-Log $normalizedSerial 'VirtioGPU display-event observer: initialized=yes enabled=yes' 'event observer initialization evidence missing'
    Assert-Log $normalizedSerial 'VirtioGPU injected topology: kind=connector-state.*injectedEvent=yes accepted=yes' 'connector injection evidence missing'
    Assert-Log $normalizedSerial 'VirtioGPU injected topology: kind=preferred-geometry.*injectedEvent=yes accepted=yes' 'geometry injection evidence missing'
    Assert-Log $normalizedSerial 'VirtioGPU injected topology: kind=output-addition.*injectedEvent=yes accepted=yes' 'addition injection evidence missing'
    Assert-Log $normalizedSerial 'VirtioGPU injected topology: kind=output-removal.*injectedEvent=yes accepted=yes' 'removal injection evidence missing'
    Assert-Log $normalizedSerial 'Pending display topology:' 'pending topology publication evidence missing'
    Assert-Log $normalizedSerial 'activeConfigurationUnchanged=yes' 'active configuration stability evidence missing'
    Assert-Log $normalizedSerial 'VirtioGPU display-event proof:.*realEventObserved=no.*injectedDiffProof=ok.*getDisplayInfo=ok.*pendingTopologyPublished=yes.*activeConfigurationUnchanged=yes.*gpuFailures=0 result=success' 'final display-event proof failed'
    if ($normalizedSerial -match 'automaticApplyPerformed=yes') { throw 'runtime proof reported an automatic topology application' }
    if ($normalizedSerial -match 'display rescan:.*activeMutation=yes') { throw 'runtime proof reported active display mutation' }

    $counter = [regex]::Match($normalizedSerial, 'VirtioGPU display-event counters:.*')
    if ($counter.Success) { Write-Host $counter.Value }
    Write-Host ("VirtioGPU display-event runtime smoke passed. genuineEventTrigger={0} injectedDiffProof=ok serial={1}" -f $genuineEventTrigger, $serialPath)
} finally {
    if ($launcher -and -not $launcher.HasExited) { Stop-ProcessTree -Process $launcher }
    foreach ($entry in $oldProbeState.GetEnumerator()) {
        if ([string]::IsNullOrWhiteSpace($entry.Value)) { Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue }
        else { Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value }
    }
    if ($null -eq $oldExtra -or [string]::IsNullOrWhiteSpace($oldExtra)) {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    } else {
        $env:EXTRA_CFLAGS = $oldExtra
    }
    if ($eventBuildCompleted) {
        Write-Host 'Restoring normal kernel build after display-event runtime smoke.'
        Remove-EventSensitiveObjects
        & (Join-Path $Root 'build-kernel.bat')
        if ($LASTEXITCODE -ne 0) { throw "normal kernel rebuild after display-event smoke failed with exit code $LASTEXITCODE" }
    }
}
