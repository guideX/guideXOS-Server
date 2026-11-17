param(
    [Parameter(Mandatory=$true)][string]$R30ManifestPath,
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R33 candidate packaging requires PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$R30ManifestPath = [System.IO.Path]::GetFullPath($R30ManifestPath)
$phaseRoot = Join-Path $ArtifactRoot 'c188r33-settings-handoff'
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $phaseRoot ('candidate-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$candidatePrefix = $phaseRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($candidatePrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R33 candidate must remain below its phase artifact root: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite R33 candidate: $candidate" }

$r30 = Get-Content -LiteralPath $R30ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($r30.schema -cne 'GUIDEXOS_C188R30_THREAD_ATTACHMENT_CANDIDATE_V1' -or
    $r30.result -cne 'PASS_HOSTED_READY_FOR_QAC_ADMISSION' -or
    [int]$r30.oneBoot.maxGuestBoots -ne 1 -or
    [bool]$r30.oneBoot.guardCreated -or [bool]$r30.oneBoot.automaticRetry -or
    [bool]$r30.oneBoot.retryAllowed) {
    throw 'R33 source package must be an intact, hosted-qualified R30 candidate with no prior R33 guard.'
}
$origin = [System.IO.Path]::GetFullPath([string]$r30.candidateRoot)
$originManifestPath = Join-Path $origin 'candidate-manifest.json'
$originIntegrityPath = Join-Path $origin 'candidate-integrity.json'
$originR25ManifestPath = [System.IO.Path]::GetFullPath([string]$r30.r25ManifestPath)
$originRuntimeManifestPath = [System.IO.Path]::GetFullPath([string]$r30.runtimePackManifestPath)
foreach ($path in @($originManifestPath,$originIntegrityPath,$originR25ManifestPath,
        $originRuntimeManifestPath,[string]$r30.linkedQualificationPath,
        [string]$r30.hostedQualificationPath,[string]$r30.buildLog)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R32 source package input is missing: $path" }
}
foreach ($pair in @(
    @($originManifestPath,[string]$r30.candidateManifestSha256),
    @($originIntegrityPath,[string]$r30.candidateIntegritySha256),
    @($originR25ManifestPath,[string]$r30.r25ManifestSha256),
    @($originRuntimeManifestPath,[string]$r30.runtimePackManifestSha256),
    @([string]$r30.linkedQualificationPath,[string]$r30.linkedQualificationSha256),
    @([string]$r30.hostedQualificationPath,[string]$r30.hostedQualificationSha256),
    @([string]$r30.buildLog,[string]$r30.buildLogSha256))) {
    if ((Get-FileHash -LiteralPath $pair[0] -Algorithm SHA256).Hash -ine $pair[1]) {
        throw "R32 source package hash mismatch: $($pair[0])"
    }
}
$originManifest = Get-Content -LiteralPath $originManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$originIntegrity = Get-Content -LiteralPath $originIntegrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
$originR25Manifest = Get-Content -LiteralPath $originR25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$runtimeManifest = Get-Content -LiteralPath $originRuntimeManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$originManifest.sourceHead -cne [string]$r30.sourceHead -or
    [string]$originManifest.actualManagedSettingsFactory -cne
        'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
    [string]$originManifest.actualFactoryConstructorSymbol -cne 'ManagedSettingsCenter___ctor_0' -or
    [string]$originR25Manifest.profile -cne
        'guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1' -or
    [string]$runtimeManifest.identity -cne
        'guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1' -or
    -not [bool]$runtimeManifest.c188r30ThreadAttachmentTrace -or
    [string]$runtimeManifest.c188r30ThreadAttachmentTraceMarker -cne
        'GUIDEXOS_NATIVEAOT_C188R30_THREAD_ATTACHMENT_TRACE' -or
    -not [bool]$originManifest.unwindPrimitive.linkedByManagedProof -or
    [string]$originManifest.unwindPrimitive.sourceSha256 -ine
        '9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E' -or
    [bool]$originIntegrity.unverifiedRootObjectInputsConsumed) {
    throw 'R32 candidate does not bind the accepted R30 trace, R20 unwind repair, or real selector-5 factory.'
}

foreach ($name in @(
    'c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj',
    'c188r12r4r5r4r5r2r2_native_alloc_record_test.obj',
    'guidexos_nativeaot_native_alloc_record.obj')) {
    $protectedPath = Join-Path $RepoRoot $name
    if (-not $originIntegrity.preservedModifiedObjects.PSObject.Properties[$name] -or
        (Get-FileHash -LiteralPath $protectedPath -Algorithm SHA256).Hash -ine
            [string]$originIntegrity.preservedModifiedObjects.$name) {
        throw "Protected object hash changed before R33 candidate packaging: $name"
    }
}

$null = New-Item -ItemType Directory -Path (Split-Path -Parent $candidate) -Force
Copy-Item -LiteralPath $origin -Destination $candidate -Recurse
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$candidateIntegrityPath = Join-Path $candidate 'candidate-integrity.json'
$integrity = Get-Content -LiteralPath $candidateIntegrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
$expectedPaths = @($integrity.files | ForEach-Object { [string]$_.path })
$actualPaths = @(Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {
    $_.FullName -notin @($candidateManifestPath,$candidateIntegrityPath)
} | ForEach-Object { [System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/') })
if ($expectedPaths.Count -ne $actualPaths.Count -or
    @($actualPaths | Where-Object { $_ -cnotin $expectedPaths }).Count -gt 0 -or
    @($expectedPaths | Where-Object { $_ -cnotin $actualPaths }).Count -gt 0) {
    throw 'Fresh R33 copy differs from the complete R32 candidate inventory.'
}
foreach ($entry in @($integrity.files)) {
    $copiedFile = Join-Path $candidate ([string]$entry.path.Replace('/','\'))
    if (-not (Test-Path -LiteralPath $copiedFile -PathType Leaf) -or
        (Get-FileHash -LiteralPath $copiedFile -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
        throw "Fresh R33 candidate integrity mismatch: $($entry.path)"
    }
}

$candidateR25Manifest = $originR25Manifest | ConvertTo-Json -Depth 24 | ConvertFrom-Json -ErrorAction Stop
$candidateR25Manifest.candidateRoot = $candidate
$candidateR25Manifest.candidateManifestPath = $candidateManifestPath
$candidateR25Manifest.candidateIntegrityPath = $candidateIntegrityPath
$candidateR25ManifestPath = Join-Path (Split-Path -Parent $candidate) ((Split-Path -Leaf $candidate) + '-r25-candidate-manifest.json')
$candidateR25Manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $candidateR25ManifestPath -Encoding utf8

$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$analyzer = Join-Path $RepoRoot 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$analyzerOutput = & $python $analyzer '--candidate-root' $candidate 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Fresh R33 R25 analyzer packaging failed: $analyzerOutput" }
$analyzerObject = $analyzerOutput | ConvertFrom-Json -ErrorAction Stop
$analyzerPath = Join-Path (Split-Path -Parent $candidate) ((Split-Path -Leaf $candidate) + '-r25-analyzer.json')
Set-Content -LiteralPath $analyzerPath -Value ($analyzerObject | ConvertTo-Json -Depth 18) -Encoding utf8

$r30Test = Join-Path $RepoRoot 'scripts\dotnet\test-c188r30-thread-attachment.py'
$r30Output = & $python $r30Test '--candidate-root' $candidate 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $r30Output -notmatch 'R30_THREAD_ATTACHMENT_TESTS=\d+ RESULT=PASS') {
    throw "Copied R33 linked R30 qualification failed: $r30Output"
}
$r30QualificationPath = Join-Path (Split-Path -Parent $candidate) ((Split-Path -Leaf $candidate) + '-r30-linked-qualification.log')
Set-Content -LiteralPath $r30QualificationPath -Value $r30Output -Encoding utf8

$sourcePaths = @(
    'scripts/dotnet/build-c188r33-settings-handoff-candidate.ps1',
    'scripts/dotnet/run-c188-qac-guest.ps1',
    'scripts/dotnet/run-c188r12r1-settings-handoff.ps1',
    'scripts/dotnet/c188r11-runner-predicates.ps1',
    'scripts/dotnet/c188r11r4r2-runner-predicates.ps1',
    'scripts/dotnet/c188r33-settings-handoff-predicates.ps1',
    'scripts/dotnet/test-c188r11-runner-predicates.ps1',
    'scripts/dotnet/test-c188r11r2-runner-predicates.ps1',
    'scripts/dotnet/test-c188r33-settings-handoff-predicates.ps1',
    'scripts/dotnet/fixtures/c188r33/r32-post-initialization.serial.txt',
    'scripts/dotnet/run-c188r24-initializemodules.ps1',
    'scripts/dotnet/run-c188r25-failfast-origin.ps1',
    'scripts/dotnet/c188r11qac-ownership-predicates.ps1',
    'scripts/dotnet/c188r12r1-settings-handoff-predicates.ps1',
    'scripts/dotnet/c188r12r2r1-settings-dispatch-predicates.ps1',
    'scripts/dotnet/c188r12r3-lifetime-predicates.ps1',
    'scripts/dotnet/c188r12r4-factory-predicates.ps1',
    'scripts/dotnet/c188r22-bootstrap-handoff-predicates.ps1',
    'scripts/dotnet/analyze-c188r25-failfast-origin.py',
    'scripts/dotnet/test-c188r30-thread-attachment.py')
$sourceInputs = @($sourcePaths | ForEach-Object {
    $sourcePath = Join-Path $RepoRoot $_
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "R33 source input is missing: $_" }
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToUpperInvariant()}
})
$branch = (& git -C $RepoRoot branch --show-current).Trim()
$sourceHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($branch -cne 'v1.1_DOTNET_SUPPORT') { throw "R33 runner source must be on v1.1_DOTNET_SUPPORT, found $branch." }

$candidateRootManifestSha256 = (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$candidateIntegritySha256 = (Get-FileHash -LiteralPath $candidateIntegrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
$kernelPath = Join-Path $candidate 'esp\kernel.elf'
$ramdiskPath = Join-Path $candidate 'esp\ramdisk.img'
$managedElfPath = [string]$originManifest.managedElf
$managedMapPath = [string]$originManifest.managedMap
$manifestPath = Join-Path (Split-Path -Parent $candidate) ((Split-Path -Leaf $candidate) + '-r33-candidate-manifest.json')
$r33Manifest = [ordered]@{
    schema='GUIDEXOS_C188R33_SETTINGS_HANDOFF_CANDIDATE_V1'
    result='PASS_HOSTED_READY_FOR_QAC_PREFLIGHT'
    createdUtc=[DateTime]::UtcNow.ToString('O')
    repoRoot=$RepoRoot;runnerSourceHead=$sourceHead;runnerSourceBranch=$branch
    runnerSourceInputs=$sourceInputs
    candidateRoot=$candidate;candidatePackageMode='VERBATIM_R32_CANDIDATE_COPY_NO_RUNTIME_REBUILD'
    candidateSourceHead=[string]$originManifest.sourceHead
    originR30ManifestPath=$R30ManifestPath
    originR30ManifestSha256=(Get-FileHash -LiteralPath $R30ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    originR32CandidateRoot=$origin
    originCandidateManifestSha256=[string]$r30.candidateManifestSha256
    originCandidateIntegritySha256=[string]$r30.candidateIntegritySha256
    candidateManifestPath=$candidateManifestPath;candidateManifestSha256=$candidateRootManifestSha256
    candidateIntegrityPath=$candidateIntegrityPath;candidateIntegritySha256=$candidateIntegritySha256
    candidateFilesVerified=@($integrity.files).Count
    candidateManifestSourceHead=[string]$originManifest.sourceHead
    candidateBuildProfile=[string]$originManifest.buildProfile
    kernelSha256=(Get-FileHash -LiteralPath $kernelPath -Algorithm SHA256).Hash.ToUpperInvariant()
    ramdiskSha256=(Get-FileHash -LiteralPath $ramdiskPath -Algorithm SHA256).Hash.ToUpperInvariant()
    managedElfSha256=(Get-FileHash -LiteralPath $managedElfPath -Algorithm SHA256).Hash.ToUpperInvariant()
    managedMapSha256=(Get-FileHash -LiteralPath $managedMapPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimePackManifestPath=$originRuntimeManifestPath
    runtimePackManifestSha256=(Get-FileHash -LiteralPath $originRuntimeManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimeProfile=[string]$runtimeManifest.identity
    selector5SettingsFactory=[string]$originManifest.actualManagedSettingsFactory
    selector5FactoryConstructorSymbol=[string]$originManifest.actualFactoryConstructorSymbol
    readinessProfile='C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS'
    expectedManagedDescriptorCount=2
    r25CandidateManifestPath=$candidateR25ManifestPath
    r25CandidateManifestSha256=(Get-FileHash -LiteralPath $candidateR25ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    analyzerPath=$analyzerPath;analyzerSha256=(Get-FileHash -LiteralPath $analyzerPath -Algorithm SHA256).Hash.ToUpperInvariant()
    r30LinkedQualificationPath=$r30QualificationPath
    r30LinkedQualificationSha256=(Get-FileHash -LiteralPath $r30QualificationPath -Algorithm SHA256).Hash.ToUpperInvariant()
    preservedModifiedObjects=$originIntegrity.preservedModifiedObjects
    oneBoot=[ordered]@{maxGuestBoots=1;automaticRetry=$false;retryAllowed=$false;guardCreated=$false}
}
$r33Manifest | ConvertTo-Json -Depth 22 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "R33_CANDIDATE=$candidate"
Write-Output "R33_MANIFEST=$manifestPath"
Write-Output "R33_MANIFEST_SHA256=$((Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant())"
Write-Output "R33_R25_MANIFEST=$candidateR25ManifestPath"
Write-Output $r30Output.Trim()
