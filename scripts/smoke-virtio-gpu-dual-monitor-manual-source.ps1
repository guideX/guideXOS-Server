param()

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Read-Text([string]$RelativePath) {
    $path = Join-Path $Root $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing source file: $path" }
    return Get-Content -LiteralPath $path -Raw
}

function Assert-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if (-not $Text.Contains($Needle)) { throw $Message }
}

$launcher = Read-Text 'scripts\run-qemu-virtio-gpu-dual-monitor-manual.bat'
$sharedLauncher = Read-Text 'scripts\run-qemu-display-probe.bat'
$recorder = Read-Text 'scripts\record-virtio-gpu-dual-monitor-manual-result.ps1'
$capture = Read-Text 'scripts\capture-virtio-gpu-dual-monitor-manual-screenshot.ps1'
$main = Read-Text 'kernel\core\main.cpp'
$gpu = Read-Text 'kernel\core\virtio_gpu.cpp'

foreach ($needle in @(
    'run-qemu-display-probe.bat',
    'virtio-gpu',
    'DISPLAY_CONFIGURATION_CONTROL_ACTIVE',
    'DISPLAY_EVENTS_ACTIVE',
    'MANUAL_VALIDATION_ACTIVE',
    'GXOS_QEMU_DISPLAY_PROBE_QMP_PORT',
    'CONFIG_STORE',
    'automaticProof=disabled',
    'realHardware=no')) {
    Assert-Contains $launcher $needle "manual launcher contract missing: $needle"
}
Assert-Contains $launcher 'FormatOnly' 'manual launcher must create a writable persistence artifact'
Assert-Contains $launcher 'mingw32-make ARCH=amd64 clean' 'manual launcher must rebuild objects for its compile-time gate'
Assert-Contains $launcher 'DISPLAY_PROBE_SERIAL_LOG' 'manual launcher must retain a guest serial log'
Assert-Contains $launcher 'DISPLAY_PROBE_DISABLE_VNC' 'manual launcher must avoid a fixed VNC display collision'
Assert-Contains $launcher 'DISPLAY_PROBE_SIMPLE_GTK' 'manual launcher must use the simple interactive GTK surface'
Assert-Contains $launcher 'no guest IPC' 'manual launcher must restrict QMP to lifecycle/screenshots'
Assert-Contains $sharedLauncher 'GXOS_QEMU_DISPLAY_PROBE_DISABLE_VNC' 'shared launcher must support opt-in VNC suppression'
Assert-Contains $sharedLauncher 'GXOS_QEMU_DISPLAY_PROBE_SIMPLE_GTK' 'shared launcher must support the manual GTK override'

foreach ($needle in @('PASS', 'FAIL', 'BLOCKED', 'NOT TESTED', 'automaticResultInference=no', 'manualOperatorConfirmationRequired=yes', 'qemuVersion', 'HEAD', 'screenshotPaths')) {
    Assert-Contains $recorder $needle "manual result recorder field/guard missing: $needle"
}
foreach ($needle in @('qmp_capabilities', 'screendump', 'quit', 'device = ''gpu0''', 'head = $head')) {
    Assert-Contains $capture $needle "manual screenshot helper contract missing: $needle"
}

foreach ($needle in @(
    'Manual dual-monitor validation:',
    'topologyTestControls=enabled=yes',
    'topologyInjectionAvailable=yes',
    'automaticProof=disabled',
    'displayOptions=not-exposed-by-current-live-probe',
    'GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE')) {
    Assert-Contains $main $needle "manual guest banner/gate missing: $needle"
}
Assert-Contains $main 'DISPLAY_CONFIGURATION_CONTROL_ACTIVE)' 'manual gate must coexist with the existing control build'
Assert-Contains $main 'DISPLAY_EVENTS_ACTIVE) && !defined(GXOS_QEMU_VIRTIO_GPU_MANUAL_VALIDATION_ACTIVE)' 'manual mode must not run injected event proof'

foreach ($needle in @('compositor-live-manual', 'continuousPresentation=manual', 'MANUAL_VALIDATION_ACTIVE', 'return false;')) {
    Assert-Contains $gpu $needle "manual presentation gate missing: $needle"
}
if ($launcher -match 'input-send-event|guest.*QMP.*(state|input)|QMP.*(inject|change|apply)') { throw 'manual launcher must not use QMP as guest IPC' }

Write-Host 'virtio-gpu dual-monitor manual validation source smoke passed.'
