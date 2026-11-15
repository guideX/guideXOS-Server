param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R28 candidate builds require PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$phaseRoot = Join-Path $ArtifactRoot 'c188r28-gc-state-guard-discrimination'
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $phaseRoot ('candidate-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $phaseRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R28 candidate must remain below its preferred artifact root: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite R28 candidate: $candidate" }
$null = New-Item -ItemType Directory -Path $phaseRoot -Force
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$builder = Join-Path $RepoRoot 'scripts\dotnet\build-c188r25-failfast-origin-candidate.ps1'
$builderOutput = & $pwsh -NoProfile -File $builder -RepoRoot $RepoRoot `
    -ArtifactRoot $ArtifactRoot -CandidateRoot $candidate -C188R28GcStateGuardDiscriminator 2>&1 | Out-String
$builderExit = $LASTEXITCODE
$buildLog = Join-Path $phaseRoot ('r28-build-' + [guid]::NewGuid().ToString('N') + '.log')
Set-Content -LiteralPath $buildLog -Value $builderOutput -Encoding utf8
if ($builderExit -ne 0) { throw "R25 baseline builder failed to produce R28; see $buildLog" }
$r25Match = [regex]::Match($builderOutput,'(?m)^R25_MANIFEST=(.+?)\s*$')
if (-not $r25Match.Success) { throw 'R25 builder output omitted its source and candidate manifest.' }
$r25ManifestPath = [System.IO.Path]::GetFullPath($r25Match.Groups[1].Value.Trim())
$r25Manifest = Get-Content -LiteralPath $r25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$integrity = Get-Content -LiteralPath $integrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
$runtimeManifest = Get-Content -LiteralPath ([string]$candidateManifest.runtimePackManifest) -Raw | ConvertFrom-Json -ErrorAction Stop
if (-not [bool]$runtimeManifest.c188r28GcStateGuardDiscriminator -or
    [int]$runtimeManifest.failFastOriginRecordSchema -ne 2 -or
    [int]$runtimeManifest.failFastOriginRecordBytes -ne 144 -or
    -not [bool]$runtimeManifest.c188r12r4r5GcOwnershipProof -or
    [string]$runtimeManifest.runtimeIdentity.gc -cne 'Workstation' -or
    [bool]$runtimeManifest.imageBackedArenaPresent -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
    [string]$candidateManifest.actualFactoryConstructorSymbol -cne 'ManagedSettingsCenter___ctor_0' -or
    -not [bool]$candidateManifest.unwindPrimitive.linkedByManagedProof -or
    [string]$candidateManifest.unwindPrimitive.sourceSha256 -ine '9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E' -or
    [bool]$integrity.unverifiedRootObjectInputsConsumed -or
    [int]$r25Manifest.diagnostics.recordBytes -ne 144) {
    throw 'R28 build did not preserve the real Workstation GC profile, disabled arena, clean object inputs, and 144-byte discriminator.'
}
$candidateManifestHash = (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$integrityHash = (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
$r25Hash = (Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$runtimeManifestPath = [System.IO.Path]::GetFullPath([string]$candidateManifest.runtimePackManifest)
$r28ManifestPath = Join-Path $phaseRoot ((Split-Path -Leaf $candidate) + '-r28-candidate-manifest.json')
$r28Manifest = [ordered]@{
    schema='GUIDEXOS_C188R28_GC_STATE_GUARD_CANDIDATE_V1'
    result='PASS_HOSTED_READY_FOR_QAC_ADMISSION'
    createdUtc=[DateTime]::UtcNow.ToString('O')
    sourceHead=[string]$r25Manifest.sourceHead
    branch=[string]$r25Manifest.branch
    repoRoot=$RepoRoot
    candidateRoot=$candidate
    candidateManifestPath=$candidateManifestPath
    candidateManifestSha256=$candidateManifestHash
    candidateIntegrityPath=$integrityPath
    candidateIntegritySha256=$integrityHash
    candidateFileCount=@($integrity.files).Count
    r25ManifestPath=$r25ManifestPath
    r25ManifestSha256=$r25Hash
    runtimePackManifestPath=$runtimeManifestPath
    runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    linkedQualificationPath=[string]$r25Manifest.linkedQualificationPath
    linkedQualificationSha256=(Get-FileHash -LiteralPath ([string]$r25Manifest.linkedQualificationPath) -Algorithm SHA256).Hash.ToUpperInvariant()
    hostedQualificationPath=[string]$r25Manifest.hostedQualificationPath
    hostedQualificationSha256=(Get-FileHash -LiteralPath ([string]$r25Manifest.hostedQualificationPath) -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimeProfile=[string]$runtimeManifest.identity
    gcProvider=[string]$runtimeManifest.runtimeIdentity.gc
    c188ArenaPresent=[bool]$runtimeManifest.imageBackedArenaPresent
    selector5Factory=[ordered]@{
        name='ManagedSettingsCenter'
        selector=5
        candidateManifestSha256=$candidateManifestHash
        provenance='inherited from the R21-R27 real Settings startup workload; see the pinned candidate manifest and R21 integration report'
    }
    firstFailureRecord=[ordered]@{
        address=[string]$r25Manifest.diagnostics.recordAddress
        schema=2
        bytes=144
        wordCount=18
        marker=[string]$r25Manifest.diagnostics.marker
        guardIdentifiers=@('GC_STATE_READ_FAILED','GC_THREAD_IDENTITY_MISMATCH','GC_HEAP_IDENTITY_MISMATCH')
    }
    oneBoot=[ordered]@{maxGuestBoots=1;automaticRetry=$false;retryAllowed=$false;guardCreated=$false}
    buildLog=$buildLog
    buildLogSha256=(Get-FileHash -LiteralPath $buildLog -Algorithm SHA256).Hash.ToUpperInvariant()
}
$r28Manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $r28ManifestPath -Encoding utf8
Write-Output "R28_CANDIDATE=$candidate"
Write-Output "R28_MANIFEST=$r28ManifestPath"
Write-Output "R28_MANIFEST_SHA256=$((Get-FileHash -LiteralPath $r28ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant())"
Write-Output "R28_HOSTED_QUALIFICATION=$([regex]::Match($builderOutput,'R28_GC_STATE_GUARD_TESTS=\d+ RESULT=PASS').Value)"
