param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [string]$CandidateRoot = '',
    [switch]$C188R28GcStateGuardDiscriminator,
    [switch]$C188R29PreallocationGcStateGuard
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R25 candidate builds require PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $ArtifactRoot ('c188r25-failfast-origin\candidate-' +
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N'))
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R25 candidate must remain below ArtifactRoot: $candidate"
}
if (Test-Path -LiteralPath $candidate) { throw "Refusing to overwrite R25 candidate: $candidate" }
$phaseRoot = [System.IO.Path]::GetDirectoryName($candidate)
$null = New-Item -ItemType Directory -Path $phaseRoot -Force
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$r24Builder = Join-Path $RepoRoot 'scripts\dotnet\build-c188r24-initializemodules-candidate.ps1'
$builderOutput = & $pwsh -NoProfile -File $r24Builder -RepoRoot $RepoRoot `
    -ArtifactRoot $ArtifactRoot -CandidateRoot $candidate -C188R25FailFastOriginCapture `
    -C188R28GcStateGuardDiscriminator:$C188R28GcStateGuardDiscriminator `
    -C188R29PreallocationGcStateGuard:$C188R29PreallocationGcStateGuard 2>&1 | Out-String
$builderExit = $LASTEXITCODE
$r25BuildLog = Join-Path $phaseRoot ('r25-build-' + [guid]::NewGuid().ToString('N') + '.log')
Set-Content -LiteralPath $r25BuildLog -Value $builderOutput -Encoding utf8
if ($builderExit -ne 0) { throw "R24 baseline builder failed while compiling the R25 profile; see $r25BuildLog" }

$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$candidateIntegrityPath = Join-Path $candidate 'candidate-integrity.json'
foreach ($path in @($candidateManifestPath,$candidateIntegrityPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R25 build output is missing: $path" }
}
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json
$candidateIntegrity = Get-Content -LiteralPath $candidateIntegrityPath -Raw | ConvertFrom-Json
$runtimeManifestPath = [string]$candidateManifest.runtimePackManifest
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
if (-not [bool]$runtimeManifest.c188r25FailFastOriginCapture -or
    [string]$runtimeManifest.c188r25FailFastOriginCaptureMarker -cne 'GUIDEXOS_NATIVEAOT_C188R25_FAILFAST_ORIGIN') {
    throw 'The linked runtime-pack manifest does not identify R25 fail-fast capture.'
}
if ($C188R28GcStateGuardDiscriminator -and
    (-not [bool]$runtimeManifest.c188r28GcStateGuardDiscriminator -or
     [string]$runtimeManifest.c188r28GcStateGuardDiscriminatorMarker -cne 'GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR')) {
    throw 'The linked runtime-pack manifest does not identify the R28 GC-state guard discriminator.'
}
if ($C188R29PreallocationGcStateGuard -and
    (-not [bool]$runtimeManifest.c188r29PreallocationGcStateGuard -or
     [string]$runtimeManifest.c188r29PreallocationGcStateGuardMarker -cne 'GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD' -or
     [int]$runtimeManifest.failFastOriginRecordSchema -ne 3 -or
     [int]$runtimeManifest.failFastOriginRecordBytes -ne 192)) {
    throw 'The linked runtime-pack manifest does not identify the R29 schema-3 pre-allocation discriminator.'
}
$analyzer = Join-Path $RepoRoot 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$qualification = (& $python $analyzer '--candidate-root' $candidate 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "R25 linked-image qualification failed: $qualification" }
$candidateToken = [System.IO.Path]::GetFileName($candidate)
$qualPath = Join-Path $phaseRoot ($candidateToken + '-linked-qualification.json')
Set-Content -LiteralPath $qualPath -Value $qualification -Encoding utf8
$qualificationObject = $qualification | ConvertFrom-Json
$testPath = Join-Path $RepoRoot 'scripts\dotnet\test-c188r25-failfast-origin.ps1'
$testOutput = & $pwsh -NoProfile -File $testPath 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "R25 hosted qualification failed: $testOutput" }
$pythonTestPath = Join-Path $RepoRoot 'scripts\dotnet\test-c188r25-failfast-origin.py'
$pythonTestOutput = & $python $pythonTestPath 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "R25 Python qualification failed: $pythonTestOutput" }
$r28TestOutput = ''
if ($C188R28GcStateGuardDiscriminator) {
    $r28TestPath = Join-Path $RepoRoot 'scripts\dotnet\test-c188r28-gc-state-guard.py'
    $r28TestOutput = & $python $r28TestPath 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "R28 GC-state guard qualification failed: $r28TestOutput" }
}
$r29TestOutput = ''
if ($C188R29PreallocationGcStateGuard) {
    $r29TestPath = Join-Path $RepoRoot 'scripts\dotnet\test-c188r29-preallocation-gc-state-guard.py'
    $r29TestOutput = & $python $r29TestPath 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "R29 pre-allocation GC-state guard qualification failed: $r29TestOutput" }
}
$hostedQualificationPath = Join-Path $phaseRoot ($candidateToken + '-hosted-qualification.log')
Set-Content -LiteralPath $hostedQualificationPath -Value ($testOutput + "`n" + $pythonTestOutput + "`n" + $r28TestOutput + "`n" + $r29TestOutput) -Encoding utf8
$recordAddress = [string]$qualificationObject.candidate.recordAddress
$sourcePaths = @(
    'scripts/dotnet/build-c188r25-failfast-origin-candidate.ps1',
    'scripts/dotnet/build-c188r28-gc-state-guard-candidate.ps1',
    'scripts/dotnet/build-c188r29-preallocation-gc-state-guard-candidate.ps1',
    'scripts/dotnet/build-c188r24-initializemodules-candidate.ps1',
    'scripts/dotnet/build-c188r23-settings-candidate.ps1',
    'scripts/dotnet/build-managed-hostlog-proof.ps1',
    'tools/dotnet/runtime-pack/build-runtime-pack.ps1',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_failfast_origin.h',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_gc_state_guard.h',
    'tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_r29_preallocation_guard.h',
    'tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_gc_allocation_probe.cpp',
    'scripts/dotnet/run-c188-qac-guest.ps1',
    'scripts/dotnet/run-c188r24-initializemodules.ps1',
    'scripts/dotnet/run-c188r12r1-settings-handoff.ps1',
    'scripts/dotnet/run-c188r25-failfast-origin.ps1',
    'scripts/dotnet/c188r26-precreated-evidence-root.ps1',
    'scripts/dotnet/c188r25-failfast-runner-predicates.ps1',
    'scripts/dotnet/analyze-c188r25-failfast-origin.py',
    'scripts/dotnet/test-c188r25-failfast-origin.ps1',
    'scripts/dotnet/test-c188r25-failfast-origin.py',
    'scripts/dotnet/test-c188r26-precreated-evidence-root.ps1',
    'scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1',
    'scripts/dotnet/test-c188r28-gc-state-guard.py',
    'scripts/dotnet/test-c188r29-preallocation-gc-state-guard.py',
    'scripts/dotnet/run-c188r28-gc-state-guard.ps1',
    'scripts/dotnet/run-c188r29-preallocation-gc-state-guard.ps1')
$sourceInputs = @($sourcePaths | ForEach-Object {
    $path = Join-Path $RepoRoot $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R25 source input is missing: $_" }
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()}
})
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
$branch = (& git -C $RepoRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or $branch -ne 'v1.1_DOTNET_SUPPORT') { throw "R25 source branch mismatch: $branch" }
$r25ManifestPath = Join-Path $phaseRoot ($candidateToken + '-r25-candidate-manifest.json')
$r25Manifest = [ordered]@{
    schema='GUIDEXOS_C188R25_CANDIDATE_V1';phase='C188R25_FAILFAST_ORIGIN_ATTRIBUTION'
    result='PASS_BUILD_READY_FOR_QAC_PREFLIGHT';sourceHead=$head;branch=$branch
    candidateRoot=$candidate;candidateManifestPath=$candidateManifestPath
    candidateManifestSha256=(Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    candidateIntegrityPath=$candidateIntegrityPath
    candidateIntegritySha256=(Get-FileHash -LiteralPath $candidateIntegrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
    candidateFileCount=@($candidateIntegrity.files).Count
    managedElf=[string]$candidateManifest.managedElf;managedElfSha256=[string]$candidateManifest.managedElfSha256
    managedMap=[string]$candidateManifest.managedMap;managedPe=[string]$candidateManifest.managedPe
    kernelSha256=[string]$candidateManifest.kernelSha256
    runtimePackManifest=$runtimeManifestPath
    runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    runtimeSourceCommit=[string]$runtimeManifest.runtimeIdentity.sourceCommit
    profile=[string]$runtimeManifest.identity
    diagnostics=[ordered]@{recordAddress=$recordAddress;recordBytes=[int]$runtimeManifest.failFastOriginRecordBytes;schemaVersion=[int]$runtimeManifest.failFastOriginRecordSchema;fastFailCode=7;marker='[C188R25-FAILFAST-CAPTURED]';firstFailureOnly=$true;threadAndTransitionStateCaptured=$false;gcStateGuardDiscriminator=[bool]$C188R28GcStateGuardDiscriminator;preallocationGcStateGuardDiscriminator=[bool]$C188R29PreallocationGcStateGuard}
    r24Baseline=[ordered]@{phase=[string]$candidateManifest.phase;candidateSourceHead=[string]$candidateManifest.sourceHead;preservedModifiedObjects=$candidateIntegrity.preservedModifiedObjects}
    linkedQualificationPath=$qualPath;hostedQualificationPath=$hostedQualificationPath;sourceInputs=$sourceInputs
    buildLog=$r25BuildLog
}
$r25Manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $r25ManifestPath -Encoding utf8

Write-Output "R25_CANDIDATE=$candidate"
Write-Output "R25_MANIFEST=$r25ManifestPath"
Write-Output "R25_RECORD_ADDRESS=$recordAddress"
Write-Output ($qualificationObject | ConvertTo-Json -Compress -Depth 5)
Write-Output ($testOutput.Trim())
Write-Output ($pythonTestOutput.Trim())
if ($r28TestOutput) { Write-Output ($r28TestOutput.Trim()) }
if ($r29TestOutput) { Write-Output ($r29TestOutput.Trim()) }
