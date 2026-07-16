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

function Assert-Ordered([string]$Text, [string[]]$Needles, [string]$Message) {
    $last = -1
    foreach ($needle in $Needles) {
        $position = $Text.IndexOf($needle, [StringComparison]::Ordinal)
        if ($position -lt 0 -or $position -le $last) {
            throw "${Message}: missing or out-of-order '$needle'"
        }
        $last = $position
    }
}

$catalog = Read-Text 'display_mode_catalog.h'
$configuration = Read-Text 'display_configuration.h'
$command = Read-Text 'display_configuration_command.h'
$model = Read-Text 'display_model.h'
$options = Read-Text 'display_options.cpp'
$service = Read-Text 'kernel\core\display_configuration_service.cpp'
$input = Read-Text 'kernel\core\display_input_mapper.cpp'
$backend = Read-Text 'kernel\core\virtio_gpu.cpp'
$backendHeader = Read-Text 'virtio_gpu_display_backend.h'
$rebuildProof = Read-Text 'kernel\core\qemu_display_resolution_rebuild_proof.cpp'
$persistenceProof = Read-Text 'kernel\core\qemu_display_resolution_persistence_proof.cpp'

# Bounded QEMU logical mode catalog and arithmetic.
Assert-Contains $catalog 'struct DisplayMode' 'backend-neutral DisplayMode is missing'
foreach ($mode in @('qemu-1280x800', 'qemu-1024x768', 'qemu-800x600')) {
    Assert-Contains $catalog $mode "bounded catalog is missing $mode"
}
foreach ($needle in @('kQemuLogicalModeMinWidth', 'kQemuLogicalModeMinHeight', 'kQemuLogicalModeMaxWidth', 'kQemuLogicalModeMaxHeight', 'checkedDisplayModeBackingBytes', 'std::numeric_limits<uint64_t>::max()', 'kQemuLogicalModePerOutputBackingLimit', 'kQemuLogicalModeTotalBackingLimit', 'bool duplicate')) {
    Assert-Contains $catalog $needle "mode catalog safety check is missing: $needle"
}
Assert-Contains $catalog '4u' 'the validated 32-bit pixel format must remain explicit'

# Stable requested/active identity and persistence must not contain volatile GPU state.
foreach ($needle in @('modeId', 'arrangement', 'virtualDesktop', 'assignedWidth', 'assignedHeight')) {
    Assert-Contains $configuration $needle "configuration model is missing $needle"
}
Assert-Contains $model 'clampPointToBounds' 'monitor-union cursor clamping is missing'
Assert-Contains $input 'nearest point in the union of active' 'input mapper must clamp to the monitor union'
Assert-Contains $service 'output.modeId' 'persisted per-output mode identity is missing'
foreach ($forbidden in @('resourceId', 'backingVirtualAddress', 'backingByteCount')) {
    $serializerStart = $service.IndexOf('serializePersistedConfiguration', [StringComparison]::Ordinal)
    if ($serializerStart -ge 0) {
        $serializerEnd = $service.IndexOf('static bool', $serializerStart + 1, [StringComparison]::Ordinal)
        if ($serializerEnd -lt 0) { $serializerEnd = $service.Length }
        $serializer = $service.Substring($serializerStart, $serializerEnd - $serializerStart)
        if ($serializer.Contains($forbidden)) { throw "persisted configuration contains volatile field $forbidden" }
    }
}

# Display Options edits requested state only and exposes unavailable settings honestly.
foreach ($needle in @('cycleVirtioGpuResolution', 'Resolution:', 'Mirror requires matching resolutions', 'Refresh rate: Not available', 'Rotation: Not available', 'Apply')) {
    Assert-Contains $options $needle "Display Options resolution behavior is missing: $needle"
}

# Prepare-before-replace, bounded backing validation, safe publication, rollback, and cleanup ordering.
foreach ($needle in @('VirtioGpuOutputRebuildPlan', 'oldResourceId', 'newResourceId', 'prepared', 'attached', 'scanoutBound', 'validationPresented', 'committed', 'kQemuLogicalModePerOutputBackingLimit', 'kQemuLogicalModeTotalBackingLimit', 'allocate_replacement_resource_id', 'physicalCoverage', 's_rebuildBackingStorage0', 's_rebuildBackingStorage1', 'failureInjectionFlags')) {
    Assert-Contains $backend $needle "resource rebuild safety structure/check is missing: $needle"
}
Assert-Contains $backend 'presentation was not paused at the safe point' 'rebuild must require the presentation pause gate'
$rebuildBody = $backend.Substring($backend.IndexOf('DiagnosticResourceState oldResources', [StringComparison]::Ordinal))
Assert-Ordered $rebuildBody @('DiagnosticResourceState oldResources', 'candidateResources', 'build_diagnostic_backing_layout', 'issue_resource_create_2d', 'issue_resource_attach_backing') 'resource preparation ordering is invalid'
$bindBody = $backend.Substring($backend.IndexOf('// Bind replacements', [StringComparison]::Ordinal))
Assert-Ordered $bindBody @('issue_set_scanout', '// Verify the actual post-bind scanouts', 'issue_resource_unref(*s_livePresentation.device, oldResources') 'old resources must remain alive through scanout bind and validation'
Assert-Contains $backend 'update_backend_layout(requested.mode' 'provisional inventory must publish only after commit'
Assert-Contains $backend 'releaseProvisional' 'provisional resources must have a bounded cleanup path'
Assert-Contains $backend 'oldResourceRestored' 'rollback must rebind the old resource'
Assert-Contains $rebuildProof 'rollbackSucceeded' 'rollback evidence must be reported'
Assert-Contains $backend 'second-output commit failure' 'second-output commit failure injection is missing'
Assert-Contains $backend 'validation-frame failure' 'validation-frame failure injection is missing'
Assert-Contains $backend 'newResourceId = id' 'replacement resource IDs must be assigned explicitly'

# Complete requested layout, mixed Extend, Mirror rejection, and Mirror viewport normalization.
Assert-Contains $configuration 'nextVirtualX += monitor.width' 'mixed-resolution Extend desktop geometry is missing'
Assert-Contains $configuration 'Mirror dimensions incompatible' 'Mirror mismatch rejection is missing'
Assert-Contains $backend 'mode == static_cast<uint32_t>(gxos::display::DisplayConfigurationMode::Mirror)' 'QEMU Mirror layout branch is missing'
Assert-Contains $backend 'both viewports occupy the same origin' 'Mirror origin normalization is missing'
Assert-Contains $rebuildProof 'virtualDesktopWidth == 2304 && current.virtualDesktopHeight == 800' 'mixed-resolution 2304x800 proof is missing'
Assert-Contains $rebuildProof 'MirrorGeometryIncompatible' 'Mirror mismatch proof is missing'

# Explicit feature exclusions and safety checkpoint.
foreach ($needle in @('REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.', 'No physical Intel GPU support', 'No real hardware GPU BAR access', 'No 3D, virgl, Venus, blob', 'No cursor queue')) {
    Assert-Contains $backend $needle "QEMU-only safety boundary is missing: $needle"
}
foreach ($forbidden in @('EDID', 'hotplug', 'HOTPLUG', 'refresh-rate', 'rotation', 'QMP')) {
    if ($rebuildProof.Contains($forbidden) -or $persistenceProof.Contains($forbidden)) {
        throw "guest proof must not use unsupported feature or QMP IPC: $forbidden"
    }
}
Assert-Contains $options 'Refresh rate: Not available' 'refresh-rate control must not be falsely advertised'
Assert-Contains $options 'Rotation: Not available' 'rotation control must not be falsely advertised'

Write-Host 'virtio-gpu logical-resolution rebuild source smoke passed.'
