param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R29 candidate builds require PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$phaseRoot = Join-Path $ArtifactRoot 'c188r29-preallocation-gc-state-guard'
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $phaseRoot ('candidate-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $phaseRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R29 candidate must remain below its fresh artifact phase root: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite R29 candidate: $candidate" }
$null = New-Item -ItemType Directory -Path $phaseRoot -Force
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$builder = Join-Path $RepoRoot 'scripts\dotnet\build-c188r25-failfast-origin-candidate.ps1'
$builderOutput = & $pwsh -NoProfile -File $builder -RepoRoot $RepoRoot `
    -ArtifactRoot $ArtifactRoot -CandidateRoot $candidate `
    -C188R28GcStateGuardDiscriminator -C188R29PreallocationGcStateGuard 2>&1 | Out-String
$builderExit = $LASTEXITCODE
$buildLog = Join-Path $phaseRoot ('r29-build-' + [guid]::NewGuid().ToString('N') + '.log')
Set-Content -LiteralPath $buildLog -Value $builderOutput -Encoding utf8
if ($builderExit -ne 0) { throw "R25/R28 hosted candidate builder failed while producing R29; see $buildLog" }
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
if (-not [bool]$runtimeManifest.c188r25FailFastOriginCapture -or
    -not [bool]$runtimeManifest.c188r28GcStateGuardDiscriminator -or
    [string]$runtimeManifest.c188r28GcStateGuardDiscriminatorMarker -cne 'GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR' -or
    -not [bool]$runtimeManifest.c188r29PreallocationGcStateGuard -or
    [string]$runtimeManifest.c188r29PreallocationGcStateGuardMarker -cne 'GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD' -or
    [int]$runtimeManifest.failFastOriginRecordSchema -ne 3 -or
    [int]$runtimeManifest.failFastOriginRecordBytes -ne 192 -or
    -not [bool]$runtimeManifest.c188r12r4r5GcOwnershipProof -or
    [string]$runtimeManifest.runtimeIdentity.gc -cne 'Workstation' -or
    [bool]$runtimeManifest.imageBackedArenaPresent -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
    [string]$candidateManifest.actualFactoryConstructorSymbol -cne 'ManagedSettingsCenter___ctor_0' -or
    -not [bool]$candidateManifest.unwindPrimitive.linkedByManagedProof -or
    [string]$candidateManifest.unwindPrimitive.sourceSha256 -ine '9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E' -or
    [bool]$integrity.unverifiedRootObjectInputsConsumed -or
    [int]$r25Manifest.diagnostics.recordBytes -ne 192 -or
    [int]$r25Manifest.diagnostics.schemaVersion -ne 3 -or
    -not [bool]$r25Manifest.diagnostics.gcStateGuardDiscriminator -or
    -not [bool]$r25Manifest.diagnostics.preallocationGcStateGuardDiscriminator) {
    throw 'R29 candidate failed its Workstation GC, accepted unwind, real Settings factory, or schema-3 source-linkage checks.'
}
$r29ManifestPath = Join-Path $phaseRoot ((Split-Path -Leaf $candidate) + '-r29-candidate-manifest.json')
$manifest = [ordered]@{
    schema='GUIDEXOS_C188R29_PREALLOCATION_GC_STATE_CANDIDATE_V1'
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
    linkedQualificationPath=[string]$r25Manifest.linkedQualificationPath
    linkedQualificationSha256=(Get-FileHash -LiteralPath ([string]$r25Manifest.linkedQualificationPath) -Algorithm SHA256).Hash.ToUpperInvariant()
    hostedQualificationPath=[string]$r25Manifest.hostedQualificationPath
    hostedQualificationSha256=(Get-FileHash -LiteralPath ([string]$r25Manifest.hostedQualificationPath) -Algorithm SHA256).Hash.ToUpperInvariant()
    buildLog=$buildLog
    buildLogSha256=(Get-FileHash -LiteralPath $buildLog -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimeProfile=[string]$runtimeManifest.identity
    runtimeSourceCommit=[string]$runtimeManifest.runtimeIdentity.sourceCommit
    nativeAotVersion=[string]$runtimeManifest.runtimeIdentity.version
    gcProvider=[string]$runtimeManifest.runtimeIdentity.gc
    c188ArenaPresent=[bool]$runtimeManifest.imageBackedArenaPresent
    acceptedR20UnwindSourceSha256=[string]$candidateManifest.unwindPrimitive.sourceSha256
    selector5SettingsFactory=[string]$candidateManifest.actualManagedSettingsFactory
    firstFailureRecord=[ordered]@{
        address=[string]$r25Manifest.diagnostics.recordAddress
        schema=3;bytes=192;wordCount=24
        marker=[string]$r25Manifest.diagnostics.marker
        guardIdentifiers=@(
            'THREADSTORE_CURRENT_THREAD_UNAVAILABLE',
            'THREAD_ALLOCATION_CONTEXT_UNAVAILABLE',
            'GC_HEAP_NOT_INITIALIZED',
            'GC_HEAP_POINTER_UNAVAILABLE',
            'GC_STATE_READ_FAILED_AFTER_PREREQUISITES',
            'CURRENT_THREAD_OUTPUT_NULL',
            'GC_HEAP_OUTPUT_NULL',
            'GC_STATE_SAFE_READ_FAILED',
            'GC_STATE_READ_INCOMPLETE')
        evaluationOrder=@('ThreadStore current Thread','Thread allocation context','GC heap initialized',
            'GC heap pointer available','safe GC state reads completed','non-null state outputs')
    }
    sourceInputs=$r25Manifest.sourceInputs
    preservedModifiedObjects=$r25Manifest.r24Baseline.preservedModifiedObjects
    oneBoot=[ordered]@{maxGuestBoots=1;automaticRetry=$false;retryAllowed=$false;guardCreated=$false}
}
$manifest | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath $r29ManifestPath -Encoding utf8
Write-Output "R29_CANDIDATE=$candidate"
Write-Output "R29_MANIFEST=$r29ManifestPath"
Write-Output "R29_MANIFEST_SHA256=$((Get-FileHash -LiteralPath $r29ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant())"
Write-Output "R29_HOSTED_QUALIFICATION=$([regex]::Match($builderOutput,'R29_PREALLOCATION_GC_STATE_TESTS=\d+ RESULT=PASS').Value)"
