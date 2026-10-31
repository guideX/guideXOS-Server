param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$CandidateRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $CandidateRoot = Join-Path $root "out\dotnet\c188r12r4r5-gc-ownership-proof\candidate-$stamp"
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
$kernelBuildDir = 'build/c188r12r4r5-gc-ownership-' + [guid]::NewGuid().ToString('N')
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
$ovmf = Join-Path $root 'OVMF.fd'
foreach ($requirement in @(
    @($bootloader, 'UEFI bootloader'), @($objcopy, 'objcopy'), @($objdump, 'objdump'),
    @($readelf, 'readelf'), @($python, 'Bundled Python'), @($converter, 'PE to ELF converter'),
    @($qemu, 'QEMU executable'), @($ovmf, 'OVMF image')
)) { Require-File $requirement[0] $requirement[1] }
if ([string]::IsNullOrWhiteSpace($make)) { throw 'mingw32-make.exe/make.exe was not found.' }

New-Item -ItemType Directory -Force -Path $candidate | Out-Null
$managedBuilder = Join-Path $root 'scripts\dotnet\build-managed-hostlog-proof.ps1'
Invoke-Logged 'pwsh' @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $managedBuilder,
    '-RepoRoot', $root, '-LegacyRoot', 'D:\dev\guideXOSUEFI',
    '-OutputRoot', $managedRoot, '-RuntimePackOutputRoot', $runtimeRoot,
    '-UseGuideXosRuntimePack', '-AllocationMode', 'Allocating',
    '-ProductionApplication', '-C188R12R4R5GcOwnershipProof',
    '-ManagedProjectMode', 'GcOwnershipProof'
) (Join-Path $candidate 'managed-build.log')

Require-File $pePath 'NativeAOT proof PE'
Require-File $mapPath 'NativeAOT proof map'
$runtimeManifestPath = Join-Path $runtimeRoot 'runtime-pack.manifest.json'
Require-File $runtimeManifestPath 'Runtime-pack manifest'
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
if (-not [bool]$runtimeManifest.c188r12r4r5GcOwnershipProof -or
    [bool]$runtimeManifest.imageBackedArenaPresent -or
    [int64]$runtimeManifest.managedHeapBytes -ne 0 -or
    [string]$runtimeManifest.runtimeIdentity.sourceCommit -ne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
    throw 'Runtime-pack manifest failed the proof isolation or pinned-runtime gate.'
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
    'GuideXosNativeAotGcStartupGetPreGcState',
    'GuideXosNativeAotGcStartupGetAllocationCount',
    'GuideXosNativeAotGcStartupGetLastAllocationSize',
    'GuideXosNativeAotGcStartupGetDiagnosticStage',
    'guideXosNativeAotC188R12R4R5ObserveObject',
    'guideXosNativeAotC188R12R4R5ReadCounters', 'RhpNewArray',
    'guideXosStockRhpNewArray', 'RhpNewFast', 'RhpCollect',
    'S_P_CoreLib_System_Runtime_InteropServices_GCHandle___ctor',
    'S_P_CoreLib_System_Runtime_InteropServices_GCHandle__Free'
)
foreach ($symbol in $requiredSymbols) {
    if (-not $mapText.Contains($symbol)) { throw "NativeAOT proof map lacks required symbol: $symbol" }
}
if ($mapText.Contains('g_guideXosManagedHeap')) { throw 'NativeAOT proof image unexpectedly contains the C188 image-backed arena.' }

$exports = [ordered]@{
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PAL_ADDRESS = 'GuideXosNativeAotGcStartupInstallPalHooks'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_TABLE_ADDRESS = 'GuideXosNativeAotGcStartupInstallHookTable'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_INSTALL_PLATFORM_ADDRESS = 'GuideXosNativeAotGcStartupInstallPlatformHooks'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_STARTUP_MAIN_ADDRESS = 'GuideXosNativeAotGcStartupMain'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_STATE_ADDRESS = 'GuideXosNativeAotGcStartupGetState'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_PRE_GC_STATE_ADDRESS = 'GuideXosNativeAotGcStartupGetPreGcState'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_ALLOCATION_COUNT_ADDRESS = 'GuideXosNativeAotGcStartupGetAllocationCount'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_LAST_ALLOCATION_SIZE_ADDRESS = 'GuideXosNativeAotGcStartupGetLastAllocationSize'
    GUIDEXOS_NATIVEAOT_GC_FIRST_ALLOCATION_GET_DIAGNOSTIC_STAGE_ADDRESS = 'GuideXosNativeAotGcStartupGetDiagnosticStage'
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
$kernelDefines += ' -DGXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST -DGXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_QEMU_TEST'
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
    $kernelLog -notmatch 'gc-startup-artifact\.o') {
    throw 'Isolated kernel build does not show the ownership selector and embedded NativeAOT image.'
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

$manifest = [ordered]@{
    result = 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT'
    repoRoot = $root
    head = (& git -C $root rev-parse HEAD).Trim()
    branch = (& git -C $root branch --show-current).Trim()
    candidateRoot = $candidate
    mode = 'GcOwnershipProof'
    productionApplicationStartupOnly = $true
    managedGraphCallbacks = 3
    isolatedKernelBuildDirectory = $kernelBuildDir
    runtimePackManifest = $runtimeManifestPath
    runtimePackIdentity = $runtimeManifest.identity
    runtimePackSourceCommit = $runtimeManifest.runtimeIdentity.sourceCommit
    runtimePackSha256 = $runtimeManifest.objectSha256
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
    imageBackedArenaPresent = [bool]$runtimeManifest.imageBackedArenaPresent
    managedHeapBytes = [int64]$runtimeManifest.managedHeapBytes
    collectionPrecedent = 'C011EC36 guest evidence: compacting collection, updated stack root and weak handle, RestartEE/resume'
}
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R12R4R5 diagnostic candidate: READY ($candidate)"
Write-Output "Kernel SHA256: $kernelHash"
Write-Output "Candidate manifest: $manifestPath"
