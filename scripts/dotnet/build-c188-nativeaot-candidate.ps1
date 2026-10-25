param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter()][switch]$NotesStartupDiagnostics,
    [Parameter()][switch]$NotesStartupOperationIsolation,
    [Parameter()][switch]$NotesProofInvocationCorrection,
    [Parameter()][switch]$NotesPostRenderStartup,
    [Parameter()][switch]$NotesProofAuthorization,
    [Parameter()][switch]$NotesInputRouting
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
if ($selectedPhaseCount -gt 1) { throw 'Choose one C188 Notes startup diagnostic phase.' }
$phaseRootName = if ($NotesInputRouting) { 'out/dotnet/c188r8-notes-input-routing' } elseif ($NotesProofAuthorization) { 'out/dotnet/c188r7-notes-proof-authorization' } elseif ($NotesPostRenderStartup) { 'out/dotnet/c188r6-notes-postrender' } elseif ($NotesProofInvocationCorrection) { 'out/dotnet/c188r5-notes-proof-invocation' } elseif ($NotesStartupOperationIsolation) { 'out/dotnet/c188r4-notes-startup-operation' } elseif ($NotesStartupDiagnostics) { 'out/dotnet/c188r3-notes-startup-diagnostics' } else { 'out/dotnet/c188-external-app-identity' }
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
if ($NotesStartupDiagnostics) {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification') -NotesStartupDiagnostics
} elseif ($NotesStartupOperationIsolation) {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification') -NotesStartupOperationIsolation
} elseif ($NotesProofInvocationCorrection) {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification') -NotesProofInvocationCorrection
} elseif ($NotesPostRenderStartup) {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification') -NotesPostRenderStartup
} elseif ($NotesProofAuthorization) {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification') -NotesProofAuthorization
} elseif ($NotesInputRouting) {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification') -NotesInputRouting
} else {
    & $verify -CandidateRoot $nativeRoot -EvidenceRoot (Join-Path $nativeRoot 'qualification')
}
if ($LASTEXITCODE -ne 0) { throw "C188 NativeAOT artifact verification failed with exit code $LASTEXITCODE." }
$kernelElf = Join-Path $nativeRoot 'artifacts/HostLogProof.elf'
$manifest = [ordered]@{
    result='PASS'; buildRoot=$build; nativeAotRoot=$nativeRoot
    runtimePackRoot=$runtimePackOutput; directoryBuildProps=$propsPath
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
    productionApplicationSelectorsPreserved=@(4,5,6,7,8)
    candidateSelector=9; candidateIdentity='com.guidexos.apps.managed.counterapp'
}
$manifestPath = Join-Path $build 'c188-nativeaot-build.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188_NATIVEAOT_BUILD=PASS ELF_SHA256=$($manifest.kernelElfSha256)"
Write-Output "C188_NATIVEAOT_MANIFEST=$manifestPath"
