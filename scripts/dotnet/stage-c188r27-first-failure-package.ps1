param(
    [Parameter(Mandatory=$true)][string]$SourceCandidateRoot,
    [Parameter(Mandatory=$true)][string]$SourceR25ManifestPath,
    [Parameter(Mandatory=$true)][string]$ConsumedGuardPath,
    [Parameter(Mandatory=$true)][string]$R26RunEvidencePath,
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R27 package staging requires PowerShell Core.' }

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$source = [System.IO.Path]::GetFullPath($SourceCandidateRoot)
$sourceManifestPath = Join-Path $source 'candidate-manifest.json'
$sourceIntegrityPath = Join-Path $source 'candidate-integrity.json'
$parentR25ManifestPath = [System.IO.Path]::GetFullPath($SourceR25ManifestPath)
$consumedGuardPath = [System.IO.Path]::GetFullPath($ConsumedGuardPath)
$r26EvidencePath = [System.IO.Path]::GetFullPath($R26RunEvidencePath)
$phaseRoot = [System.IO.Path]::GetDirectoryName($source)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $source.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
    -not $phaseRoot.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'R27 source candidate and package must remain below ArtifactRoot.'
}

function Read-JsonHashtable([string]$Path) {
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable -Depth 100 -ErrorAction Stop
}

function Write-JsonHashtable([string]$Path, $Value) {
    $json = ConvertTo-Json -InputObject $Value -Depth 100
    [System.IO.File]::WriteAllText($Path,$json,[System.Text.UTF8Encoding]::new($false))
}

function Get-FileSet([string]$Root, [string[]]$Exclude) {
    return @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Where-Object {
        $_.FullName -notin $Exclude
    } | ForEach-Object {
        [System.IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/')
    } | Sort-Object)
}

function Assert-Inventory([string]$Root, $Integrity) {
    $manifestPath = Join-Path $Root 'candidate-manifest.json'
    $integrityPath = Join-Path $Root 'candidate-integrity.json'
    $expected = @($Integrity.files | ForEach-Object { [string]$_.path } | Sort-Object)
    $actual = Get-FileSet $Root @($manifestPath,$integrityPath)
    if ($expected.Count -ne $actual.Count -or
        @($expected | Where-Object { $_ -cnotin $actual }).Count -gt 0 -or
        @($actual | Where-Object { $_ -cnotin $expected }).Count -gt 0) {
        throw "Candidate inventory path set differs from the package contents: $Root"
    }
    foreach ($entry in @($Integrity.files)) {
        $file = Join-Path $Root ([string]$entry.path.Replace('/','\'))
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
            throw "Candidate inventory hash mismatch: $($entry.path)"
        }
    }
}

function Replace-CandidatePath($Value, [string]$OldRoot, [string]$NewRoot) {
    if ($null -eq $Value) { return $null }
    if ($Value -is [string]) {
        return $Value.Replace($OldRoot,$NewRoot).Replace($OldRoot.Replace('\','/'),$NewRoot.Replace('\','/'))
    }
    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($key in @($Value.Keys)) { $Value[$key] = Replace-CandidatePath $Value[$key] $OldRoot $NewRoot }
        return $Value
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        $items = [System.Collections.Generic.List[object]]::new()
        foreach ($item in $Value) { $items.Add((Replace-CandidatePath $item $OldRoot $NewRoot)) }
        return ,@($items.ToArray())
    }
    return $Value
}

foreach ($required in @($sourceManifestPath,$sourceIntegrityPath,$parentR25ManifestPath,$consumedGuardPath,$r26EvidencePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "R27 staging input is missing: $required" }
}
$parentR25 = Read-JsonHashtable $parentR25ManifestPath
$sourceManifest = Read-JsonHashtable $sourceManifestPath
$sourceIntegrity = Read-JsonHashtable $sourceIntegrityPath
$guard = Read-JsonHashtable $consumedGuardPath
$r26Evidence = Read-JsonHashtable $r26EvidencePath
$sourceManifestHash = (Get-FileHash -LiteralPath $sourceManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$sourceIntegrityHash = (Get-FileHash -LiteralPath $sourceIntegrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
$parentR25Hash = (Get-FileHash -LiteralPath $parentR25ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()

if ($parentR25.schema -cne 'GUIDEXOS_C188R25_CANDIDATE_V1' -or
    [System.IO.Path]::GetFullPath([string]$parentR25.candidateRoot) -cne $source -or
    [string]$parentR25.candidateManifestSha256 -ine $sourceManifestHash -or
    [string]$parentR25.candidateIntegritySha256 -ine $sourceIntegrityHash -or
    [int]$parentR25.candidateFileCount -ne 347 -or
    [string]$sourceManifest.phase -cne 'C188R24_INITIALIZEMODULES_REGISTRATION' -or
    [string]$sourceManifest.result -cne 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT' -or
    [bool]$sourceIntegrity.unverifiedRootObjectInputsConsumed) {
    throw 'Frozen R25 source candidate and external manifest do not form the expected qualified baseline.'
}
Assert-Inventory $source $sourceIntegrity
if ([System.IO.Path]::GetFullPath([string]$guard.candidateRoot) -cne $source -or
    [string]$guard.schema -cne 'GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1' -or
    [int]$guard.attemptNumber -ne 1 -or [int]$guard.maxGuestBoots -ne 1 -or
    [bool]$guard.automaticRetry -or [bool]$guard.retryAllowed -or -not [bool]$guard.consumed -or
    [string]$guard.candidateManifestSha256 -ine $sourceManifestHash -or
    [string]$guard.candidateIntegritySha256 -ine $sourceIntegrityHash) {
    throw 'R26 guard is not the preserved consumed one-boot guard for the frozen R25 candidate.'
}
if ([string]$r26Evidence.schema -cne 'GUIDEXOS_C188R25_FAILFAST_ORIGIN_RUN_V1' -or
    [string]$r26Evidence.outcome -cne 'FAILFAST_EVIDENCE_BLOCKED' -or
    [string]$r26Evidence.runIdentity -notmatch '^[A-Fa-f0-9]{32}$' -or
    [System.IO.Path]::GetFullPath([string]$r26Evidence.candidateRoot) -cne $source -or
    [System.IO.Path]::GetFullPath([string]$r26Evidence.guardPath) -cne $consumedGuardPath -or
    [string]$r26Evidence.qacPreflight -cne 'CLEAR' -or
    -not [bool]$r26Evidence.nativeExecutionAttempted -or
    -not [string]::IsNullOrWhiteSpace([string]$r26Evidence.qacReservationId)) {
    throw 'R26 run evidence does not establish the expected prelaunch-only failure semantics.'
}
$r26BootRoot = [System.IO.Path]::GetFullPath([string]$r26Evidence.bootEvidencePath)
$stderrPath = Join-Path $phaseRoot ('r25-runner-' + [string]$r26Evidence.runIdentity + '.stderr.log')
$preflightPath = [System.IO.Path]::GetFullPath([string]$guard.preflightPath)
$preflightReportPath = Join-Path $preflightPath 'qac-preflight.json'
$preflightStatusPath = Join-Path $preflightPath 'status.txt'
$preflightListPath = Join-Path $preflightPath 'list.txt'
$afterStatusPath = Join-Path $r26BootRoot 'qac-after-r24.status.txt'
$afterListPath = Join-Path $r26BootRoot 'qac-after-r24.list.txt'
foreach ($required in @($stderrPath,$preflightReportPath,$preflightStatusPath,$preflightListPath,$afterStatusPath,$afterListPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "R26 no-launch evidence is missing: $required" }
}
$preflight = Read-JsonHashtable $preflightReportPath
$preStatus = Get-Content -LiteralPath $preflightStatusPath -Raw
$preList = Get-Content -LiteralPath $preflightListPath -Raw
$afterStatus = Get-Content -LiteralPath $afterStatusPath -Raw
$afterList = Get-Content -LiteralPath $afterListPath -Raw
$nestedRejectLog = Get-Content -LiteralPath $stderrPath -Raw
if ([string]$preflight.result -cne 'CLEAR' -or
    $preStatus -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
    $preStatus -notmatch 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -or
    $preList -notmatch 'Reservations:\s+0' -or
    $afterStatus -notmatch 'QEMU processes:\s+0 \(0 foreign\)' -or
    $afterList -notmatch 'Reservations:\s+0' -or
    $nestedRejectLog -notmatch 'Guest evidence root already exists') {
    throw 'R26 logs do not prove clear preflight followed by nested-runner evidence-root rejection.'
}
foreach ($forbiddenGuestArtifact in @('active.json','serial.log','qmp.log','qmp-control.log','cpu-timeout-snapshot.json','acceptance-manifest.json')) {
    if (Test-Path -LiteralPath (Join-Path $r26BootRoot $forbiddenGuestArtifact)) {
        throw "R26 prelaunch failure unexpectedly contains guest artifact $forbiddenGuestArtifact."
    }
}

$attemptId = [guid]::NewGuid().ToString('N')
$candidateRoot = Join-Path $phaseRoot ('candidate-r27-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + $attemptId)
$candidateRoot = [System.IO.Path]::GetFullPath($candidateRoot)
if (Test-Path -LiteralPath $candidateRoot) { throw "Refusing to overwrite R27 package: $candidateRoot" }
$null = New-Item -ItemType Directory -Path $candidateRoot
foreach ($entry in @(Get-ChildItem -LiteralPath $source -Force)) {
    Copy-Item -LiteralPath $entry.FullName -Destination $candidateRoot -Recurse
}

$newIntegrityPath = Join-Path $candidateRoot 'candidate-integrity.json'
$newManifestPath = Join-Path $candidateRoot 'candidate-manifest.json'
$newIntegrity = Replace-CandidatePath (Read-JsonHashtable $newIntegrityPath) $source $candidateRoot
Write-JsonHashtable $newIntegrityPath $newIntegrity
$newIntegrityHash = (Get-FileHash -LiteralPath $newIntegrityPath -Algorithm SHA256).Hash.ToUpperInvariant()
$newManifest = Replace-CandidatePath (Read-JsonHashtable $newManifestPath) $source $candidateRoot
$newManifest.candidateIntegritySha256 = $newIntegrityHash
Write-JsonHashtable $newManifestPath $newManifest
$newManifestHash = (Get-FileHash -LiteralPath $newManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
Assert-Inventory $candidateRoot $newIntegrity

$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$analyzerPath = Join-Path $RepoRoot 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$qualificationText = & $python $analyzerPath '--candidate-root' $candidateRoot 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Copied R25 candidate failed linked qualification: $qualificationText" }
$qualification = $qualificationText | ConvertFrom-Json -ErrorAction Stop
$qualificationPath = Join-Path $phaseRoot ([System.IO.Path]::GetFileName($candidateRoot) + '-linked-qualification.json')
[System.IO.File]::WriteAllText($qualificationPath,$qualificationText,[System.Text.UTF8Encoding]::new($false))

$newR25 = [ordered]@{}
foreach ($key in $parentR25.Keys) { $newR25[$key] = $parentR25[$key] }
$newR25.candidateRoot = $candidateRoot
$newR25.candidateManifestPath = $newManifestPath
$newR25.candidateManifestSha256 = $newManifestHash
$newR25.candidateIntegrityPath = $newIntegrityPath
$newR25.candidateIntegritySha256 = $newIntegrityHash
$newR25.candidateFileCount = [int]@($newIntegrity.files).Count
$newR25.managedElf = [string]$newManifest.managedElf
$newR25.managedElfSha256 = [string]$newManifest.managedElfSha256
$newR25.managedMap = [string]$newManifest.managedMap
$newR25.managedPe = [string]$newManifest.managedPe
$newR25.kernelSha256 = [string]$newManifest.kernelSha256
$newR25.runtimePackManifest = [string]$newManifest.runtimePackManifest
$newR25.runtimePackManifestSha256 = [string]$newManifest.runtimePackManifestSha256
$newR25.linkedQualificationPath = $qualificationPath
$newR25.parentR25ManifestPath = $parentR25ManifestPath
$newR25.parentR25ManifestSha256 = $parentR25Hash
$newR25.r27AttemptId = $attemptId
$newR25.r26ConsumedGuardPath = $consumedGuardPath
$newR25.r26ConsumedGuardSha256 = (Get-FileHash -LiteralPath $consumedGuardPath -Algorithm SHA256).Hash.ToUpperInvariant()
$newR25.r26RunEvidencePath = $r26EvidencePath
$newR25.r26RunEvidenceSha256 = (Get-FileHash -LiteralPath $r26EvidencePath -Algorithm SHA256).Hash.ToUpperInvariant()
$newR25.r27RunnerPackaging = 'FROZEN_R25_FILES_COPIED_WITHOUT_REBUILD; LIVE_R27_RUNNER_HASHED_SEPARATELY'
$newR25ManifestPath = Join-Path $phaseRoot ([System.IO.Path]::GetFileName($candidateRoot) + '-r25-candidate-manifest.json')
Write-JsonHashtable $newR25ManifestPath $newR25
$newR25Hash = (Get-FileHash -LiteralPath $newR25ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()

$runnerPaths = @(
    'scripts/dotnet/stage-c188r27-first-failure-package.ps1',
    'scripts/dotnet/run-c188r27-first-failure.ps1',
    'scripts/dotnet/run-c188r25-failfast-origin.ps1',
    'scripts/dotnet/run-c188r24-initializemodules.ps1',
    'scripts/dotnet/run-c188r12r1-settings-handoff.ps1',
    'scripts/dotnet/c188r26-precreated-evidence-root.ps1',
    'scripts/dotnet/test-c188r27-prelaunch-rehearsal.ps1',
    'scripts/dotnet/analyze-c188r25-failfast-origin.py',
    'scripts/dotnet/analyze-c188r26-first-failure.py'
)
$runnerProvenance = @($runnerPaths | ForEach-Object {
    $path = Join-Path $RepoRoot $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R27 runner source is missing: $_" }
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()}
})
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
$branch = (& git -C $RepoRoot branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or $branch -cne 'v1.1_DOTNET_SUPPORT') { throw "Unexpected R27 repository branch: $branch" }
$package = [ordered]@{
    schema='GUIDEXOS_C188R27_ATTEMPT_PACKAGE_V1';result='PASS_STAGED_AWAITING_QAC_ADMISSION'
    attemptId=$attemptId;createdUtc=[DateTime]::UtcNow.ToString('O');repoRoot=$RepoRoot
    sourceHead=$head;branch=$branch;authorizationStatus='AWAITING_BOUNDED_QAC_ADMISSION'
    candidateRoot=$candidateRoot;r25ManifestPath=$newR25ManifestPath;r25ManifestSha256=$newR25Hash
    candidateManifestPath=$newManifestPath;candidateManifestSha256=$newManifestHash
    candidateIntegrityPath=$newIntegrityPath;candidateIntegritySha256=$newIntegrityHash
    candidateFileCount=[int]@($newIntegrity.files).Count
    candidateBinaries=[ordered]@{
        kernelSha256=[string]$newManifest.kernelSha256;managedElfSha256=[string]$newManifest.managedElfSha256
        runtimePackManifestSha256=[string]$newManifest.runtimePackManifestSha256
        copiedFromFrozenR25=$true;binaryRebuildPerformed=$false
    }
    lineage=[ordered]@{
        frozenR25CandidateRoot=$source;frozenR25CandidateManifestSha256=$sourceManifestHash
        frozenR25CandidateIntegritySha256=$sourceIntegrityHash;frozenR25ExternalManifestPath=$parentR25ManifestPath
        frozenR25ExternalManifestSha256=$parentR25Hash
        consumedR26AttemptIdentity=[string]$r26Evidence.runIdentity
        consumedR26GuardPath=$consumedGuardPath
        consumedR26GuardSha256=(Get-FileHash -LiteralPath $consumedGuardPath -Algorithm SHA256).Hash.ToUpperInvariant()
        consumedR26RunEvidencePath=$r26EvidencePath
        consumedR26RunEvidenceSha256=(Get-FileHash -LiteralPath $r26EvidencePath -Algorithm SHA256).Hash.ToUpperInvariant()
        consumedR26NoLaunchLogPath=$stderrPath
        consumedR26NoLaunchLogSha256=(Get-FileHash -LiteralPath $stderrPath -Algorithm SHA256).Hash.ToUpperInvariant()
        consumedR26QacPreflightReportSha256=(Get-FileHash -LiteralPath $preflightReportPath -Algorithm SHA256).Hash.ToUpperInvariant()
        consumedR26QacAfterStatusSha256=(Get-FileHash -LiteralPath $afterStatusPath -Algorithm SHA256).Hash.ToUpperInvariant()
        consumedR26QacAfterListSha256=(Get-FileHash -LiteralPath $afterListPath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    linkedQualificationPath=$qualificationPath
    linkedQualificationResult=[string]$qualification.result
    runnerProvenance=$runnerProvenance
    oneBoot=[ordered]@{attemptNumber=1;maxGuestBoots=1;automaticRetry=$false;retryAllowed=$false;guardCreated=$false}
    qacAdmission=[ordered]@{performed=$false;exclusiveAuthorization=$false;reservationId=$null}
}
$packagePath = Join-Path $phaseRoot ([System.IO.Path]::GetFileName($candidateRoot) + '-r27-attempt-package.json')
Write-JsonHashtable $packagePath $package

Assert-Inventory $source $sourceIntegrity
Write-Output "R27_CANDIDATE=$candidateRoot"
Write-Output "R27_R25_MANIFEST=$newR25ManifestPath"
Write-Output "R27_ATTEMPT_PACKAGE=$packagePath"
Write-Output "R27_ATTEMPT_ID=$attemptId"
Write-Output "R27_CANDIDATE_FILES=$(@($newIntegrity.files).Count)"
Write-Output "R27_LINKED_QUALIFICATION=$($qualification.result)"
