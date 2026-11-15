param(
    [Parameter(Mandatory=$true)][string]$AttemptPackagePath,
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [ValidateRange(60,300)][int]$MaxRuntimeSeconds = 120,
    [switch]$PreflightOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R27 runner requires PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$AttemptPackagePath = [System.IO.Path]::GetFullPath($AttemptPackagePath)

function Read-JsonHashtable([string]$Path) {
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable -Depth 100 -ErrorAction Stop
}

function Assert-Inventory([string]$Root, $Integrity) {
    $manifestPath = Join-Path $Root 'candidate-manifest.json'
    $integrityPath = Join-Path $Root 'candidate-integrity.json'
    $expected = @($Integrity.files | ForEach-Object { [string]$_.path } | Sort-Object)
    $actual = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Where-Object {
        $_.FullName -notin @($manifestPath,$integrityPath)
    } | ForEach-Object { [System.IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/') } | Sort-Object)
    if ($expected.Count -ne $actual.Count -or
        @($expected | Where-Object { $_ -cnotin $actual }).Count -gt 0 -or
        @($actual | Where-Object { $_ -cnotin $expected }).Count -gt 0) {
        throw 'R27 attempt package file set differs from its candidate integrity inventory.'
    }
    foreach ($entry in @($Integrity.files)) {
        $file = Join-Path $Root ([string]$entry.path.Replace('/','\'))
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
            throw "R27 candidate inventory hash mismatch: $($entry.path)"
        }
    }
}

$package = Read-JsonHashtable $AttemptPackagePath
if ([string]$package.schema -cne 'GUIDEXOS_C188R27_ATTEMPT_PACKAGE_V1' -or
    [string]$package.result -cne 'PASS_STAGED_AWAITING_QAC_ADMISSION' -or
    [string]$package.authorizationStatus -cne 'AWAITING_BOUNDED_QAC_ADMISSION' -or
    [int]$package.oneBoot.maxGuestBoots -ne 1 -or [bool]$package.oneBoot.automaticRetry -or
    [bool]$package.oneBoot.retryAllowed -or [bool]$package.oneBoot.guardCreated) {
    throw 'R27 attempt package is not a fresh staged package awaiting its single QAC admission check.'
}
$candidate = [System.IO.Path]::GetFullPath([string]$package.candidateRoot)
$r25ManifestPath = [System.IO.Path]::GetFullPath([string]$package.r25ManifestPath)
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$manifest = Read-JsonHashtable $manifestPath
$integrity = Read-JsonHashtable $integrityPath
$r25Manifest = Read-JsonHashtable $r25ManifestPath
if ([string]$manifest.phase -cne 'C188R24_INITIALIZEMODULES_REGISTRATION' -or
    [string]$manifest.result -cne 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT' -or
    (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -ine [string]$package.candidateManifestSha256 -or
    (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash -ine [string]$package.candidateIntegritySha256 -or
    [System.IO.Path]::GetFullPath([string]$r25Manifest.candidateRoot) -cne $candidate -or
    (Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash -ine [string]$package.r25ManifestSha256 -or
    [string]$r25Manifest.candidateManifestSha256 -ine [string]$package.candidateManifestSha256 -or
    [string]$r25Manifest.candidateIntegritySha256 -ine [string]$package.candidateIntegritySha256 -or
    @($integrity.files).Count -ne 347) {
    throw 'R27 package, R25 manifest, candidate manifest, or integrity inventory binding is invalid.'
}
Assert-Inventory $candidate $integrity

$parentCandidate = [System.IO.Path]::GetFullPath([string]$package.lineage.frozenR25CandidateRoot)
$parentManifest = Read-JsonHashtable ([string]$package.lineage.frozenR25ExternalManifestPath)
$parentManifestPath = [System.IO.Path]::GetFullPath([string]$package.lineage.frozenR25ExternalManifestPath)
$parentCandidateManifest = Join-Path $parentCandidate 'candidate-manifest.json'
$parentCandidateIntegrity = Join-Path $parentCandidate 'candidate-integrity.json'
if ((Get-FileHash -LiteralPath $parentManifestPath -Algorithm SHA256).Hash -ine [string]$package.lineage.frozenR25ExternalManifestSha256 -or
    [System.IO.Path]::GetFullPath([string]$parentManifest.candidateRoot) -cne $parentCandidate -or
    [string]$parentManifest.schema -cne 'GUIDEXOS_C188R25_CANDIDATE_V1' -or
    (Get-FileHash -LiteralPath $parentCandidateManifest -Algorithm SHA256).Hash -ine [string]$package.lineage.frozenR25CandidateManifestSha256 -or
    (Get-FileHash -LiteralPath $parentCandidateIntegrity -Algorithm SHA256).Hash -ine [string]$package.lineage.frozenR25CandidateIntegritySha256) {
    throw 'R27 frozen R25 parent hashes no longer match the staged package lineage.'
}
$parentInventory = Read-JsonHashtable $parentCandidateIntegrity
if (@($parentInventory.files).Count -ne @($integrity.files).Count) { throw 'R27 package differs from its R25 parent inventory count.' }
for ($index=0; $index -lt @($integrity.files).Count; $index++) {
    if ([string]$integrity.files[$index].path -cne [string]$parentInventory.files[$index].path -or
        [string]$integrity.files[$index].sha256 -ine [string]$parentInventory.files[$index].sha256) {
        throw "R27 package inventory differs from the frozen R25 parent at entry $index."
    }
}
if ([string]$manifest.kernelSha256 -ine [string]$package.candidateBinaries.kernelSha256 -or
    [string]$manifest.managedElfSha256 -ine [string]$package.candidateBinaries.managedElfSha256 -or
    [string]$manifest.runtimePackManifestSha256 -ine [string]$package.candidateBinaries.runtimePackManifestSha256 -or
    [bool]$package.candidateBinaries.binaryRebuildPerformed) {
    throw 'R27 package binary provenance is inconsistent.'
}
$r26GuardPath = [System.IO.Path]::GetFullPath([string]$package.lineage.consumedR26GuardPath)
$r26RunEvidencePath = [System.IO.Path]::GetFullPath([string]$package.lineage.consumedR26RunEvidencePath)
if ((Get-FileHash -LiteralPath $r26GuardPath -Algorithm SHA256).Hash -ine [string]$package.lineage.consumedR26GuardSha256 -or
    (Get-FileHash -LiteralPath $r26RunEvidencePath -Algorithm SHA256).Hash -ine [string]$package.lineage.consumedR26RunEvidenceSha256) {
    throw 'R27 parent attempt evidence changed after package staging.'
}
$r26Guard = Read-JsonHashtable $r26GuardPath
if (-not [bool]$r26Guard.consumed -or [bool]$r26Guard.retryAllowed -or
    [System.IO.Path]::GetFullPath([string]$r26Guard.candidateRoot) -cne $parentCandidate) {
    throw 'R27 lineage does not preserve the consumed R26 guard.'
}

foreach ($source in @($package.runnerProvenance)) {
    $path = Join-Path $RepoRoot ([string]$source.path.Replace('/','\'))
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$source.sha256) {
        throw "R27 execution source changed after package staging: $($source.path)"
    }
}
$r25Analyzer = Join-Path $RepoRoot 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$qualificationText = & $python $r25Analyzer '--candidate-root' $candidate 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "R27 candidate linked qualification failed: $qualificationText" }
if ($PreflightOnly) {
    Write-Output "R27_OFFLINE_PACKAGE_PREFLIGHT=PASS ATTEMPT_ID=$($package.attemptId) CANDIDATE_FILES=$(@($integrity.files).Count) QAC_QUERIED=FALSE GUARD_CREATED=FALSE QEMU_LAUNCHED=FALSE"
    return
}

$attemptId = [string]$package.attemptId
$phaseRoot = [System.IO.Path]::GetDirectoryName($candidate)
$preflightRoot = Join-Path $phaseRoot ('qac-preflight-r27-' + $attemptId)
if (Test-Path -LiteralPath $preflightRoot) { throw "Refusing to overwrite R27 QAC preflight evidence: $preflightRoot" }
$null = New-Item -ItemType Directory -Path $preflightRoot
$statusPath = Join-Path $preflightRoot 'status.txt'
$listPath = Join-Path $preflightRoot 'list.txt'
$censusPath = Join-Path $preflightRoot 'host-qemu-census.json'
$reportPath = Join-Path $preflightRoot 'qac-preflight.json'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$status = ''
$list = ''
$statusExit = $null
$listExit = $null
$censusSucceeded = $false
$qemuProcesses = @()
$coordinatorResponsive = $false
$reservationDataConsistent = $false
$exclusive = $false
$admissionReason = ''
$statusQemu = [regex]::Match('','QEMU processes:\s+(\d+) \((\d+) foreign\)')
$listReservations = [regex]::Match('','Reservations:\s+(\d+)')

if (Test-Path -LiteralPath $qacCli -PathType Leaf) {
    $status = (& $qacCli status 2>&1 | Out-String)
    $statusExit = $LASTEXITCODE
    $list = (& $qacCli list 2>&1 | Out-String)
    $listExit = $LASTEXITCODE
    Set-Content -LiteralPath $statusPath -Value $status -Encoding utf8
    Set-Content -LiteralPath $listPath -Value $list -Encoding utf8
    try {
        $qemuProcesses = @(Get-CimInstance -ClassName Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" -ErrorAction Stop |
            Select-Object ProcessId,ParentProcessId,ExecutablePath,CommandLine)
        $censusSucceeded = $true
    } catch { $admissionReason = 'complete-host-process-census-failed' }
} else {
    $admissionReason = 'qac-cli-unavailable'
}
$census = [ordered]@{
    schema='GUIDEXOS_C188R27_HOST_QEMU_CENSUS_V1';capturedUtc=[DateTime]::UtcNow.ToString('O')
    succeeded=$censusSucceeded;query='Win32_Process name=qemu-system-x86_64.exe'
    count=@($qemuProcesses).Count;processes=@($qemuProcesses)
}
$census | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $censusPath -Encoding utf8
if ($statusExit -eq 0 -and $listExit -eq 0) {
    $coordinatorResponsive = $status -match 'Coordinator:\s+Online' -and $list -match 'Coordinator:\s+Online'
    $statusQemu = [regex]::Match($status,'QEMU processes:\s+(\d+) \((\d+) foreign\)')
    $listReservations = [regex]::Match($list,'Reservations:\s+(\d+)')
    $reservationDataConsistent = $statusQemu.Success -and $listReservations.Success -and
        $statusQemu.Groups[1].Value -eq $listReservations.Groups[1].Value -and
        $status -match 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -and
        $list -match 'Managed sessions:\s+0'
}
$exclusive = $coordinatorResponsive -and $reservationDataConsistent -and $censusSucceeded -and
    @($qemuProcesses).Count -eq 0 -and $statusQemu.Success -and $statusQemu.Groups[1].Value -eq '0' -and
    $statusQemu.Groups[2].Value -eq '0' -and $listReservations.Success -and $listReservations.Groups[1].Value -eq '0'
if (-not $exclusive -and -not $admissionReason) { $admissionReason = 'qac-or-process-census-reports-existing-or-inconsistent-ownership' }
$preflight = [ordered]@{
    schema='GUIDEXOS_C188R27_QAC_ADMISSION_V1';attemptId=$attemptId
    capturedUtc=[DateTime]::UtcNow.ToString('O');result=if ($exclusive) { 'CLEAR' } else { 'NOT_CLEAR' }
    qacCli=$qacCli;qacCliAvailable=(Test-Path -LiteralPath $qacCli -PathType Leaf)
    qacStatusExitCode=$statusExit;qacListExitCode=$listExit
    qacStatusSha256=if (Test-Path -LiteralPath $statusPath) { (Get-FileHash -LiteralPath $statusPath -Algorithm SHA256).Hash } else { '' }
    qacListSha256=if (Test-Path -LiteralPath $listPath) { (Get-FileHash -LiteralPath $listPath -Algorithm SHA256).Hash } else { '' }
    hostProcessCensusPath=$censusPath;hostProcessCensusSha256=(Get-FileHash -LiteralPath $censusPath -Algorithm SHA256).Hash
    coordinatorResponsive=$coordinatorResponsive;reservationSessionDataConsistent=$reservationDataConsistent
    completeProcessCensus=$censusSucceeded;exclusiveAuthorization=$exclusive;reason=$admissionReason
    statusText=$status;listText=$list
    reservationRequested=$false;attemptGuardCreated=$false;qemuLaunched=$false
}
$preflight | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding utf8
if (-not $exclusive) {
    Write-Output "R27_NATIVE_OUTCOME=NATIVE_QUALIFICATION_DEFERRED QAC_PREFLIGHT=NOT_CLEAR NO_GUARD=TRUE REASON=$admissionReason"
    Write-Output "R27_PREFLIGHT=$reportPath"
    return
}

$runnerPath = Join-Path $RepoRoot 'scripts\dotnet\run-c188r25-failfast-origin.ps1'
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$runnerStdoutPath = Join-Path $phaseRoot ('r27-runner-' + $attemptId + '.stdout.log')
$runnerStderrPath = Join-Path $phaseRoot ('r27-runner-' + $attemptId + '.stderr.log')
$runnerOutput = ''
$runnerError = ''
$runnerExit = $null
$runnerStarted = $false
try {
    $runnerStarted = $true
    $runnerOutput = (& $pwsh -NoProfile -File $runnerPath -CandidateRoot $candidate `
        -R25ManifestPath $r25ManifestPath -ArtifactRoot $ArtifactRoot `
        -MaxRuntimeSeconds $MaxRuntimeSeconds 2>&1 | Out-String)
    $runnerExit = $LASTEXITCODE
} catch { $runnerError = $_.Exception.Message }
Set-Content -LiteralPath $runnerStdoutPath -Value $runnerOutput -Encoding utf8
Set-Content -LiteralPath $runnerStderrPath -Value $runnerError -Encoding utf8
$r25EvidenceMatch = [regex]::Match($runnerOutput,'(?m)^R25_EVIDENCE=(.+?)\s*$')
$r25EvidencePath = if ($r25EvidenceMatch.Success) { $r25EvidenceMatch.Groups[1].Value.Trim() } else { '' }
$r25Evidence = $null
if ($r25EvidencePath -and (Test-Path -LiteralPath $r25EvidencePath -PathType Leaf)) {
    $r25Evidence = Read-JsonHashtable $r25EvidencePath
}
$execution = [ordered]@{
    schema='GUIDEXOS_C188R27_NATIVE_EXECUTION_V1';attemptId=$attemptId
    packagePath=$AttemptPackagePath;packageSha256=(Get-FileHash -LiteralPath $AttemptPackagePath -Algorithm SHA256).Hash
    candidateRoot=$candidate;candidateManifestSha256=$package.candidateManifestSha256
    r25ManifestPath=$r25ManifestPath;r25ManifestSha256=$package.r25ManifestSha256
    qacPreflightPath=$reportPath;qacPreflightSha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash
    qacExclusiveAuthorization=($null -ne $r25Evidence -and [string]$r25Evidence.qacPreflight -eq 'CLEAR')
    runnerInvocationCount=1;runnerPath=$runnerPath
    runnerSha256=(Get-FileHash -LiteralPath $runnerPath -Algorithm SHA256).Hash
    runnerExitCode=$runnerExit;runnerStdoutPath=$runnerStdoutPath
    runnerStdoutSha256=(Get-FileHash -LiteralPath $runnerStdoutPath -Algorithm SHA256).Hash
    runnerStderrPath=$runnerStderrPath;runnerStderrSha256=(Get-FileHash -LiteralPath $runnerStderrPath -Algorithm SHA256).Hash
    r25RunEvidencePath=$r25EvidencePath
    r25RunEvidenceSha256=if ($r25EvidencePath -and (Test-Path -LiteralPath $r25EvidencePath -PathType Leaf)) { (Get-FileHash -LiteralPath $r25EvidencePath -Algorithm SHA256).Hash } else { '' }
    outcome=if ($null -ne $r25Evidence) { [string]$r25Evidence.outcome } else { 'NATIVE_EVIDENCE_INCOMPLETE' }
    guardPath=if ($null -ne $r25Evidence) { [string]$r25Evidence.guardPath } else { '' }
    qacReservationId=if ($null -ne $r25Evidence) { $r25Evidence.qacReservationId } else { $null }
    guestBootAuthorized=($null -ne $r25Evidence -and [string]$r25Evidence.qacPreflight -eq 'CLEAR')
    guestBootCount=if ($null -ne $r25Evidence -and [bool]$r25Evidence.nativeExecutionAttempted) { 1 } else { 0 }
    automaticRetry=$false;failure=$runnerError
}
$executionPath = Join-Path $phaseRoot ('r27-run-evidence-' + $attemptId + '.json')
$execution | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $executionPath -Encoding utf8
Write-Output "R27_NATIVE_OUTCOME=$($execution.outcome) QAC_PREFLIGHT=CLEAR RUNNER_EXIT=$runnerExit"
Write-Output "R27_RUN_EVIDENCE=$executionPath"
if ($runnerExit -ne 0) { throw "The single R27 runner invocation exited $runnerExit; evidence is preserved at $executionPath." }
