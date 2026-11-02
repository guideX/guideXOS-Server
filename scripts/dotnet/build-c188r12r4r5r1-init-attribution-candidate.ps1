param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$CandidateRoot = '',
    [switch]$ThreadStoreReadiness,
    [switch]$GcInitProbes,
    [switch]$SemiSharedAttribution,
    [switch]$NativeAllocationAttribution,
    [switch]$ResumeCandidate
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $phaseRoot = if ($NativeAllocationAttribution) { 'c188r12r4r5r4r5r2r2-native-allocation-attribution' } elseif ($SemiSharedAttribution) { 'c188r12r4r5r4r5r2-semishared-attribution' } elseif ($GcInitProbes) { 'c188r12r4r5r4r5-gc-init-probes' } elseif ($ThreadStoreReadiness) { 'c188r12r4r5r4-threadstore-readiness' } else { 'c188r12r4r5r3-init-return-contract' }
    $CandidateRoot = Join-Path $root "out\dotnet\$phaseRoot\candidate-$stamp"
}
if ($GcInitProbes -and -not $ThreadStoreReadiness) {
    throw 'C188R12R4R5R4R5 requires the R4 ThreadStore diagnostic profile.'
}
if ($SemiSharedAttribution -and (-not $GcInitProbes -or -not $ThreadStoreReadiness)) {
    throw 'C188R12R4R5R4R5R2 requires both the R4R5 and R4 diagnostic profiles.'
}
if ($NativeAllocationAttribution -and (-not $SemiSharedAttribution -or -not $GcInitProbes -or -not $ThreadStoreReadiness)) {
    throw 'C188R12R4R5R4R5R2R2 requires the preceding R2, R4R5, and R4 diagnostic profiles.'
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$allowed = [System.IO.Path]::GetFullPath((Join-Path $root 'out\dotnet')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($allowed, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate output must remain below out\dotnet: $candidate"
}
if ((Test-Path -LiteralPath $candidate) -and -not $ResumeCandidate) { throw "Refusing to overwrite candidate: $candidate" }
if ($ResumeCandidate -and -not $candidate.StartsWith(
    [System.IO.Path]::GetFullPath((Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2-semishared-attribution')) + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'ResumeCandidate is limited to the isolated R2 output root.'
}

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
$kernelBuildPrefix = if ($NativeAllocationAttribution) { 'build/c188r12r4r5r4r5r2r2-native-allocation-' } elseif ($SemiSharedAttribution) { 'build/c188r12r4r5r4r5r2-semishared-' } elseif ($GcInitProbes) { 'build/c188r12r4r5r4r5-gc-init-probes-' } elseif ($ThreadStoreReadiness) { 'build/c188r12r4r5r4-threadstore-readiness-' } else { 'build/c188r12r4r5r1-init-attribution-' }
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
if ($GcInitProbes) { $managedBuildArguments += '-C188R12R4R5R4R5GcInitProbes' }
if ($SemiSharedAttribution) { $managedBuildArguments += '-C188R12R4R5R4R5R2SemiSharedAttribution' }
if ($NativeAllocationAttribution) { $managedBuildArguments += '-C188R12R4R5R4R5R2R2NativeAllocationAttribution' }
if ($ResumeCandidate) {
    if (-not (Test-Path -LiteralPath $pePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $mapPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $elfPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'runtime-pack.manifest.json') -PathType Leaf) -or
        (Test-Path -LiteralPath (Join-Path $candidate 'candidate-manifest.json'))) {
        throw 'ResumeCandidate requires the existing diagnostic NativeAOT outputs and no final candidate manifest.'
    }
} else {
    New-Item -ItemType Directory -Force -Path $candidate | Out-Null
    Invoke-Logged 'pwsh' $managedBuildArguments (Join-Path $candidate 'managed-build.log')
}

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
if ([bool]$runtimeManifest.c188r12r4r5r4r5GcInitProbes -ne [bool]$GcInitProbes) {
    throw 'Runtime-pack GC-init probe profile does not match the requested candidate mode.'
}
if ($GcInitProbes -and
    ([string]$runtimeManifest.c188r12r4r5r4r5GcInitMarker -cne 'C188R12R4R5R4R5_GCINIT' -or
     [int]$runtimeManifest.c188r12r4r5r4r5GcInitRecordSizeBytes -ne 64 -or
     [int]$runtimeManifest.c188r12r4r5r4r5GcInitFieldCount -ne 16)) {
    throw 'Runtime-pack GC-init record layout does not match the bounded source contract.'
}
if ([bool]$runtimeManifest.c188r12r4r5r4r5r2SemiSharedAttribution -ne [bool]$SemiSharedAttribution) {
    throw 'Runtime-pack semi-shared attribution profile does not match the requested candidate mode.'
}
if ($SemiSharedAttribution -and
    ([string]$runtimeManifest.c188r12r4r5r4r5r2SemiSharedMarker -cne 'C188R12R4R5R4R5R2_SEMISHARED' -or
     [int]$runtimeManifest.c188r12r4r5r4r5r2OperationCapacity -ne 48 -or
     [int]$runtimeManifest.c188r12r4r5r4r5r2OperationFieldCount -ne 14 -or
     [int]$runtimeManifest.c188r12r4r5r4r5r2SummaryFieldCount -ne 15)) {
    throw 'Runtime-pack semi-shared record layout does not match the bounded source contract.'
}
if ([bool]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttribution -ne [bool]$NativeAllocationAttribution) {
    throw 'Runtime-pack native allocation attribution profile does not match the requested candidate mode.'
}
if ($NativeAllocationAttribution -and
    ([string]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationMarker -cne 'C188R12R4R5R4R5R2R2_NATIVE_ALLOC' -or
     [int]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttemptCapacity -ne 8 -or
     [int]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttemptFieldCount -ne 25 -or
     [int]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationSummaryFieldCount -ne 9)) {
    throw 'Runtime-pack native allocation record layout does not match the bounded source contract.'
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
if ($GcInitProbes) {
    # The begin/complete helpers are intentionally small and may be inlined
    # into the linked GCHeap::Initialize body under /O2.  Require the external
    # post-return accessor plus the HRESULT wrapper that remains callable,
    # along with the actual Workstation-GC object identity below.
    $requiredSymbols += @('GuideXosNativeAotGcInitRecordHeapResult','GuideXosNativeAotGcInitGetField')
}
if ($SemiSharedAttribution) {
    $requiredSymbols += @(
        'GuideXosNativeAotSemiSharedGetSummaryField',
        'GuideXosNativeAotSemiSharedGetOperationCount',
        'GuideXosNativeAotSemiSharedGetOperationField'
    )
}
if ($NativeAllocationAttribution) {
    $requiredSymbols += @(
        'GuideXosNativeAotNativeAllocGetSummaryField',
        'GuideXosNativeAotNativeAllocGetAttemptCount',
        'GuideXosNativeAotNativeAllocGetAttemptField',
        'guidexos_nativeaot_pal_virtual_alloc'
    )
}
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
if ($GcInitProbes -and -not $mapText.Contains('Runtime.WorkstationGC:gcwks.cpp.obj')) {
    throw 'The NativeAOT map does not identify the instrumented pinned Workstation GC object.'
}
if ($GcInitProbes -and $mapText -notmatch '\?Initialize@GCHeap@WKS@@UEAAJXZ\s+[0-9A-Fa-f]{16}\s+f\s+Runtime\.WorkstationGC:gcwks\.cpp\.obj') {
    throw 'The NativeAOT map does not retain the concrete instrumented Workstation GCHeap::Initialize implementation.'
}
if ($SemiSharedAttribution -and
    (-not $mapText.Contains('Runtime.WorkstationGC:gcwks.cpp.obj') -or
     -not $mapText.Contains('GuideXosNativeAotSemiSharedGetOperationField'))) {
    throw 'Diagnostic map lacks the linked semi-shared record and pinned Workstation GC object.'
}
if ($NativeAllocationAttribution -and
    ($mapText -notmatch '(?m)^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+\?\?2@YAPEAX_KAEBUnothrow_t@std@@@Z\s+[0-9A-F]{16}\s+f\s+guidexos_nativeaot_startup_imports\.production\.obj\r?$' -or
     $mapText -notmatch '(?m)^\s+[0-9A-F]{4}:[0-9A-F]{8}\s+guidexos_nativeaot_pal_virtual_alloc\s+[0-9A-F]{16}\s+f\s+guidexos_nativeaot_pal_contract\.obj\r?$' -or
     -not $mapText.Contains('GuideXosNativeAotNativeAllocGetAttemptField'))) {
    throw 'Diagnostic map does not prove the selected nothrow overload, PAL callback, and native allocation record linkage.'
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
if ($GcInitProbes) { $kernelDefines += ' -DGXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES' }
if ($SemiSharedAttribution) { $kernelDefines += ' -DGXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED' }
if ($NativeAllocationAttribution) { $kernelDefines += ' -DGXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC' }
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
if ($GcInitProbes -and $kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5R4R5_GC_INIT_PROBES') {
    throw 'GC-init probe kernel build does not show its isolated marker define.'
}
if ($SemiSharedAttribution -and $kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED') {
    throw 'Semi-shared attribution kernel build does not show its isolated marker define.'
}
if ($NativeAllocationAttribution -and $kernelLog -notmatch '-DGXOS_NATIVEAOT_C188R12R4R5R4R5R2R2_NATIVE_ALLOC') {
    throw 'Native-allocation diagnostic kernel build does not show its isolated marker define.'
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

$stagedRoot = if ($ResumeCandidate -and (Test-Path -LiteralPath (Join-Path $candidate 'staged'))) {
    Join-Path $candidate 'staged-final'
} else { Join-Path $candidate 'staged' }
$stagedDiagnostic = Join-Path $stagedRoot 'diagnostic'
$stagedRuntimeSource = Join-Path $stagedRoot 'diagnostic\runtime-pack'
New-Item -ItemType Directory -Force -Path $stagedDiagnostic,$stagedRuntimeSource | Out-Null
Copy-Item -LiteralPath $esp -Destination $stagedDiagnostic -Recurse
if ($SemiSharedAttribution) {
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'gcinit-workstation-source\gcwks.cpp') -Destination (Join-Path $stagedRuntimeSource 'gcwks.cpp')
    Copy-Item -LiteralPath (Join-Path $runtimeRoot 'gcinit-workstation-source\gc.cpp') -Destination (Join-Path $stagedRuntimeSource 'gc.cpp')
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h') -Destination $stagedRuntimeSource
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.cpp') -Destination $stagedRuntimeSource
}
if ($NativeAllocationAttribution) {
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_native_alloc_record.h') -Destination $stagedRuntimeSource
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_native_alloc_record.cpp') -Destination $stagedRuntimeSource
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp') -Destination $stagedRuntimeSource
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_contract.cpp') -Destination $stagedRuntimeSource
    Copy-Item -LiteralPath (Join-Path $root 'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp') -Destination $stagedRuntimeSource
    Copy-Item -LiteralPath (Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1') -Destination $stagedDiagnostic
}
$stagedFiles = @(Get-ChildItem -LiteralPath $stagedRoot -File -Recurse | ForEach-Object {
    $relativePath = $_.FullName.Substring($stagedRoot.Length + 1).Replace('\','/')
    [ordered]@{ path=$relativePath; sizeBytes=$_.Length; sha256=Hash-File $_.FullName }
})
$stagedQualification = [ordered]@{
    result = 'PASS'
    phase = if ($NativeAllocationAttribution) { 'C188R12R4R5R4R5R2R2' } elseif ($SemiSharedAttribution) { 'C188R12R4R5R4R5R2' } else { 'C188R12R4R5R4R5' }
    sourceHead = (& git -C $root rev-parse HEAD).Trim()
    runtimeSourceCommit = [string]$runtimeManifest.runtimeIdentity.sourceCommit
    files = $stagedFiles
}
$stagedQualificationPath = Join-Path $stagedRoot 'qualification-manifest.json'
[System.IO.File]::WriteAllText($stagedQualificationPath,($stagedQualification | ConvertTo-Json -Depth 6),[System.Text.UTF8Encoding]::new($false))

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
    [bool]$productionRuntimeManifest.c188r12r4r5r4r5GcInitProbes -or
    [bool]$productionRuntimeManifest.c188r12r4r5r4r5r2SemiSharedAttribution -or
    [bool]$productionRuntimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttribution -or
    $productionMapText.Contains('C188R12R4R5R1_INIT') -or
    $productionMapText.Contains('C188R12R4R5R3_INIT') -or
    $productionMapText.Contains('C188R12R4R5R4_THREAD') -or
    $productionMapText.Contains('C188R12R4R5R4R5_GCINIT') -or
    $productionMapText.Contains('GuideXosNativeAotGcInit') -or
    $productionMapText.Contains('SemiShared') -or
    $productionMapText.Contains('C188R12R4R5R4R5R2_SEMISHARED') -or
    $productionMapText.Contains('C188R12R4R5R4R5R2R2_NATIVE_ALLOC') -or
    $productionMapText.Contains('GuideXosNativeAotNativeAllocGetAttemptField') -or
    $productionMapText.Contains('GuideXosNativeAotGcStartupGetRhInitializeRawResult')) {
    throw 'Proof-disabled production NativeAOT outputs contain C188 return-contract instrumentation.'
}

$branchMapPath = Join-Path $candidate 'native-allocation-failure-branch-map.json'
$branchMap = [ordered]@{
    marker = 'C188R12R4R5R4R5R2R2_NATIVE_ALLOC'
    operations = [ordered]@{
        '0x0200'='GCEvent::Impl allocation site'; '0x0201'='global nothrow operator new'
        '0x0202'='guideXos startup malloc'; '0x0203'='PAL virtual allocation'
        '0x0204'='guest kernel virtual allocation adapter'; '0x0205'='true VM reserve'
        '0x0206'='true VM commit'; '0x0207'='rollback release'; '0x0208'='final pointer'
    }
    failureBranches = [ordered]@{
        '0x0301'='scoped record overflow'; '0x0302'='malloc header-size overflow'
        '0x0303'='PAL not ready'; '0x0304'='PAL callback null; status unavailable at pointer-only boundary'
        '0x0310'='kernel VM-slot capacity'; '0x0311'='VM reserve failure'
        '0x0312'='VM commit failure'; '0x0313'='rollback release failure'
        '0x03FF'='source observation incomplete'
        '0x0401'='reserve lacks current address space'; '0x0402'='reserve invalid argument'
        '0x0403'='reserve region already reserved'; '0x0404'='reserve alignment invalid'
        '0x0405'='reserve range overflow or backend limit'; '0x0406'='preferred base alignment invalid'
        '0x0407'='preferred base outside runtime range'; '0x0408'='virtual interval unavailable'
        '0x0409'='VM region metadata exhausted'; '0x0410'='commit region state missing'
        '0x0411'='commit protection unsupported'; '0x0412'='commit page range/alignment invalid'
        '0x0413'='physical frame allocation failed'; '0x0414'='physical frame zeroing failed'
        '0x0415'='page-table mapping failed'; '0x0416'='protection update failed'
    }
    statusSources = [ordered]@{ '0'='unavailable at source API'; '1'='raw VmResult before rollback'; '2'='kernel bool/capacity diagnostic status' }
    vmResultValues = [ordered]@{ '0'='Ok'; '6'='AddressUnavailable'; '7'='OutOfMemory'; '13'='Unsupported'; '14'='HostFailure' }
}
$branchMap | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $branchMapPath -Encoding utf8
$hostedTestRoot = Join-Path $candidate 'hosted-tests'
$hostedTestRunner = Join-Path $root 'tests\dotnet\runtime-pack\c188r12r4r5r4r5r2r2_native_alloc_hosted_tests.ps1'
Invoke-Logged 'pwsh' @('-NoProfile','-ExecutionPolicy','Bypass','-File',$hostedTestRunner,
    '-RepoRoot',$root,'-OutputDirectory',$hostedTestRoot,'-DiagnosticMap',$mapPath,
    '-ProductionRuntimeManifest',$productionRuntimeManifestPath,
    '-StartupImportsObject',(Join-Path $runtimeRoot 'guidexos_nativeaot_startup_imports.production.obj'),
    '-PalContractObject',(Join-Path $artifactRoot 'guidexos_nativeaot_pal_contract.obj')) (Join-Path $candidate 'hosted-tests.log')
Require-File (Join-Path $hostedTestRoot 'hosted-test-results.json') 'Native allocation hosted-test result'
$hostedTestResults = Get-Content -LiteralPath (Join-Path $hostedTestRoot 'hosted-test-results.json') -Raw | ConvertFrom-Json
if ($hostedTestResults.result -cne 'PASS' -or [bool]$hostedTestResults.qemuLaunched -or [bool]$hostedTestResults.qacReserved) {
    throw 'Native allocation hosted qualification failed or crossed the hosted-only boundary.'
}
if ([string]::IsNullOrWhiteSpace([string]$hostedTestResults.actualOperatorNewIntegrationTest)) {
    throw 'Hosted qualification did not execute the actual linked nothrow operator-new and PAL integration test.'
}

$manifest = [ordered]@{
    result = if ($NativeAllocationAttribution) { 'NATIVE_ALLOC_PROBES_HOSTED_READY' } elseif ($SemiSharedAttribution) { 'PASS_BUILD_READY_FOR_SEMISHARED_ATTRIBUTION_GUEST' } elseif ($GcInitProbes) { 'PASS_BUILD_READY_FOR_GC_INIT_ATTRIBUTION_GUEST' } elseif ($ThreadStoreReadiness) { 'PASS_BUILD_READY_FOR_THREADSTORE_READINESS' } else { 'PASS_BUILD_READY_FOR_INIT_RETURN_CONTRACT_GUEST' }
    repoRoot = $root
    head = (& git -C $root rev-parse HEAD).Trim()
    branch = (& git -C $root branch --show-current).Trim()
    candidateRoot = $candidate
    mode = if ($NativeAllocationAttribution) { 'NativeAllocationFailureAttribution' } elseif ($SemiSharedAttribution) { 'SemiSharedAttribution' } elseif ($GcInitProbes) { 'GcInitFailureAttribution' } elseif ($ThreadStoreReadiness) { 'ThreadStoreReadiness' } else { 'InitReturnContract' }
    productionApplicationStartupOnly = $true
    managedMainEnteredByCandidate = $false
    managedMainGateReason = if ($GcInitProbes) { 'Candidate is unbooted; after RhInitialize returns, the adapter snapshots the internal GC initialization record. Managed dispatch remains governed by actual initialization success and the established R4 thread-attachment gate.' } elseif ($ThreadStoreReadiness) { 'Candidate is unbooted; a successful RhInitialize proceeds through module registration and the pinned stock reverse-P/Invoke attachment helper before ManagedMain.' } else { 'Runtime ThreadStore attachment is not exposed by a safe read-only accessor in this freestanding path.' }
    managedGraphCallbacks = 0
    isolatedKernelBuildDirectory = $kernelBuildDir
    runtimePackManifest = $runtimeManifestPath
    runtimePackIdentity = $runtimeManifest.identity
    runtimePackSourceCommit = $runtimeManifest.runtimeIdentity.sourceCommit
    runtimeInitDiagnostics = [bool]$runtimeManifest.c188r12r4r5r1InitDiagnostics
    c188R12R4R5R3InitReturnContract = [bool]$runtimeManifest.c188r12r4r5r3InitReturnContract
    c188R12R4R5R4ThreadStoreReadiness = [bool]$runtimeManifest.c188r12r4r5r4ThreadStoreReadiness
    c188R12R4R5R4R5GcInitProbes = [bool]$runtimeManifest.c188r12r4r5r4r5GcInitProbes
    c188R12R4R5R4R5R2SemiSharedAttribution = [bool]$runtimeManifest.c188r12r4r5r4r5r2SemiSharedAttribution
    c188R12R4R5R4R5R2Marker = [string]$runtimeManifest.c188r12r4r5r4r5r2SemiSharedMarker
    c188R12R4R5R4R5R2OperationCapacity = [int]$runtimeManifest.c188r12r4r5r4r5r2OperationCapacity
    c188R12R4R5R4R5R2OperationFieldCount = [int]$runtimeManifest.c188r12r4r5r4r5r2OperationFieldCount
    c188R12R4R5R4R5R2SummaryFieldCount = [int]$runtimeManifest.c188r12r4r5r4r5r2SummaryFieldCount
    c188R12R4R5R4R5R2R2NativeAllocationAttribution = [bool]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttribution
    c188R12R4R5R4R5R2R2NativeAllocationMarker = [string]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationMarker
    c188R12R4R5R4R5R2R2NativeAllocationAttemptCapacity = [int]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttemptCapacity
    c188R12R4R5R4R5R2R2NativeAllocationAttemptFieldCount = [int]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationAttemptFieldCount
    c188R12R4R5R4R5R2R2NativeAllocationSummaryFieldCount = [int]$runtimeManifest.c188r12r4r5r4r5r2r2NativeAllocationSummaryFieldCount
    nativeAllocationAnalyzer = Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1'
    nativeAllocationFailureBranchMap = $branchMapPath
    nativeAllocationHostedTestResults = Join-Path $hostedTestRoot 'hosted-test-results.json'
    nativeAllocationHostedTestClassifications = @($hostedTestResults.fixtureClassifications)
    nextGuestPhase = 'C188R12R4R5R4R5R2R2R1_Native_Allocation_Failure_Guest_Attribution'
    nextGuestRunPerformed = $false
    semiSharedSourceFiles = $runtimeManifest.c188r12r4r5r4r5r2SourceFiles
    gcInitProbeSourceFiles = $runtimeManifest.c188r12r4r5r4r5GcInitSourceFiles
    gcInitRecordSha256 = $runtimeManifest.c188r12r4r5r4r5GcInitRecordObjectSha256
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
    semiSharedAttributionSerialLineLimit = if ($SemiSharedAttribution) { 384 } else { 0 }
    nativeAllocationAttributionSerialLineLimit = if ($NativeAllocationAttribution) { 768 } else { 0 }
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
        gcInternalRecord='00000070'
    }
    initializationSingletonBits = [ordered]@{
        bit0='RuntimeInstance pointer published'; bit1='ThreadStore pointer available through RuntimeInstance'
        bit2='GC heap pointer published'; bit3='GC handle-manager pointer published'
        bit4='RhConfig pointer available'; bit5='guideXOS NativeAOT modules initialized'
        bit6='code manager registered'; bit7='ThreadStore current-thread accessor non-null (not proof of list attachment)'
    }
    adapterVersion = if ($NativeAllocationAttribution) { 'c188r12r4r5r4r5r2r2-native-allocation-v1' } elseif ($SemiSharedAttribution) { 'c188r12r4r5r4r5r2-semishared-v1' } elseif ($GcInitProbes) { 'c188r12r4r5r4r5-gc-init-probes-v1' } elseif ($ThreadStoreReadiness) { 'c188r12r4r5r4-threadstore-readiness-v1' } else { 'c188r12r4r5r3-init-return-contract-v1' }
    gcInitializationInternalRecord = if ($GcInitProbes) { [ordered]@{
        marker='C188R12R4R5R4R5_GCINIT'
        serialStageSentinel='00000070'
        recordSizeBytes=[int]$runtimeManifest.c188r12r4r5r4r5GcInitRecordSizeBytes
        fieldCount=[int]$runtimeManifest.c188r12r4r5r4r5GcInitFieldCount
        fields=@($runtimeManifest.c188r12r4r5r4r5GcInitFields)
        snapshotTiming='after RhInitialize returns'
        inFlightOperationsFinalizedAs='did_not_return'
        nativeStatusAvailable=$false
    } } else { $null }
    semiSharedAttributionRecord = if ($SemiSharedAttribution) { [ordered]@{
        marker='C188R12R4R5R4R5R2_SEMISHARED'
        eventStage='00000071'
        operationCapacity=[int]$runtimeManifest.c188r12r4r5r4r5r2OperationCapacity
        operationFieldCount=[int]$runtimeManifest.c188r12r4r5r4r5r2OperationFieldCount
        summaryFieldCount=[int]$runtimeManifest.c188r12r4r5r4r5r2SummaryFieldCount
        snapshotTiming='after RhInitialize returns; fixed record is read after partial GC cleanup'
        nativeStatusAvailability='raw event-close callback status is retained; other PAL failure codes are unavailable when APIs return only pointers or booleans'
        failureBranchHeader=(Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h')
        stagedRoot=$stagedRoot
        stagedQualificationManifest=$stagedQualificationPath
    } } else { $null }
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
        productionRuntimePackR2SemiSharedProfile = [bool]$productionRuntimeManifest.c188r12r4r5r4r5r2SemiSharedAttribution
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
        gcInitRecordHeader = if ($GcInitProbes) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcinit_record.h') } else { $null }
        gcInitRecordImplementation = if ($GcInitProbes) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_gcinit_record.cpp') } else { $null }
        gcInitReplacementObject = if ($GcInitProbes) { Hash-File ([string]$runtimeManifest.c188r12r4r5r4r5GcInitRecordObject) } else { $null }
        gcInitInstrumentedSource = if ($GcInitProbes) { [string]$runtimeManifest.c188r12r4r5r4r5GcInitSourceFiles.instrumentedGc } else { $null }
        gcInitMarkerSinkSource = if ($GcInitProbes) { Hash-File (Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp') } else { $null }
        semiSharedRecordHeader = if ($SemiSharedAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.h') } else { $null }
        semiSharedRecordImplementation = if ($SemiSharedAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_semishared_record.cpp') } else { $null }
        instrumentedSemiSharedGcSource = if ($SemiSharedAttribution) { Hash-File (Join-Path $runtimeRoot 'gcinit-workstation-source\gc.cpp') } else { $null }
        instrumentedSemiSharedGcWrapper = if ($SemiSharedAttribution) { Hash-File (Join-Path $runtimeRoot 'gcinit-workstation-source\gcwks.cpp') } else { $null }
        semiSharedMarkerSinkSource = if ($SemiSharedAttribution) { Hash-File (Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp') } else { $null }
        nativeAllocationRecordHeader = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_native_alloc_record.h') } else { $null }
        nativeAllocationRecordImplementation = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_native_alloc_record.cpp') } else { $null }
        nativeAllocationGcEnvironmentSource = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\gcenv\guidexos_gcenv.cpp') } else { $null }
        nativeAllocationStartupImportsSource = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp') } else { $null }
        nativeAllocationPalContractSource = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_pal_contract.cpp') } else { $null }
        nativeAllocationVmBackendSource = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'runtime\memory\guidexos_virtual_memory_region_baremetal.cpp') } else { $null }
        nativeAllocationKernelPalSource = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'kernel\core\nativeaot_pal_qemu_test.cpp') } else { $null }
        nativeAllocationAnalyzer = if ($NativeAllocationAttribution) { Hash-File (Join-Path $root 'scripts\dotnet\analyze-c188r12r4r5r4r5r2r2-native-alloc.ps1') } else { $null }
        nativeAllocationBranchMap = if ($NativeAllocationAttribution) { Hash-File $branchMapPath } else { $null }
        nativeAllocationHostedTests = if ($NativeAllocationAttribution) { Hash-File (Join-Path $hostedTestRoot 'hosted-test-results.json') } else { $null }
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
