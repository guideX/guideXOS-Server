param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [Parameter()][string]$ArtifactRoot='E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT',
    [Parameter()][ValidateRange(60,300)][int]$MaxRuntimeSeconds=120,
    [Parameter()][string]$R25ManifestPath=''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'R24 QAC runner requires PowerShell Core.' }
. (Join-Path $PSScriptRoot 'c188r24-initializemodules-predicates.ps1')
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$ArtifactRoot = [System.IO.Path]::GetFullPath($ArtifactRoot)
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$artifactPrefix = $ArtifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $candidate.StartsWith($artifactPrefix,[System.StringComparison]::OrdinalIgnoreCase)) {
    throw "R24 candidate must remain below ArtifactRoot: $candidate"
}
$phaseRoot = [System.IO.Path]::GetDirectoryName($candidate)
$isR25 = -not [string]::IsNullOrWhiteSpace($R25ManifestPath)
$runPrefix = if ($isR25) { 'r25' } else { 'r24' }
$runId = [guid]::NewGuid().ToString('N')
$bootRoot = Join-Path $phaseRoot ('qac-guest-' + $runPrefix + '-' + $runId)
$guardRoot = Join-Path $phaseRoot 'one-boot-guards'
$guardPath = Join-Path $guardRoot ('attempt-' + $runPrefix + '-' + $runId + '.json')
$preflightRoot = Join-Path $phaseRoot ('qac-preflight-' + $runPrefix + '-' + $runId)
$evidencePath = Join-Path $phaseRoot ($runPrefix + '-run-evidence-' + $runId + '.json')
$manifestPath = Join-Path $candidate 'candidate-manifest.json'
$integrityPath = Join-Path $candidate 'candidate-integrity.json'
$qacCli = 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$outerRunner = Join-Path $repo 'scripts\dotnet\run-c188r12r1-settings-handoff.ps1'
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$final = [ordered]@{
    schema=if ($isR25) { 'GUIDEXOS_C188R25_FAILFAST_ORIGIN_RUN_V1' } else { 'GUIDEXOS_C188R24_INITIALIZEMODULES_RUN_V1' };runIdentity=$runId
    candidateRoot=$candidate;bootEvidencePath=$bootRoot;guardPath=$guardPath
    outcome='INITIALIZEMODULES_EVIDENCE_BLOCKED';nativeExecutionAttempted=$false
    candidateIntegrityVerified=$false;qacPreflight='not-run';runnerExitCode=$null
    qacReleaseVerified=$false;cpuSnapshotPath=$null;cpuBoundaryEvidence=$null
    r25FirstFailureRecordValid=$false;r25CallerAttribution=$null;r25SnapshotPath=$null;r25OriginAnalysisPath=$null
    serialSha256='';qacReservationId=$null;failure='';milestones=$null
}

try {
    foreach ($required in @($manifestPath,$integrityPath,(Join-Path $candidate 'ramdisk-staging.json'),
            (Join-Path $candidate 'r24-runtime-addresses.json'),(Join-Path $candidate 'r24-hosted-qualification.log'))) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "R24 candidate input is missing: $required" }
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $integrity = Get-Content -LiteralPath $integrityPath -Raw | ConvertFrom-Json
    $addresses = Get-Content -LiteralPath ([string]$manifest.r24RuntimeAddresses) -Raw | ConvertFrom-Json
    if ($manifest.phase -ne 'C188R24_INITIALIZEMODULES_REGISTRATION' -or
        $manifest.result -ne 'PASS_BUILD_READY_FOR_QAC_PREFLIGHT' -or
        $integrity.schema -ne 'GUIDEXOS_C188R24_CANDIDATE_INTEGRITY_V1' -or
        (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash -ne $manifest.candidateIntegritySha256 -or
        (Get-FileHash -LiteralPath ([string]$manifest.r24RuntimeAddresses) -Algorithm SHA256).Hash -ne $manifest.r24RuntimeAddressesSha256) {
        throw 'R24 phase, candidate result, integrity hash, or runtime-address evidence is invalid.'
    }
    $r25Manifest = $null
    if ($isR25) {
        $R25ManifestPath = [System.IO.Path]::GetFullPath($R25ManifestPath)
        $r25Manifest = Get-Content -LiteralPath $R25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
        if ($r25Manifest.schema -cne 'GUIDEXOS_C188R25_CANDIDATE_V1' -or
            [System.IO.Path]::GetFullPath([string]$r25Manifest.candidateRoot) -cne $candidate -or
            [string]$r25Manifest.diagnostics.recordAddress -notmatch '^0x[0-9A-Fa-f]{8,16}$' -or
            [int]$r25Manifest.diagnostics.recordBytes -ne 80 -or
            (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -ine [string]$r25Manifest.candidateManifestSha256 -or
            (Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash -ine [string]$r25Manifest.candidateIntegritySha256 -or
            [System.IO.Path]::GetFullPath([string]$r25Manifest.runtimePackManifest) -cne
                [System.IO.Path]::GetFullPath([string]$manifest.runtimePackManifest) -or
            (Get-FileHash -LiteralPath ([string]$r25Manifest.runtimePackManifest) -Algorithm SHA256).Hash -ine
                [string]$r25Manifest.runtimePackManifestSha256) {
            throw 'R25 manifest does not bind a valid fail-fast record in the requested candidate.'
        }
        $r25Analyzer = Join-Path $repo 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
        $python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
        $r25Qualification = & $python $r25Analyzer '--candidate-root' $candidate 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "R25 linked candidate qualification failed: $r25Qualification" }
        $r25QualificationObject = $r25Qualification | ConvertFrom-Json -ErrorAction Stop
        if ([string]$r25QualificationObject.candidate.recordAddress -ine [string]$r25Manifest.diagnostics.recordAddress) {
            throw 'R25 record address differs from the linked candidate symbol.'
        }
        $final.r25ManifestPath=$R25ManifestPath
        $final.r25RecordAddress=[string]$r25Manifest.diagnostics.recordAddress
    }
    $expectedPaths = @($integrity.files | ForEach-Object { [string]$_.path })
    $actualPaths = @(Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {
        $_.FullName -notin @($manifestPath,$integrityPath)
    } | ForEach-Object { [System.IO.Path]::GetRelativePath($candidate,$_.FullName).Replace('\','/') })
    if ($expectedPaths.Count -ne $actualPaths.Count -or
        @($actualPaths | Where-Object { $_ -notin $expectedPaths }).Count -gt 0 -or
        @($expectedPaths | Where-Object { $_ -notin $actualPaths }).Count -gt 0) {
        throw 'R24 candidate file set differs from its complete integrity inventory.'
    }
    foreach ($entry in @($integrity.files)) {
        $path = Join-Path $candidate ([string]$entry.path.Replace('/','\'))
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne [string]$entry.sha256) {
            throw "R24 candidate integrity mismatch: $($entry.path)"
        }
    }
    $hosted = Get-Content -LiteralPath (Join-Path $candidate 'r24-hosted-qualification.log') -Raw
    if ($hosted -notmatch 'R24_INITIALIZEMODULES_HOSTED_ASSERTIONS=\d+ RESULT=PASS' -or
        $hosted -notmatch 'R24_INITIALIZEMODULES_SNAPSHOT_ASSERTIONS=\d+ RESULT=PASS') {
        throw 'R24 hosted reconstruction or snapshot tests did not pass.'
    }
    foreach ($objectName in @($integrity.preservedModifiedObjects.PSObject.Properties.Name)) {
        $path = Join-Path $repo $objectName
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne [string]$integrity.preservedModifiedObjects.$objectName) {
            throw "Pre-existing modified object hash changed: $objectName"
        }
    }
    $final.candidateIntegrityVerified = $true

    if (Test-Path -LiteralPath $guardRoot -PathType Container) {
        foreach ($existingGuardPath in @(Get-ChildItem -LiteralPath $guardRoot -File -Filter 'attempt-*.json')) {
            try { $existingGuard = Get-Content -LiteralPath $existingGuardPath.FullName -Raw | ConvertFrom-Json -ErrorAction Stop }
            catch { throw "Existing R24 guard is unreadable; refusing a new boot: $($existingGuardPath.FullName)" }
            if ([System.IO.Path]::GetFullPath([string]$existingGuard.candidateRoot) -eq $candidate) {
                throw "This exact R24 candidate already has a one-boot guard; automatic retries are forbidden: $($existingGuardPath.FullName)"
            }
        }
    }

    foreach ($path in @($bootRoot,$guardPath,$preflightRoot,$evidencePath)) {
        if (Test-Path -LiteralPath $path) { throw "R24 run evidence path already exists: $path" }
    }
    if (-not (Test-Path -LiteralPath $qacCli -PathType Leaf)) {
        $final.qacPreflight='unavailable'
        throw 'QAC CLI is unavailable; no guest was launched.'
    }
    $null = New-Item -ItemType Directory -Path $preflightRoot -Force
    $status = (& $qacCli status 2>&1 | Out-String)
    $statusCode = $LASTEXITCODE
    $list = (& $qacCli list 2>&1 | Out-String)
    $listCode = $LASTEXITCODE
    $statusPath = Join-Path $preflightRoot 'status.txt'
    $listPath = Join-Path $preflightRoot 'list.txt'
    Set-Content -LiteralPath $statusPath -Value $status -Encoding utf8
    Set-Content -LiteralPath $listPath -Value $list -Encoding utf8
    $clear = $statusCode -eq 0 -and $listCode -eq 0 -and
        $status -match 'Coordinator:\s+Online' -and
        $status -match 'QEMU processes:\s+0 \(0 foreign\)' -and
        $status -match 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -and
        $list -match 'Reservations:\s+0' -and $list -match 'Managed sessions:\s+0'
    $final.qacPreflight = if ($clear) { 'CLEAR' } else { 'NOT_CLEAR' }
    $preflight = [ordered]@{
        schema='GUIDEXOS_C188R24_QAC_READONLY_PREFLIGHT_V1'
        result=if ($clear) { 'CLEAR' } else { 'NOT_CLEAR' }
        qacStatusExitCode=$statusCode;qacListExitCode=$listCode
        statusSha256=(Get-FileHash -LiteralPath $statusPath -Algorithm SHA256).Hash
        listSha256=(Get-FileHash -LiteralPath $listPath -Algorithm SHA256).Hash
        captureRoot=$preflightRoot
    }
    $preflight | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $preflightRoot 'qac-preflight.json') -Encoding utf8
    if (-not $clear) {
        $final.outcome=if ($isR25) { 'FAILFAST_QUALIFICATION_GUEST_DEFERRED' } else { 'INITIALIZEMODULES_GUEST_DEFERRED' }
        $final.failure='Exclusive QAC preflight was not clear; no reservation or boot was attempted.'
        $final | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $evidencePath -Encoding utf8
        Write-Output "$($runPrefix.ToUpperInvariant())_NATIVE_OUTCOME=$($final.outcome) QAC_PREFLIGHT=NOT_CLEAR NO_BOOT=TRUE"
        Write-Output "$($runPrefix.ToUpperInvariant())_EVIDENCE=$evidencePath"
        return
    }

    $null = New-Item -ItemType Directory -Path $guardRoot -Force
    $guard = [ordered]@{
        schema=if ($isR25) { 'GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1' } else { 'GUIDEXOS_C188R24_ONE_BOOT_GUARD_V1' };candidateRoot=$candidate
        candidateSourceHead=[string]$manifest.sourceHead
        candidateManifestSha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
        candidateIntegritySha256=(Get-FileHash -LiteralPath $integrityPath -Algorithm SHA256).Hash
        kernelSha256=[string]$manifest.kernelSha256;managedElfSha256=[string]$manifest.managedElfSha256
        preflightPath=$preflightRoot;preflightReportSha256=(Get-FileHash -LiteralPath (Join-Path $preflightRoot 'qac-preflight.json') -Algorithm SHA256).Hash
        bootEvidencePath=$bootRoot;attemptNumber=1;maxGuestBoots=1;automaticRetry=$false
        retryAllowed=$false;consumed=$true;consumedUtc=[DateTime]::UtcNow.ToString('O')
    }
    $guard | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $guardPath -Encoding utf8
    if ($isR25) {
        $null = New-Item -ItemType Directory -Path $bootRoot -Force
        [ordered]@{schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1';recordAddress=[string]$r25Manifest.diagnostics.recordAddress;recordBytes=80;candidateManifestSha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash} |
            ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bootRoot 'r25-failfast-capture.json') -Encoding utf8
    }
    $runnerStdoutPath = Join-Path $phaseRoot ($runPrefix + '-runner-' + $runId + '.stdout.log')
    $runnerStderrPath = Join-Path $phaseRoot ($runPrefix + '-runner-' + $runId + '.stderr.log')
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName=$pwsh;$psi.WorkingDirectory=$repo;$psi.UseShellExecute=$false
    $psi.CreateNoWindow=$true;$psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    foreach ($argument in @('-NoProfile','-File',$outerRunner,'-CandidateRoot',$candidate,
            '-ExpectedKernelSha256',[string]$manifest.kernelSha256,'-BootEvidencePath',$bootRoot,
            '-BootEvidenceAuthorizationPath',$guardPath,
            '-MaxRuntimeSeconds',[string]$MaxRuntimeSeconds,'-CaptureCpuOnReadinessTimeout')) {
        [void]$psi.ArgumentList.Add([string]$argument)
    }
    $process = [System.Diagnostics.Process]::Start($psi)
    if (-not $process) { throw 'Could not start the bounded R24 QAC runner.' }
    $stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(660000)) {
        $final.nativeExecutionAttempted=$true
        throw 'R24 outer runner exceeded its bounded 660-second cleanup window; no retry is permitted.'
    }
    $process.WaitForExit()
    $final.nativeExecutionAttempted=$true
    $final.runnerExitCode=$process.ExitCode
    $stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult()
    Set-Content -LiteralPath $runnerStdoutPath -Value $stdout -Encoding utf8
    Set-Content -LiteralPath $runnerStderrPath -Value $stderr -Encoding utf8
    $serialPath=Join-Path $bootRoot 'serial.log'
    $snapshotPath=Join-Path $bootRoot 'cpu-timeout-snapshot.json'
    $registerPath=Join-Path $bootRoot 'cpu-timeout-registers.txt'
    $serial=if (Test-Path -LiteralPath $serialPath) { Get-Content -LiteralPath $serialPath -Raw } else { '' }
    $final.serialSha256=if (Test-Path -LiteralPath $serialPath) { (Get-FileHash -LiteralPath $serialPath -Algorithm SHA256).Hash } else { '' }
    $final.cpuSnapshotPath=if (Test-Path -LiteralPath $snapshotPath) { $snapshotPath } else { $null }
    $cpuEvidence=$null
    if (Test-Path -LiteralPath $registerPath) {
        $registerText=Get-Content -LiteralPath $registerPath -Raw
        $cpuEvidence=Get-C188R24CpuBoundaryEvidence $registerText $addresses
    }
    $final.cpuBoundaryEvidence=$cpuEvidence

    $afterStatus=(& $qacCli status 2>&1 | Out-String)
    $afterStatusCode=$LASTEXITCODE
    $afterList=(& $qacCli list 2>&1 | Out-String)
    $afterListCode=$LASTEXITCODE
    $afterStatusPath=Join-Path $bootRoot 'qac-after-r24.status.txt'
    $afterListPath=Join-Path $bootRoot 'qac-after-r24.list.txt'
    Set-Content -LiteralPath $afterStatusPath -Value $afterStatus -Encoding utf8
    Set-Content -LiteralPath $afterListPath -Value $afterList -Encoding utf8
    $qacStderrPath=Join-Path $bootRoot 'qac.stderr.log'
    $qacStderr=if (Test-Path -LiteralPath $qacStderrPath) { Get-Content -LiteralPath $qacStderrPath -Raw } else { '' }
    $request=[regex]::Match($qacStderr,'QAC reservation requested: id=([0-9a-f]{32})')
    $released=[regex]::Match($qacStderr,'QAC reservation released: id=([0-9a-f]{32})')
    $childExit=[regex]::Match($qacStderr,'QAC QEMU exited: reservation=([0-9a-f]{32}) pid=(\d+) exitCode=(\d+)')
    $qemuExitPath=Join-Path $bootRoot 'qac-exit-code.txt'
    $qemuExit=if (Test-Path -LiteralPath $qemuExitPath) { [int](Get-Content -LiteralPath $qemuExitPath -Raw) } else { -1 }
    $qacRelease=$afterStatusCode -eq 0 -and $afterListCode -eq 0 -and
        $afterStatus -match 'QEMU processes:\s+0 \(0 foreign\)' -and
        $afterStatus -match 'Managed sessions:\s+0; queued:\s+0; blocked:\s+0' -and
        $afterList -match 'Reservations:\s+0' -and $afterList -match 'Managed sessions:\s+0' -and
        $request.Success -and $released.Success -and $childExit.Success -and
        $request.Groups[1].Value -eq $released.Groups[1].Value -and
        $request.Groups[1].Value -eq $childExit.Groups[1].Value -and
        [int]$childExit.Groups[3].Value -eq 0 -and $qemuExit -eq 0
    $final.qacReleaseVerified=$qacRelease
    if ($request.Success) { $final.qacReservationId=$request.Groups[1].Value }

    $r12Path=Join-Path $bootRoot 'r12r2r1-settings-dispatch-evidence.json'
    $r12Evidence=if (Test-Path -LiteralPath $r12Path) {
        try { Get-Content -LiteralPath $r12Path -Raw | ConvertFrom-Json -ErrorAction Stop } catch { $null }
    } else { $null }
    $factoryVerified=$null -ne $r12Evidence -and [string]$r12Evidence.factoryEvidence.outcome -eq 'SETTINGS_STARTUP_SUCCESS'
    $initializeReturn=($serial -match '(?m)^\[C102-STARTUP\] stage=C188B005\s*$') -or
        ($null -ne $cpuEvidence -and [bool]$cpuEvidence.initializerReturned)
    $m1=($serial -match '(?m)^\[C102-STARTUP\] stage=C188B006\s*$') -and
        ($serial -match '(?m)^\[C102-STARTUP\] stage=C188B007\s*$')
    $m2=$null -ne $cpuEvidence -and [bool]$cpuEvidence.initializerEntered -or $initializeReturn
    $m3=$null -ne $cpuEvidence -and [bool]$cpuEvidence.rhpRegisterEntered -or $initializeReturn
    $m4=$null -ne $cpuEvidence -and [bool]$cpuEvidence.registrationSuccessObserved
    $m5=$null -ne $cpuEvidence -and [bool]$cpuEvidence.registrationReturned -or $initializeReturn
    $m6=$null -ne $cpuEvidence -and [string]$cpuEvidence.classification -eq 'type-manager-helper-current' -or $initializeReturn
    $m7=$initializeReturn
    $m8=$serial -match '(?m)^\[C102-STARTUP\] stage=C188B002\s*$'
    $m9=$serial -match 'C188R21-FACTORY .*stage=factory-invoke'
    $final.milestones=[ordered]@{
        M1=$m1;M2=$m2;M3=$m3;M4=$m4;M5=$m5;M6=$m6;M7=$m7;M8=$m8;M9=$m9
        M10=[bool]($null -ne $r12Evidence -and [string]$r12Evidence.factoryEvidence.outcome -eq 'SETTINGS_STARTUP_SUCCESS')
        M11=[bool]($null -ne $r12Evidence -and [string]$r12Evidence.factoryEvidence.captureState -eq 'complete')
        M12=$qacRelease
    }
    if ($isR25) {
        $r25SnapshotPath = Join-Path $bootRoot 'r25-failfast-origin-fatal-marker-snapshot.json'
        if (-not (Test-Path -LiteralPath $r25SnapshotPath -PathType Leaf)) {
            $r25SnapshotPath = Join-Path $bootRoot 'r25-failfast-origin-readiness-timeout-snapshot.json'
        }
        $final.r25SnapshotPath=if (Test-Path -LiteralPath $r25SnapshotPath -PathType Leaf) { $r25SnapshotPath } else { $null }
        if ($null -ne $final.r25SnapshotPath) {
            $r25Snapshot = Get-Content -LiteralPath $r25SnapshotPath -Raw | ConvertFrom-Json -ErrorAction Stop
            $r25Analyzer = Join-Path $repo 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
            $python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
            $r25AnalysisText = & $python $r25Analyzer '--candidate-root' $candidate '--snapshot' $r25SnapshotPath 2>&1 | Out-String
            if ($LASTEXITCODE -eq 0) {
                $r25AnalysisPath = Join-Path $bootRoot 'r25-origin-analysis.json'
                Set-Content -LiteralPath $r25AnalysisPath -Value $r25AnalysisText -Encoding utf8
                $r25Analysis = $r25AnalysisText | ConvertFrom-Json -ErrorAction Stop
                $final.r25OriginAnalysisPath=$r25AnalysisPath
                $final.r25FirstFailureRecordValid=[bool]$r25Analysis.recordValid
                $callerAttribution = if ($r25Analysis.PSObject.Properties['caller']) { $r25Analysis.caller } else { $null }
                $final.r25CallerAttribution=$callerAttribution
                if ([bool]$r25Analysis.recordValid -and $null -ne $callerAttribution -and [bool]$callerAttribution.resolved) {
                    $final.failure='The first fatal boundary and its enclosing linked function were captured; the underlying violated contract remains unproven.'
                } elseif ([bool]$r25Analysis.recordValid) {
                    $final.outcome='FAILFAST_EVIDENCE_BLOCKED'
                    $final.failure='The first-failure record was valid, but its capture return PC did not resolve to the linked capture call.'
                }
            } else {
                $final.r25AnalysisError=$r25AnalysisText
            }
        }
    }
    if ($isR25 -and $null -ne $final.r25SnapshotPath) {
        $r25Snapshot = Get-Content -LiteralPath $final.r25SnapshotPath -Raw | ConvertFrom-Json -ErrorAction Stop
        $triggerName = [string]$r25Snapshot.trigger
        $r25CaptureFiles = @(
            $final.r25SnapshotPath,
            (Join-Path $bootRoot "r25-failfast-origin-$triggerName-registers.txt"),
            (Join-Path $bootRoot "r25-failfast-origin-$triggerName-stack.txt"),
            (Join-Path $bootRoot 'r25-origin-analysis.json'),
            (Join-Path $bootRoot 'serial.log'),
            (Join-Path $bootRoot 'qmp.log'),
            (Join-Path $bootRoot 'qac.stdout.log'),
            (Join-Path $bootRoot 'qac.stderr.log'),
            (Join-Path $bootRoot 'qac-exit-code.txt'))
        $r25FinalFiles = @($r25CaptureFiles | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | ForEach-Object {
            [ordered]@{
                path=[System.IO.Path]::GetRelativePath($bootRoot,$_)
                lengthBytes=(Get-Item -LiteralPath $_).Length
                sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToUpperInvariant()
            }
        })
        $r25HashManifestPath = Join-Path $bootRoot 'r25-final-artifact-hashes.json'
        if (Test-Path -LiteralPath $r25HashManifestPath) { throw 'Refusing to overwrite post-QEMU R25 artifact hash manifest.' }
        [ordered]@{
            schema='GUIDEXOS_C188R25_POST_QEMU_ARTIFACT_HASHES_V1'
            generatedUtc=[DateTime]::UtcNow.ToString('O')
            candidateRoot=$candidate
            qemuExitCode=$qemuExit
            qacReleaseVerified=[bool]$qacRelease
            files=$r25FinalFiles
        } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $r25HashManifestPath -Encoding utf8
        $final.r25FinalHashManifestPath=$r25HashManifestPath
        $final.r25FinalHashManifestSha256=(Get-FileHash -LiteralPath $r25HashManifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
    }
    if (-not $qacRelease) {
        $final.outcome=if ($isR25) { 'FAILFAST_EVIDENCE_BLOCKED' } else { 'INITIALIZEMODULES_EVIDENCE_BLOCKED' }
        $final.failure='Post-run QAC shutdown/release could not be verified; inspect the preserved direct-child and QAC evidence.'
    } elseif ($isR25 -and [bool]$final.r25FirstFailureRecordValid -and
            $null -ne $final.r25CallerAttribution -and [bool]$final.r25CallerAttribution.resolved) {
        $final.outcome='FAILFAST_ORIGIN_ATTRIBUTED'
    } elseif ($isR25 -and [bool]$final.r25FirstFailureRecordValid) {
        $final.outcome='FAILFAST_ORIGIN_CALLER_UNRESOLVED'
    } elseif ($factoryVerified) {
        $final.outcome='REAL_SETTINGS_FACTORY_NATIVE_VERIFIED'
    } elseif ($m7) {
        $final.outcome='INITIALIZEMODULES_RETURN_VERIFIED'
    } elseif ($m4 -and $m5) {
        $final.outcome='OS_MODULE_REGISTRATION_VERIFIED'
    } elseif ($null -ne $cpuEvidence -and $cpuEvidence.classification -eq 'rhp-register-allocation-failure-return-path') {
        $final.outcome='MODULE_REGISTRATION_CAUSE_PROVEN'
        $final.failure='RIP is in the linked RhpRegisterOsModule null-allocation return path.'
    } elseif ($null -ne $cpuEvidence -and $cpuEvidence.initializerEntered) {
        $final.outcome='INITIALIZEMODULES_BOUNDARY_ADVANCED'
    } else {
        $final.outcome='INITIALIZEMODULES_EVIDENCE_BLOCKED'
        $final.failure='No initializer RIP or return marker was captured; the last serial marker is not treated as the last executed instruction.'
    }
    if ($isR25 -and $final.outcome -in @('INITIALIZEMODULES_EVIDENCE_BLOCKED','INITIALIZEMODULES_BOUNDARY_ADVANCED')) {
        $final.outcome='FAILFAST_EVIDENCE_BLOCKED'
        if ([string]::IsNullOrWhiteSpace([string]$final.failure)) {
            $final.failure='The boot produced no valid first-failure record and no accepted initialization handoff.'
        }
    }
    if ($process.ExitCode -ne 0 -and [string]::IsNullOrWhiteSpace($final.failure)) {
        $final.failure='Bounded Settings runner exited nonzero after preserving its native/QAC evidence.'
    }
} catch {
    $final.failure=$_.Exception.Message
    if ($final.qacPreflight -eq 'NOT_CLEAR' -or $final.qacPreflight -eq 'unavailable') {
        $final.outcome=if ($isR25) { 'FAILFAST_QUALIFICATION_GUEST_DEFERRED' } else { 'INITIALIZEMODULES_GUEST_DEFERRED' }
    }
}
if (-not (Test-Path -LiteralPath (Split-Path -Parent $evidencePath))) {
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $evidencePath) -Force
}
$final | ConvertTo-Json -Depth 18 | Set-Content -LiteralPath $evidencePath -Encoding utf8
Write-Output "$($runPrefix.ToUpperInvariant())_NATIVE_OUTCOME=$($final.outcome) QAC_RELEASE=$($final.qacReleaseVerified) SNAPSHOT=$($final.cpuSnapshotPath)"
Write-Output "$($runPrefix.ToUpperInvariant())_EVIDENCE=$evidencePath"
if ($final.outcome -in @('INITIALIZEMODULES_EVIDENCE_BLOCKED','MODULE_REGISTRATION_CAUSE_PROVEN','FAILFAST_EVIDENCE_BLOCKED')) {
    throw "$($runPrefix.ToUpperInvariant()) ended as $($final.outcome): $($final.failure)"
}
