param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R30 candidate builds require PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$phaseRoot = Join-Path $ArtifactRoot 'c188r30-thread-attachment'
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $phaseRoot ('candidate-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $phaseRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R30 candidate must remain below its fresh artifact phase root: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite R30 candidate: $candidate" }
$null = New-Item -ItemType Directory -Path $phaseRoot -Force
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$builder = Join-Path $RepoRoot 'scripts\dotnet\build-c188r25-failfast-origin-candidate.ps1'
$builderOutput = & $pwsh -NoProfile -File $builder -RepoRoot $RepoRoot `
    -ArtifactRoot $ArtifactRoot -CandidateRoot $candidate `
    -C188R30ThreadAttachmentTrace `
    -C188R28GcStateGuardDiscriminator -C188R29PreallocationGcStateGuard 2>&1 | Out-String
$builderExit = $LASTEXITCODE
$buildLog = Join-Path $phaseRoot ('r30-build-' + [guid]::NewGuid().ToString('N') + '.log')
Set-Content -LiteralPath $buildLog -Value $builderOutput -Encoding utf8
if ($builderExit -ne 0) { throw "R25/R28/R29 candidate builder failed while producing R30; see $buildLog" }
$r25Match = [regex]::Match($builderOutput,'(?m)^R25_MANIFEST=(.+?)\s*$')
if (-not $r25Match.Success) { throw 'R25 builder output omitted its source and candidate manifest.' }
$r25ManifestPath = [System.IO.Path]::GetFullPath($r25Match.Groups[1].Value.Trim())
$r25Manifest = Get-Content -LiteralPath $r25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$integrity = Get-Content -LiteralPath $integrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
$runtimeManifestPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.runtimePackManifest)
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$palReplacement = $runtimeManifest.archiveMembership.threadStorePalCommonReplacement
if ([string]$r25Manifest.sourceHead -cne (& git -C $RepoRoot rev-parse HEAD).Trim() -or
    [string]$r25Manifest.branch -cne 'v1.1_DOTNET_SUPPORT' -or
    [string]$runtimeManifest.identity -cne 'guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1' -or
    [string]$runtimeManifest.runtimeIdentity.sourceCommit -cne '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3' -or
    [string]$runtimeManifest.runtimeIdentity.gc -cne 'Workstation' -or
    [bool]$runtimeManifest.imageBackedArenaPresent -or
    -not [bool]$runtimeManifest.c188r30ThreadAttachmentTrace -or
    [string]$runtimeManifest.c188r30ThreadAttachmentTraceMarker -cne 'GUIDEXOS_NATIVEAOT_C188R30_THREAD_ATTACHMENT_TRACE' -or
    [bool]$runtimeManifest.c188r12r4r5r4ThreadStoreReadiness -or
    [bool]$runtimeManifest.c188r12r4r5r2r3InitializeModulesAttribution -or
    -not [bool]$runtimeManifest.c188r25FailFastOriginCapture -or
    -not [bool]$runtimeManifest.c188r28GcStateGuardDiscriminator -or
    -not [bool]$runtimeManifest.c188r29PreallocationGcStateGuard -or
    $null -eq $palReplacement -or
    [string]$palReplacement.profile -cne 'C188R30_THREAD_ATTACHMENT_TRACE' -or
    [int]$palReplacement.removedCount -ne 0 -or
    [int]$palReplacement.archiveMemberCount -ne 1 -or
    [string]$runtimeManifest.c188r30PalCommonStockMember -cne [string]$palReplacement.stockMember -or
    [string]$runtimeManifest.c188r30PalCommonReplacementObject -cne [string]$palReplacement.replacementObject -or
    [string]$runtimeManifest.c188r30PalCommonReplacementSha256 -ine [string]$palReplacement.replacementSha256 -or
    -not (Test-Path -LiteralPath ([string]$palReplacement.replacementObject) -PathType Leaf) -or
    (Get-FileHash -LiteralPath ([string]$palReplacement.replacementObject) -Algorithm SHA256).Hash -ine [string]$palReplacement.replacementSha256 -or
    [int]$runtimeManifest.failFastOriginRecordSchema -ne 3 -or
    [int]$runtimeManifest.failFastOriginRecordBytes -ne 192 -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
    [string]$candidateManifest.actualFactoryConstructorSymbol -cne 'ManagedSettingsCenter___ctor_0' -or
    -not [bool]$candidateManifest.unwindPrimitive.linkedByManagedProof -or
    [string]$candidateManifest.unwindPrimitive.sourceSha256 -ine '9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E' -or
    [bool]$integrity.unverifiedRootObjectInputsConsumed -or
    [string]$candidateManifest.runtimePackManifest -cne $runtimeManifestPath) {
    throw 'R30 candidate failed source-head, pinned runtime, Workstation GC, stock PAL removal/guideXOS stack-bounds replacement, selector-5 factory, accepted R20 unwind, or schema-3 checks.'
}
foreach ($name in @(
    'c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj',
    'c188r12r4r5r4r5r2r2_native_alloc_record_test.obj',
    'guidexos_nativeaot_native_alloc_record.obj')) {
    $path = Join-Path $RepoRoot $name
    if (-not $integrity.preservedModifiedObjects.PSObject.Properties[$name] -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$integrity.preservedModifiedObjects.$name) {
        throw "R30 candidate did not preserve protected object input: $name"
    }
}
$analyzer = Join-Path $RepoRoot 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$analyzerOutput = & $python $analyzer '--candidate-root' $candidate 2>&1 | Out-String
$analyzerExit = $LASTEXITCODE
$analyzerPath = Join-Path $phaseRoot ((Split-Path -Leaf $candidate) + '-r25-analyzer.json')
if ($analyzerExit -ne 0) { throw "R30 analyzer packaging rehearsal failed: $analyzerOutput" }
$analyzerObject = $analyzerOutput | ConvertFrom-Json -ErrorAction Stop
Set-Content -LiteralPath $analyzerPath -Value ($analyzerObject | ConvertTo-Json -Depth 12) -Encoding utf8
$linkedQualification = Join-Path $phaseRoot ((Split-Path -Leaf $candidate) + '-r30-linked-qualification.log')
$r30Test = Join-Path $RepoRoot 'scripts\dotnet\test-c188r30-thread-attachment.py'
$qualificationOutput = & $python $r30Test '--candidate-root' $candidate 2>&1 | Out-String
$qualificationExit = $LASTEXITCODE
Set-Content -LiteralPath $linkedQualification -Value $qualificationOutput -Encoding utf8
if ($qualificationExit -ne 0 -or $qualificationOutput -notmatch 'R30_THREAD_ATTACHMENT_TESTS=\d+ RESULT=PASS') {
    throw "R30 linked attachment qualification failed; see $linkedQualification"
}
$sourcePaths = @(
    'scripts/dotnet/build-c188r30-thread-attachment-candidate.ps1',
    'scripts/dotnet/run-c188r30-thread-attachment.ps1',
    'scripts/dotnet/test-c188r30-thread-attachment.py',
    'scripts/dotnet/build-c188r23-settings-candidate.ps1',
    'scripts/dotnet/build-c188r24-initializemodules-candidate.ps1',
    'scripts/dotnet/build-c188r25-failfast-origin-candidate.ps1',
    'scripts/dotnet/build-managed-hostlog-proof.ps1',
    'scripts/dotnet/run-c188r25-failfast-origin.ps1',
    'scripts/dotnet/run-c188r24-initializemodules.ps1',
    'scripts/dotnet/c188r24-initializemodules-predicates.ps1',
    'scripts/dotnet/analyze-c188r25-failfast-origin.py',
    'scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_r29_preallocation_guard.h',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_failfast_origin.h',
    'tools/dotnet/runtime-pack/runtime-pack.lock.json',
    'tools/dotnet/runtime-pack/build-runtime-pack.ps1',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_common_replacement.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_startup_imports.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_pal_minwin_replacement.cpp',
    'tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_readiness.h')
$sourceInputs = @($sourcePaths | ForEach-Object {
    $path = Join-Path $RepoRoot $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R30 source input is missing: $_" }
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()}
})
$manifestPath = Join-Path $phaseRoot ((Split-Path -Leaf $candidate) + '-r30-candidate-manifest.json')
$manifest = [ordered]@{
    schema='GUIDEXOS_C188R30_THREAD_ATTACHMENT_CANDIDATE_V1'
    result='PASS_HOSTED_READY_FOR_QAC_ADMISSION'
    createdUtc=[DateTime]::UtcNow.ToString('O')
    sourceHead=[string]$r25Manifest.sourceHead
    branch=[string]$r25Manifest.branch
    repoRoot=$RepoRoot
    candidateRoot=$candidate
    candidateManifestPath=$candidateManifestPath
    candidateManifestSha256=(Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    candidateIntegrityPath=$integrityPath
    candidateIntegritySha256=(Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
    candidateFileCount=@($integrity.files).Count
    r25ManifestPath=$r25ManifestPath
    r25ManifestSha256=(Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimePackManifestPath=$runtimeManifestPath
    runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimeProfile=[string]$runtimeManifest.identity
    hostedQualificationPath=[string]$r25Manifest.hostedQualificationPath
    hostedQualificationSha256=(Get-FileHash -LiteralPath ([string]$r25Manifest.hostedQualificationPath) -Algorithm SHA256).Hash.ToUpperInvariant()
    linkedQualificationPath=$linkedQualification
    linkedQualificationSha256=(Get-FileHash -LiteralPath $linkedQualification -Algorithm SHA256).Hash.ToUpperInvariant()
    analyzerQualificationPath=$analyzerPath
    analyzerQualificationSha256=(Get-FileHash -LiteralPath $analyzerPath -Algorithm SHA256).Hash.ToUpperInvariant()
    buildLog=$buildLog
    buildLogSha256=(Get-FileHash -LiteralPath $buildLog -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimeSourceCommit=[string]$runtimeManifest.runtimeIdentity.sourceCommit
    nativeAotVersion=[string]$runtimeManifest.runtimeIdentity.nativeAot
    gcProvider=[string]$runtimeManifest.runtimeIdentity.gc
    imageBackedArenaPresent=[bool]$runtimeManifest.imageBackedArenaPresent
    selector5SettingsFactory=[string]$candidateManifest.actualManagedSettingsFactory
    acceptedR20UnwindSourceSha256=[string]$candidateManifest.unwindPrimitive.sourceSha256
    threadAttachmentTraceProfile=[bool]$runtimeManifest.c188r30ThreadAttachmentTrace
    r12ThreadStoreReadinessProfile=[bool]$runtimeManifest.c188r12r4r5r4ThreadStoreReadiness
    r29PreallocationGuard=[ordered]@{schema=3;bytes=192;enabled=[bool]$runtimeManifest.c188r29PreallocationGcStateGuard}
    sourceInputs=$sourceInputs
    preservedModifiedObjects=$integrity.preservedModifiedObjects
    oneBoot=[ordered]@{maxGuestBoots=1;automaticRetry=$false;retryAllowed=$false;guardCreated=$false}
}
$manifest | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "R30_CANDIDATE=$candidate"
Write-Output "R30_MANIFEST=$manifestPath"
Write-Output "R30_MANIFEST_SHA256=$((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant())"
Write-Output ($qualificationOutput.Trim())
