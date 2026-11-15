param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = '',
    [switch]$C188R12R4R5R4ThreadStoreReadiness,
    [switch]$C188R30ThreadAttachmentTrace,
    [switch]$C188R25FailFastOriginCapture,
    [switch]$C188R28GcStateGuardDiscriminator,
    [switch]$C188R29PreallocationGcStateGuard
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R24 candidate builds require PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $ArtifactRoot ('c188r24-initializemodules-registration\candidate-' +
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R24 candidate must remain below ArtifactRoot: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite R24 candidate: $candidate" }
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$r23Builder = Join-Path $RepoRoot 'scripts\dotnet\build-c188r23-settings-candidate.ps1'
$buildLog = Join-Path (Split-Path -Parent $candidate) ('r24-r23-build-' + [guid]::NewGuid().ToString('N') + '.log')
$buildOutput = (& $pwsh -NoProfile -File $r23Builder -RepoRoot $RepoRoot `
    -ArtifactRoot $ArtifactRoot -CandidateRoot $candidate `
    -C188R12R4R5R4ThreadStoreReadiness:$C188R12R4R5R4ThreadStoreReadiness `
    -C188R30ThreadAttachmentTrace:$C188R30ThreadAttachmentTrace `
    -C188R25FailFastOriginCapture:$C188R25FailFastOriginCapture `
    -C188R28GcStateGuardDiscriminator:$C188R28GcStateGuardDiscriminator `
    -C188R29PreallocationGcStateGuard:$C188R29PreallocationGcStateGuard 2>&1 | Out-String)
$buildCode = $LASTEXITCODE
Set-Content -LiteralPath $buildLog -Value $buildOutput -Encoding utf8
if ($buildCode -ne 0) { throw "R23 source build failed while preparing R24; see $buildLog" }
if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
    throw "R23 builder reported success without creating R24 candidate root: $candidate"
}

$oldManifestPath = Join-Path $candidate 'candidate-manifest.json'
$manifest = Get-Content -LiteralPath $oldManifestPath -Raw | ConvertFrom-Json
$mapText = Get-Content -LiteralPath ([string]$manifest.managedMap) -Raw
function Get-MapAddress([string]$Symbol) {
    $pattern = '(?m)^\s*\S+\s+' + [regex]::Escape($Symbol) + '\s+([A-Fa-f0-9]{16})\b'
    $match = [regex]::Match($mapText,$pattern)
    if (-not $match.Success) { throw "R24 linked map omits required symbol $Symbol" }
    return $match.Groups[1].Value.ToLowerInvariant()
}
$initialize = Get-MapAddress 'InitializeModules'
$rhp = Get-MapAddress 'RhpRegisterOsModule'
$malloc = Get-MapAddress 'malloc'
$stateHelper = Get-MapAddress '?initializeRuntimeState@?A0x1729e141@@YAXPEAE@Z'
$typeManagers = Get-MapAddress 'S_P_CoreLib_Internal_Runtime_CompilerHelpers_StartupCodeHelpers__CreateTypeManagers'
$modulesA = [Convert]::ToUInt64((Get-MapAddress '__modules_a'),16)
$modulesZ = [Convert]::ToUInt64((Get-MapAddress '__modules_z'),16)
if ($modulesZ -lt $modulesA -or (($modulesZ - $modulesA) % 8) -ne 0) {
    throw 'R24 module-header bounds are invalid in the linked map.'
}
if (-not $C188R25FailFastOriginCapture -and
    ($initialize -ne '00000001000ad660' -or $rhp -ne '00000001000f94d0' -or
     $malloc -ne '00000001000f6640' -or $stateHelper -ne '00000001000f2f80' -or
     $typeManagers -ne '00000001000ad820')) {
    throw 'R24 regenerated link changed the inspected code address profile; review disassembly before packaging.'
}
$addresses = [ordered]@{
    imageBase='0000000100000000'
    initializeRuntimeState=$stateHelper
    initializeModulesCallSite=('0x{0:x}' -f ([Convert]::ToUInt64($stateHelper,16) + 0x211))
    initializeModules=$initialize
    initializeModulesCallReturn=('0x{0:x}' -f ([Convert]::ToUInt64($stateHelper,16) + 0x216))
    initializeModulesRhpCallReturn=('0x{0:x}' -f ([Convert]::ToUInt64($initialize,16) + 0x28))
    firstReversePInvokeCall=('0x{0:x}' -f ([Convert]::ToUInt64($initialize,16) + 0x1b))
    rhpRegisterOsModuleCallSite=('0x{0:x}' -f ([Convert]::ToUInt64($initialize,16) + 0x23))
    rhpRegisterCallReturn=('0x{0:x}' -f ([Convert]::ToUInt64($initialize,16) + 0x28))
    rhpRegisterOsModule=$rhp
    rhpRegisterOsModuleSize='0x61'
    rhpNativeAllocationCallSite=('0x{0:x}' -f ([Convert]::ToUInt64($rhp,16) + 0x15))
    rhpListPublicationLoop=('0x{0:x}' -f ([Convert]::ToUInt64($rhp,16) + 0x40))
    rhpListPublished=('0x{0:x}' -f ([Convert]::ToUInt64($rhp,16) + 0x52))
    rhpAllocationFailure=('0x{0:x}' -f ([Convert]::ToUInt64($rhp,16) + 0x5b))
    nativeMalloc=$malloc
    createTypeManagers=$typeManagers
    createTypeManagersCall=('0x{0:x}' -f ([Convert]::ToUInt64($initialize,16) + 0x41))
    osModuleHandle='0000000100000000'
    moduleHeadersStart=('0x{0:x}' -f $modulesA)
    moduleHeadersEnd=('0x{0:x}' -f $modulesZ)
    moduleHeaderPointerBytes=8
    moduleHeaderCount=[int](($modulesZ - $modulesA) / 8)
    classlibFunctionCount=16
    sharedHelperStackAllocation='0x68'
    initializerStackAllocation='0x40'
    initializerFifthArgumentAtEntry='0x28'
    initializerFifthArgumentAfterPrologue='0x90'
    rhpShadowSpace='0x20'
    osModuleEntrySize='0x10'
    registrationReturnSuccess='OS module handle in RAX'
}
$addressesPath = Join-Path $candidate 'r24-runtime-addresses.json'
$addresses | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $addressesPath -Encoding utf8

$package = Join-Path $candidate 'r24-runner-package'
$null = New-Item -ItemType Directory -Path $package
$packageRelative = @(
    'scripts/dotnet/run-c188r24-initializemodules.ps1',
    'scripts/dotnet/build-c188r24-initializemodules-candidate.ps1',
    'scripts/dotnet/c188r24-initializemodules-predicates.ps1',
    'scripts/dotnet/test-c188r24-initializemodules.py',
    'scripts/dotnet/test-c188r24-initializemodules-snapshot.ps1',
    'scripts/dotnet/run-c188-qac-guest.ps1',
    'scripts/dotnet/run-c188r12r1-settings-handoff.ps1')
$packageSources = @()
foreach ($relative in $packageRelative) {
    $source = Join-Path $RepoRoot $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "R24 runner source is missing: $relative" }
    $destination = Join-Path $package (Split-Path -Leaf $source)
    Copy-Item -LiteralPath $source -Destination $destination
    $packageSources += [ordered]@{source=$relative;packagedPath=[System.IO.Path]::GetRelativePath($candidate,$destination).Replace('\','/');sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToUpperInvariant()}
}

$binaryTest = Join-Path $RepoRoot 'scripts\dotnet\test-c188r24-initializemodules.py'
$binaryOutput = ''
$binaryCode = 0
if ($C188R30ThreadAttachmentTrace) {
    # This R24 check attributes a direct call from the isolated R6 helper.
    # R30 deliberately uses the persistent R21 production lifecycle; its
    # separate linked qualification verifies generated InitializeModules ->
    # RhpReversePInvoke -> stock attachment -> first array allocation.
    $binaryOutput = 'R24 R6 shared-helper call-site assertion is not applicable to R30 persistent lifecycle; R30 linked qualification owns generated-entry verification.'
    $addresses.initializeModulesCallSite = $null
    $addresses.initializeModulesCallReturn = $null
} else {
    $binaryOutput = (& $python $binaryTest '--candidate-root' $candidate 2>&1 | Out-String)
    $binaryCode = $LASTEXITCODE
}
if ($binaryCode -eq 0 -and -not $C188R30ThreadAttachmentTrace) {
    $callOffsetMatch = [regex]::Match($binaryOutput,'R24_INITIALIZEMODULES_SHARED_CALL_OFFSET=(0x[0-9a-fA-F]+)')
    if (-not $callOffsetMatch.Success) { throw 'R24 ELF test did not identify the linked InitializeModules call site.' }
    $helperAddress = [Convert]::ToUInt64($stateHelper,16)
    $callOffset = [Convert]::ToUInt64($callOffsetMatch.Groups[1].Value.Substring(2),16)
    $addresses.initializeModulesCallSite = '0x{0:x}' -f ($helperAddress + $callOffset)
    $addresses.initializeModulesCallReturn = '0x{0:x}' -f ($helperAddress + $callOffset + 5)
    $addresses | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $addressesPath -Encoding utf8
}
$snapshotTest = Join-Path $RepoRoot 'scripts\dotnet\test-c188r24-initializemodules-snapshot.ps1'
$snapshotArgs = @()
if ($C188R25FailFastOriginCapture) { $snapshotArgs += @('-CandidateRoot',$candidate) }
$snapshotOutput = (& $pwsh -NoProfile -File $snapshotTest @snapshotArgs 2>&1 | Out-String)
$snapshotCode = $LASTEXITCODE
$hostedLog = Join-Path $candidate 'r24-hosted-qualification.log'
Set-Content -LiteralPath $hostedLog -Value ($binaryOutput + "`n" + $snapshotOutput) -Encoding utf8
if ($binaryCode -ne 0 -or $snapshotCode -ne 0) { throw "R24 hosted qualification failed; see $hostedLog" }

$sourceRelativePaths = @($packageRelative + @(
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_startup_imports.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_contract.cpp'))
$r24Sources = @($sourceRelativePaths | ForEach-Object {
    $path = Join-Path $RepoRoot $_
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()}
})
$files = @(Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {
    $_.FullName -notin @((Join-Path $candidate 'candidate-manifest.json'),(Join-Path $candidate 'candidate-integrity.json'))
} | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=[System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToUpperInvariant()}
})
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$preservedObjects = [ordered]@{
    'c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj'='3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD'
    'c188r12r4r5r4r5r2r2_native_alloc_record_test.obj'='605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3'
    'guidexos_nativeaot_native_alloc_record.obj'='2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183'
}
$integrity = [ordered]@{
    schema='GUIDEXOS_C188R24_CANDIDATE_INTEGRITY_V1';candidateRoot=$candidate
    files=$files;preservedModifiedObjects=$preservedObjects
    unverifiedRootObjectInputsConsumed=$false
}
$integrity | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $integrityPath -Encoding utf8
$manifest.result='PASS_BUILD_READY_FOR_QAC_PREFLIGHT'
$manifest.phase='C188R24_INITIALIZEMODULES_REGISTRATION'
$manifest.buildProfile=[string]$manifest.buildProfile + '+R24InitializeModulesRegistrationObservation'
$manifest.nativeBootTarget='Pause on readiness timeout and classify vCPU RIP/registers against exact linked initializer/import ranges; capture M2-M7 without guest instrumentation'
$manifest | Add-Member -NotePropertyName r24RuntimeAddresses -NotePropertyValue $addressesPath -Force
$manifest | Add-Member -NotePropertyName r24SharedCallsiteQualification -NotePropertyValue $(if ($C188R30ThreadAttachmentTrace) { 'NOT_APPLICABLE_R30_LINKED_INITIALIZER_QUALIFICATION' } else { 'PASS' }) -Force
$manifest | Add-Member -NotePropertyName r24RuntimeAddressesSha256 -NotePropertyValue ((Get-FileHash -LiteralPath $addressesPath -Algorithm SHA256).Hash.ToUpperInvariant()) -Force
$manifest | Add-Member -NotePropertyName r24HostedQualification -NotePropertyValue $hostedLog -Force
$manifest | Add-Member -NotePropertyName r24HostedQualificationSha256 -NotePropertyValue ((Get-FileHash -LiteralPath $hostedLog -Algorithm SHA256).Hash.ToUpperInvariant()) -Force
$manifest | Add-Member -NotePropertyName r24RunnerPackage -NotePropertyValue $package -Force
$manifest | Add-Member -NotePropertyName r24RunnerSources -NotePropertyValue $packageSources -Force
$manifest | Add-Member -NotePropertyName r24SourceInputs -NotePropertyValue $r24Sources -Force
$manifest | Add-Member -NotePropertyName r24BuildLog -NotePropertyValue $buildLog -Force
$manifest | Add-Member -NotePropertyName r24BuildLogSha256 -NotePropertyValue ((Get-FileHash -LiteralPath $buildLog -Algorithm SHA256).Hash.ToUpperInvariant()) -Force
$manifest | Add-Member -NotePropertyName r24ExpectedNativeMilestones -NotePropertyValue ([ordered]@{
    M2='RIP inside generated InitializeModules range or a downstream callee'
    M3='RIP inside linked RhpRegisterOsModule body'
    M4='RIP in post-publication success epilogue or expected OS module handle observed in RAX after import return'
    M5='RIP in generated code after the RhpRegisterOsModule call'
    M6='RIP at/inside CreateTypeManagers or its post-call continuation'
    M7='RIP at/after shared helper continuation following InitializeModules'}) -Force
$manifest.candidateIntegrity=$integrityPath
$manifest.candidateIntegritySha256=(Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
$manifest.candidateFiles=$files
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R24_CANDIDATE=PASS ROOT=$candidate KERNEL_SHA256=$($manifest.kernelSha256) SETTINGS_ELF_SHA256=$($manifest.managedElfSha256)"
Write-Output "C188R24_INTEGRITY_FILES=$($files.Count) R24_ASSERTIONS=$(if ($C188R30ThreadAttachmentTrace) { 'NOT_APPLICABLE_R30_PROFILE' } else { ([regex]::Match($binaryOutput,'R24_INITIALIZEMODULES_HOSTED_ASSERTIONS=(\d+)')).Groups[1].Value }) SNAPSHOT_ASSERTIONS=$((([regex]::Match($snapshotOutput,'R24_INITIALIZEMODULES_SNAPSHOT_ASSERTIONS=(\d+)')).Groups[1].Value))"
Write-Output "C188R24_CANDIDATE_MANIFEST=$manifestPath"
