param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][ValidateSet('Rehearse','RunNative')][string]$Mode,
    [Parameter()][ValidateRange(60,300)][int]$MaxRuntimeSeconds=120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R35 Settings launch runner requires PowerShell Core.' }
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$artifactRoot = [System.IO.Path]::GetFullPath('E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT')
$packageRootFull = [System.IO.Path]::GetFullPath($PackageRoot)
$packageManifestPath = Join-Path $packageRootFull 'r35-execution-package.json'
if (-not (Test-Path -LiteralPath $packageManifestPath -PathType Leaf)) {
    throw "R35 execution package manifest is missing: $packageManifestPath"
}
$package = Get-Content -LiteralPath $packageManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$packageManifestSha256 = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash
if ([string]$package.schema -cne 'GUIDEXOS_C188R35_SETTINGS_LAUNCH_PACKAGE_V1' -or
    [string]$package.result -cne 'PASS_PACKAGE_STAGED' -or
    [System.IO.Path]::GetFullPath([string]$package.candidateRoot) -cne $packageRootFull.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar + [System.IO.Path]::GetFileName([string]$package.candidateRoot) -or
    [System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath([string]$package.candidateRoot)) -cne $packageRootFull -or
    [string]$package.executionPackageId -notmatch '^[A-Fa-f0-9]{32}$' -or
    [string]$package.executionAttemptId -notmatch '^[A-Fa-f0-9]{32}$') {
    throw 'R35 package identity or candidate-root binding is invalid.'
}

$candidate = [System.IO.Path]::GetFullPath([string]$package.candidateRoot)
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$candidateIntegrityPath = Join-Path $candidate 'candidate-integrity.json'
$r25ManifestPath = [System.IO.Path]::GetFullPath([string]$package.r25CandidateManifestPath)
$r25AnalyzerPath = Join-Path $repo 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$runnerHashMap = @{}
foreach ($component in @($package.runnerComponents)) {
    $relative = [string]$component.path
    $sourcePath = Join-Path $repo ($relative.Replace('/','\'))
    $packagedPath = [string]$component.packagedPath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $packagedPath -PathType Leaf)) {
        throw "R35 runner package input is missing: $relative"
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $packagedHash = (Get-FileHash -LiteralPath $packagedPath -Algorithm SHA256).Hash
    if ($sourceHash -ine [string]$component.sha256 -or $packagedHash -ine [string]$component.sha256) {
        throw "R35 runner source/package hash differs from the immutable execution package: $relative"
    }
    $runnerHashMap[$relative] = $sourceHash
}
if ($runnerHashMap['scripts/dotnet/run-c188r12r1-settings-handoff.ps1'] -ine
        [string]$package.correctedR35NestedRunnerSha256 -or
    [string]$package.r34CorrectedNestedRunnerSha256 -ine
        'D79C5CCB90F68F348E7F53A110C77CAEE6AA13425E318A23684585B1A574B7BC') {
    throw 'R35 runner is not descended from the corrected R34 nested runner.'
}

if (-not (Test-Path -LiteralPath $candidate -PathType Container) -or
    (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ine
        [string]$package.candidateManifestSha256 -or
    (Get-FileHash -LiteralPath $candidateIntegrityPath -Algorithm SHA256).Hash -ine
        [string]$package.candidateIntegritySha256 -or
    (Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash -ine
        [string]$package.r25CandidateManifestSha256 -or
    (Get-FileHash -LiteralPath $r25AnalyzerPath -Algorithm SHA256).Hash -ine
        [string]$package.r25AnalyzerSha256) {
    throw 'R35 candidate, R25 sidecar, or analyzer package hash mismatch.'
}
$candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$candidateIntegrity = Get-Content -LiteralPath $candidateIntegrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([int]$package.candidateInventoryFileCount -ne 349 -or $candidateIntegrity.files.Count -ne 349) {
    throw 'R35 candidate does not contain the complete original 349-file inventory.'
}
$expectedPaths = @($candidateIntegrity.files | ForEach-Object { [string]$_.path } | Sort-Object)
$actualPaths = @(Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {
    $_.FullName -notin @($candidateManifestPath,$candidateIntegrityPath)
} | ForEach-Object { [System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/') } | Sort-Object)
if ($expectedPaths.Count -ne 349 -or $actualPaths.Count -ne 349 -or
    @($expectedPaths | Where-Object { $_ -notin $actualPaths }).Count -gt 0 -or
    @($actualPaths | Where-Object { $_ -notin $expectedPaths }).Count -gt 0) {
    throw 'R35 candidate file set differs from the bound 349-file inventory.'
}
foreach ($entry in @($candidateIntegrity.files)) {
    $path = Join-Path $candidate ([string]$entry.path.Replace('/','\'))
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
        throw "R35 candidate inventory hash mismatch: $($entry.path)"
    }
}

. (Join-Path $repo 'scripts\dotnet\c188r11-runner-predicates.ps1')
. (Join-Path $repo 'scripts\dotnet\c188r11r4r2-runner-predicates.ps1')
. (Join-Path $repo 'scripts\dotnet\c188r12r1-settings-handoff-predicates.ps1')
. (Join-Path $repo 'scripts\dotnet\c188r33-settings-handoff-predicates.ps1')
$readinessProfile = Get-C188R33ManagedReadinessProfile $candidate
if ([string]$readinessProfile.profile -cne [string]$package.readinessProfile -or
    [uint32]$readinessProfile.expectedDescriptorCount -ne [uint32]$package.expectedManagedDescriptorCount -or
    ($readinessProfile.expectedDescriptorSelectors -join ',') -cne
        (@($package.expectedManagedDescriptorSelectors) -join ',') -or
    [uint32]$package.expectedManagedDescriptorCount -ne 2 -or
    (@($package.expectedManagedDescriptorSelectors) -join ',') -cne '4,5' -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne [string]$package.settingsFactory -or
    [string]$candidateManifest.actualFactoryConstructorSymbol -cne [string]$package.settingsFactoryConstructor) {
    throw 'R35 candidate-specific descriptor profile or genuine Settings factory metadata is invalid.'
}
$r25AnalyzerOutput = & $python $r25AnalyzerPath '--candidate-root' $candidate 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "R35 linked candidate analyzer failed: $r25AnalyzerOutput" }
$r25AnalyzerObject = $r25AnalyzerOutput | ConvertFrom-Json -ErrorAction Stop
if ([string]$r25AnalyzerObject.result -cne 'PASS' -or
    [string]$r25AnalyzerObject.candidate.managedElfSha256 -ine [string]$package.candidateHashes.managedElfSha256) {
    throw 'R35 analyzer does not identify the bound managed ELF.'
}

function Get-C188R35LiveReadinessEvidence([object]$Package,[object]$Profile,[string]$RepoRoot) {
    $evidence = $Package.r34Evidence
    foreach ($path in @([string]$evidence.serialPath,[string]$evidence.screenshotPpmPath,
            [string]$evidence.screenshotPngPath,[string]$evidence.qmpLogPath,[string]$evidence.activeStatePath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R34 prelaunch evidence is missing: $path" }
    }
    foreach ($pair in @(
        @([string]$evidence.serialPath,[string]$evidence.serialSha256),
        @([string]$evidence.screenshotPpmPath,[string]$evidence.screenshotPpmSha256),
        @([string]$evidence.screenshotPngPath,[string]$evidence.screenshotPngSha256),
        @([string]$evidence.qmpLogPath,[string]$evidence.qmpLogSha256))) {
        if ((Get-FileHash -LiteralPath $pair[0] -Algorithm SHA256).Hash -ine $pair[1]) {
            throw "R34 prelaunch evidence hash mismatch: $($pair[0])"
        }
    }
    $active = Get-Content -LiteralPath ([string]$evidence.activeStatePath) -Raw | ConvertFrom-Json -ErrorAction Stop
    $serial = Get-Content -LiteralPath ([string]$evidence.serialPath) -Raw
    $qmp = Get-Content -LiteralPath ([string]$evidence.qmpLogPath) -Raw
    $selectors = [uint32[]]@($Profile.expectedDescriptorSelectors)
    $failure = Get-C188R11ReadinessFailureReason $serial ([uint32]$Profile.expectedDescriptorCount) $selectors
    if (-not [string]::IsNullOrWhiteSpace($failure) -or
        -not (Test-C188R33SettingsClickGate $serial $Profile.profile `
            ([uint32]$Profile.expectedDescriptorCount) ([int]$active.screenWidth) ([int]$active.screenHeight) $selectors)) {
        throw 'Preserved live Notes rendering/input-loop trace does not meet the candidate-specific readiness gate.'
    }
    $target = Get-C188R12R1NotesSettingsClickTarget $serial $RepoRoot `
        ([int]$active.screenWidth) ([int]$active.screenHeight)
    if (-not [bool]$target.accepted -or [int]$target.x -ne 815 -or [int]$target.y -ne 550 -or
        [int]$active.screenWidth -ne 1280 -or [int]$active.screenHeight -ne 800 -or
        [string]$target.reason -cne 'source-defined-settings-button-center' -or
        $qmp -match 'input-send-event|button-down|button-up') {
        throw 'Preserved Settings target, display, overlay, or no-input evidence failed validation.'
    }
    $ppm = [System.IO.File]::ReadAllBytes([string]$evidence.screenshotPpmPath)
    $header = [System.Text.Encoding]::ASCII.GetString($ppm,0,[Math]::Min(128,$ppm.Length))
    $match = [regex]::Match($header,'^P6\s+(?<width>\d+)\s+(?<height>\d+)\s+255\s')
    if (-not $match.Success -or [int]$match.Groups['width'].Value -ne 1280 -or
        [int]$match.Groups['height'].Value -ne 800) {
        throw 'Preserved live framebuffer screenshot does not confirm the 1280x800 guest layout.'
    }
    return [ordered]@{
        accepted=$true;readinessProfile=[string]$Profile.profile
        expectedDescriptorCount=[uint32]$Profile.expectedDescriptorCount
        expectedDescriptorSelectors=@($selectors);readinessFailure=''
        mainLoopObserved=$true;notesRenderAndFocusObserved=$true
        screenWidth=[int]$active.screenWidth;screenHeight=[int]$active.screenHeight
        settingsTarget=$target;screenshotPpmPath=[string]$evidence.screenshotPpmPath
        screenshotPpmSha256=[string]$evidence.screenshotPpmSha256
        screenshotPngPath=[string]$evidence.screenshotPngPath
        screenshotPngSha256=[string]$evidence.screenshotPngSha256
        qmpInputSubmitted=$false;settingsLaunchAlreadyRequested=$false
        genuineSelector5FactoryLinked=($Package.settingsFactory -eq 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter')
        factoryConstructor=[string]$Package.settingsFactoryConstructor
    }
}
$liveReadiness = Get-C188R35LiveReadinessEvidence $package $readinessProfile $repo

if ($Mode -eq 'Rehearse') {
    $rehearsalId = [guid]::NewGuid().ToString('N')
    $rehearsalPhaseRoot = Join-Path $packageRootFull ('rehearsal-' + $rehearsalId)
    $rehearsalCandidateLeaf = 'candidate-r35-rehearsal-' + $rehearsalId
    $rehearsalCandidate = Join-Path $rehearsalPhaseRoot $rehearsalCandidateLeaf
    $rehearsalGuardRoot = Join-Path $rehearsalPhaseRoot 'one-boot-guards'
    $rehearsalBootRoot = Join-Path $rehearsalPhaseRoot ('precreated-evidence-' + $rehearsalId)
    foreach ($path in @($rehearsalPhaseRoot,$rehearsalCandidate,$rehearsalGuardRoot,$rehearsalBootRoot)) {
        if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite R35 rehearsal path: $path" }
    }
    $null = New-Item -ItemType Directory -Path $rehearsalPhaseRoot,$rehearsalGuardRoot,$rehearsalBootRoot -Force
    Copy-Item -LiteralPath $candidate -Destination $rehearsalCandidate -Recurse
    $rehearsalCandidateManifest = Join-Path $rehearsalCandidate 'candidate-manifest.json'
    $rehearsalCandidateIntegrity = Join-Path $rehearsalCandidate 'candidate-integrity.json'
    if ((Get-FileHash -LiteralPath $rehearsalCandidateManifest -Algorithm SHA256).Hash -ine
            [string]$package.candidateManifestSha256 -or
        (Get-FileHash -LiteralPath $rehearsalCandidateIntegrity -Algorithm SHA256).Hash -ine
            [string]$package.candidateIntegritySha256) {
        throw 'R35 rehearsal candidate copy differs from the immutable execution package.'
    }
    $rehearsalIntegrity = Get-Content -LiteralPath $rehearsalCandidateIntegrity -Raw | ConvertFrom-Json -ErrorAction Stop
    foreach ($entry in @($rehearsalIntegrity.files)) {
        $copiedPath = Join-Path $rehearsalCandidate ([string]$entry.path.Replace('/','\'))
        if ((Get-FileHash -LiteralPath $copiedPath -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
            throw "R35 rehearsal candidate inventory hash mismatch: $($entry.path)"
        }
    }
    $rehearsalR25Path = Join-Path $rehearsalPhaseRoot ($rehearsalCandidateLeaf + '-r25-candidate-manifest.json')
    $r25Source = Get-Content -LiteralPath $r25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $rehearsalR25 = [ordered]@{}
    foreach ($property in $r25Source.PSObject.Properties) { $rehearsalR25[$property.Name] = $property.Value }
    $rehearsalR25.candidateRoot = $rehearsalCandidate
    $rehearsalR25.candidateManifestPath = $rehearsalCandidateManifest
    $rehearsalR25.candidateIntegrityPath = $rehearsalCandidateIntegrity
    $rehearsalR25 | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $rehearsalR25Path -Encoding utf8
    $rehearsalManifestHash = (Get-FileHash -LiteralPath $rehearsalCandidateManifest -Algorithm SHA256).Hash
    $captureConfig = [ordered]@{
        schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1'
        recordAddress=[string]$r25Source.diagnostics.recordAddress;recordBytes=192;recordSchema=3
        candidateManifestSha256=$rehearsalManifestHash
    }
    $captureConfigPath = Join-Path $rehearsalBootRoot 'r25-failfast-capture.json'
    $captureConfig | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $captureConfigPath -Encoding utf8
    $rehearsalGuardId = [guid]::NewGuid().ToString('N')
    $rehearsalGuardPath = Join-Path $rehearsalGuardRoot ('attempt-r25-' + $rehearsalGuardId + '.json')
    [ordered]@{
        schema='GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1';candidateRoot=$rehearsalCandidate
        candidateManifestSha256=$rehearsalManifestHash
        candidateIntegritySha256=(Get-FileHash -LiteralPath $rehearsalCandidateIntegrity -Algorithm SHA256).Hash
        bootEvidencePath=$rehearsalBootRoot;attemptNumber=1;maxGuestBoots=1
        automaticRetry=$false;retryAllowed=$false;consumed=$true;syntheticFixture=$true
        executionPackageId=[string]$package.executionPackageId
        executionPackageManifestSha256=$packageManifestSha256
        rehearsalOnly=$true
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $rehearsalGuardPath -Encoding utf8
    $nestedRunner = Join-Path $repo 'scripts\dotnet\run-c188r12r1-settings-handoff.ps1'
    $pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
    $nestedArgs = @('-NoProfile','-File',$nestedRunner,
        '-CandidateRoot',$rehearsalCandidate,
        '-ExpectedKernelSha256',[string]$package.candidateHashes.kernelSha256,
        '-BootEvidencePath',$rehearsalBootRoot,
        '-BootEvidenceAuthorizationPath',$rehearsalGuardPath,
        '-ExecutionPackageManifestPath',$packageManifestPath,
        '-ExecutionAttemptId',[string]$package.executionAttemptId,
        '-PrelaunchRehearsal')
    $nestedOutput = & $pwsh @nestedArgs 2>&1 | Out-String
    $nestedExit = $LASTEXITCODE
    $nestedJson = @($nestedOutput -split "`r?`n" | Where-Object {
        $_ -match '^\{"schema":"GUIDEXOS_C188R27_PRELAUNCH_REHEARSAL_V1"'
    } | Select-Object -Last 1)
    if ($nestedJson.Count -ne 1) { throw "R35 nested rehearsal output is missing: $nestedOutput" }
    $nestedProof = $nestedJson[0] | ConvertFrom-Json -ErrorAction Stop
    $inputManifestPath = Join-Path $rehearsalBootRoot 'r12r2r1-input-manifest.json'
    if ($nestedExit -ne 0 -or [string]$nestedProof.result -cne 'PASS' -or
        [bool]$nestedProof.qacInvoked -or [bool]$nestedProof.qemuLaunched -or
        [bool]$nestedProof.guestExecuted -or -not [bool]$nestedProof.precreatedEvidenceRoot -or
        -not (Test-Path -LiteralPath $inputManifestPath -PathType Leaf)) {
        throw 'R35 parent-to-nested runner rehearsal failed or reached QAC/QEMU.'
    }
    $inputManifest = Get-Content -LiteralPath $inputManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    if ([string]$inputManifest.readinessProfile -cne [string]$readinessProfile.profile -or
        [uint32]$inputManifest.expectedManagedDescriptorCount -ne 2 -or
        (@($inputManifest.expectedManagedDescriptorSelectors) -join ',') -cne '4,5' -or
        [string]$inputManifest.executionPackageManifestSha256 -ine $packageManifestSha256 -or
        [string]$inputManifest.executionAttemptId -ine [string]$package.executionAttemptId) {
        throw 'R35 nested handoff manifest lost the candidate profile or execution-package identity.'
    }
    foreach ($relative in @('kernel.elf','ramdisk.img','EFI/BOOT/BOOTX64.EFI')) {
        $sourceMediaHash = [string]$nestedProof.sourceMediaHashes.PSObject.Properties[$relative].Value
        $copiedMediaHash = [string]$nestedProof.copiedMediaHashes.PSObject.Properties[$relative].Value
        if ($sourceMediaHash -ne $copiedMediaHash) {
            throw "R35 rehearsal media copy mismatch: $relative"
        }
    }
    $evidencePath = [System.IO.Path]::GetFullPath([string]$package.rehearsalEvidencePath)
    if (Test-Path -LiteralPath $evidencePath) { throw "Refusing to overwrite R35 rehearsal evidence: $evidencePath" }
    [ordered]@{
        schema='GUIDEXOS_C188R35_OFFLINE_REHEARSAL_V1';result='PASS_OFFLINE_REHEARSAL'
        executionPackageId=[string]$package.executionPackageId;executionAttemptId=[string]$package.executionAttemptId
        executionPackageManifestPath=$packageManifestPath;executionPackageManifestSha256=$packageManifestSha256
        candidateRoot=$candidate;candidateManifestSha256=[string]$package.candidateManifestSha256
        candidateIntegritySha256=[string]$package.candidateIntegritySha256;candidateInventoryFileCount=349
        r25CandidateManifestSha256=[string]$package.r25CandidateManifestSha256
        r25AnalyzerSha256=[string]$package.r25AnalyzerSha256
        runnerComponents=@($package.runnerComponents | ForEach-Object { [ordered]@{path=$_.path;sha256=$_.sha256} })
        readiness=$liveReadiness
        nestedRehearsal=[ordered]@{
            exitCode=$nestedExit;precreatedEvidenceRootAccepted=[bool]$nestedProof.precreatedEvidenceRoot
            readinessProfile=[string]$inputManifest.readinessProfile
            expectedDescriptorCount=[uint32]$inputManifest.expectedManagedDescriptorCount
            expectedDescriptorSelectors=@($inputManifest.expectedManagedDescriptorSelectors)
            sourceMediaHashes=$nestedProof.sourceMediaHashes;copiedMediaHashes=$nestedProof.copiedMediaHashes
            qacInvoked=[bool]$nestedProof.qacInvoked;qemuLaunched=[bool]$nestedProof.qemuLaunched
            guestExecuted=[bool]$nestedProof.guestExecuted
            inputManifestPath=$inputManifestPath;rehearsalEvidencePath=$nestedProof.bootEvidencePath
            syntheticGuardPath=$rehearsalGuardPath;syntheticGuardOnly=$true
        }
        settingsInteractionStageEligible=$true;settingsClickSubmitted=$false
        realAttemptGuardConsumed=$false;qacInvoked=$false;qemuLaunched=$false;guestExecuted=$false
    } | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $evidencePath -Encoding utf8
    Write-Output "R35_OFFLINE_REHEARSAL=PASS PACKAGE=$($package.executionPackageId) EVIDENCE=$evidencePath"
    return
}

$rehearsalEvidencePath = [System.IO.Path]::GetFullPath([string]$package.rehearsalEvidencePath)
if (-not (Test-Path -LiteralPath $rehearsalEvidencePath -PathType Leaf)) {
    throw 'R35 native launch is blocked until the complete offline rehearsal passes.'
}
$rehearsalEvidence = Get-Content -LiteralPath $rehearsalEvidencePath -Raw | ConvertFrom-Json -ErrorAction Stop
if ([string]$rehearsalEvidence.result -cne 'PASS_OFFLINE_REHEARSAL' -or
    [string]$rehearsalEvidence.executionPackageManifestSha256 -ine $packageManifestSha256 -or
    [string]$rehearsalEvidence.executionAttemptId -ine [string]$package.executionAttemptId) {
    throw 'R35 offline rehearsal evidence does not authorize this exact package and attempt.'
}
$guardRoot = Join-Path $packageRootFull 'one-boot-guards'
if (Test-Path -LiteralPath $guardRoot -PathType Container) {
    foreach ($guardPath in @(Get-ChildItem -LiteralPath $guardRoot -File -Filter 'attempt-r25-*.json')) {
        $guard = Get-Content -LiteralPath $guardPath.FullName -Raw | ConvertFrom-Json -ErrorAction Stop
        if ([System.IO.Path]::GetFullPath([string]$guard.candidateRoot) -ceq $candidate) {
            throw 'R35 execution package already has a consumed candidate guard; automatic retry is forbidden.'
        }
    }
}
$preflightRoot = Join-Path $packageRootFull ('r35-qac-preflight-' + [string]$package.executionAttemptId)
if (Test-Path -LiteralPath $preflightRoot) { throw 'R35 attempt identity already has a QAC preflight record; a second execution is forbidden.' }
$null = New-Item -ItemType Directory -Path $preflightRoot
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) { throw 'QAC CLI is unavailable; native execution is deferred.' }
$qacStatus = (& $qacCli status 2>&1 | Out-String); $qacStatusCode = $LASTEXITCODE
$qacList = (& $qacCli list 2>&1 | Out-String); $qacListCode = $LASTEXITCODE
$processCensusComplete = $false
$processCount = 0
$qemuProcesses = @()
$censusError = ''
try {
    $processes = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
    $processCount = $processes.Count
    $qemuProcesses = @($processes | Where-Object {
        ([string]$_.Name -match '^qemu-system-.*\.exe$') -or
        ([string]$_.ExecutablePath -match 'qemu-system-.*\.exe$')
    } | ForEach-Object {
        [ordered]@{processId=[int]$_.ProcessId;parentProcessId=[int]$_.ParentProcessId
            name=[string]$_.Name;executablePath=[string]$_.ExecutablePath;commandLine=[string]$_.CommandLine}
    })
    $processCensusComplete = $true
} catch { $censusError = $_.Exception.Message }
Set-Content -LiteralPath (Join-Path $preflightRoot 'qac-status.txt') -Value $qacStatus -Encoding utf8
Set-Content -LiteralPath (Join-Path $preflightRoot 'qac-list.txt') -Value $qacList -Encoding utf8
[ordered]@{complete=$processCensusComplete;processCount=$processCount;qemuProcesses=@($qemuProcesses);error=$censusError} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $preflightRoot 'process-census.json') -Encoding utf8
$qacClear = $qacStatusCode -eq 0 -and $qacListCode -eq 0 -and $processCensusComplete -and
    $qemuProcesses.Count -eq 0 -and $qacStatus -match 'Coordinator:\s+Online' -and
    $qacStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and
    $qacStatus -match 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -and
    $qacList -match 'Reservations:\s+0' -and $qacList -match 'Managed sessions:\s+0'
$preflight = [ordered]@{
    schema='GUIDEXOS_C188R35_QAC_FRESH_PREFLIGHT_V1';result=if ($qacClear) { 'CLEAR' } else { 'NOT_CLEAR' }
    capturedUtc=[DateTime]::UtcNow.ToString('O')
    executionPackageId=[string]$package.executionPackageId;executionAttemptId=[string]$package.executionAttemptId
    executionPackageManifestSha256=$packageManifestSha256
    candidateManifestSha256=[string]$package.candidateManifestSha256
    candidateIntegritySha256=[string]$package.candidateIntegritySha256
    qacStatusExitCode=$qacStatusCode;qacListExitCode=$qacListCode
    qacStatusSha256=(Get-FileHash -LiteralPath (Join-Path $preflightRoot 'qac-status.txt') -Algorithm SHA256).Hash
    qacListSha256=(Get-FileHash -LiteralPath (Join-Path $preflightRoot 'qac-list.txt') -Algorithm SHA256).Hash
    processCensusComplete=$processCensusComplete;processCount=$processCount;qemuProcesses=@($qemuProcesses)
    processCensusError=$censusError
    coordinatorOnline=($qacStatus -match 'Coordinator:\s+Online')
    reservationCountZero=($qacList -match 'Reservations:\s+0')
    managedSessionsZero=($qacStatus -match 'Managed sessions:\s+0' -and $qacList -match 'Managed sessions:\s+0')
    queuedAndBlockedZero=($qacStatus -match 'queued:\s+0; blocked:\s+0')
    exclusive=$qacClear;captureRoot=$preflightRoot
}
$preflightPath = Join-Path $preflightRoot 'qac-preflight.json'
$preflight | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $preflightPath -Encoding utf8
if (-not $qacClear) {
    $deferredPath = Join-Path $packageRootFull ('r35-run-evidence-' + [string]$package.executionAttemptId + '.json')
    [ordered]@{
        schema='GUIDEXOS_C188R35_SETTINGS_LAUNCH_RUN_V1';outcome='SETTINGS_LAUNCH_GUEST_DEFERRED'
        executionPackageId=[string]$package.executionPackageId;executionAttemptId=[string]$package.executionAttemptId
        executionPackageManifestPath=$packageManifestPath;executionPackageManifestSha256=$packageManifestSha256
        candidateRoot=$candidate;candidateManifestSha256=[string]$package.candidateManifestSha256
        candidateIntegritySha256=[string]$package.candidateIntegritySha256
        offlineRehearsalPath=$rehearsalEvidencePath;qacPreflightPath=$preflightPath
        qacPreflight='NOT_CLEAR';nativeExecutionAttempted=$false;bootGuardCreated=$false
        qacInvoked=$false;qemuLaunched=$false;failure='Fresh QAC coordinator/process census was not exclusively clear.'
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $deferredPath -Encoding utf8
    Write-Output "R35_OUTCOME=SETTINGS_LAUNCH_GUEST_DEFERRED PREFLIGHT=$preflightPath EVIDENCE=$deferredPath"
    return
}

$parentRunner = Join-Path $repo 'scripts\dotnet\run-c188r24-initializemodules.ps1'
$pwshNative = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$parentArgs = @('-NoProfile','-File',$parentRunner,
    '-CandidateRoot',$candidate,
    '-ArtifactRoot',$artifactRoot,
    '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,
    '-R25ManifestPath',$r25ManifestPath,
    '-RunIdentityId',[string]$package.executionAttemptId,
    '-ExecutionPackageManifestPath',$packageManifestPath)
$parentOutput = & $pwshNative @parentArgs 2>&1 | Out-String
$parentExit = $LASTEXITCODE
$parentEvidencePath = Join-Path $packageRootFull ('r25-run-evidence-' + [string]$package.executionAttemptId + '.json')
$parentEvidence = if (Test-Path -LiteralPath $parentEvidencePath -PathType Leaf) {
    Get-Content -LiteralPath $parentEvidencePath -Raw | ConvertFrom-Json -ErrorAction Stop
} else { $null }
$nativeExecuted = $null -ne $parentEvidence -and [bool]$parentEvidence.nativeExecutionAttempted
$r12EvidencePath = if ($null -ne $parentEvidence -and $parentEvidence.bootEvidencePath) {
    Join-Path ([string]$parentEvidence.bootEvidencePath) 'r12r2r1-settings-dispatch-evidence.json'
} else { '' }
$r12Evidence = if ($r12EvidencePath -and (Test-Path -LiteralPath $r12EvidencePath -PathType Leaf)) {
    Get-Content -LiteralPath $r12EvidencePath -Raw | ConvertFrom-Json -ErrorAction Stop
} else { $null }
$factoryEvidence = if ($null -ne $r12Evidence) { $r12Evidence.factoryEvidence } else { $null }
$factoryEvents = if ($null -ne $factoryEvidence) { @($factoryEvidence.factoryEvents) } else { @() }
$selector5InvokeEvent = $factoryEvents | Where-Object {
    [string]$_.selector -eq '5' -and [string]$_.stage -eq 'factory-invoke'
} | Select-Object -First 1
$selector5Invoke = $null -ne $selector5InvokeEvent
$selector5InvocationEvents = if ($selector5Invoke) {
    @($factoryEvents | Where-Object {
        [string]$_.selector -eq '5' -and [string]$_.invocation -ceq [string]$selector5InvokeEvent.invocation
    })
} else { @() }
$selector5Stages = @($selector5InvocationEvents | ForEach-Object { [string]$_.stage })
$selector5InvokeIndex = [Array]::IndexOf($selector5Stages,'factory-invoke')
$selector5EntryIndex = -1
for ($i = 0; $i -lt $selector5Stages.Count; $i++) {
    if ($selector5Stages[$i] -in @('type-init-enter','constructor-enter') -and $i -gt $selector5InvokeIndex) {
        $selector5EntryIndex = $i; break
    }
}
$selector5Entry = $selector5EntryIndex -ge 0
$selector5ConstructionIndex = -1
if ($selector5Entry) {
    for ($i = $selector5EntryIndex + 1; $i -lt $selector5Stages.Count; $i++) {
        if ($selector5Stages[$i] -in @('constructor-return','object-construction-result')) {
            $selector5ConstructionIndex = $i; break
        }
    }
}
$selector5Construction = $selector5ConstructionIndex -ge 0
$selector5Return = $selector5Construction -and @($selector5Stages | Select-Object -Skip ($selector5ConstructionIndex + 1) |
    Where-Object { $_ -eq 'factory-return' }).Count -gt 0
$settingsMilestones = [ordered]@{
    M1=[bool]($null -ne $parentEvidence -and [bool]$parentEvidence.milestones.M1)
    M2=[bool]($null -ne $parentEvidence -and [bool]$parentEvidence.milestones.M7)
    M3=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.bootReady)
    M4=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.runnerInputEvidence.pointerTargetVerified)
    M5=[bool]($null -ne $r12Evidence -and [string]$r12Evidence.readinessProfile -ceq [string]$package.readinessProfile -and
        [uint32]$r12Evidence.expectedManagedDescriptorCount -eq [uint32]$package.expectedManagedDescriptorCount -and
        (@($r12Evidence.expectedManagedDescriptorSelectors) -join ',') -ceq (@($package.expectedManagedDescriptorSelectors) -join ','))
    M6=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.pointerTargetEvidence.accepted)
    M7=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.clickEvidence.accepted)
    M8=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.settingsActionInvoked -and [bool]$r12Evidence.settingsActionSucceeded)
    M9=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.settingsAppModelLaunchRequested)
    M10=$selector5Invoke;M11=$selector5Entry;M12=$selector5Construction;M13=$selector5Return
    M14=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.settingsEvidence.accepted -and
        [bool]$r12Evidence.settingsEvidence.settingsRegistryReturnValid)
    M15=[bool]($null -ne $r12Evidence -and [bool]$r12Evidence.settingsEvidence.surfaceVisibleAndActive -and
        [bool]$r12Evidence.settingsEvidence.framePresented)
    M16=[bool]($null -ne $parentEvidence -and [bool]$parentEvidence.qacReleaseVerified -and
        [bool]$parentEvidence.nativeExecutionAttempted -and $null -ne $r12Evidence -and
        -not [bool]$r12Evidence.runtimeRegressionProven)
}
$factorySuccess = $null -ne $factoryEvidence -and
    [string]$factoryEvidence.outcome -eq 'SETTINGS_STARTUP_SUCCESS' -and
    $selector5Return -and [bool]$settingsMilestones.M14 -and [bool]$parentEvidence.qacReleaseVerified
$causeProven = $null -ne $factoryEvidence -and [bool]$factoryEvidence.rootCauseProven
$outcome = if (-not $nativeExecuted) { 'SETTINGS_LAUNCH_GUEST_DEFERRED' }
    elseif ($null -eq $parentEvidence -or -not [bool]$parentEvidence.qacReleaseVerified -or $null -eq $r12Evidence) { 'SETTINGS_LAUNCH_EVIDENCE_INCOMPLETE' }
    elseif ($factorySuccess) { 'REAL_SETTINGS_FACTORY_NATIVE_VERIFIED' }
    elseif ($causeProven) { 'SETTINGS_LAUNCH_CAUSE_PROVEN' }
    elseif ($selector5Entry) { 'REAL_SETTINGS_FACTORY_ENTRY_VERIFIED' }
    elseif ($selector5Invoke) { 'SETTINGS_LAUNCH_DISPATCH_VERIFIED' }
    elseif ([bool]$settingsMilestones.M8) { 'SETTINGS_INPUT_DELIVERY_VERIFIED' }
    else { 'SETTINGS_LAUNCH_EVIDENCE_INCOMPLETE' }
$postRunIntegrityErrors = [System.Collections.Generic.List[string]]::new()
try {
    if ((Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash -ine $packageManifestSha256 -or
        (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ine [string]$package.candidateManifestSha256 -or
        (Get-FileHash -LiteralPath $candidateIntegrityPath -Algorithm SHA256).Hash -ine [string]$package.candidateIntegritySha256) {
        $postRunIntegrityErrors.Add('package-or-candidate-manifest-hash-mismatch')
    }
    foreach ($entry in @($candidateIntegrity.files)) {
        $path = Join-Path $candidate ([string]$entry.path.Replace('/','\'))
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
            $postRunIntegrityErrors.Add("candidate-file-hash-mismatch:$($entry.path)")
        }
    }
    foreach ($component in @($package.runnerComponents)) {
        $sourcePath = Join-Path $repo ([string]$component.path.Replace('/','\'))
        if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ine [string]$component.sha256 -or
            (Get-FileHash -LiteralPath ([string]$component.packagedPath) -Algorithm SHA256).Hash -ine [string]$component.sha256) {
            $postRunIntegrityErrors.Add("runner-component-hash-mismatch:$($component.path)")
        }
    }
} catch { $postRunIntegrityErrors.Add($_.Exception.Message) }
$postRunIntegrityVerified = $postRunIntegrityErrors.Count -eq 0
$finalPath = Join-Path $packageRootFull ('r35-run-evidence-' + [string]$package.executionAttemptId + '.json')
[ordered]@{
    schema='GUIDEXOS_C188R35_SETTINGS_LAUNCH_RUN_V1';outcome=$outcome
    executionPackageId=[string]$package.executionPackageId;executionAttemptId=[string]$package.executionAttemptId
    executionPackageManifestPath=$packageManifestPath;executionPackageManifestSha256=$packageManifestSha256
    candidateRoot=$candidate;candidateManifestSha256=[string]$package.candidateManifestSha256
    candidateIntegritySha256=[string]$package.candidateIntegritySha256
    offlineRehearsalPath=$rehearsalEvidencePath;qacPreflightPath=$preflightPath
    qacPreflight='CLEAR';nativeExecutionAttempted=$nativeExecuted
    bootGuardPath=if ($null -ne $parentEvidence) { [string]$parentEvidence.guardPath } else { '' }
    bootGuardConsumed=$nativeExecuted;qacParentEvidencePath=if ($null -ne $parentEvidence) { $parentEvidencePath } else { '' }
    qacParentEvidence=$parentEvidence;qacParentExitCode=$parentExit;parentRunnerOutput=$parentOutput
    settingsDispatchEvidencePath=$r12EvidencePath;settingsMilestones=$settingsMilestones
    factoryEvidence=$factoryEvidence
    qacReleaseVerified=if ($null -ne $parentEvidence) { [bool]$parentEvidence.qacReleaseVerified } else { $false }
    packageIntegrityRevalidatedAfterRun=$postRunIntegrityVerified;packageIntegrityErrors=@($postRunIntegrityErrors)
} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $finalPath -Encoding utf8
Write-Output "R35_OUTCOME=$outcome PREFLIGHT=$preflightPath EVIDENCE=$finalPath"
if ($parentExit -ne 0 -and $nativeExecuted) { exit $parentExit }
