param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter()][switch]$NotesStartupDiagnostics,
    [Parameter()][switch]$NotesStartupOperationIsolation,
    [Parameter()][switch]$NotesProofInvocationCorrection,
    [Parameter()][switch]$NotesPostRenderStartup,
    [Parameter()][switch]$NotesProofAuthorization,
    [Parameter()][switch]$NotesInputRouting,
    [Parameter()][switch]$RegistryInputReentry,
    [Parameter()][switch]$ManagedStateReentry,
    [Parameter()][switch]$ManagedStateProvenance,
    [Parameter()][switch]$LifetimeStartAttribution
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188 NativeAOT candidate builds require PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$selectedPhaseCount = 0
if ($NotesStartupDiagnostics) { $selectedPhaseCount++ }
if ($NotesStartupOperationIsolation) { $selectedPhaseCount++ }
if ($NotesProofInvocationCorrection) { $selectedPhaseCount++ }
if ($NotesPostRenderStartup) { $selectedPhaseCount++ }
if ($NotesProofAuthorization) { $selectedPhaseCount++ }
if ($NotesInputRouting) { $selectedPhaseCount++ }
if ($RegistryInputReentry) { $selectedPhaseCount++ }
if ($ManagedStateReentry) { $selectedPhaseCount++ }
if ($ManagedStateProvenance) { $selectedPhaseCount++ }
if ($LifetimeStartAttribution) { $selectedPhaseCount++ }
if ($selectedPhaseCount -gt 1) { throw 'Choose one C188 Notes startup diagnostic phase.' }
$phaseRootName = if ($LifetimeStartAttribution) { 'out/dotnet/c188r12r3-lifetime-start-attribution' } elseif ($ManagedStateProvenance) { 'out/dotnet/c188r11-managed-state-provenance' } elseif ($ManagedStateReentry) { 'out/dotnet/c188r10-nativeaot-state-reentry' } elseif ($RegistryInputReentry) { 'out/dotnet/c188r9-registry-input-reentry' } elseif ($NotesInputRouting) { 'out/dotnet/c188r8-notes-input-routing' } elseif ($NotesProofAuthorization) { 'out/dotnet/c188r7-notes-proof-authorization' } elseif ($NotesPostRenderStartup) { 'out/dotnet/c188r6-notes-postrender' } elseif ($NotesProofInvocationCorrection) { 'out/dotnet/c188r5-notes-proof-invocation' } elseif ($NotesStartupOperationIsolation) { 'out/dotnet/c188r4-notes-startup-operation' } elseif ($NotesStartupDiagnostics) { 'out/dotnet/c188r3-notes-startup-diagnostics' } else { 'out/dotnet/c188-external-app-identity' }
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo $phaseRootName))
$build = [System.IO.Path]::GetFullPath($BuildRoot)
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $build)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "BuildRoot must be a unique directory below $phaseRoot."
}
if (Test-Path -LiteralPath $build) {
    throw "Refusing to overwrite C188 NativeAOT build root: $build"
}
$null = New-Item -ItemType Directory -Path $build
$propsDir = Join-Path $build 'msbuild'
$null = New-Item -ItemType Directory -Path $propsDir
$propsPath = Join-Path $propsDir 'Directory.Build.props'
$props = @'
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
'@
Set-Content -LiteralPath $propsPath -Value $props -Encoding utf8

$nativeRoot = Join-Path $build 'nativeaot'
$runtimePackRoot = Join-Path $repo 'tools/dotnet/runtime-pack'
$runtimePackOutput = Join-Path $build 'runtime-pack'
$buildScript = Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1'
$arguments = @{
    RepoRoot = $repo
    OutputRoot = $nativeRoot
    DirectoryBuildPropsPath = $propsPath
    RuntimePackRoot = $runtimePackRoot
    RuntimePackOutputRoot = $runtimePackOutput
    AllocationMode = 'Allocating'
    # Keep the candidate's existing production application path identical to
    # the standard C160 composite profile. C188 adds only the gated selector-9
    # CounterApp route; it does not need C167 proof-control behavior.
    ManagedProjectMode = 'C160Composite'
    ProductionApplication = $true
    # The resident composite is re-entered for app input after Launch returns.
    # Keep its image-backed managed heap alive across those reverse-P/Invoke
    # boundaries instead of clearing objects still referenced by static roots.
    PersistentCompositeLifecycle = $true
    # Use the ordinary app build for guest UI runs; hosted suites carry the
    # regression coverage without executing proof routines on first launch.
    C166OrdinaryBuild = $true
    C185ExternalComposition = $true
    C188ExternalAppIdentity = $true
    UseGuideXosRuntimePack = $true
    C155ManagedNotesSession = $true
    C156ControlModifierShortcuts = $true
    C157ManagedNotesNewDocument = $true
    C158ManagedCalculator = $true
    C161ManagedTaskManager = $true
    C162ManagedTaskManagerClose = $true
    C163ManagedFileExplorer = $true
}
if ($NotesStartupDiagnostics) {
    $arguments.C188R3NotesStartupDiagnostics = $true
}
if ($NotesStartupOperationIsolation) {
    $arguments.C188R4NotesStartupOperationIsolation = $true
}
if ($NotesProofInvocationCorrection) {
    $arguments.C188R3NotesStartupDiagnostics = $true
    $arguments.C188R4NotesStartupOperationIsolation = $true
}
if ($NotesPostRenderStartup) {
    $arguments.C188R3NotesStartupDiagnostics = $true
    $arguments.C188R4NotesStartupOperationIsolation = $true
    $arguments.C188R6NotesPostRenderStartup = $true
}
if ($NotesProofAuthorization) {
    $arguments.C188R7NotesProofAuthorization = $true
}
if ($NotesInputRouting) {
    $arguments.C188R7NotesProofAuthorization = $true
    $arguments.C188R8NotesInputRouting = $true
}
$verifyArguments = @{
    CandidateRoot = $nativeRoot
    EvidenceRoot = (Join-Path $nativeRoot 'qualification')
}
if ($NotesStartupDiagnostics) { $verifyArguments.NotesStartupDiagnostics = $true }
if ($NotesStartupOperationIsolation) { $verifyArguments.NotesStartupOperationIsolation = $true }
if ($NotesProofInvocationCorrection) { $verifyArguments.NotesProofInvocationCorrection = $true }
if ($NotesPostRenderStartup) { $verifyArguments.NotesPostRenderStartup = $true }
if ($NotesProofAuthorization) { $verifyArguments.NotesProofAuthorization = $true }
if ($NotesInputRouting) { $verifyArguments.NotesInputRouting = $true }
if ($RegistryInputReentry) {
    $arguments.C188R7NotesProofAuthorization = $true
    $arguments.C188R8NotesInputRouting = $true
    $arguments.C188R9RegistryInputReentry = $true
}
if ($ManagedStateReentry) {
    $arguments.C188R7NotesProofAuthorization = $true
    $arguments.C188R8NotesInputRouting = $true
    $arguments.C188R9RegistryInputReentry = $true
    $arguments.C188R10ManagedStateReentry = $true
}
if ($ManagedStateProvenance -or $LifetimeStartAttribution) {
    $arguments.C188R7NotesProofAuthorization = $true
    $arguments.C188R8NotesInputRouting = $true
    $arguments.C188R9RegistryInputReentry = $true
    $arguments.C188R11ManagedStateProvenance = $true
}
if ($LifetimeStartAttribution) {
    $arguments.C188R12R3LifetimeStartAttribution = $true
}
$logPath = Join-Path $build 'nativeaot-build.log'
$output = ''
$buildExitCode = 0
try {
    $output = (& $buildScript @arguments *>&1 | Out-String)
    $buildExitCode = $LASTEXITCODE
} catch {
    $buildExitCode = if ($LASTEXITCODE -ne 0) { $LASTEXITCODE } else { 1 }
    $output += "`nBUILD SCRIPT ERROR:`n$($_ | Out-String)"
}
Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
if ($buildExitCode -ne 0) {
    throw "C188 NativeAOT build failed with exit code $buildExitCode. See $logPath"
}
$verify = Join-Path $PSScriptRoot 'verify-c188-nativeaot-artifacts.ps1'
if ($RegistryInputReentry) { $verifyArguments.NotesRegistryInputReentry = $true }
if ($ManagedStateReentry) { $verifyArguments.ManagedStateReentry = $true }
if ($ManagedStateProvenance) { $verifyArguments.ManagedStateProvenance = $true }
if ($LifetimeStartAttribution) { $verifyArguments.LifetimeStartAttribution = $true }
& $verify @verifyArguments
if ($LASTEXITCODE -ne 0) { throw "C188 NativeAOT artifact verification failed with exit code $LASTEXITCODE." }
$runtimePackManifestPath = Join-Path $runtimePackOutput 'runtime-pack.manifest.json'
if (-not (Test-Path -LiteralPath $runtimePackManifestPath -PathType Leaf)) {
    throw "C188 runtime-pack manifest is missing: $runtimePackManifestPath"
}
$runtimePackManifest = Get-Content -LiteralPath $runtimePackManifestPath -Raw | ConvertFrom-Json
if (-not [bool]$runtimePackManifest.persistentCompositeLifecycle) {
    throw 'C188 resident managed application candidate must preserve its image-backed heap across reverse-P/Invoke entries.'
}
$kernelElf = Join-Path $nativeRoot 'artifacts/HostLogProof.elf'
$manifest = [ordered]@{
    result='PASS'; buildRoot=$build; nativeAotRoot=$nativeRoot
    runtimePackRoot=$runtimePackOutput; directoryBuildProps=$propsPath
    persistentCompositeLifecycle=[bool]$runtimePackManifest.persistentCompositeLifecycle
    runtimePackManifest=$runtimePackManifestPath
    runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimePackManifestPath -Algorithm SHA256).Hash
    kernelElf=$kernelElf
    kernelElfSha256=(Get-FileHash -LiteralPath $kernelElf -Algorithm SHA256).Hash
    buildLogSha256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
    isolatedArtifactsPath=(Join-Path $nativeRoot 'project-artifacts')
    notesStartupDiagnostics=[bool]$NotesStartupDiagnostics
    notesStartupOperationIsolation=[bool]$NotesStartupOperationIsolation
    notesProofInvocationCorrection=[bool]$NotesProofInvocationCorrection
    notesPostRenderStartup=[bool]$NotesPostRenderStartup
    notesProofAuthorization=[bool]$NotesProofAuthorization
    notesInputRouting=[bool]$NotesInputRouting
    registryInputReentry=[bool]$RegistryInputReentry
    managedStateReentry=[bool]$ManagedStateReentry
    managedStateProvenance=[bool]$ManagedStateProvenance
    lifetimeStartAttribution=[bool]$LifetimeStartAttribution
    productionApplicationSelectorsPreserved=@(4,5,6,7,8)
    candidateSelector=9; candidateIdentity='com.guidexos.apps.managed.counterapp'
}
$manifest['managedStateProvenance'] = [bool]($ManagedStateProvenance -or $LifetimeStartAttribution)
if ($LifetimeStartAttribution) {
    $candidateElfText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($kernelElf))
    foreach ($requiredMarker in @(
        'C188R12R3_LIFETIME invocation=',
        'factory-enter',
        'factory-return',
        'resident-register-enter',
        'resident-register-return'
    )) {
        if (-not $candidateElfText.Contains($requiredMarker)) {
            throw "C188R12R3 NativeAOT candidate is missing internal lifetime marker: $requiredMarker"
        }
    }
    $manifest['lifetimeMarkerFamily'] = 'C188R12R3_LIFETIME'
}
$manifestPath = Join-Path $build 'c188-nativeaot-build.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188_NATIVEAOT_BUILD=PASS ELF_SHA256=$($manifest.kernelElfSha256)"
Write-Output "C188_NATIVEAOT_MANIFEST=$manifestPath"
