param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = '',
    [switch]$C188R25FailFastOriginCapture
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r22-bootstrap-handoff-predicates.ps1')
. (Join-Path $PSScriptRoot 'c188r23-preparation-boundary-predicates.ps1')
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'R23 candidate builds require PowerShell Core.'
}

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
if (-not (Test-Path -LiteralPath $ArtifactRoot -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $ArtifactRoot -Force
}
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $CandidateRoot = Join-Path $ArtifactRoot ('c188r23-settings-module-preparation\candidate-' +
        $stamp + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R23 candidate must be below ArtifactRoot: $candidate"
}
if (Test-Path -LiteralPath $candidate) {
    throw "Refusing to overwrite R23 candidate: $candidate"
}
$null = New-Item -ItemType Directory -Path $candidate

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }
}
function Invoke-Logged([string]$FilePath, [string[]]$Arguments,
    [string]$LogPath, [string]$WorkingDirectory=$RepoRoot) {
    Push-Location -LiteralPath $WorkingDirectory
    try {
        $output = (& $FilePath @Arguments 2>&1 | Out-String)
        $code = $LASTEXITCODE
    } finally { Pop-Location }
    Set-Content -LiteralPath $LogPath -Value $output -Encoding utf8
    if ($code -ne 0) {
        throw "Command failed ($code): $FilePath $($Arguments -join ' '). See $LogPath"
    }
    return $output
}

$preservedModifiedObjects = [ordered]@{
    'c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj'='3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD'
    'c188r12r4r5r4r5r2r2_native_alloc_record_test.obj'='605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3'
    'guidexos_nativeaot_native_alloc_record.obj'='2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183'
}
foreach ($name in @($preservedModifiedObjects.Keys)) {
    $path = Join-Path $RepoRoot $name
    Require-File $path "Pre-existing modified object $name"
    if ((Get-Hash $path) -ne $preservedModifiedObjects[$name]) {
        throw "Pre-existing modified object hash changed before R23 build: $name"
    }
}

$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
$branch = (& git -C $RepoRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head) -or
    $branch -ne 'v1.1_DOTNET_SUPPORT') {
    throw "R23 source checkout mismatch: branch=$branch HEAD=$head"
}
$expectedHead = $head
$sourceRelativePaths = @(
    'kernel/core/nativeaot_application.cpp',
    'scripts/dotnet/build-c188-candidate-kernel.ps1',
    'scripts/dotnet/stage-c188-ramdisk.ps1',
    'scripts/dotnet/run-c188r12r1-settings-handoff.ps1',
    'samples/managed/HostLogProof/c102_startup.c',
    'samples/managed/HostLogProof/HostLogProof.csproj',
    'samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs',
    'samples/managed/HostLogProof/GuideXos/C188R12R4FactoryDiagnostics.cs',
    'samples/managed/HostLogProof/Applications/ManagedSettingsCenter.cs',
    'scripts/dotnet/build-managed-hostlog-proof.ps1',
    'scripts/dotnet/build-c188r23-settings-candidate.ps1',
    'scripts/dotnet/build-c188r22-settings-candidate.ps1',
    'scripts/dotnet/c188r23-preparation-boundary-predicates.ps1',
    'scripts/dotnet/test-c188r23-settings-module-preparation.ps1',
    'scripts/dotnet/c188r22-bootstrap-handoff-predicates.ps1',
    'scripts/dotnet/test-c188r22-settings-bootstrap-handoff.ps1',
    'scripts/dotnet/test-c188r12r4r5r3-init-return-contract.ps1',
    'scripts/dotnet/test-c188r20-iterator-advancement.py',
    'tools/dotnet/runtime-pack/build-runtime-pack.ps1',
    'tools/dotnet/runtime-pack/runtime-pack.lock.json',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.h',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_startup_imports.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_amd64_unwind_primitive.cpp')
$sourceInputs = @($sourceRelativePaths | ForEach-Object {
    $path = Join-Path $RepoRoot $_
    Require-File $path "R23 source input $_"
    [ordered]@{path=$_;sha256=(Get-Hash $path)}
})
$sourceWorkingTreeStatus = (& git -C $RepoRoot status --short)
$expectedPreExistingStatus = @(
    ' M c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj',
    ' M c188r12r4r5r4r5r2r2_native_alloc_record_test.obj',
    ' M guidexos_nativeaot_native_alloc_record.obj')
if (@($sourceWorkingTreeStatus).Count -ne $expectedPreExistingStatus.Count -or
    @($sourceWorkingTreeStatus | Where-Object { $_ -notin $expectedPreExistingStatus }).Count -ne 0) {
    throw "R23 requires committed candidate sources and only the three recorded pre-existing object modifications. Status: $($sourceWorkingTreeStatus -join '; ')"
}

$managedRoot = Join-Path $candidate 'managed'
$runtimeRoot = Join-Path $candidate 'runtime-pack'
$managedOutput = Join-Path $managedRoot 'nativeaot'
$propsDir = Join-Path $candidate 'msbuild'
$propsPath = Join-Path $propsDir 'Directory.Build.props'
$ramdiskSource = Join-Path $RepoRoot 'ESP\ramdisk.img'
$bootloader = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$sourceRamdiskHash = 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
Require-File $ramdiskSource 'Source guideXOS ramdisk'
Require-File $bootloader 'UEFI bootloader'
if ((Get-Hash $ramdiskSource) -ne $sourceRamdiskHash) {
    throw 'Source ramdisk changed from the established C188 image identity.'
}
$null = New-Item -ItemType Directory -Path $propsDir
@'
<Project>
  <PropertyGroup>
    <C188BuildScope Condition="'$(C188BuildScope)' == ''">nativeaot</C188BuildScope>
  </PropertyGroup>
  <PropertyGroup Condition="'$(MSBuildProjectName)' == 'HostLogProof'">
    <BaseOutputPath>$(C188HostOutputRoot)\host-bin\$(C188BuildScope)\</BaseOutputPath>
    <BaseIntermediateOutputPath>$(C188HostOutputRoot)\host-obj\$(C188BuildScope)\</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$(C188HostOutputRoot)\host-obj\$(C188BuildScope)\</MSBuildProjectExtensionsPath>
  </PropertyGroup>
</Project>
'@ | Set-Content -LiteralPath $propsPath -Encoding utf8

$managedBuilder = Join-Path $RepoRoot 'scripts\dotnet\build-managed-hostlog-proof.ps1'
$managedArgs = @(
    '-NoProfile','-File',$managedBuilder,
    '-RepoRoot',$RepoRoot,'-ArtifactRoot',$ArtifactRoot,
    '-OutputRoot',$managedOutput,
    '-RuntimePackOutputRoot',$runtimeRoot,
    '-DirectoryBuildPropsPath',$propsPath,
    '-UseGuideXosRuntimePack','-AllocationMode','Allocating',
    '-ProductionApplication','-PersistentCompositeLifecycle',
    '-C188R12R4R5GcOwnershipProof','-C188R21ManagedSettingsFactory',
    '-C188R22SettingsBootstrapHandoff',
    '-C188R23SettingsPreparationTrace',
    '-C156ControlModifierShortcuts',
    '-C185ExternalComposition','-C166OrdinaryBuild',
    '-C188ExternalAppIdentity','-C188R7NotesProofAuthorization',
    '-C188R8NotesInputRouting','-C188R9RegistryInputReentry',
    '-C188R11ManagedStateProvenance','-C188R12R3LifetimeStartAttribution',
    '-ManagedProjectMode','C160Composite')
if ($C188R25FailFastOriginCapture) {
    $managedArgs += '-C188R25FailFastOriginCapture'
}
$pwsh = (Get-Command 'pwsh.exe' -ErrorAction Stop | Select-Object -First 1).Source
$r22Test = Join-Path $RepoRoot 'scripts\dotnet\test-c188r22-settings-bootstrap-handoff.ps1'
$null = Invoke-Logged $pwsh @('-NoProfile','-File',$r22Test) `
    (Join-Path $candidate 'r22-hosted-regression.log') $RepoRoot
$r23Test = Join-Path $RepoRoot 'scripts\dotnet\test-c188r23-settings-module-preparation.ps1'
$null = Invoke-Logged $pwsh @('-NoProfile','-File',$r23Test) `
    (Join-Path $candidate 'r23-hosted-regression.log') $RepoRoot
$null = Invoke-Logged $pwsh $managedArgs (Join-Path $candidate 'managed-build.log') $RepoRoot

$managedArtifactRoot = Join-Path $managedOutput 'artifacts'
$managedPe = Join-Path $managedArtifactRoot 'HostLogProof.exe'
$managedMap = Join-Path $managedArtifactRoot 'HostLogProof.map'
$managedElf = Join-Path $managedArtifactRoot 'HostLogProof.elf'
$runtimeManifestPath = Join-Path $runtimeRoot 'runtime-pack.manifest.json'
foreach ($entry in @(
    @($managedPe,'NativeAOT PE'),@($managedMap,'NativeAOT map'),
    @($managedElf,'NativeAOT ELF'),@($runtimeManifestPath,'Runtime-pack manifest'))) {
    Require-File $entry[0] $entry[1]
}
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
$unwind = $runtimeManifest.productionNativeUnwindPrimitive
if (-not [bool]$runtimeManifest.c188r21ManagedSettingsFactory -or
    -not [bool]$runtimeManifest.c188r22SettingsBootstrapHandoff -or
    -not [bool]$runtimeManifest.c188r23SettingsPreparationTrace -or
    -not [bool]$runtimeManifest.c188r12r4r5GcOwnershipProof -or
    -not [bool]$runtimeManifest.persistentCompositeLifecycle -or
    [bool]$runtimeManifest.imageBackedArenaPresent -or
    [int64]$runtimeManifest.managedHeapBytes -ne 0 -or
    [string]$runtimeManifest.allocationStrategy -ne 'experimental-nativeaot-workstation-gc-object-and-array-allocation-no-c188-arena' -or
    [string]$unwind.implementation -cne 'guideXosRtlVirtualUnwind' -or
    -not [bool]$unwind.linkedByManagedProof -or
    [string]$unwind.objectSha256 -cne (Get-Hash ([string]$unwind.object)) -or
    [string]$unwind.sourceSha256 -cne (Get-Hash ([string]$unwind.source)) -or
    [string]$unwind.startupImportSourceSha256 -cne (Get-Hash ([string]$unwind.startupImportSource))) {
    throw 'Runtime manifest failed R23/R22 Workstation GC, persistent-owner, no-arena, R20 unwind, or preparation-trace profile checks.'
}
$mapText = Get-Content -LiteralPath $managedMap -Raw
$requiredSymbols = @(
    'GuideXosNativeAotApplicationEntry',
    'guidexos_nativeaot_prepare_managed_entry',
    'guidexos_nativeaot_pal_startup_marker',
    'InitializeModules',
    'ManagedMain',
    'ManagedSettingsCenter___ctor_0',
    'PalGetMaximumStackBounds')
$missingSymbols = @(Get-C188R22MissingCandidateSymbols $mapText $requiredSymbols)
if ($missingSymbols.Count -gt 0) {
    throw "R23 NativeAOT map omitted preparation or real Settings symbols: $($missingSymbols -join ', ')"
}
$c102StartupObj = Join-Path $managedArtifactRoot 'c102_startup.obj'
Require-File $c102StartupObj 'R23 C102 startup shim object'
$elfText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($managedElf))
foreach ($marker in @('C188R21-FACTORY invocation=',
        'C188R21-STACK stage=settings-factory-entry',
        'C188R21-ROOT stage=post-gc',
        'Managed Settings Center')) {
    if (-not $elfText.Contains($marker)) {
        throw "R23 NativeAOT ELF omitted real Settings qualification marker: $marker"
    }
}
$linkResponses = @(Get-ChildItem -LiteralPath $managedOutput -Recurse -File -Filter 'link.rsp')
$linkedUnwind = $false
foreach ($response in $linkResponses) {
    if ([System.IO.File]::ReadAllText($response.FullName).Contains(
            [string]$unwind.object)) { $linkedUnwind = $true; break }
}
if (-not $linkedUnwind) {
    throw 'NativeAOT link response omitted the hash-verified R20 unwind primitive object.'
}

# Keep the link command under Windows' CreateProcess command-line limit. The
# kernel linker expands every object path, so placing this hundreds-of-objects
# build beneath the long phase/candidate path can exceed that limit.
$kernelBuildRoot = Join-Path $ArtifactRoot ('r23-kernel-' + [guid]::NewGuid().ToString('N'))
$kernelBuilder = Join-Path $RepoRoot 'scripts\dotnet\build-c188-candidate-kernel.ps1'
$kernelArgs = @('-NoProfile','-File',$kernelBuilder,
    '-ArtifactRoot',$ArtifactRoot,'-BuildRoot',$kernelBuildRoot,
    '-ManagedStateProvenance')
$null = Invoke-Logged $pwsh $kernelArgs (Join-Path $candidate 'kernel-build.log') $RepoRoot
$kernel = Join-Path $kernelBuildRoot 'bin\kernel.elf'
Require-File $kernel 'C188 Settings-capable kernel'

$esp = Join-Path $candidate 'esp'
$espBoot = Join-Path $esp 'EFI\BOOT'
$null = New-Item -ItemType Directory -Path $espBoot -Force
Copy-Item -LiteralPath $bootloader -Destination (Join-Path $espBoot 'BOOTX64.EFI')
Copy-Item -LiteralPath $kernel -Destination (Join-Path $esp 'kernel.elf')
Copy-Item -LiteralPath $ramdiskSource -Destination (Join-Path $esp 'ramdisk.img')
$stager = Join-Path $RepoRoot 'scripts\dotnet\stage-c188-ramdisk.ps1'
$stageArgs = @('-NoProfile','-File',$stager,'-CandidateRoot',$candidate,
    '-ManagedApp',$managedElf)
$null = Invoke-Logged $pwsh $stageArgs (Join-Path $candidate 'ramdisk-stage.log') $RepoRoot

$r20Harness = Join-Path $candidate 'r20-unwind-regression'
$r20Candidate = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z'
$r20Run = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261113T003700302Z-0317d25e6f6c456b850c4904190019fa'
$r20SavedAnalysis = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r18-hosted-20261113T002215470Z\native-progress-analysis.json'
$r20RuntimeSource = Join-Path $RepoRoot 'out\dotnet\gc-feasibility-baseline\nativeaot-runtime'
$r20Script = Join-Path $RepoRoot 'scripts\dotnet\test-c188r20-iterator-advancement.py'
$python = (Get-Command 'python.exe' -ErrorAction Stop | Select-Object -First 1).Source
Require-File (Join-Path $r20Candidate 'candidate-manifest.json') 'Preserved R20 candidate manifest'
Require-File $r20SavedAnalysis 'Preserved R18 iterator analysis'
$r20Args = @($r20Script,'--repo-root',$RepoRoot,'--candidate-root',$r20Candidate,
    '--run-root',$r20Run,'--saved-analysis',$r20SavedAnalysis,
    '--runtime-source',$r20RuntimeSource,
    '--output-directory',$r20Harness)
$null = Invoke-Logged $python $r20Args (Join-Path $candidate 'r20-unwind-regression.log') $RepoRoot
$r20ResultPath = Join-Path $r20Harness 'hosted-tests.json'
Require-File $r20ResultPath 'R20 no-op import regression result'
$r20Result = Get-Content -LiteralPath $r20ResultPath -Raw | ConvertFrom-Json
if ($r20Result.status -ne 'passed' -or
    $r20Result.outcome -ne 'ITERATOR_ADVANCEMENT_CAUSE_PROVEN') {
    throw 'R20 hosted no-op RtlVirtualUnwind regression did not pass.'
}
foreach ($name in @($preservedModifiedObjects.Keys)) {
    $path = Join-Path $RepoRoot $name
    if ((Get-Hash $path) -ne $preservedModifiedObjects[$name]) {
        throw "Pre-existing modified object changed during the R23 build: $name"
    }
}

$kernelHash = Get-Hash (Join-Path $esp 'kernel.elf')
$managedHash = Get-Hash $managedElf
$stagingManifestPath = Join-Path $candidate 'ramdisk-staging.json'
$stagingManifest = Get-Content -LiteralPath $stagingManifestPath -Raw | ConvertFrom-Json
$fileEntries = @(Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {
    $_.Name -notin @('candidate-manifest.json','candidate-integrity.json')
} | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=[System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/');sha256=(Get-Hash $_.FullName)}
})
$integrity = [ordered]@{
    schema='GUIDEXOS_C188R23_CANDIDATE_INTEGRITY_V1'
    candidateRoot=$candidate
    files=$fileEntries
    preservedModifiedObjects=$preservedModifiedObjects
    unverifiedRootObjectInputsConsumed=$false
}
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$integrity | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $integrityPath -Encoding utf8
$manifest = [ordered]@{
    result='PASS_BUILD_READY_FOR_QAC_PREFLIGHT'
    phase='C188R23_SETTINGS_MODULE_PREPARATION'
    startingHead=$expectedHead
    sourceHead=$head
    sourceWorkingTreeStatus=$sourceWorkingTreeStatus
    sourceInputs=$sourceInputs
    branch=$branch
    candidateRoot=$candidate
    artifactRoot=$ArtifactRoot
    isolatedKernelBuildDirectory=$kernelBuildRoot
    buildProfile='C160Composite+C185+C188R11+C188R12R3+R20WorkstationGC+R21SettingsFactory+R22BootstrapHandoff+R23PreparationTrace'
    managedProjectMode='C160Composite'
    actualManagedSettingsFactory='GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter'
    actualFactoryConstructorSymbol='ManagedSettingsCenter___ctor_0'
    managedEntryPrerequisite='guidexos_nativeaot_prepare_managed_entry invokes shared initializeRuntimeState after TLS; requires real InitializeModules and code-manager registration before C188B002 and ManagedMain'
    preparationCheckpoints=[ordered]@{codeManagerRegistrationCall='C188B006 immediately before real RhRegisterOSModule';initializeModulesCall='C188B007 immediately before generated five-argument InitializeModules';preparationReturn='C188B005 only after shared helper validates module and code-manager state'}
    nativeBootTarget='Observe C188B006 and C188B007 ordering, then require C188B005 before ManagedMain or selector-5 Settings dispatch'
    startupShimObject=$c102StartupObj;startupShimObjectSha256=(Get-Hash $c102StartupObj)
    managedElf=$managedElf;managedElfSha256=$managedHash
    managedPe=$managedPe;managedPeSha256=(Get-Hash $managedPe)
    managedMap=$managedMap;managedMapSha256=(Get-Hash $managedMap)
    kernel=(Join-Path $esp 'kernel.elf');kernelSha256=$kernelHash
    runtimePackManifest=$runtimeManifestPath
    runtimePackManifestSha256=(Get-Hash $runtimeManifestPath)
    runtimePackObject=$runtimeManifest.object
    runtimePackObjectSha256=$runtimeManifest.objectSha256
    unwindPrimitive=$unwind
    workstationGcAllocationStrategy=$runtimeManifest.allocationStrategy
    imageBackedArenaPresent=[bool]$runtimeManifest.imageBackedArenaPresent
    persistentCompositeLifecycle=[bool]$runtimeManifest.persistentCompositeLifecycle
    stackBudgetMeasurement='PAL bounds plus live stackalloc marker at Settings factory entry'
    managedOwnershipCheck='temporary weak reference; one forced compacting collection; selector/generation relookup from static App Model pool'
    ramdiskStaging=$stagingManifestPath
    ramdiskStagingSha256=(Get-Hash $stagingManifestPath)
    ramdiskSourceSha256=(Get-Hash $ramdiskSource)
    bootloaderSha256=(Get-Hash (Join-Path $espBoot 'BOOTX64.EFI'))
    r20UnwindRegressionResult=$r20ResultPath
    r20UnwindRegressionSha256=(Get-Hash $r20ResultPath)
    r23HostedRegression=(Join-Path $candidate 'r23-hosted-regression.log')
    r23HostedRegressionSha256=(Get-Hash (Join-Path $candidate 'r23-hosted-regression.log'))
    candidateIntegrity=$integrityPath
    candidateIntegritySha256=(Get-Hash $integrityPath)
    candidateFiles=$fileEntries
}
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R23_CANDIDATE=PASS ROOT=$candidate KERNEL_SHA256=$kernelHash SETTINGS_ELF_SHA256=$managedHash"
Write-Output "C188R23_CANDIDATE_MANIFEST=$manifestPath"
