param(
    [Parameter(Mandatory=$true)][string]$R30ManifestPath,
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$ArtifactRoot = 'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [ValidateRange(60,300)][int]$MaxRuntimeSeconds = 120,
    [switch]$RunOneBoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R30 runner requires PowerShell Core.' }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$R30ManifestPath = [System.IO.Path]::GetFullPath($R30ManifestPath)
$manifest = Get-Content -LiteralPath $R30ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($manifest.schema -cne 'GUIDEXOS_C188R30_THREAD_ATTACHMENT_CANDIDATE_V1' -or
    $manifest.result -cne 'PASS_HOSTED_READY_FOR_QAC_ADMISSION' -or
    [int]$manifest.oneBoot.maxGuestBoots -ne 1 -or
    [bool]$manifest.oneBoot.automaticRetry -or [bool]$manifest.oneBoot.retryAllowed -or
    [bool]$manifest.oneBoot.guardCreated) {
    throw 'R30 candidate is not an intact hosted-qualified package awaiting its single boot.'
}
$currentHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$currentBranch = (& git -C $RepoRoot branch --show-current).Trim()
if ($currentHead -cne [string]$manifest.sourceHead -or
    $currentBranch -cne 'v1.1_DOTNET_SUPPORT') {
    throw 'R30 source checkout no longer matches the candidate commit.'
}
foreach ($sourceInput in @($manifest.sourceInputs)) {
    $sourcePath = Join-Path $RepoRoot ([string]$sourceInput.path)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ine [string]$sourceInput.sha256) {
        throw "R30 source input hash changed: $($sourceInput.path)"
    }
}
$candidate = [System.IO.Path]::GetFullPath([string]$manifest.candidateRoot)
$phaseRoot = [System.IO.Path]::GetDirectoryName($candidate)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
    [System.IO.Path]::GetDirectoryName($R30ManifestPath) -cne $phaseRoot) {
    throw 'R30 manifest and candidate must be sibling artifacts below ArtifactRoot.'
}
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$r25ManifestPath = [System.IO.Path]::GetFullPath([string]$manifest.r25ManifestPath)
foreach ($path in @($candidateManifestPath,$integrityPath,$r25ManifestPath,
        [string]$manifest.runtimePackManifestPath,[string]$manifest.linkedQualificationPath,
        [string]$manifest.hostedQualificationPath,[string]$manifest.buildLog)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R30 qualification input is missing: $path" }
}
if ((Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ine [string]$manifest.candidateManifestSha256 -or
    (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash -ine [string]$manifest.candidateIntegritySha256 -or
    (Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash -ine [string]$manifest.r25ManifestSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.runtimePackManifestPath) -Algorithm SHA256).Hash -ine [string]$manifest.runtimePackManifestSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.linkedQualificationPath) -Algorithm SHA256).Hash -ine [string]$manifest.linkedQualificationSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.hostedQualificationPath) -Algorithm SHA256).Hash -ine [string]$manifest.hostedQualificationSha256 -or
    (Get-FileHash -LiteralPath ([string]$manifest.buildLog) -Algorithm SHA256).Hash -ine [string]$manifest.buildLogSha256) {
    throw 'R30 source candidate, linked checks, or hosted qualification hashes changed.'
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
    throw 'R30 manifest does not bind its 192-byte pre-allocation discriminator candidate and source-safe inventory.'
}
$runtimeManifest = Get-Content -LiteralPath ([string]$manifest.runtimePackManifestPath) -Raw | ConvertFrom-Json -ErrorAction Stop
if (-not [bool]$runtimeManifest.c188r29PreallocationGcStateGuard -or
    [string]$runtimeManifest.identity -cne 'guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1' -or
    [string]$runtimeManifest.c188r29PreallocationGcStateGuardMarker -cne 'GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD' -or
    -not [bool]$runtimeManifest.c188r30ThreadAttachmentTrace -or
    [string]$runtimeManifest.c188r30ThreadAttachmentTraceMarker -cne 'GUIDEXOS_NATIVEAOT_C188R30_THREAD_ATTACHMENT_TRACE' -or
    [bool]$runtimeManifest.c188r12r4r5r4ThreadStoreReadiness -or
    [bool]$runtimeManifest.c188r12r4r5r2r3InitializeModulesAttribution -or
    [int]$runtimeManifest.failFastOriginRecordSchema -ne 3 -or
    [int]$runtimeManifest.failFastOriginRecordBytes -ne 192) {
    throw 'R30 runtime-pack manifest is not bound to the schema-3 pre-allocation guard.'
}
$expected = @($integrity.files | ForEach-Object { [string]$_.path } | Sort-Object)
$actual = @(Get-ChildItem -LiteralPath $candidate -Recurse -File -Force | Where-Object {
    $_.FullName -notin @($candidateManifestPath,$integrityPath)
} | ForEach-Object { [System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/') } | Sort-Object)
if ($expected.Count -ne $actual.Count -or
    @($expected | Where-Object { $_ -cnotin $actual }).Count -gt 0 -or
    @($actual | Where-Object { $_ -cnotin $expected }).Count -gt 0) {
    throw 'R30 candidate file set differs from its integrity inventory.'
}
foreach ($entry in @($integrity.files)) {
    $path = Join-Path $candidate ([string]$entry.path.Replace('/','\'))
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
        throw "R30 candidate inventory hash mismatch: $($entry.path)"
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
if ($LASTEXITCODE -ne 0) { throw "R30 linked-image analyzer rehearsal failed: $qualificationText" }
$qualification = $qualificationText | ConvertFrom-Json -ErrorAction Stop
if ([int]$qualification.candidate.recordBytes -ne 192 -or
    [int]$qualification.candidate.recordSchema -ne 3 -or
    [string]$qualification.candidate.runtimeProfile -cne [string]$manifest.runtimeProfile) {
    throw 'R30 analyzer does not resolve the linked discriminator candidate.'
}
$linkedQualificationText = Get-Content -LiteralPath ([string]$manifest.linkedQualificationPath) -Raw
if ($linkedQualificationText -notmatch 'R30_THREAD_ATTACHMENT_TESTS=\d+ RESULT=PASS' -or
    -not [bool]$runtimeManifest.c188r30ThreadAttachmentTrace -or
    [bool]$runtimeManifest.c188r12r4r5r2r3InitializeModulesAttribution) {
    throw 'R30 attachment linkage and bounded TLS/FLS transition qualification is missing.'
}

$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$preflightId = [guid]::NewGuid().ToString('N')
$preflightRoot = Join-Path $phaseRoot ('qac-preflight-r30-' + $preflightId)
if (Test-Path -LiteralPath $preflightRoot) { throw "Refusing to overwrite R30 preflight evidence: $preflightRoot" }
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
    schema='GUIDEXOS_C188R30_QAC_PREFLIGHT_V1'
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
    schema='GUIDEXOS_C188R30_HOST_QEMU_CENSUS_V1'
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
    Write-Output "R30_PREFLIGHT_RESULT=$($preflightData.result) REPORT=$reportPath GUARD_CREATED=FALSE QEMU_LAUNCHED=FALSE"
    return
}
if (-not $clear) {
    $outcome = [ordered]@{
        schema='GUIDEXOS_C188R30_THREAD_ATTACHMENT_NATIVE_EVIDENCE_V1'
        result='THREAD_ATTACHMENT_GUEST_DEFERRED'
        candidateRoot=$candidate
        preflightReportPath=$reportPath
        reason=$reason
        runnerInvocations=0
        guestBootCount=0
        automaticRetry=$false
    }
    $outcomePath = Join-Path $phaseRoot ('r30-deferred-' + $preflightId + '.json')
    $outcome | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outcomePath -Encoding utf8
    Write-Output "R30_NATIVE_OUTCOME=THREAD_ATTACHMENT_GUEST_DEFERRED REPORT=$outcomePath"
    return
}
$runner = Join-Path $RepoRoot 'scripts\dotnet\run-c188r25-failfast-origin.ps1'
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$runnerOutputPath = Join-Path $phaseRoot ('r30-runner-' + $preflightId + '.stdout.log')
$runnerErrorPath = Join-Path $phaseRoot ('r30-runner-' + $preflightId + '.stderr.log')
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
function Get-R30ThreadEvent([string]$Text, [string]$Name) {
    $pattern = '(?m)^\[C188R12R4R5R4_THREAD\](?: seq=([0-9A-Fa-f]{16}))? event=' +
        [regex]::Escape($Name) + ' raw0=([0-9A-Fa-f]{16}) raw1=([0-9A-Fa-f]{16})\s*$'
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) { return $null }
    [pscustomobject]@{
        Sequence=if ($match.Groups[1].Success) { [Convert]::ToUInt64($match.Groups[1].Value,16) } else { 0 }
        Raw0=[Convert]::ToUInt64($match.Groups[2].Value,16)
        Raw1=[Convert]::ToUInt64($match.Groups[3].Value,16)
        Raw0Hex=$match.Groups[2].Value.ToUpperInvariant()
        Raw1Hex=$match.Groups[3].Value.ToUpperInvariant()
    }
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
$serialPath = if ($null -ne $runEvidence) { Join-Path ([string]$runEvidence.bootEvidencePath) 'serial.log' } else { '' }
$serialText = if ($serialPath -and (Test-Path -LiteralPath $serialPath -PathType Leaf)) {
    Get-Content -LiteralPath $serialPath -Raw
} else { '' }
$serialSha256 = if ($serialPath -and (Test-Path -LiteralPath $serialPath -PathType Leaf)) {
    (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash.ToUpperInvariant()
} else { '' }
$serialIntegrityValid = $serialSha256 -and $null -ne $runEvidence -and
    $serialSha256 -ieq [string]$runEvidence.serialSha256
$r25Milestones = if ($null -ne $runEvidence -and $null -ne $runEvidence.milestones) {
    $runEvidence.milestones
} else { $null }
function Test-R25Milestone($Milestones, [string]$Name) {
    if ($null -eq $Milestones) { return $false }
    $property = $Milestones.PSObject.Properties[$Name]
    return $null -ne $property -and [bool]$property.Value
}
$runtimeInitializedEvent = Get-R30ThreadEvent $serialText 'rhinitialize_return'
$threadStoreEvent = Get-R30ThreadEvent $serialText 'threadstore'
$currentThreadEvent = Get-R30ThreadEvent $serialText 'current_thread_state'
$transitionEnterEvent = Get-R30ThreadEvent $serialText 'transition_enter'
$palAttachEnterEvent = Get-R30ThreadEvent $serialText 'pal_attach_enter'
$palAttachCompleteEvent = Get-R30ThreadEvent $serialText 'pal_attach_complete'
$threadStorePublicationEvent = Get-R30ThreadEvent $serialText 'threadstore_publication'
$transitionAttachCompleteEvent = Get-R30ThreadEvent $serialText 'transition_attach_complete'
$transitionAttachIncompleteEvent = Get-R30ThreadEvent $serialText 'transition_attach_incomplete'
$transitionAttachStateEvent = Get-R30ThreadEvent $serialText 'transition_attach_state'
$transitionAttachFlsEvent = Get-R30ThreadEvent $serialText 'transition_attach_fls'
$palThreadIdEvent = Get-R30ThreadEvent $serialText 'pal_thread_id'
$arrayStateValidEvent = Get-R30ThreadEvent $serialText 'rhp_array_gc_state_valid'
$arrayStockEnterEvent = Get-R30ThreadEvent $serialText 'rhp_array_stock_enter'
$arrayStockReturnEvent = Get-R30ThreadEvent $serialText 'rhp_array_stock_return'
$moduleInitializeCompleteEvent = Get-R30ThreadEvent $serialText 'module_initialize_complete'
$palFlsSetEvent = Get-R30ThreadEvent $serialText 'pal_fls_set_value'
$m4Pointer = $null -ne $transitionEnterEvent -and
    $transitionEnterEvent.Raw1 -ne 0 -and
    $null -ne $currentThreadEvent -and
    $currentThreadEvent.Raw0 -eq $transitionEnterEvent.Raw1 -and
    $null -ne $threadStorePublicationEvent -and
    $threadStorePublicationEvent.Raw1 -eq 1 -and
    $threadStorePublicationEvent.Raw0 -eq $transitionEnterEvent.Raw1 -and
    $null -ne $transitionAttachCompleteEvent -and
    $transitionAttachCompleteEvent.Raw0 -eq $transitionEnterEvent.Raw1
$m4State = $null -ne $transitionAttachStateEvent -and
    (($transitionAttachStateEvent.Raw0 -band 1) -eq 1) -and
    $transitionAttachStateEvent.Raw1 -eq 0
$m4Fls = $null -ne $transitionAttachFlsEvent -and
    $null -ne $transitionEnterEvent -and
    $transitionAttachFlsEvent.Raw0 -ne [uint64]0xFFFFFFFF -and
    $transitionAttachFlsEvent.Raw1 -ne 0 -and
    $transitionAttachFlsEvent.Raw1 -eq $transitionEnterEvent.Raw1 -and
    ($null -eq $palFlsSetEvent -or
        ($palFlsSetEvent.Raw0 -eq $transitionAttachFlsEvent.Raw0 -and
         $palFlsSetEvent.Raw1 -eq $transitionAttachFlsEvent.Raw1))
$m4OsThread = $null -ne $palThreadIdEvent -and
    $palThreadIdEvent.Raw0 -ne 0 -and $palThreadIdEvent.Raw1 -eq 0 -and
    $null -ne $transitionAttachCompleteEvent -and
    $transitionAttachCompleteEvent.Raw1 -eq $palThreadIdEvent.Raw0
$m4ThreadStore = $null -ne $threadStoreEvent -and $threadStoreEvent.Raw0 -ne 0 -and
    $threadStoreEvent.Raw1 -eq 1
$threadAttachmentContractVerified = $m4Pointer -and $m4State -and $m4Fls -and
    $m4OsThread -and $m4ThreadStore -and $serialIntegrityValid
$initializationStage = 0
if ($null -ne $analysis -and [bool]$analysis.recordValid -and
    @($analysis.recordWords).Count -gt 7) {
    try { $initializationStage = [int][Convert]::ToUInt64(([string]$analysis.recordWords[7]).Substring(2),16) }
    catch { $initializationStage = 0 }
}
$guardFailureCaptured = $specificSubcondition -or
    $null -ne $transitionAttachIncompleteEvent
$threadUnavailable = $specificSubcondition -and
    $subcondition -eq 'THREADSTORE_CURRENT_THREAD_UNAVAILABLE'
$palAttachmentIncomplete = $null -ne $palAttachEnterEvent -and
    $null -eq $palAttachCompleteEvent
$exactAttachmentFailure = $threadUnavailable -or
    $null -ne $transitionAttachIncompleteEvent -or $palAttachmentIncomplete
$r30Milestones = [ordered]@{
    M1=(($null -ne $runtimeInitializedEvent -and $runtimeInitializedEvent.Raw0 -ne 0) -or
        (Test-R25Milestone $r25Milestones 'M1'))
    M2=($null -ne $transitionEnterEvent -and $serialIntegrityValid)
    M3=($null -ne $transitionEnterEvent -or
        $null -ne $transitionAttachCompleteEvent -or
        $null -ne $transitionAttachIncompleteEvent)
    M4=$threadAttachmentContractVerified
    M5=($null -ne $arrayStateValidEvent -or $exactAttachmentFailure)
    M6=($null -ne $arrayStockEnterEvent)
    M7=($null -ne $arrayStockReturnEvent)
    M8=($null -ne (Get-R30ThreadEvent $serialText 'rhp_array_return'))
    M9=($null -ne $moduleInitializeCompleteEvent -or
        (Test-R25Milestone $r25Milestones 'M7'))
    M10=(Test-R25Milestone $r25Milestones 'M10')
    M11=(Test-R25Milestone $r25Milestones 'M11')
    M12=($threadAttachmentContractVerified -and
        $null -ne $runEvidence -and [bool]$runEvidence.qacReleaseVerified)
}
$factorySuccess = [bool]$r30Milestones.M10 -and [bool]$r30Milestones.M11
$nativeRepairVerified = [bool]$r30Milestones.M4 -and
    [bool]$r30Milestones.M5 -and [bool]$r30Milestones.M6
$attachmentFailureStage = if ($null -ne $transitionAttachIncompleteEvent) {
    'transition_attach_incomplete'
} elseif ($threadUnavailable) {
    'preallocation_threadstore_current_thread_unavailable'
} elseif ($null -ne $palAttachEnterEvent -and $null -eq $palAttachCompleteEvent) {
    'pal_attachment_or_thread_construction_incomplete'
} elseif ($null -ne $palAttachCompleteEvent -and $null -eq $threadStorePublicationEvent) {
    'threadstore_publication_missing'
} elseif ($null -ne $threadStorePublicationEvent -and -not $threadAttachmentContractVerified) {
    'tls_fls_or_execution_mode_contract_mismatch'
} else { '' }
$nativeOutcomeName = if ($factorySuccess) { 'REAL_SETTINGS_FACTORY_NATIVE_VERIFIED' }
    elseif ($nativeRepairVerified) { 'NATIVE_THREAD_ATTACHMENT_REPAIR_VERIFIED' }
    elseif ($attachmentFailureStage) { 'THREAD_ATTACHMENT_CAUSE_PROVEN' }
    elseif ($null -ne $runEvidence -and [string]$runEvidence.qacPreflight -eq 'NOT_CLEAR') { 'THREAD_ATTACHMENT_GUEST_DEFERRED' }
    elseif ([bool]$r30Milestones.M3 -or [bool]$r30Milestones.M4 -or [bool]$r30Milestones.M5) { 'THREAD_ATTACHMENT_BOUNDARY_ADVANCED' }
    else { 'THREAD_ATTACHMENT_EVIDENCE_BLOCKED' }
$qacPostRunPath = ''
$qacPostRunClear = $false
if ($null -ne $runEvidence -and [string]$runEvidence.bootEvidencePath) {
    $qacAfterStatusPath = Join-Path ([string]$runEvidence.bootEvidencePath) 'qac-after-r24.status.txt'
    $qacAfterListPath = Join-Path ([string]$runEvidence.bootEvidencePath) 'qac-after-r24.list.txt'
    $qacAfterStatus = if (Test-Path -LiteralPath $qacAfterStatusPath -PathType Leaf) { Get-Content -LiteralPath $qacAfterStatusPath -Raw } else { '' }
    $qacAfterList = if (Test-Path -LiteralPath $qacAfterListPath -PathType Leaf) { Get-Content -LiteralPath $qacAfterListPath -Raw } else { '' }
    $qacPostCensus = @()
    $qacPostCensusSucceeded = $false
    try {
        $qacPostCensus = @(Get-CimInstance -ClassName Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" -ErrorAction Stop |
            Select-Object ProcessId,ParentProcessId,ExecutablePath,CommandLine)
        $qacPostCensusSucceeded = $true
    } catch { }
    $qacPostRunPath = Join-Path $phaseRoot ('r30-qac-postrun-' + $preflightId + '.json')
    [ordered]@{
        schema='GUIDEXOS_C188R30_QAC_POSTRUN_V1'
        qacReleaseVerified=[bool]$runEvidence.qacReleaseVerified
        statusPath=$qacAfterStatusPath
        statusSha256=if (Test-Path -LiteralPath $qacAfterStatusPath -PathType Leaf) { (Get-FileHash -LiteralPath $qacAfterStatusPath -Algorithm SHA256).Hash.ToUpperInvariant() } else { '' }
        listPath=$qacAfterListPath
        listSha256=if (Test-Path -LiteralPath $qacAfterListPath -PathType Leaf) { (Get-FileHash -LiteralPath $qacAfterListPath -Algorithm SHA256).Hash.ToUpperInvariant() } else { '' }
        completeHostProcessCensus=$qacPostCensusSucceeded
        qemuProcesses=$qacPostCensus
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $qacPostRunPath -Encoding utf8
    $qacPostRunClear = [bool]$runEvidence.qacReleaseVerified -and
        $qacPostCensusSucceeded -and @($qacPostCensus).Count -eq 0 -and
        $qacAfterStatus -match 'Coordinator:\s+Online' -and
        $qacAfterStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and
        $qacAfterStatus -match 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -and
        $qacAfterList -match 'Reservations:\s+0' -and
        $qacAfterList -match 'Managed sessions:\s+0'
    $r30Milestones.M12 = $threadAttachmentContractVerified -and $qacPostRunClear
}
$nativeOutcome = [ordered]@{
    schema='GUIDEXOS_C188R30_THREAD_ATTACHMENT_NATIVE_EVIDENCE_V1'
    result=$nativeOutcomeName
    candidateRoot=$candidate
    candidateManifestSha256=[string]$manifest.candidateManifestSha256
    r30ManifestPath=$R30ManifestPath
    r30ManifestSha256=(Get-FileHash -LiteralPath $R30ManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    r25RunEvidencePath=$runEvidencePath
    r25RunEvidenceSha256=if ($runEvidencePath -and (Test-Path -LiteralPath $runEvidencePath -PathType Leaf)) { (Get-FileHash -LiteralPath $runEvidencePath -Algorithm SHA256).Hash.ToUpperInvariant() } else { '' }
    serialPath=$serialPath
    serialSha256=$serialSha256
    serialIntegrityValid=[bool]$serialIntegrityValid
    analysisPath=$analysisPath
    analysisSha256=if ($analysisPath -and (Test-Path -LiteralPath $analysisPath -PathType Leaf)) { (Get-FileHash -LiteralPath $analysisPath -Algorithm SHA256).Hash.ToUpperInvariant() } else { '' }
    originalReason=if ($null -ne $analysis) { [string]$analysis.originalReason } else { '' }
    callerSymbol=if ($null -ne $analysis -and $null -ne $analysis.caller) { [string]$analysis.caller.enclosingSymbol } else { '' }
    initializationStage=$initializationStage
    guardSubcondition=$subcondition
    guardEvidence=$guardEvidence
    attachmentFailureStage=$attachmentFailureStage
    attachmentFailureCaptured=[bool]$guardFailureCaptured
    threadAttachmentContractVerified=[bool]$threadAttachmentContractVerified
    attachmentEvidence=[ordered]@{
        rawTlsThread=if ($null -ne $transitionEnterEvent) { $transitionEnterEvent.Raw1Hex } else { '' }
        initialThreadStateFlags=if ($null -ne $currentThreadEvent) { $currentThreadEvent.Raw1Hex } else { '' }
        threadStorePublished=if ($null -ne $threadStorePublicationEvent) { $threadStorePublicationEvent.Raw1 -eq 1 } else { $false }
        threadStoreThread=if ($null -ne $threadStorePublicationEvent) { $threadStorePublicationEvent.Raw0Hex } else { '' }
        attachedStateFlags=if ($null -ne $transitionAttachStateEvent) { $transitionAttachStateEvent.Raw0Hex } else { '' }
        transitionFrame=if ($null -ne $transitionAttachStateEvent) { $transitionAttachStateEvent.Raw1Hex } else { '' }
        attachedFlsIndex=if ($null -ne $transitionAttachFlsEvent) { $transitionAttachFlsEvent.Raw0Hex } else { '' }
        attachedFlsValue=if ($null -ne $transitionAttachFlsEvent) { $transitionAttachFlsEvent.Raw1Hex } else { '' }
        palFlsSetValue=if ($null -ne $palFlsSetEvent) { $palFlsSetEvent.Raw1Hex } else { '' }
        palOsThreadId=if ($null -ne $palThreadIdEvent) { $palThreadIdEvent.Raw0Hex } else { '' }
        attachedOsThreadId=if ($null -ne $transitionAttachCompleteEvent) { $transitionAttachCompleteEvent.Raw1Hex } else { '' }
    }
    arrayBoundary=[ordered]@{
        validGcState=($null -ne $arrayStateValidEvent)
        stockAllocatorEntry=($null -ne $arrayStockEnterEvent)
        stockAllocatorReturned=($null -ne $arrayStockReturnEvent)
        currentThread=if ($null -ne $arrayStateValidEvent) { $arrayStateValidEvent.Raw0Hex } else { '' }
        gcHeap=if ($null -ne $arrayStateValidEvent) { $arrayStateValidEvent.Raw1Hex } else { '' }
        stockResult=if ($null -ne $arrayStockReturnEvent) { $arrayStockReturnEvent.Raw0Hex } else { '' }
    }
    settingsFactoryEntered=[bool]$r30Milestones.M10
    milestones=$r30Milestones
    r25Milestones=$r25Milestones
    qacPostRunEvidencePath=$qacPostRunPath
    qacPostRunClear=[bool]$qacPostRunClear
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
$nativePath = Join-Path $phaseRoot ('r30-native-evidence-' + $preflightId + '.json')
$nativeOutcome | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $nativePath -Encoding utf8
Write-Output "R30_NATIVE_OUTCOME=$nativeOutcomeName RUNNER_EXIT=$runnerExit BOOTS=$($nativeOutcome.guestBootCount)"
Write-Output "R30_NATIVE_EVIDENCE=$nativePath"
if ($nativeOutcomeName -eq 'THREAD_ATTACHMENT_EVIDENCE_BLOCKED') { throw "R30 failed to establish an attachment or initialization boundary; evidence is at $nativePath" }
