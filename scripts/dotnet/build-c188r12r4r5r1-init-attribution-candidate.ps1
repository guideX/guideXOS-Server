param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$CandidateRoot = '',
    [switch]$ThreadStoreReadiness
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $phaseRoot = if ($ThreadStoreReadiness) { 'c188r12r4r5r4-threadstore-readiness' } else { 'c188r12r4r5r3-init-return-contract' }
    $CandidateRoot = Join-Path $root "out\dotnet\$phaseRoot\candidate-$stamp"
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$allowed = [System.IO.Path]::GetFullPath((Join-Path $root 'out\dotnet')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($allowed, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate output must remain below out\dotnet: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite candidate: $candidate" }

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is missing: $Path" }
}
function Hash-File([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Invoke-Logged([string]$FilePath, [string[]]$Arguments, [string]$LogPath, [string]$WorkingDirectory=$root) {
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        Push-Location -LiteralPath $WorkingDirectory
        try {
            & $FilePath @Arguments *> $LogPath
            $code = $LASTEXITCODE
        } finally { Pop-Location }
    } finally { $ErrorActionPreference = $oldPreference }
    if ($code -ne 0) { throw "Command failed ($code): $FilePath $($Arguments -join ' '). See $LogPath" }
}

$ramdisk = Join-Path $root 'ESP\ramdisk.img'
$expectedRamdiskHash = 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
Require-File $ramdisk 'Source ramdisk'
if ((Hash-File $ramdisk) -ne $expectedRamdiskHash) { throw 'Source ramdisk SHA-256 no longer matches the C188R12R4R5 preservation identity.' }

$managedRoot = Join-Path $candidate 'managed'
$runtimeRoot = Join-Path $candidate 'runtime-pack'
$artifactRoot = Join-Path $managedRoot 'artifacts'
$pePath = Join-Path $artifactRoot 'HostLogProof.exe'
$mapPath = Join-Path $artifactRoot 'HostLogProof.map'
$elfPath = Join-Path $artifactRoot 'HostLogProof.elf'
$embeddedRaw = Join-Path $candidate 'gc-ownership-artifact.raw.o'
$embeddedObject = Join-Path $candidate 'gc-ownership-artifact.o'
$esp = Join-Path $candidate 'esp'
$kernelBuildPrefix = if ($ThreadStoreReadiness) { 'build/c188r12r4r5r4-threadstore-readiness-' } else { 'build/c188r12r4r5r1-init-attribution-' }
$kernelBuildDir = $kernelBuildPrefix + [guid]::NewGuid().ToString('N')
$kernel = Join-Path $root "kernel\$($kernelBuildDir.Replace('/','\'))\bin\kernel.elf"
$bootloader = Join-Path $root 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$objcopy = 'C:\mingw64\bin\objcopy.exe'
$objdump = 'C:\mingw64\bin\objdump.exe'
$readelf = 'C:\mingw64\bin\readelf.exe'
$python = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$converter = Join-Path $root 'tools\dotnet\pe_to_elf_v2_fixed_base.py'
$make = (Get-Command mingw32-make.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
if ([string]::IsNullOrWhiteSpace($make)) { $make = (Get-Command make.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source) }
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
foreach ($requirement in @(
    @($bootloader, 'UEFI bootloader'), @($objcopy, 'objcopy'), @($objdump, 'objdump'),
    @($readelf, 'readelf'), @($python, 'Bundled Python'), @($converter, 'PE to ELF converter'),
    @($make, 'MinGW make')
)) { Require-File $requirement[0] $requirement[1] }

New-Item -ItemType Directory -Force -Path $candidate | Out-Null
$managedBuilder = Join-Path $root 'scripts\dotnet\build-managed-hostlog-proof.ps1'
$managedBuildArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $managedBuilder,
    '-RepoRoot', $root, '-LegacyRoot', 'D:\dev\guideXOSUEFI',
    '-OutputRoot', $managedRoot, '-RuntimePackOutputRoot', $runtimeRoot,
    '-UseGuideXosRuntimePack', '-AllocationMode', 'Allocating',
    '-ProductionApplication', '-C188R12R4R5GcOwnershipProof',
    '-C188R12R4R5R1InitDiagnostics', '-C188R12R4R5R3InitReturnContract',
    '-ManagedProjectMode', 'GcOwnershipProof'
)
if ($ThreadStoreReadiness) { $managedBuildArguments += '-C188R12R4R5R4ThreadStoreReadiness' }
Invoke-Logged 'pwsh' $managedBuildArguments (Join-Path $candidate 'managed-build.log')

Require-File $pePath 'NativeAOT proof PE'
Require-File $mapPath 'NativeAOT proof map'
$runtimeManifestPath = Join-Path $runtimeRoot 'runtime-pack.manifest.json'
Require-File $runtimeManifestPath 'Runtime-pack manifest'
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
if (-not [bool]$runtimeManifest.c188r12r4r5GcOwnershipProof -or
    -not [bool]$runtimeManifest.c188r12r4r5r1InitDiagnostics -or
    -not [bool]$runtimeManifest.c188r12r4r5r3InitReturnContract -or
    [bool]$runtimeManifest.imageBackedArenaPresent -or
    [int64]$runtimeManifest.managedHeapBytes -ne 0 -or
    [string]$runtimeManifest.runtimeIdentity.sourceCommit -ne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
    throw 'Runtime-pack manifest failed the proof isolation or pinned-runtime gate.'
}
if ([bool]$runtimeManifest.c188r12r4r5r4ThreadStoreReadiness -ne [bool]$ThreadStoreReadiness) {
    throw 'Runtime-pack ThreadStore-readiness profile does not match the requested candidate mode.'
}
if ($ThreadStoreReadiness -and
    ([string]::IsNullOrWhiteSpace([string]$runtimeManifest.c188r12r4r5r4PalCommonReplacementObject) -or
     [string]::IsNullOrWhiteSpace([string]$runtimeManifest.c188r12r4r5r4PalCommonReplacementSha256))) {
    throw 'R4 runtime-pack manifest does not identify the active stack-bounds PAL replacement.'
}

Invoke-Logged $python @($converter, $pePath, $elfPath, '--map', $mapPath, '--symbol', 'ManagedMain') (Join-Path $candidate 'pe-to-elf.log')
Require-File $elfPath 'Converted NativeAOT proof ELF'
Invoke-Logged $objdump @('-p', $pePath) (Join-Path $candidate 'pe-imports.txt')
Invoke-Logged $readelf @('-h', '-l', '-S', '-r', '-s', '-d', $elfPath) (Join-Path $candidate 'elf-inspection.txt')
$mapText = Get-Content -LiteralPath $mapPath -Raw
$requiredSymbols = @(
    'ManagedMain', 'GuideXosNativeAotGcStartupInstallPalHooks',
    'GuideXosNativeAotGcStartupInstallHookTable',
    'GuideXosNativeAotGcStartupInstallPlatformHooks',
    'GuideXosNativeAotGcStartupMain', 'GuideXosNativeAotGcStartupGetState',
    'GuideXosNativeAotGcStartupGetRhInitializeRawResult',
    'GuideXosNativeAotGcStartupGetPreGcState',
    'GuideXosNativeAotGcStartupGetAllocationCount',
    'GuideXosNativeAotGcStartupGetLastAllocationSize',
    'GuideXosNativeAotGcStartupGetDiagnosticStage',
    'guideXosNativeAotInitTraceSetSink',
    'GuideXosNativeAotInitTraceGetCount',
    'GuideXosNativeAotInitTraceGetOverflow',
    'GuideXosNativeAotInitTraceGetInvocation',
    'GuideXosNativeAotInitTraceGetStage',
    'GuideXosNativeAotInitTraceGetEvent',
    'GuideXosNativeAotInitTraceGetResult',
    'GuideXosNativeAotInitTraceGetSingletonBits',
    'guideXosNativeAotC188R12R4R5ObserveObject',
    'guideXosNativeAotC188R12R4R5ReadCounters', 'RhpNewArray',
    'guideXosStockRhpNewArray', 'RhpNewFast', 'RhpCollect',
    'S_P_CoreLib_System_Runtime_InteropServices_GCHandle___ctor',
    'S_P_CoreLib_System_Runtime_InteropServices_GCHandle__Free'
)
foreach ($symbol in $requiredSymbols) {
    if (-not $mapText.Contains($symbol)) { throw "NativeAOT proof map lacks required symbol: $symbol" }
}
if ($ThreadStoreReadiness) {
    foreach ($symbol in @('guideXosStockRhpReversePInvoke', 'guideXosStockRhpReversePInvokeReturn', 'guideXosNativeAotThreadReadinessManagedMainEntered')) {
        if (-not $mapText.Contains($symbol)) { throw "ThreadStore-readiness NativeAOT map lacks required symbol: $symbol" }
    }
    if ($mapText -notmatch '(?s)PalGetMaximumStackBounds\s+[0-9A-Fa-f]{16}\s+f\s+Runtime\.WorkstationGC:guidexos_nativeaot_pal_common_replacement\.threadstore\.obj' -or
        $mapText.Contains('Runtime.WorkstationGC:PalRedhawkCommon.cpp.obj')) {
        throw 'R4 NativeAOT link did not select the freestanding stack-bounds PAL implementation.'
    }
}
if ($mapText.Contains('g_guideXosManagedHeap')) { throw 'NativeAOT proof image unexpectedly contains the C188 image-backed arena.' }

$exports = [ordered]@{
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PAL_ADDRESS = 'GuideXosNativeAotGcStartupInstallPalHooks'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_TABLE_ADDRESS = 'GuideXosNativeAotGcStartupInstallHookTable'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PLATFORM_ADDRESS = 'GuideXosNativeAotGcStartupInstallPlatformHooks'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_STARTUP_MAIN_ADDRESS = 'GuideXosNativeAotGcStartupMain'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_RH_INITIALIZE_RAW_RESULT_ADDRESS = 'GuideXosNativeAotGcStartupGetRhInitializeRawResult'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_STATE_ADDRESS = 'GuideXosNativeAotGcStartupGetState'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_PRE_GC_STATE_ADDRESS = 'GuideXosNativeAotGcStartupGetPreGcState'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_ALLOCATION_COUNT_ADDRESS = 'GuideXosNativeAotGcStartupGetAllocationCount'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_LAST_ALLOCATION_SIZE_ADDRESS = 'GuideXosNativeAotGcStartupGetLastAllocationSize'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTIC_STAGE_ADDRESS = 'GuideXosNativeAotGcStartupGetDiagnosticStage'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_SET_SINK_ADDRESS = 'guideXosNativeAotInitTraceSetSink'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_COUNT_ADDRESS = 'GuideXosNativeAotInitTraceGetCount'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_OVERFLOW_ADDRESS = 'GuideXosNativeAotInitTraceGetOverflow'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_INVOCATION_ADDRESS = 'GuideXosNativeAotInitTraceGetInvocation'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_STAGE_ADDRESS = 'GuideXosNativeAotInitTraceGetStage'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_EVENT_ADDRESS = 'GuideXosNativeAotInitTraceGetEvent'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_RESULT_ADDRESS = 'GuideXosNativeAotInitTraceGetResult'
    GUIDEXOS_NATIVEAOT_C188R12R4R5R1_TRACE_GET_SINGLETON_BITS_ADDRESS = 'GuideXosNativeAotInitTraceGetSingletonBits'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_MANAGED_MAIN_ADDRESS = 'ManagedMain'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_FINALIZE_ADDRESS = 'guideXosManagedAllocationFinalize'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTICS_ADDRESS = 'guideXosManagedAllocationGetDiagnostics'
}
$header = Join-Path $artifactRoot 'guidexos_nativeaot_gc_first_allocation_exports.h'
$headerLines = @('#pragma once', '', '#include <stdint.h>', '')
foreach ($define in $exports.Keys) {
    $pattern = '(?m)^\s+\S+\s+' + [regex]::Escape([string]$exports[$define]) + '\s+([0-9A-Fa-f]{16})(?:\s+f)?\s'
    $match = [regex]::Match($mapText, $pattern)
    if (-not $match.Success) { throw "PE map is missing startup entry $($exports[$define])." }
    $headerLines += "#define $define ((uintptr_t)0x$($match.Groups[1].Value)u)"
}
Set-Content -LiteralPath $header -Value $headerLines -Encoding ASCII

Invoke-Logged $objcopy @('-I', 'binary', '-O', 'pe-x86-64', '-B', 'i386:x86-64', $elfPath, $embeddedRaw) (Join-Path $candidate 'embed-raw.log')
$symbols = & $objdump -t $embeddedRaw
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect the embedded proof object symbols.' }
$startSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_start)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
$endSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_end)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
$sizeSymbol = ($symbols | Where-Object { $_ -match '(_binary_\S+_size)\s*$' } | ForEach-Object { $Matches[1] } | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($startSymbol) -or [string]::IsNullOrWhiteSpace($endSymbol) -or [string]::IsNullOrWhiteSpace($sizeSymbol)) {
    throw 'Embedded proof image symbols were not produced.'
}
Copy-Item -LiteralPath $embeddedRaw -Destination $embeddedObject
Invoke-Logged $objcopy @(
    '--redefine-sym', "${startSymbol}=guidexos_nativeaot_gc_startup_artifact_start",
    '--redefine-sym', "${endSymbol}=guidexos_nativeaot_gc_startup_artifact_end",
    '--redefine-sym', "${sizeSymbol}=guidexos_nativeaot_gc_startup_artifact_size",
    '--set-section-alignment', '.data=4096', '--rename-section', '.data=.data,alloc,load,readonly,data,contents',
    $embeddedObject
) (Join-Path $candidate 'embed-final.log')

$kernelFeatures = @(
    'GXOS_NATIVEAOT_PRODUCTION_APPLICATION',
    'GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    'GXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION',
    'GXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    'GXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES',
    'GXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER',
    'GXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT',
    'GXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA',
    'GXOS_NATIVEAOT_C118_MANAGED_LIST_BOX',
    'GXOS_NATIVEAOT_C119_MANAGED_BUTTON',
    'GXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST',
    'GXOS_NATIVEAOT_C121_MANAGED_CHECKBOX',
    'GXOS_NATIVEAOT_C122_MANAGED_LABEL',
    'GXOS_NATIVEAOT_C123_MANAGED_SEPARATOR',
    'GXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON',
    'GXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR',
    'GXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX',
    'GXOS_NATIVEAOT_C127_MANAGED_PANEL',
    'GXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE',
    'GXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT',
    'GXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX',
    'GXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON',
    'GXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX',
    'GXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING',
    'GXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU',
    'GXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU',
    'GXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING',
    'GXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR',
    'GXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT',
    'GXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW',
    'GXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK',
    'GXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK',
    'GXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX',
    'GXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER',
    'GXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG',
    'GXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE',
    'GXOS_NATIVEAOT_C147_RUNTIME_SETTINGS',
    'GXOS_NATIVEAOT_C148_SETTINGS_V2',
    'GXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING',
    'GXOS_NATIVEAOT_C150_MANAGED_APP_RETURN',
    'GXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    'GXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW',
    'GXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    'GXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS',
    'GXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    'GXOS_NATIVEAOT_C158_MANAGED_CALCULATOR',
    'GXOS_NATIVEAOT_C161_MANAGED_TASK_MANAGER',
    'GXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
    'GXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE',
    'GXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER',
    'GUIDEXOS_PROOF_CONTROL',
    'GXOS_NATIVEAOT_C188_EXTERNAL_APP_IDENTITY'
)
$kernelDefines = ($kernelFeatures | ForEach-Object { '-D' + $_ }) -join ' '
$kernelDefines += ' -DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST -DGXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS -DGXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT'
if ($ThreadStoreReadiness) { $kernelDefines += ' -DGXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS' }
$makeArgs = @(
    '-C', 'kernel', 'ARCH=amd64', "BUILD_DIR=$kernelBuildDir",
    'GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1',
    'GXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST=1',
    "NATIVEAOT_GC_STARTUP_QEMU_ARTIFACT_OBJ=$embeddedObject",
    "EXTRA_CFLAGS=$kernelDefines -I$artifactRoot"
)
Invoke-Logged $make $makeArgs (Join-Path $candidate 'kernel-build.log')
Require-File $kernel 'Isolated ownership kernel'
$kernelHash = Hash-File $kernel
$kernelLog = Get-Content -LiteralPath (Join-Path $candidate 'kernel-build.log') -Raw
if ($kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST' -or
    $kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS' -or
    $kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT' -or
    $kernelLog -notmatch 'gc-startup-artifact\.o') {
    throw 'Isolated kernel build does not show the ownership selector and embedded NativeAOT image.'
}
if ($ThreadStoreReadiness -and $kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5R4_THREADSTORE_READINESS') {
    throw 'ThreadStore-readiness kernel build does not show its isolated marker and handoff define.'
}

$espBoot = Join-Path $esp 'EFI\BOOT'
New-Item -ItemType Directory -Force -Path $espBoot | Out-Null
Copy-Item -LiteralPath $bootloader -Destination (Join-Path $espBoot 'BOOTX64.EFI')
Copy-Item -LiteralPath $kernel -Destination (Join-Path $esp 'kernel.elf')
Copy-Item -LiteralPath $ramdisk -Destination (Join-Path $esp 'ramdisk.img')
$candidateRamdiskHash = Hash-File (Join-Path $esp 'ramdisk.img')
if ($candidateRamdiskHash -ne $expectedRamdiskHash -or (Hash-File $ramdisk) -ne $expectedRamdiskHash) {
    throw 'Source or isolated candidate ramdisk SHA-256 changed during packaging.'
}

$productionRoot = Join-Path $candidate 'production'
$productionNativeAotRoot = Join-Path $productionRoot 'nativeaot-build'
$productionKernelRoot = Join-Path $productionRoot 'kernel'
New-Item -ItemType Directory -Force -Path $productionRoot | Out-Null
$productionNativeAotBuilder = Join-Path $root 'scripts\dotnet\build-c188-production-nativeaot.ps1'
$productionKernelBuilder = Join-Path $root 'scripts\dotnet\build-c188-production-kernel.ps1'
Invoke-Logged 'pwsh' @('-NoProfile','-ExecutionPolicy','Bypass','-File',$productionNativeAotBuilder,
    '-BuildRoot',$productionNativeAotRoot) (Join-Path $productionRoot 'production-nativeaot-build.log')
Invoke-Logged 'pwsh' @('-NoProfile','-ExecutionPolicy','Bypass','-File',$productionKernelBuilder,
    '-BuildRoot',$productionKernelRoot) (Join-Path $productionRoot 'production-kernel-build.log')
$productionManifestPath = Join-Path $productionNativeAotRoot 'production-nativeaot-qualification.json'
$productionKernelManifestPath = Join-Path $productionKernelRoot 'kernel-qualification.json'
Require-File $productionManifestPath 'Proof-disabled production NativeAOT qualification'
Require-File $productionKernelManifestPath 'Proof-disabled production kernel qualification'
$productionManifest = Get-Content -LiteralPath $productionManifestPath -Raw | ConvertFrom-Json
$productionKernelManifest = Get-Content -LiteralPath $productionKernelManifestPath -Raw | ConvertFrom-Json
if ($productionManifest.result -cne 'PASS' -or
    -not [bool]$productionManifest.persistentCompositeLifecycle -or
    $productionKernelManifest.result -cne 'PASS') {
    throw 'Proof-disabled production NativeAOT/kernel isolation qualification failed.'
}
$productionRuntimeManifestPath = [string]$productionManifest.runtimePackManifest
Require-File $productionRuntimeManifestPath 'Proof-disabled production runtime-pack manifest'
$productionRuntimeManifest = Get-Content -LiteralPath $productionRuntimeManifestPath -Raw | ConvertFrom-Json
$productionMapPath = Join-Path $productionNativeAotRoot 'nativeaot\artifacts\HostLogProof.map'
Require-File $productionMapPath 'Proof-disabled production NativeAOT map'
$productionMapText = Get-Content -LiteralPath $productionMapPath -Raw
if ([bool]$productionRuntimeManifest.c188r12r4r5GcOwnershipProof -or
    [bool]$productionRuntimeManifest.c188r12r4r5r1InitDiagnostics -or
    [bool]$productionRuntimeManifest.c188r12r4r5r3InitReturnContract -or
    [bool]$productionRuntimeManifest.c188r12r4r5r4ThreadStoreReadiness -or
    $productionMapText.Contains('C188R12R4R5R1_INIT') -or
    $productionMapText.Contains('C188R12R4R5R3_INIT') -or
    $productionMapText.Contains('C188R12R4R5R4_THREAD') -or
    $productionMapText.Contains('GuideXosNativeAotGcStartupGetRhInitializeRawResult')) {
    throw 'Proof-disabled production NativeAOT outputs contain C188 return-contract instrumentation.'
}

$manifest = [ordered]@{
    result = if ($ThreadStoreReadiness) { 'PASS_BUILD_READY_FOR_THREADSTORE_READINESS_GUEST' } else { 'PASS_BUILD_READY_FOR_INIT_RETURN_CONTRACT_GUEST' }
    repoRoot = $root
    head = (& git -C $root rev-parse HEAD).Trim()
    branch = (& git -C $root branch --show-current).Trim()
    candidateRoot = $candidate
    mode = if ($ThreadStoreReadiness) { 'ThreadStoreReadiness' } else { 'InitReturnContract' }
    productionApplicationStartupOnly = $true
    managedMainEnteredByCandidate = $false
    managedMainGateReason = if ($ThreadStoreReadiness) { 'Candidate is unbooted; a successful RhInitialize proceeds through module registration and the pinned stock reverse-P/Invoke attachment helper before ManagedMain.' } else { 'Runtime ThreadStore attachment is not exposed by a safe read-only accessor in this freestanding path.' }
    managedGraphCallbacks = 0
    isolatedKernelBuildDirectory = $kernelBuildDir
    runtimePackManifest = $runtimeManifestPath
    runtimePackIdentity = $runtimeManifest.identity
    runtimePackSourceCommit = $runtimeManifest.runtimeIdentity.sourceCommit
    runtimeInitDiagnostics = [bool]$runtimeManifest.c188r12r4r5r1InitDiagnostics
    c188R12R4R5R3InitReturnContract = [bool]$runtimeManifest.c188r12r4r5r3InitReturnContract
    c188R12R4R5R4ThreadStoreReadiness = [bool]$runtimeManifest.c188r12r4r5r4ThreadStoreReadiness
    threadStoreReadinessRuntimeSourceCommit = $runtimeManifest.c188r12r4r5r4ThreadStoreReadinessSourceCommit
    threadStoreReadinessRuntimeSourceFiles = $runtimeManifest.c188r12r4r5r4ThreadStoreReadinessSourceFiles
    threadStoreReadinessPalCommonStockMember = $runtimeManifest.c188r12r4r5r4PalCommonStockMember
    threadStoreReadinessPalCommonReplacementObject = $runtimeManifest.c188r12r4r5r4PalCommonReplacementObject
    threadStoreReadinessPalCommonReplacementSha256 = $runtimeManifest.c188r12r4r5r4PalCommonReplacementSha256
    threadStoreReadinessEvents = if ($ThreadStoreReadiness) { @('adapter_enter','rhinitialize_return','kernel_adapter_dispatch','kernel_init_result','managed_entry_blocked_init_failure','managed_entry_gate_open','tls_installed','tls_gsbase','runtime_instance','threadstore','current_thread_tls','current_thread_state','module_registration_enter','module_registration_complete','module_initialize_enter','module_initialize_complete','transition_enter','pal_attach_enter','pal_attach_existing','pal_attach_complete','pal_thread_id','pal_stack_bounds','pal_stack_current','transition_attach_complete','transition_attach_incomplete','managed_dispatch','managed_entry','transition_return') } else { @() }
    threadStoreTelemetryStackCapacity = if ($ThreadStoreReadiness) { 8 } else { 0 }
    runtimePackSha256 = $runtimeManifest.objectSha256
    runtimePackManifestSha256 = Hash-File $runtimeManifestPath
    managedPe = $pePath
    managedPeSha256 = Hash-File $pePath
    managedMapSha256 = Hash-File $mapPath
    managedElf = $elfPath
    managedElfSha256 = Hash-File $elfPath
    embeddedArtifactObject = $embeddedObject
    embeddedArtifactObjectSha256 = Hash-File $embeddedObject
    startupExportsHeader = $header
    specializedKernel = $kernel
    specializedKernelSha256 = $kernelHash
    esp = $esp
    espKernelSha256 = Hash-File (Join-Path $esp 'kernel.elf')
    sourceRamdiskSha256 = Hash-File $ramdisk
    candidateRamdiskSha256 = $candidateRamdiskHash
    initializationTraceMarker = 'C188R12R4R5R1_INIT'
    initializationReturnContractMarker = 'C188R12R4R5R3_INIT'
    initializationTraceCapacity = [int]$runtimeManifest.initializationTraceRecordCapacity
    initializationTraceSerialLineLimit = [int]$runtimeManifest.initializationTraceSerialLineLimit
    initializationTraceStageIds = [ordered]@{
        adapter='00000001'; rhInitialize='00000002'; palInit='00000010'; palFls='00000011'
        gcConfig='00000012'; gcToOs='00000013'; initDll='00000020'
        interfaceDispatch='00000021'; gcEventLock='00000022'; restrictedCallouts='00000023'
        runtimeInstance='00000024'; moduleIdentity='00000025'; startupImportLookup='00000026'
        debugHeaders='00000027'; gcInitialize='00000030'; gcHeapPublication='00000031'
        runtimeInstanceAllocation='00000028'; threadStoreCreate='00000029'; runtimeInstancePublish='0000002A'
        gcHeapInitialize='00000032'; gcFinalization='00000033'; gcHandleManager='00000034'
        cpuFeatures='00000040'; gcEventCreate='00000050'; gcVmReserve='00000051'
        gcVmCommit='00000052'; gcVmDecommit='00000053'; gcVmRelease='00000054'
        gcVmAdapterInit='00000055'; managedReversePInvoke='00000060'
        moduleRegistration='00000061'; codeManagerRegistration='00000062'; moduleInitialize='00000063'
    }
    initializationSingletonBits = [ordered]@{
        bit0='RuntimeInstance pointer published'; bit1='ThreadStore pointer available through RuntimeInstance'
        bit2='GC heap pointer published'; bit3='GC handle-manager pointer published'
        bit4='RhConfig pointer available'; bit5='guideXOS NativeAOT modules initialized'
        bit6='code manager registered'; bit7='ThreadStore current-thread accessor non-null (not proof of list attachment)'
    }
    adapterVersion = if ($ThreadStoreReadiness) { 'c188r12r4r5r4-threadstore-readiness-v1' } else { 'c188r12r4r5r3-init-return-contract-v1' }
    runnerVersion = if ($ThreadStoreReadiness) { 'c188r12r4r5r4r1-one-boot-guard-planned' } else { 'c188r12r4r5r1-qac-runner-v1' }
    nativeRhInitialize = [ordered]@{
        signature = 'extern "C" bool RhInitialize(bool isDll)'
        successBoolean = 1
        failureBoolean = 0
        adapterStatusSuccess = 0
        adapterStatusFailure = 1
        rawResultExport = 'GuideXosNativeAotGcStartupGetRhInitializeRawResult'
        moduleRegistrationIncluded = $false
        managedMainIncluded = $false
    }
    productionNativeAotManifest = $productionManifestPath
    productionNativeAotElfSha256 = [string]$productionManifest.elfSha256
    productionNativeAotPersistentCompositeLifecycle = [bool]$productionManifest.persistentCompositeLifecycle
    productionKernelManifest = $productionKernelManifestPath
    productionKernelSha256 = [string]$productionKernelManifest.kernelSha256
    productionIsolationChecks = [ordered]@{
        diagnosticAndProductionOutputsSeparate = $true
        diagnosticInstrumentationAbsentFromProduction = $true
        productionRuntimePackC188ProofProfile = [bool]$productionRuntimeManifest.c188r12r4r5GcOwnershipProof
        productionRuntimePackR1TraceProfile = [bool]$productionRuntimeManifest.c188r12r4r5r1InitDiagnostics
        productionRuntimePackR3ReturnProfile = [bool]$productionRuntimeManifest.c188r12r4r5r3InitReturnContract
        productionNativeAotMapExcludesReturnMarkers = $true
        productionPersistentCompositeLifecycle = [bool]$productionManifest.persistentCompositeLifecycle
        noGcOwnershipProofProfileInProduction = $true
    }
    qemuLaunched = $false
    qacReserved = $false
    imageBackedArenaPresent = [bool]$runtimeManifest.imageBackedArenaPresent
    managedHeapBytes = [int64]$runtimeManifest.managedHeapBytes
    collectionPrecedent = 'C011EC36 guest evidence: compacting collection, updated stack root and weak handle, RestartEE/resume'
    buildProvenance = [ordered]@{
        sourceHead = (& git -C $root rev-parse HEAD).Trim()
        sourceBranch = (& git -C $root branch --show-current).Trim()
        runtimeSourceCommit = $runtimeManifest.runtimeIdentity.sourceCommit
        candidateBuilder = $PSCommandPath
        candidateBuilderSha256 = Hash-File $PSCommandPath
        runtimePackBuilder = $runtimeManifest.buildCommandIdentity.script
        runtimePackBuilderSha256 = $runtimeManifest.buildCommandIdentity.scriptSha256
        runtimePackLockFile = $runtimeManifest.lockFile
        runtimePackLockFileSha256 = $runtimeManifest.lockFileSha256
        qemuLaunched = $false
        qacReserved = $false
    }
    artifactHashes = [ordered]@{
        candidateNativeAotPe = Hash-File $pePath
        candidateNativeAotMap = Hash-File $mapPath
        candidateNativeAotElf = Hash-File $elfPath
        candidateNativeAotBuildLog = Hash-File (Join-Path $candidate 'managed-build.log')
        candidatePeImports = Hash-File (Join-Path $candidate 'pe-imports.txt')
        candidateElfInspection = Hash-File (Join-Path $candidate 'elf-inspection.txt')
        candidatePeToElfLog = Hash-File (Join-Path $candidate 'pe-to-elf.log')
        startupAdapterObject = Hash-File (Join-Path $artifactRoot 'guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.obj')
        palMinWinReplacementObject = Hash-File (Join-Path $artifactRoot 'guidexos_nativeaot_pal_minwin_replacement.obj')
        threadStoreReadinessPalCommonReplacementObject = if ($ThreadStoreReadiness) { Hash-File ([string]$runtimeManifest.c188r12r4r5r4PalCommonReplacementObject) } else { $null }
        threadStoreReadinessHeader = if ($ThreadStoreReadiness) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_threadstore_readiness.h') } else { $null }
        embeddedNativeAotObject = Hash-File $embeddedObject
        embeddedNativeAotRawObject = Hash-File $embeddedRaw
        startupExportsHeader = Hash-File $header
        diagnosticKernelBuildLog = Hash-File (Join-Path $candidate 'kernel-build.log')
        diagnosticKernelElf = $kernelHash
        stagedEspKernel = Hash-File (Join-Path $esp 'kernel.elf')
        stagedEspBootloader = Hash-File (Join-Path $espBoot 'BOOTX64.EFI')
        stagedEspRamdisk = Hash-File (Join-Path $esp 'ramdisk.img')
        bootloader = Hash-File $bootloader
        runtimePackPlatformObject = Hash-File ([string]$runtimeManifest.object)
        runtimePackLibrary = Hash-File ([string]$runtimeManifest.adaptedRuntimeLibrary)
        runtimePackStartupReplacementSource = Hash-File (Join-Path $runtimeRoot 'guidexos_nativeaot_startup_replacement.cpp')
        runtimePackManifest = Hash-File $runtimeManifestPath
        runtimePackVersionHeader = Hash-File ([string]$runtimeManifest.c188r12r4r5r1RuntimeVersionHeader)
        productionNativeAotBuildLog = Hash-File (Join-Path $productionRoot 'production-nativeaot-build.log')
        productionNativeAotMap = Hash-File $productionMapPath
        sourceRamdisk = Hash-File $ramdisk
        productionNativeAotElf = Hash-File ([string]$productionManifest.elf)
        productionNativeAotQualification = Hash-File $productionManifestPath
        productionNativeAotRuntimePackManifest = Hash-File $productionRuntimeManifestPath
        productionKernelBuildLog = Hash-File (Join-Path $productionRoot 'production-kernel-build.log')
        productionKernelElf = Hash-File ([string]$productionKernelManifest.kernelElf)
        productionKernelQualification = Hash-File $productionKernelManifestPath
    }
}
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R12R4R5 candidate: READY ($candidate)"
Write-Output "Kernel SHA256: $kernelHash"
Write-Output "Candidate manifest: $manifestPath"
