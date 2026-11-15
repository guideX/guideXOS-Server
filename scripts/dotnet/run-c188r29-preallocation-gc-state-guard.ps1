param(
    [Parameter(Mandatory=$true)][string]$R29ManifestPath,
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [ValidateRange(60,300)][int]$MaxRuntimeSeconds = 120,
    [switch]$RunOneBoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R29 runner requires PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$R29ManifestPath = [System.IO.Path]::GetFullPath($R29ManifestPath)
$manifest = Get-Content -LiteralPath $R29ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($manifest.schema -cne 'GUIDEXOS_C188R29_PREALLOCATION_GC_STATE_CANDIDATE_V1' -or
    $manifest.result -cne 'PASS_HOSTED_READY_FOR_QAC_ADMISSION' -or
    [int]$manifest.oneBoot.maxGuestBoots -ne 1 -or
    [bool]$manifest.oneBoot.automaticRetry -or [bool]$manifest.oneBoot.retryAllowed -or
    [bool]$manifest.oneBoot.guardCreated) {
    throw 'R29 candidate is not an intact hosted-qualified package awaiting its single boot.'
}
$candidate = [System.IO.Path]::GetFullPath([string]$manifest.candidateRoot)
$phaseRoot = [System.IO.Path]::GetDirectoryName($candidate)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
    [System.IO.Path]::GetDirectoryName($R29ManifestPath) -cne $phaseRoot) {
    throw 'R29 manifest and candidate must be sibling artifacts below ArtifactRoot.'
}
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$r25ManifestPath = [System.IO.Path]::GetFullPath([string]$manifest.r25ManifestPath)
foreach ($path in @($candidateManifestPath,$integrityPath,$r25ManifestPath,
        [string]$manifest.runtimePackManifestPath,[string]$manifest.linkedQualificationPath,
        [string]$manifest.hostedQualificationPath,[string]$manifest.buildLog)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R29 qualification input is missing: $path" }
}
if ((Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ine [string]$manifest.candidateManifestSha256 -or
    (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash -ine [string]$manifest.candidateIntegritySha256 -or
    (Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash -ine [string]$manifest.r25ManifestSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.runtimePackManifestPath) -Algorithm SHA256).Hash -ine [string]$manifest.runtimePackManifestSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.linkedQualificationPath) -Algorithm SHA256).Hash -ine [string]$manifest.linkedQualificationSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.hostedQualificationPath) -Algorithm SHA256).Hash -ine [string]$manifest.hostedQualificationSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.buildLog) -Algorithm SHA256).Hash -ine [string]$manifest.buildLogSha256) {
    throw 'R29 source candidate, linked checks, or hosted qualification hashes changed.'
}
$r25Manifest = Get-Content -LiteralPath $r25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$integrity = Get-Content -LiteralPath $integrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([System.IO.Path]::GetFullPath([string]$r25Manifest.candidateRoot) -cne $candidate -or
    [int]$r25Manifest.diagnostics.recordBytes -ne 192 -or
    [int]$r25Manifest.diagnostics.schemaVersion -ne 3 -or
    [bool]$r25Manifest.diagnostics.gcStateGuardDiscriminator -ne $true -or
    [bool]$r25Manifest.diagnostics.preallocationGcStateGuardDiscriminator -ne $true -or
    [int]$manifest.candidateFileCount -ne @($integrity.files).Count -or
    [bool]$integrity.unverifiedRootObjectInputsConsumed -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
    [string]$candidateManifest.actualFactoryConstructorSymbol -cne 'ManagedSettingsCenter___ctor_0' -or
    -not [bool]$candidateManifest.unwindPrimitive.linkedByManagedProof -or
    [string]$candidateManifest.unwindPrimitive.sourceSha256 -ine '9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E' -or
    [string]$candidateManifest.runtimePackManifest -cne [string]$manifest.runtimePackManifestPath) {
    throw 'R29 manifest does not bind its 192-byte pre-allocation discriminator candidate and source-safe inventory.'
}
$runtimeManifest = Get-Content -LiteralPath ([string]$manifest.runtimePackManifestPath) -Raw | ConvertFrom-Json -ErrorAction Stop
if (-not [bool]$runtimeManifest.c188r29PreallocationGcStateGuard -or
    [string]$runtimeManifest.c188r29PreallocationGcStateGuardMarker -cne 'GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD' -or
    [int]$runtimeManifest.failFastOriginRecordSchema -ne 3 -or
    [int]$runtimeManifest.failFastOriginRecordBytes -ne 192) {
    throw 'R29 runtime-pack manifest is not bound to the schema-3 pre-allocation guard.'
}
$expected = @($integrity.files | ForEach-Object { [string]$_.path } | Sort-Object)
$actual = @(Get-ChildItem -LiteralPath $candidate -Recurse -File -Force | Where-Object {
    $_.FullName -notin @($candidateManifestPath,$integrityPath)
} | ForEach-Object { [System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/') } | Sort-Object)
if ($expected.Count -ne $actual.Count -or
    @($expected | Where-Object { $_ -cnotin $actual }).Count -gt 0 -or
    @($actual | Where-Object { $_ -cnotin $expected }).Count -gt 0) {
    throw 'R29 candidate file set differs from its integrity inventory.'
}
foreach ($entry in @($integrity.files)) {
    $path = Join-Path $candidate ([string]$entry.path.Replace('/','\'))
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
        throw "R29 candidate inventory hash mismatch: $($entry.path)"
    }
}
foreach ($name in @($integrity.preservedModifiedObjects.PSObject.Properties.Name)) {
    $path = Join-Path $RepoRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$integrity.preservedModifiedObjects.$name) {
        throw "Pre-existing modified object hash changed: $name"
    }
}
$analyzer = Join-Path $RepoRoot 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$qualificationText = & $python $analyzer '--candidate-root' $candidate 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "R29 linked-image analyzer rehearsal failed: $qualificationText" }
$qualification = $qualificationText | ConvertFrom-Json -ErrorAction Stop
if ([int]$qualification.candidate.recordBytes -ne 192 -or
    [int]$qualification.candidate.recordSchema -ne 3 -or
    [string]$qualification.candidate.runtimeProfile -cne [string]$manifest.runtimeProfile) {
    throw 'R29 analyzer does not resolve the linked discriminator candidate.'
}

$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$preflightId = [guid]::NewGuid().ToString('N')
$preflightRoot = Join-Path $phaseRoot ('qac-preflight-r29-' + $preflightId)
if (Test-Path -LiteralPath $preflightRoot) { throw "Refusing to overwrite R29 preflight evidence: $preflightRoot" }
$null = New-Item -ItemType Directory -Path $preflightRoot
$statusPath = Join-Path $preflightRoot 'status.txt'
$listPath = Join-Path $preflightRoot 'list.txt'
$censusPath = Join-Path $preflightRoot 'host-qemu-census.json'
$reportPath = Join-Path $preflightRoot 'qac-preflight.json'
$status = ''
$list = ''
$statusExit = $null
$listExit = $null
$qemuProcesses = @()
$censusSucceeded = $false
$reason = ''
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
    } catch { $reason = 'complete-host-process-census-failed' }
} else { $reason = 'qac-cli-unavailable' }
$statusQemu = [regex]::Match($status,'QEMU processes:\s+(\d+) \((\d+) foreign\)')
$listReservations = [regex]::Match($list,'Reservations:\s+(\d+)')
$responsive = $statusExit -eq 0 -and $listExit -eq 0 -and
    $status -match 'Coordinator:\s+Online' -and $list -match 'Coordinator:\s+Online'
$consistent = $statusQemu.Success -and $listReservations.Success -and
    $statusQemu.Groups[1].Value -eq $listReservations.Groups[1].Value -and
    $status -match 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -and
    $list -match 'Managed sessions:\s+0'
$clear = $responsive -and $consistent -and $censusSucceeded -and
    @($qemuProcesses).Count -eq 0 -and $statusQemu.Success -and
    $statusQemu.Groups[1].Value -eq '0' -and $statusQemu.Groups[2].Value -eq '0' -and
    $listReservations.Success -and $listReservations.Groups[1].Value -eq '0'
if (-not $clear -and -not $reason) { $reason = 'qac-or-host-census-not-exclusive' }
[ordered]@{
    schema='GUIDEXOS_C188R29_QAC_PREFLIGHT_V1'
    capturedUtc=[DateTime]::UtcNow.ToString('O')
    result=if ($clear) { 'CLEAR' } else { 'NOT_CLEAR' }
    candidateRoot=$candidate
    candidateManifestSha256=[string]$manifest.candidateManifestSha256
    qacCli=$qacCli;qacCliAvailable=(Test-Path -LiteralPath $qacCli -PathType Leaf)
    qacStatusExitCode=$statusExit;qacListExitCode=$listExit
    statusSha256=if (Test-Path -LiteralPath $statusPath) { (Get-FileHash -LiteralPath $statusPath -Algorithm SHA256).Hash } else { '' }
    listSha256=if (Test-Path -LiteralPath $listPath) { (Get-FileHash -LiteralPath $listPath -Algorithm SHA256).Hash } else { '' }
    hostProcessCensusPath=$censusPath
    hostProcessCensusSha256=''
    completeProcessCensus=$censusSucceeded
    qemuProcesses=@($qemuProcesses)
    coordinatorResponsive=$responsive
    reservationDataConsistent=$consistent
    exclusiveAuthorization=$clear
    reason=$reason
    reservationRequested=$false
    attemptGuardCreated=$false
    qemuLaunched=$false
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding utf8
$census = [ordered]@{
    schema='GUIDEXOS_C188R29_HOST_QEMU_CENSUS_V1'
    capturedUtc=[DateTime]::UtcNow.ToString('O')
    succeeded=$censusSucceeded
    query='Win32_Process name=qemu-system-x86_64.exe'
    count=@($qemuProcesses).Count
    processes=@($qemuProcesses)
}
$census | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $censusPath -Encoding utf8
$preflightData = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$preflightData.hostProcessCensusSha256 = (Get-FileHash -LiteralPath $censusPath -Algorithm SHA256).Hash
$preflightData | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding utf8

if (-not $RunOneBoot) {
    Write-Output "R29_PREFLIGHT_RESULT=$($preflightData.result) REPORT=$reportPath GUARD_CREATED=FALSE QEMU_LAUNCHED=FALSE"
    return
}
if (-not $clear) {
    $outcome = [ordered]@{
        schema='GUIDEXOS_C188R29_PREALLOCATION_GC_STATE_NATIVE_EVIDENCE_V1'
        result='GC_STATE_GUARD_GUEST_DEFERRED'
        candidateRoot=$candidate
        preflightReportPath=$reportPath
        reason=$reason
        runnerInvocations=0
        guestBootCount=0
        automaticRetry=$false
    }
    $outcomePath = Join-Path $phaseRoot ('r29-deferred-' + $preflightId + '.json')
    $outcome | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outcomePath -Encoding utf8
    Write-Output "R29_NATIVE_OUTCOME=GC_STATE_GUARD_GUEST_DEFERRED REPORT=$outcomePath"
    return
}
$runner = Join-Path $RepoRoot 'scripts\dotnet\run-c188r25-failfast-origin.ps1'
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$runnerOutputPath = Join-Path $phaseRoot ('r29-runner-' + $preflightId + '.stdout.log')
$runnerErrorPath = Join-Path $phaseRoot ('r29-runner-' + $preflightId + '.stderr.log')
$runnerOutput = ''
$runnerError = ''
$runnerExit = $null
try {
    $runnerOutput = & $pwsh -NoProfile -File $runner -CandidateRoot $candidate `
        -R25ManifestPath $r25ManifestPath -ArtifactRoot $ArtifactRoot `
        -MaxRuntimeSeconds $MaxRuntimeSeconds 2>&1 | Out-String
    $runnerExit = $LASTEXITCODE
} catch { $runnerError = $_.Exception.Message }
Set-Content -LiteralPath $runnerOutputPath -Value $runnerOutput -Encoding utf8
Set-Content -LiteralPath $runnerErrorPath -Value $runnerError -Encoding utf8
$evidenceMatch = [regex]::Match($runnerOutput,'(?m)^R25_EVIDENCE=(.+?)\s*$')
$runEvidencePath = if ($evidenceMatch.Success) { [System.IO.Path]::GetFullPath($evidenceMatch.Groups[1].Value.Trim()) } else { '' }
$runEvidence = $null
if ($runEvidencePath -and (Test-Path -LiteralPath $runEvidencePath -PathType Leaf)) {
    $runEvidence = Get-Content -LiteralPath $runEvidencePath -Raw | ConvertFrom-Json -ErrorAction Stop
}
$analysis = $null
$analysisPath = ''
if ($null -ne $runEvidence -and [string]$runEvidence.r25OriginAnalysisPath -and
    (Test-Path -LiteralPath ([string]$runEvidence.r25OriginAnalysisPath) -PathType Leaf)) {
    $analysisPath = [string]$runEvidence.r25OriginAnalysisPath
    $analysis = Get-Content -LiteralPath $analysisPath -Raw | ConvertFrom-Json -ErrorAction Stop
}
$subcondition = ''
$guardEvidence = $null
if ($null -ne $analysis -and $null -ne $analysis.PSObject.Properties['gcStateGuard'] -and
    $null -ne $analysis.gcStateGuard) {
    $guardEvidence = $analysis.gcStateGuard
    $subcondition = [string]$analysis.gcStateGuard.firstFailedSubcondition
}
$callerIsGuard = $null -ne $analysis -and [bool]$analysis.recordValid -and
    [string]$analysis.originalReason -eq '0x0000000000000008' -and
    $null -ne $analysis.caller -and [bool]$analysis.caller.resolved -and
    [string]$analysis.caller.enclosingSymbol -match 'RhpNewArray'
$factorySuccess = $null -ne $runEvidence -and
    [string]$runEvidence.outcome -eq 'REAL_SETTINGS_FACTORY_NATIVE_VERIFIED' -and
    $null -ne $runEvidence.milestones -and [bool]$runEvidence.milestones.M10
$preallocationFailure = $callerIsGuard -and $null -ne $guardEvidence -and
    [string]$guardEvidence.guardLocation -eq 'PREALLOCATION'
$stockAllocatorReached = $callerIsGuard -and $null -ne $guardEvidence -and
    [string]$guardEvidence.guardLocation -eq 'POSTALLOCATION'
$specificSubcondition = $preallocationFailure -and
    $subcondition -in @('THREADSTORE_CURRENT_THREAD_UNAVAILABLE',
        'THREAD_ALLOCATION_CONTEXT_UNAVAILABLE','GC_HEAP_NOT_INITIALIZED',
        'GC_HEAP_POINTER_UNAVAILABLE','GC_STATE_READ_FAILED_AFTER_PREREQUISITES',
        'CURRENT_THREAD_OUTPUT_NULL','GC_HEAP_OUTPUT_NULL','GC_STATE_SAFE_READ_FAILED',
        'GC_STATE_READ_INCOMPLETE')
$outcomeName = if ($factorySuccess) { 'REAL_SETTINGS_FACTORY_NATIVE_VERIFIED' }
    elseif ($specificSubcondition) { 'PREALLOCATION_SUBCONDITION_ATTRIBUTED' }
    elseif ($null -ne $runEvidence -and [string]$runEvidence.qacPreflight -eq 'NOT_CLEAR') { 'PREALLOCATION_GUEST_DEFERRED' }
    elseif ($null -ne $runEvidence -and [bool]$runEvidence.qacReleaseVerified -and
            ($stockAllocatorReached -or ($callerIsGuard -and
                [string]$guardEvidence.guardLocation -eq 'PREALLOCATION' -and $subcondition -eq 'NONE'))) {
        'PREALLOCATION_BOUNDARY_ADVANCED'
    }
    else { 'PREALLOCATION_EVIDENCE_BLOCKED' }
$nativeOutcome = [ordered]@{
    schema='GUIDEXOS_C188R29_PREALLOCATION_GC_STATE_NATIVE_EVIDENCE_V1'
    result=$outcomeName
    candidateRoot=$candidate
    candidateManifestSha256=[string]$manifest.candidateManifestSha256
    r29ManifestPath=$R29ManifestPath
    r29ManifestSha256=(Get-FileHash -LiteralPath $R29ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    r25RunEvidencePath=$runEvidencePath
    r25RunEvidenceSha256=if ($runEvidencePath -and (Test-Path -LiteralPath $runEvidencePath -PathType Leaf)) { (Get-FileHash -LiteralPath $runEvidencePath -Algorithm SHA256).Hash.ToUpperInvariant() } else { '' }
    analysisPath=$analysisPath
    analysisSha256=if ($analysisPath -and (Test-Path -LiteralPath $analysisPath -PathType Leaf)) { (Get-FileHash -LiteralPath $analysisPath -Algorithm SHA256).Hash.ToUpperInvariant() } else { '' }
    originalReason=if ($null -ne $analysis) { [string]$analysis.originalReason } else { '' }
    callerSymbol=if ($null -ne $analysis -and $null -ne $analysis.caller) { [string]$analysis.caller.enclosingSymbol } else { '' }
    guardSubcondition=$subcondition
    guardEvidence=$guardEvidence
    preallocationGuardEntered=($null -ne $guardEvidence -and
        [string]$guardEvidence.guardLocation -in @('PREALLOCATION','POSTALLOCATION'))
    preallocationGuardPassed=$stockAllocatorReached
    stockAllocatorReached=$stockAllocatorReached
    settingsFactoryEntered=$factorySuccess
    milestones=if ($null -ne $runEvidence) { $runEvidence.milestones } else { $null }
    qacPreflightReport=$reportPath
    qacReservationId=if ($null -ne $runEvidence) { $runEvidence.qacReservationId } else { $null }
    qacReleaseVerified=if ($null -ne $runEvidence) { [bool]$runEvidence.qacReleaseVerified } else { $false }
    guardPath=if ($null -ne $runEvidence) { [string]$runEvidence.guardPath } else { '' }
    runnerPath=$runner
    runnerSha256=(Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash.ToUpperInvariant()
    runnerInvocationCount=1
    runnerExitCode=$runnerExit
    runnerOutputPath=$runnerOutputPath
    runnerErrorPath=$runnerErrorPath
    guestBootCount=if ($null -ne $runEvidence -and [bool]$runEvidence.nativeExecutionAttempted) { 1 } else { 0 }
    automaticRetry=$false
    failure=$runnerError
}
$nativePath = Join-Path $phaseRoot ('r29-native-evidence-' + $preflightId + '.json')
$nativeOutcome | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $nativePath -Encoding utf8
Write-Output "R29_NATIVE_OUTCOME=$outcomeName RUNNER_EXIT=$runnerExit BOOTS=$($nativeOutcome.guestBootCount)"
Write-Output "R29_NATIVE_EVIDENCE=$nativePath"
if ($outcomeName -eq 'PREALLOCATION_EVIDENCE_BLOCKED') { throw "R29 failed to establish the discriminator outcome; evidence is at $nativePath" }
