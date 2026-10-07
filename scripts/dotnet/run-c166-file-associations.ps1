param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [switch]$BuildProofProducts,
    [switch]$RunProofBoots,
    [switch]$HostTestsOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ((& git -C $RepoRoot branch --show-current).Trim() -ne 'v1.1_DOTNET_SUPPORT') {
    throw 'C166R3 requires the existing v1.1_DOTNET_SUPPORT branch.'
}
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
if (-not $head) { throw 'Could not resolve repository HEAD.' }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($HostTestsOnly) {
        Join-Path $RepoRoot 'out\dotnet\c166r7-host-validation'
    } else {
        Join-Path $RepoRoot 'out\dotnet\c166r3-file-associations'
    }
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\', '/') + '\'
if (-not $EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C166R3 evidence must remain under $allowedRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

if ($HostTestsOnly) {
    $gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $dotnetCommand = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $gxxCommand) { throw 'C166R7 host mode requires a C++ host compiler (g++).' }
    if (-not $dotnetCommand) { throw 'C166R7 host mode requires the .NET SDK.' }
    $nativeExe = Join-Path $EvidenceRoot 'C166Association.exe'
    $nativeLog = Join-Path $EvidenceRoot 'native-host-tests.txt'
    $nativeSources = @(
        (Join-Path $RepoRoot 'samples\host\C166Association\main.cpp'),
        (Join-Path $RepoRoot 'kernel\core\file_association_service.cpp'))
    & $gxxCommand.Source -std=c++14 -O2 -Wall -Wextra "-I$RepoRoot" `
        "-I$(Join-Path $RepoRoot 'kernel\core')" @nativeSources -o $nativeExe
    if ($LASTEXITCODE -ne 0) { throw "C166R7 native host compilation failed: $LASTEXITCODE" }
    $nativeOutput = & $nativeExe 2>&1 | Out-String
    $nativeExit = $LASTEXITCODE
    Set-Content -LiteralPath $nativeLog -Value $nativeOutput -Encoding utf8
    if ($nativeExit -ne 0) { throw "C166R7 native host tests failed; see $nativeLog" }
    $managedProject = Join-Path $RepoRoot 'samples\host\C166Association\ManagedHostTests.csproj'
    $managedLog = Join-Path $EvidenceRoot 'managed-host-tests.txt'
    $managedOutput = & $dotnetCommand.Source run --project $managedProject --configuration Release 2>&1 | Out-String
    $managedExit = $LASTEXITCODE
    Set-Content -LiteralPath $managedLog -Value $managedOutput -Encoding utf8
    if ($managedExit -ne 0) { throw "C166R7 managed host tests failed; see $managedLog" }
    $nativeCount = 0
    if ($nativeOutput -match 'native host cases=(\d+)') { $nativeCount = [int]$Matches[1] }
    $managedCount = 0
    if ($managedOutput -match 'C166-MANAGED-ASSOCIATION-TESTS cases=(\d+)') {
        $managedCount = [int]$Matches[1]
    }
    $manifest = [ordered]@{
        phase='C166R7'; sourceHead=$head; productionModule='kernel/core/file_association_service.cpp'
        storageInterface='kernel/core/include/kernel/file_association_service.h'
        productionVfsAdapter='kernel/core/file_association_vfs_storage.cpp'
        hostStorageAdapter='samples/host/C166Association/fake_storage.h'
        nativeCases=$nativeCount; abiCases=1; managedCases=$managedCount
        crcVectors=@('123456789=0xCBF43926','empty=0x00000000')
        partialWriteCases=136; stressTransactions=1000; resolverLookups=1000
        hostSuiteStatus='incomplete: full 30-case dual-slot validation and persistent CRC/generation stress audit remain'
        accepted=$false
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
        (Join-Path $EvidenceRoot 'c166r7-host-manifest.json') -Encoding utf8
    Write-Host $nativeOutput.Trim()
    Write-Host $managedOutput.Trim()
    Write-Host "C166R7 host results recorded; incomplete suite does not emit the acceptance marker."
    return
}

function Resolve-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$dotnet = Resolve-Tool 'dotnet.exe' @('C:\Program Files\dotnet\dotnet.exe')
$gcc = Resolve-Tool 'x86_64-elf-gcc.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-gcc.exe')
$gxx = Resolve-Tool 'x86_64-elf-g++.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-g++.exe')
$ld = Resolve-Tool 'x86_64-elf-ld.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-ld.exe')
$objcopy = Resolve-Tool 'x86_64-elf-objcopy.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-objcopy.exe')
$make = Resolve-Tool 'mingw32-make.exe' @('C:\mingw64\bin\mingw32-make.exe','C:\msys64\mingw64\bin\mingw32-make.exe')
$qemu = Resolve-Tool 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\qemu\qemu-system-x86_64.exe'),
    'C:\qemu\qemu-system-x86_64.exe',
    'C:\msys64\mingw64\bin\qemu-system-x86_64.exe')
$qemuRelevant = @(Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" |
    ForEach-Object { [ordered]@{ pid=$_.ProcessId; path=$_.ExecutablePath; commandLine=$_.CommandLine } })

$sdk = 'unavailable'
if ($dotnet) {
    $sdkOutput = & $dotnet --version 2>&1
    if ($LASTEXITCODE -eq 0) { $sdk = ([string]$sdkOutput -split "`r?`n" | Select-Object -First 1).Trim() }
}
$crossAvailable = $false
if ($gcc -and $gxx -and $ld -and $objcopy) { $crossAvailable = $true }
$productionCompiler = Resolve-Tool 'g++.exe' @('C:\mingw64\bin\g++.exe','C:\msys64\mingw64\bin\g++.exe')
$productionLinker = Resolve-Tool 'ld.exe' @('C:\mingw64\bin\ld.exe','C:\msys64\mingw64\bin\ld.exe')
$productionObjcopy = Resolve-Tool 'objcopy.exe' @('C:\mingw64\bin\objcopy.exe','C:\msys64\mingw64\bin\objcopy.exe')
$productionToolchain = [bool]($make -and $productionCompiler -and $productionLinker -and $productionObjcopy)
$qemuVersion = 'unavailable'
if ($qemu) {
    $versionOutput = & $qemu -version 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $qemuVersion = ($versionOutput -split "`r?`n" | Select-Object -First 1).Trim()
    }
}
$capability = @"
SOURCE_HEAD=$head
DOTNET_TOOLCHAIN=$(if ($sdk -ne 'unavailable') {'AVAILABLE'} else {'UNAVAILABLE'}) path=$dotnet sdk=$sdk
CROSS_TOOLCHAIN=$(if ($crossAvailable) {'AVAILABLE'} else {'UNAVAILABLE'}) gcc=$gcc g++=$gxx ld=$ld objcopy=$objcopy
PRODUCTION_KERNEL_TOOLCHAIN=$(if ($productionToolchain) {'AVAILABLE'} else {'UNAVAILABLE'}) make=$make g++=$productionCompiler ld=$productionLinker objcopy=$productionObjcopy
QEMU=$(if ($qemu) {'AVAILABLE'} else {'UNAVAILABLE'}) path=$qemu version=$qemuVersion
QEMU_EXISTING_RELEVANT_COUNT=$($qemuRelevant.Count)
"@
Set-Content -LiteralPath (Join-Path $EvidenceRoot 'environment-capability.txt') -Value $capability -Encoding utf8
Write-Host $capability
if (-not $dotnet) { throw 'Outcome E: current .NET SDK is unavailable.' }

if (-not $productionToolchain) { throw 'Outcome E: repository production kernel build toolchain is unavailable.' }
if (-not $BuildProofProducts) {
    Write-Host 'Focused C166R3 source/tool discovery complete; pass -BuildProofProducts to build fresh proof products.'
    if ($RunProofBoots -and $qemuRelevant.Count -gt 0) {
        throw 'Outcome E: proof boots blocked by an already running QEMU workload; no process was terminated.'
    }
    return
}

if (-not $qemu -and $RunProofBoots) { throw 'Outcome D: QEMU executable is unavailable; fresh builds remain usable.' }
if ($RunProofBoots -and $qemuRelevant.Count -gt 0) {
    $owners = ($qemuRelevant | ForEach-Object { "pid=$($_.pid) exe=$($_.path) cmd=$($_.commandLine)" }) -join "`n"
    throw "Outcome E: proof boots are blocked by existing QEMU process(es); no process was terminated.`n$owners"
}

$buildRoot = Join-Path $EvidenceRoot 'build'
$proofComposite = Join-Path $buildRoot 'composite-c166-proof'
$productionComposite = Join-Path $buildRoot 'composite-c166-production'
$runtimeOutput = Join-Path $buildRoot 'runtime-pack'
$python = Resolve-Tool 'python.exe' @(
    'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    'C:\Python312\python.exe','C:\Python311\python.exe')
if (-not $python) { throw 'The managed artifact converter Python runtime could not be located.' }
$managedBuilder = Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1'
$managedArgs = @('-ExecutionPolicy','Bypass','-File',$managedBuilder,
    '-RepoRoot',$RepoRoot,'-OutputRoot',$proofComposite,
    '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
    '-RuntimePackOutputRoot',$runtimeOutput,'-UseGuideXosRuntimePack',
    '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating',
    '-ManagedProjectMode','C160Composite','-C155ManagedNotesSession',
    '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C158ManagedCalculator',
    '-C160ApplicationSnapshotProof','-C161ManagedTaskManager','-C161TaskManagerProof',
    '-C162ManagedTaskManagerClose','-C162TaskManagerCloseProof','-C163ManagedFileExplorer',
    '-C163FileExplorerProof','-C164FileActivationProof','-C166FileAssociationProof',
    '-HeapConfiguration','Primary8MiB','-PythonExe',$python)
& powershell.exe @managedArgs
if ($LASTEXITCODE -ne 0) { throw "Fresh C166 managed proof build failed: $LASTEXITCODE" }
$proofElf = Join-Path $proofComposite 'artifacts\HostLogProof.elf'
if (-not (Test-Path -LiteralPath $proofElf -PathType Leaf)) { throw "Proof composite missing: $proofElf" }
$proofCompositeHash = Get-Sha256 $proofElf

$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
$proofRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c166-proof.img'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $proofRamdisk) | Out-Null
$mediaArgs = @('-ExecutionPolicy','Bypass','-File',$generator,
    '-OutputDir',(Join-Path $EvidenceRoot 'staging\wallpaper-pack-c166-proof'),
    '-OutputImage',$proofRamdisk,'-C104AppAPath',$proofElf,
    '-ProductionCompositeApplicationPath',$proofElf,'-C114ManagedDirectoryServices',
    '-C117ManagedTextArea','-C118ManagedListBox','-C151ManagedOpenFileDialog',
    '-C152ManagedNotesSaveWorkflow','-C155ManagedNotesSession',
    '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C164FileActivation')
& powershell.exe @mediaArgs
if ($LASTEXITCODE -ne 0) { throw "Proof ramdisk generation failed: $LASTEXITCODE" }
$proofRamdiskHash = Get-Sha256 $proofRamdisk

$flags = @('-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION',
    '-DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    '-DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION',
    '-DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    '-DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES',
    '-DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER','-DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT',
    '-DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA','-DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX',
    '-DGXOS_NATIVEAOT_C119_MANAGED_BUTTON','-DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST',
    '-DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX','-DGXOS_NATIVEAOT_C122_MANAGED_LABEL',
    '-DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR','-DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON',
    '-DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR','-DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX',
    '-DGXOS_NATIVEAOT_C127_MANAGED_PANEL','-DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE',
    '-DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT','-DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX',
    '-DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON','-DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX',
    '-DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING','-DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU',
    '-DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU','-DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING',
    '-DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR','-DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT',
    '-DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW','-DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK',
    '-DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK','-DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX',
    '-DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER','-DGXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG',
    '-DGXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE','-DGXOS_NATIVEAOT_C147_RUNTIME_SETTINGS',
    '-DGXOS_NATIVEAOT_C148_SETTINGS_V2','-DGXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING',
    '-DGXOS_NATIVEAOT_C150_MANAGED_APP_RETURN','-DGXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    '-DGXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW','-DGXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    '-DGXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS','-DGXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    '-DGXOS_NATIVEAOT_C158_MANAGED_CALCULATOR','-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF',
    '-DGXOS_NATIVEAOT_C161_MANAGED_TASK_MANAGER','-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
    '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE','-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF',
    '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER','-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF',
    '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF','-DGXOS_NATIVEAOT_C166_FILE_ASSOCIATION_PROOF')
$kernelArgs = @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
    "EXTRA_CFLAGS=$($flags -join ' ')",'-B')
& $make @kernelArgs
if ($LASTEXITCODE -ne 0) { throw "Fresh C166 kernel proof link failed: $LASTEXITCODE" }
$canonicalKernel = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
$proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
Copy-Item -LiteralPath $canonicalKernel -Destination $proofKernel -Force
$proofKernelHash = Get-Sha256 $proofKernel

$manifest = [ordered]@{
    phase='C166R3'; outcome='validation-lane-reconstructed'; accepted=$false
    sourceHead=$head; sourceBranch='v1.1_DOTNET_SUPPORT'
    dotnetSdkPath=$dotnet; dotnetSdkVersion=$sdk
    crossCompilerGcc=$gcc; crossCompilerGxx=$gxx; crossLinker=$ld; crossObjcopy=$objcopy
    productionCompiler=$productionCompiler; productionLinker=$productionLinker
    productionMake=$make; qemuPath=$qemu; qemuVersion=$qemuVersion
    existingQemu=$qemuRelevant; proofCompositeSha256=$proofCompositeHash
    proofKernelSha256=$proofKernelHash; proofRamdiskSha256=$proofRamdiskHash
    qemuBoots='not-run'; focusedNativeCases='guest-only; not yet executed'
    artifactAuthority='current committed source and freshly built proof products; historical C163/C164 hashes are informational only'
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c166r3-proof-manifest.json') -Encoding utf8
Write-Host "C166R3 proof products built from $head"
Write-Host "composite=$proofCompositeHash kernel=$proofKernelHash ramdisk=$proofRamdiskHash"
if (-not $RunProofBoots) {
    Write-Host 'No QEMU boot requested; proof products and manifest are retained.'
    return
}
throw 'The inherited lifecycle scenarios are not yet connected to C166 state transitions; proof boot acceptance remains blocked.'
