param([Parameter()][string]$OutputRoot = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') { throw 'C188R11 hosted tests require PowerShell Core.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repo 'out/dotnet/c188r11-managed-state-provenance/hosted/state-provenance'
}
$root = [System.IO.Path]::GetFullPath($OutputRoot)
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r11-managed-state-provenance'))
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $root)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) { throw "OutputRoot must be below $phaseRoot." }
if (Test-Path -LiteralPath $root) { throw "Refusing to overwrite C188R11 hosted evidence: $root" }
$null = New-Item -ItemType Directory -Path $root
$project = Join-Path $repo 'samples/host/ManagedApplicationRegistryC188R11/ManagedApplicationRegistryC188R11.csproj'
$r9Project = Join-Path $repo 'samples/host/ManagedApplicationRegistryC188R9/ManagedApplicationRegistryC188R9.csproj'
$contractsProject = Join-Path $repo 'samples/managed/ManagedUiContracts/GuideXos.ManagedUi.Contracts.csproj'
$counterProject = Join-Path $repo 'samples/managed/C185CounterApp/GuideXos.C185.CounterApp.csproj'
$artifacts = Join-Path $root 'artifacts'
$isolationArgs = @('-p:C188ArtifactIsolation=true','-p:C188BuildScope=hosted',"-p:C188HostOutputRoot=$root/host-output")
$r9Artifacts = Join-Path $root 'r9-artifacts'
$r9IsolationArgs = @('-p:C188ArtifactIsolation=true','-p:C188BuildScope=hosted',"-p:C188HostOutputRoot=$root/r9-host-output")
$restoreLogs = [System.Collections.Generic.List[string]]::new()
foreach ($restoreProject in @($contractsProject,$counterProject,$r9Project)) {
    $restore = (& dotnet restore $restoreProject --artifacts-path $r9Artifacts @r9IsolationArgs 2>&1 | Out-String)
    $restoreCode = $LASTEXITCODE
    $restoreLogs.Add("profile=R9 project=$restoreProject exitCode=$restoreCode`n$restore")
    if ($restoreCode -ne 0) {
        Set-Content -LiteralPath (Join-Path $root 'restore.log') -Value ($restoreLogs -join "`n") -Encoding utf8
        throw "C188R11 project restore failed for $restoreProject ($restoreCode)."
    }
}
foreach ($restoreProject in @($contractsProject,$counterProject,$project)) {
    $restore = (& dotnet restore $restoreProject --artifacts-path $artifacts @isolationArgs 2>&1 | Out-String)
    $restoreCode = $LASTEXITCODE
    $restoreLogs.Add("profile=R11 project=$restoreProject exitCode=$restoreCode`n$restore")
    if ($restoreCode -ne 0) {
        Set-Content -LiteralPath (Join-Path $root 'restore.log') -Value ($restoreLogs -join "`n") -Encoding utf8
        throw "C188R11 project restore failed for $restoreProject ($restoreCode)."
    }
}
Set-Content -LiteralPath (Join-Path $root 'restore.log') -Value ($restoreLogs -join "`n") -Encoding utf8
$independentBuildLogs = [System.Collections.Generic.List[string]]::new()
foreach ($buildProject in @($contractsProject,$counterProject)) {
    $build = (& dotnet build $buildProject --configuration Release --no-restore --artifacts-path $artifacts @isolationArgs 2>&1 | Out-String)
    $buildCode = $LASTEXITCODE
    $independentBuildLogs.Add("project=$buildProject exitCode=$buildCode`n$build")
    if ($buildCode -ne 0) {
        Set-Content -LiteralPath (Join-Path $root 'independent-builds.log') -Value ($independentBuildLogs -join "`n") -Encoding utf8
        throw "C188R11 independent dependency build failed for $buildProject ($buildCode)."
    }
}
Set-Content -LiteralPath (Join-Path $root 'independent-builds.log') -Value ($independentBuildLogs -join "`n") -Encoding utf8
$r9Output = (& dotnet run --project $r9Project --configuration Release --artifacts-path $r9Artifacts @r9IsolationArgs 2>&1 | Out-String)
$r9Code = $LASTEXITCODE
Set-Content -LiteralPath (Join-Path $root 'c188r9-registry.log') -Value $r9Output -Encoding utf8
if ($r9Code -ne 0 -or -not [regex]::IsMatch($r9Output,'(?m)^C188R9-REGISTRY cases=14 firstFailedCase=0 result=PASS\s*$')) {
    throw "C188R9 registry reentry regressions failed ($r9Code). See $(Join-Path $root 'c188r9-registry.log')"
}
$output = (& dotnet run --project $project --configuration Release --no-restore --artifacts-path $artifacts @isolationArgs 2>&1 | Out-String)
$runCode = $LASTEXITCODE
$log = Join-Path $root 'qualification.log'
Set-Content -LiteralPath $log -Value $output -Encoding utf8
$r10 = [regex]::IsMatch($output,'(?m)^C188R10-STATE-REENTRY cases=14 firstFailedCase=0 result=PASS\s*$')
$r11 = [regex]::IsMatch($output,'(?m)^C188R11-STATE-PROVENANCE cases=15 firstFailedCase=0 result=PASS\s*$')
$r12r2 = [regex]::IsMatch($output,'(?m)^C188R12R2-LAUNCH-BOUNDARY cases=8 firstFailedCase=0 result=PASS\s*$')
if ($runCode -ne 0 -or -not $r10 -or -not $r11 -or -not $r12r2) { throw "C188R11/R12R2 hosted provenance regressions failed ($runCode). See $log" }
& (Join-Path $PSScriptRoot 'test-c188r11-runner-predicates.ps1') 2>&1 | Set-Content -LiteralPath (Join-Path $root 'runner-predicates.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'C188R11 runner predicate qualification failed.' }
$outputBounds = (& (Join-Path $PSScriptRoot 'test-c188r11-output-bounds.ps1') 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'output-bounds.log') -Value $outputBounds -Encoding utf8
if (-not [regex]::IsMatch($outputBounds,'(?m)^C188R11-OUTPUT-BOUNDS cases=6 result=PASS\b')) {
    throw 'C188R11 output-bound qualification failed.'
}
$runtimeLifecycle = (& (Join-Path $PSScriptRoot 'test-c188r11-runtime-lifecycle.ps1') 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'runtime-lifecycle.log') -Value $runtimeLifecycle -Encoding utf8
if (-not [regex]::IsMatch($runtimeLifecycle,'(?m)^C188R11-RUNTIME-LIFECYCLE cases=7 result=PASS\b')) {
    throw 'C188R11 runtime lifecycle qualification failed.'
}
$qacOwnership = (& (Join-Path $PSScriptRoot 'test-c188r11r4-qac-ownership-predicates.ps1') 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'qac-ownership-predicates.log') -Value $qacOwnership -Encoding utf8
if (-not [regex]::IsMatch($qacOwnership,'(?m)^C188R11R4-QAC-OWNERSHIP cases=\d+ result=PASS\b')) {
    throw 'C188R11R4 QAC ownership predicate qualification failed.'
}
$qacStreamCapture = (& (Join-Path $PSScriptRoot 'test-c188r11r4-qac-stream-capture.ps1') 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'qac-stream-capture.log') -Value $qacStreamCapture -Encoding utf8
if (-not [regex]::IsMatch($qacStreamCapture,'(?m)^C188R11R4-QAC-STREAM-CAPTURE cases=2 result=PASS\b')) {
    throw 'C188R11R4 live QAC stream capture qualification failed.'
}
$keyboardContracts = (& (Join-Path $PSScriptRoot 'test-c188r11r2-keyboard-contracts.ps1') 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'keyboard-contracts.log') -Value $keyboardContracts -Encoding utf8
if (-not [regex]::IsMatch($keyboardContracts,'(?m)^C188R11R2-KEYBOARD-CONTRACTS cases=\d+ result=PASS\b')) {
    throw 'C188R11R2 keyboard contract qualification failed.'
}
$keyboardRunnerPredicates = (& (Join-Path $PSScriptRoot 'test-c188r11r2-runner-predicates.ps1') 2>&1 | Out-String)
Set-Content -LiteralPath (Join-Path $root 'keyboard-runner-predicates.log') -Value $keyboardRunnerPredicates -Encoding utf8
if (-not [regex]::IsMatch($keyboardRunnerPredicates,'(?m)^C188R11R2-RUNNER-PREDICATES cases=\d+ result=PASS\b')) {
    throw 'C188R11R2 keyboard runner predicate qualification failed.'
}
$manifest = [ordered]@{
    result='PASS';outputRoot=$root;r9Cases=14;priorR10Cases=14;r11Cases=15;r12r2LaunchBoundaryCases=8
    runnerPredicateCases=30;outputBoundCases=6;runtimeLifecycleCases=7
    qacOwnershipPredicateCases=[int]([regex]::Match($qacOwnership,'cases=(\d+)').Groups[1].Value)
    qacStreamCaptureCases=2
    keyboardContractCases=[int]([regex]::Match($keyboardContracts,'cases=(\d+)').Groups[1].Value)
    keyboardRunnerPredicateCases=[int]([regex]::Match($keyboardRunnerPredicates,'cases=(\d+)').Groups[1].Value)
    restoreExitCode=$restoreCode;runExitCode=$runCode
    independentBuildsSha256=(Get-FileHash -LiteralPath (Join-Path $root 'independent-builds.log') -Algorithm SHA256).Hash
    r9QualificationSha256=(Get-FileHash -LiteralPath (Join-Path $root 'c188r9-registry.log') -Algorithm SHA256).Hash
    restoreSha256=(Get-FileHash -LiteralPath (Join-Path $root 'restore.log') -Algorithm SHA256).Hash
    qualificationSha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash
    runnerPredicatesSha256=(Get-FileHash -LiteralPath (Join-Path $root 'runner-predicates.log') -Algorithm SHA256).Hash
    runtimeLifecycleSha256=(Get-FileHash -LiteralPath (Join-Path $root 'runtime-lifecycle.log') -Algorithm SHA256).Hash
    qacOwnershipPredicatesSha256=(Get-FileHash -LiteralPath (Join-Path $root 'qac-ownership-predicates.log') -Algorithm SHA256).Hash
    qacStreamCaptureSha256=(Get-FileHash -LiteralPath (Join-Path $root 'qac-stream-capture.log') -Algorithm SHA256).Hash
    keyboardContractsSha256=(Get-FileHash -LiteralPath (Join-Path $root 'keyboard-contracts.log') -Algorithm SHA256).Hash
    keyboardRunnerPredicatesSha256=(Get-FileHash -LiteralPath (Join-Path $root 'keyboard-runner-predicates.log') -Algorithm SHA256).Hash
}
$manifestPath = Join-Path $root 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188R11_HOSTED=PASS R9_CASES=14 R10_CASES=14 R11_CASES=15 R12R2_LAUNCH_BOUNDARY_CASES=8 RUNNER_PREDICATES=30 RUNTIME_LIFECYCLE=7"
Write-Output "C188R11_HOSTED_MANIFEST=$manifestPath"
