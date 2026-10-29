param(
    [Parameter(Mandatory=$true)][string]$BuildRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188 production NativeAOT builds require PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoots = @(
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188-external-app-identity')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r3-notes-startup-diagnostics')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r4-notes-startup-operation')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r5-notes-proof-invocation')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r6-notes-postrender')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r7-notes-proof-authorization')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r9-registry-input-reentry')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r10-nativeaot-state-reentry')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r11-managed-state-provenance')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r12r3-lifetime-start-attribution')))
$root = [System.IO.Path]::GetFullPath($BuildRoot)
$allowedRoot = @($phaseRoots | Where-Object {
    $relative = [System.IO.Path]::GetRelativePath($_, $root)
    $relative -ne '.' -and -not $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -and
        -not [System.IO.Path]::IsPathRooted($relative)
} | Select-Object -First 1)
if ($allowedRoot.Count -eq 0) {
    throw "BuildRoot must be a unique directory below one of: $($phaseRoots -join ', ')."
}
if (Test-Path -LiteralPath $root) {
    throw "Refusing to overwrite C188 proof-disabled production NativeAOT output: $root"
}
$null = New-Item -ItemType Directory -Path $root
$nativeRoot = Join-Path $root 'nativeaot'
$runtimePackRoot = Join-Path $repo 'tools/dotnet/runtime-pack'
$runtimePackOutput = Join-Path $root 'runtime-pack'
$hostOutput = Join-Path $root 'host-output'
$propsPath = Join-Path $root 'msbuild/Directory.Build.props'
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $propsPath), $hostOutput
$props = @"
<Project>
  <PropertyGroup>
    <ResolveNuGetPackages>false</ResolveNuGetPackages>
    <C188ArtifactIsolation>true</C188ArtifactIsolation>
    <C188BuildScope>production</C188BuildScope>
    <C188HostOutputRoot>$hostOutput</C188HostOutputRoot>
  </PropertyGroup>
</Project>
"@
Set-Content -LiteralPath $propsPath -Value $props -Encoding utf8

$buildScript = Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1'
$arguments = @{
    RepoRoot = $repo
    OutputRoot = $nativeRoot
    DirectoryBuildPropsPath = $propsPath
    RuntimePackRoot = $runtimePackRoot
    RuntimePackOutputRoot = $runtimePackOutput
    AllocationMode = 'Allocating'
    ManagedProjectMode = 'C160Composite'
    ProductionApplication = $true
    # The production composite remains resident and receives later managed
    # input callbacks, so its image-backed managed heap must remain resident.
    PersistentCompositeLifecycle = $true
    UseGuideXosRuntimePack = $true
    C155ManagedNotesSession = $true
    C156ControlModifierShortcuts = $true
    C157ManagedNotesNewDocument = $true
    C158ManagedCalculator = $true
    C161ManagedTaskManager = $true
    C162ManagedTaskManagerClose = $true
    C163ManagedFileExplorer = $true
}
$logPath = Join-Path $root 'production-nativeaot-build.log'
$output = (& $buildScript @arguments *>&1 | Out-String)
Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
if ($LASTEXITCODE -ne 0 -or $output -notmatch 'Managed host-log proof built successfully\.') {
    throw "Proof-disabled production NativeAOT build failed with exit code $LASTEXITCODE. See $logPath"
}

$elfPath = Join-Path $nativeRoot 'artifacts/HostLogProof.elf'
$mapPath = Join-Path $nativeRoot 'artifacts/HostLogProof.map'
foreach ($path in @($elfPath,$mapPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Production NativeAOT artifact is missing: $path" }
}
$elfBytes = [System.IO.File]::ReadAllBytes($elfPath)
if ($elfBytes.Length -lt 64 -or $elfBytes[0] -ne 0x7f -or $elfBytes[1] -ne 0x45 -or $elfBytes[2] -ne 0x4c -or $elfBytes[3] -ne 0x46 -or $elfBytes[4] -ne 2 -or $elfBytes[5] -ne 1) {
    throw 'Proof-disabled production NativeAOT image is not an ELF64 little-endian image.'
}
$readelf = (Get-Command 'readelf' -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $readelf) { throw 'readelf is required to validate the production NativeAOT ELF.' }
$header = (& $readelf -h $elfPath 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $header -notmatch 'Advanced Micro Devices X86-64') {
    throw 'Proof-disabled production NativeAOT image is not an x86-64 ELF.'
}
$mapText = [System.IO.File]::ReadAllText($mapPath)
$elfText = [System.Text.Encoding]::ASCII.GetString($elfBytes)
$runtimePackManifestPath = Join-Path $runtimePackOutput 'runtime-pack.manifest.json'
if (-not (Test-Path -LiteralPath $runtimePackManifestPath -PathType Leaf)) {
    throw "Production runtime-pack manifest is missing: $runtimePackManifestPath"
}
$runtimePackManifest = Get-Content -LiteralPath $runtimePackManifestPath -Raw | ConvertFrom-Json
if (-not [bool]$runtimePackManifest.persistentCompositeLifecycle) {
    throw 'C188 production NativeAOT must preserve its managed heap across reverse-P/Invoke entries.'
}
foreach ($required in @('GuideXosNativeAotApplicationEntry','GuideXosApplicationRegistry__Dispatch')) {
    if (-not $mapText.Contains($required)) { throw "Production NativeAOT map omitted standard dispatch symbol $required" }
}
foreach ($development in @(
    'DispatchC188ExternalAppIdentity',
    'GetC188GuestSelectorRoute',
    'com.guidexos.apps.managed.counterapp',
    'Managed CounterApp',
    'GuideXos_C185_CounterApp'
)) {
    if ($mapText.Contains($development) -or $elfText.Contains($development)) {
        throw "Proof-disabled production NativeAOT artifacts contain development-only identity material: $development"
    }
}
if ($mapText.Contains('C188R12R3_LIFETIME') -or $elfText.Contains('C188R12R3_LIFETIME') -or
    $mapText.Contains('C188R3-') -or $elfText.Contains('C188R3-') -or
    $mapText.Contains('C188R4-') -or $elfText.Contains('C188R4-') -or
    $mapText.Contains('C188R6-') -or $elfText.Contains('C188R6-') -or
    $mapText.Contains('C188R7-') -or $elfText.Contains('C188R7-') -or
    $mapText.Contains('C188R11-') -or $elfText.Contains('C188R11-')) {
    throw 'Proof-disabled production NativeAOT artifacts contain C188R3/R4/R6/R7 startup instrumentation.'
}
$manifest = [ordered]@{
    result='PASS'
    buildRoot=$root
    profile='C160Composite with production applications and no test controls'
    productionApplicationSelectors=@(4,5,6,7,8)
    counterAppDescriptorPresent=$false
    c188SelectorRoutePresent=$false
    elf=$elfPath
    elfSha256=(Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash
    mapSha256=(Get-FileHash -LiteralPath $mapPath -Algorithm SHA256).Hash
    buildLogSha256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
    persistentCompositeLifecycle=[bool]$runtimePackManifest.persistentCompositeLifecycle
    runtimePackManifest=$runtimePackManifestPath
    runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimePackManifestPath -Algorithm SHA256).Hash
    freestandingElfHeader=$header.Trim()
}
$manifestPath = Join-Path $root 'production-nativeaot-qualification.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188_PRODUCTION_NATIVEAOT=PASS ELF_SHA256=$($manifest.elfSha256)"
Write-Output "C188_PRODUCTION_NATIVEAOT_MANIFEST=$manifestPath"
