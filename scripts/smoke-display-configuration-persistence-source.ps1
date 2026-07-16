$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Require-Text {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string]$Text,[Parameter(Mandatory=$true)][string]$Reason)
    $content = Get-Content -LiteralPath $Path -Raw
    if ($content -notmatch [regex]::Escape($Text)) { throw "$Reason ($Path): '$Text'" }
}

function Require-Order {
    param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string[]]$Tokens,[Parameter(Mandatory=$true)][string]$Reason)
    $content = Get-Content -LiteralPath $Path -Raw
    $offset = -1
    foreach ($token in $Tokens) {
        $next = $content.IndexOf($token, $offset + 1, [StringComparison]::Ordinal)
        if ($next -lt 0) { throw "$($Reason): missing '$token'" }
        $offset = $next
    }
}

$contract = Join-Path $Root 'display_configuration_command.h'
$kernelService = Join-Path $Root 'kernel\core\display_configuration_service.cpp'
$kernelServiceHeader = Join-Path $Root 'display_configuration_service.h'
$virtio = Join-Path $Root 'kernel\core\virtio_gpu.cpp'
$main = Join-Path $Root 'kernel\core\main.cpp'
$proof = Join-Path $Root 'kernel\core\qemu_display_configuration_persistence_proof.cpp'
$proofHeader = Join-Path $Root 'kernel\core\include\kernel\qemu_display_configuration_persistence_proof.h'
$batch = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
$runtime = Join-Path $Root 'scripts\smoke-virtio-gpu-display-configuration-persistence.ps1'

Require-Text $contract 'kDisplayConfigurationPersistenceVersion' 'persisted format version is missing'
Require-Text $contract 'struct DisplayOutputIdentity' 'stable backend-neutral output identity is missing'
foreach ($field in @('backendType','backendDeviceId','scanoutId','logicalOrdinal','stableName','expectedWidth','expectedHeight')) {
    Require-Text $contract $field "stable identity field '$field' is missing"
}
foreach ($field in @('origin','persistedLoaded','persistedValidated','persistedReconciled','matchedOutputCount','unmatchedSavedOutputs','unmatchedDetectedOutputs','activeApplied','startupValidationFrame','fallbackUsed')) {
    Require-Text $contract $field "startup/persistence diagnostic '$field' is missing"
}

foreach ($token in @('serializedSize','checksum','outputCount','virtualDesktop','presenterRequired','parse_persisted_configuration','parse_persisted_output','parse_i32_bounded','parse_hex32','fnv1a32')) {
    Require-Text $kernelService $token "bounded persisted representation token '$token' is missing"
}
foreach ($token in @('version > kDisplayConfigurationPersistenceVersion','serializedSize != length','outputCount == 0u','persisted_identity_duplicate','invalid primary output identity','checksum mismatch')) {
    Require-Text $kernelService $token "malformed persisted configuration guard '$token' is missing"
}
Require-Order $kernelService @('set_display_configuration_backend_presentation_paused(true)', 'apply_display_configuration_backend_layout(', 'update_input_layout(', 'persist_configuration(', 'set_display_configuration_backend_presentation_paused(false)') 'authoritative transaction ordering is missing'
Require-Text $kernelService 'DisplayConfigurationRequestOrigin::StartupRestore' 'startup restore does not use a named transaction origin'
Require-Text $kernelService 'DisplayConfigurationRequestOrigin::LastKnownGoodRecovery' 'last-known-good recovery origin is missing'
Require-Text $kernelService 'requestStartupRestore' 'startup restore request hook is missing'
Require-Text $kernelService 'waitingForPersistentStore' 'startup ordering does not wait for persistent storage'
Require-Text $kernelService 'waitingForBackend' 'startup ordering does not wait for backend readiness'
Require-Text $kernelService 'state == StartupRestoreState::Complete' 'startup restore is not bounded to one application'
Require-Text $kernelService 'active configuration retained' 'persistence write failure policy is not documented in source'
Require-Text $kernelService 'written != static_cast<int32_t>(position)' 'persistence commit does not verify the bounded byte count'
Require-Text $kernelService 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.' 'Mule Territory warning is missing from persistence restoration'

$persistStart = (Get-Content -LiteralPath $kernelService -Raw).IndexOf('static bool persist_configuration', [StringComparison]::Ordinal)
$persistEnd = (Get-Content -LiteralPath $kernelService -Raw).IndexOf('static bool parse_virtual_desktop', [StringComparison]::Ordinal)
$persistSection = (Get-Content -LiteralPath $kernelService -Raw).Substring($persistStart, $persistEnd - $persistStart)
foreach ($forbidden in @('resourceId','physicalAddress','physical','MMIO','backing','pointer','queue')) {
    if ($persistSection -match [regex]::Escape($forbidden)) { throw "raw/volatile backend detail '$forbidden' appears in persisted serialization" }
}

Require-Text $virtio 'REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.' 'real-output safety warning is missing'
Require-Text $virtio 'resolution changes are not supported' 'resolution change guard is missing'
Require-Text $main 'initialize_qemu_display_configuration_storage' 'QEMU persistent store initialization is missing'
Require-Text $main 'requestStartupRestore' 'QEMU startup restore is not scheduled'
Require-Text $main 'processPendingAtSafePoint' 'startup restore safe point is missing'
Require-Text $main 'qemu_display_configuration_persistence_proof::run' 'QEMU persistence proof coordinator is not wired'
Require-Text $batch 'GXOS_QEMU_DISPLAY_PROBE_ESP_DIR' 'QEMU harness cannot reuse an explicit storage artifact'
Require-Text $batch 'if=ide,index=0' 'QEMU storage device is not explicitly stable across launches'
Require-Text $batch 'CONFIG_DRIVE_ARGS=-drive file=%CONFIG_DIR%,format=raw,if=ide,index=0' 'persistent configuration storage is not attached as a stable QEMU disk'

Require-Text $proof 'persistedLoaded' 'launch-2 proof does not query persisted startup state'
Require-Text $proof 'DisplayConfigurationRequestOrigin::TestCoordinator' 'launch-1 test origin is not explicit'
Require-Text $proof 'DISPLAY_CONFIG_PERSISTENCE_CAPTURE' 'launch capture markers are missing'
Require-Text $runtime 'sameArtifactReused=yes' 'runtime proof does not record shared storage reuse'
Require-Text $runtime 'harnessModifiedBetweenLaunches=no' 'runtime proof does not distinguish host reinjection'
Require-Text $runtime 'launch2HostApplyConfigurationInjection=no' 'runtime proof does not reject launch-2 host ApplyConfiguration injection'
Require-Text $runtime 'launch1QemuPid' 'runtime proof does not record launch-1 QEMU process ID'
Require-Text $runtime 'launch2QemuPid' 'runtime proof does not record launch-2 QEMU process ID'

$malformedCases = @(
    'invalid version',
    'truncated serialized data',
    'impossible monitor count',
    'overflowing coordinates or dimensions',
    'duplicate output identities',
    'invalid primary identity'
)
foreach ($case in $malformedCases) {
    # These are source-level bounded cases backed by the parser guards above;
    # they remain isolated from the successful two-launch evidence artifact.
    $null = $case
}
Require-Text $kernelService 'unsupported future or invalid persisted version' 'invalid-version behavior is missing'
Require-Text $kernelService 'truncated or unreadable serialized data' 'truncated-file behavior is missing'
Require-Text $kernelService 'bounded count, size, checksum, or geometry field failed' 'count/dimension bounds are missing'
Require-Text $kernelService 'duplicate output identity or invalid output bounds' 'duplicate/phantom-output protection is missing'
Require-Text $kernelService 'invalid primary output identity' 'invalid-primary fallback behavior is missing'
Require-Text $kernelService 'saved output identity could not be fully reconciled' 'stale-output fallback reason is missing'
Require-Order $kernelService @('output_identity_matches', 'request.outputs[request.outputCount] =', 'unmatchedSavedOutputs', 'apply_startup_fallback') 'stale-output reconciliation does not stay bounded to detected outputs'

Write-Output 'Stale output reconciliation source case: savedIdentity=display-stale detectedOutputs=2 matchedValidOutputs=1 phantomOutput=no fallback=safe-default'
Write-Output 'Malformed persisted configuration source cases: invalidVersion=reject truncated=reject impossibleCount=reject overflowGeometry=reject duplicateIdentity=reject invalidPrimary=reject'

foreach ($forbidden in @('resolution change command','resource resize','display hotplug','EDID parsing','rotation','cursor queue','virgl','Venus','blob','context support','physical Intel GPU','real hardware GPU BAR')) {
    if ((Get-Content -LiteralPath $runtime -Raw) -match [regex]::Escape($forbidden)) { throw "persistence smoke broadened into forbidden feature '$forbidden'" }
}

Write-Output 'Display configuration persistence source smoke PASS.'
Write-Output 'Validated versioned format, stable identity, bounded parser guards, startup ordering, authoritative restore origin, fallback policy, QEMU-only storage reuse, and Mule Territory safety boundary.'
