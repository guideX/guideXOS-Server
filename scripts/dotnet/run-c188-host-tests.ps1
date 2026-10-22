param(
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188 hosted tests require PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188-external-app-identity'))
$root = [System.IO.Path]::GetFullPath($OutputRoot)
$relative = [System.IO.Path]::GetRelativePath($phaseRoot, $root)
if ($relative -eq '.' -or $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
    [System.IO.Path]::IsPathRooted($relative)) {
    throw "OutputRoot must be a unique directory below $phaseRoot."
}
if (Test-Path -LiteralPath $root) {
    throw "Refusing to overwrite C188 hosted test output: $root"
}
$null = New-Item -ItemType Directory -Path $root
$artifacts = Join-Path $root 'managed-artifacts'
$hostOutput = Join-Path $root 'managed-output'
$hostProject = Join-Path $repo 'samples/host/ManagedHostServicesC188/ManagedHostServicesC188.csproj'
$contractsProject = Join-Path $repo 'samples/managed/ManagedUiContracts/GuideXos.ManagedUi.Contracts.csproj'
$counterProject = Join-Path $repo 'samples/managed/C185CounterApp/GuideXos.C185.CounterApp.csproj'
$isolationArgs = @(
    "-p:C188ArtifactIsolation=true",
    "-p:C188BuildScope=hosted",
    "-p:C188HostOutputRoot=$hostOutput"
)

$restoreLogs = [System.Collections.Generic.List[string]]::new()
foreach ($project in @($contractsProject,$counterProject,$hostProject)) {
    $restore = (& dotnet restore $project --artifacts-path $artifacts @isolationArgs 2>&1 | Out-String)
    $restoreExitCode = $LASTEXITCODE
    $restoreLogs.Add("project=$project exitCode=$restoreExitCode`n$restore")
    if ($restoreExitCode -ne 0) {
        Set-Content -LiteralPath (Join-Path $root 'restore.log') -Value ($restoreLogs -join "`n") -Encoding utf8
        throw "C188 project restore failed for $project with exit code $restoreExitCode."
    }
}
Set-Content -LiteralPath (Join-Path $root 'restore.log') -Value ($restoreLogs -join "`n") -Encoding utf8

$independentBuildLogs = [System.Collections.Generic.List[string]]::new()
foreach ($project in @($contractsProject,$counterProject)) {
    $build = (& dotnet build $project --configuration Release --no-restore `
        --artifacts-path $artifacts @isolationArgs 2>&1 | Out-String)
    $buildExitCode = $LASTEXITCODE
    $independentBuildLogs.Add("project=$project exitCode=$buildExitCode`n$build")
    if ($buildExitCode -ne 0) {
        Set-Content -LiteralPath (Join-Path $root 'independent-builds.log') `
            -Value ($independentBuildLogs -join "`n") -Encoding utf8
        throw "Independent C188 dependency build failed for $project with exit code $buildExitCode."
    }
}
Set-Content -LiteralPath (Join-Path $root 'independent-builds.log') `
    -Value ($independentBuildLogs -join "`n") -Encoding utf8

$output = (& dotnet run --project $hostProject --configuration Release --no-restore `
    --artifacts-path $artifacts @isolationArgs 2>&1 | Out-String)
$exitCode = $LASTEXITCODE
$logPath = Join-Path $root 'qualification.log'
Set-Content -LiteralPath $logPath -Value $output -Encoding utf8
$match = [regex]::Match($output,
    '(?m)^C188-EXTERNAL-IDENTITY cases=25 firstFailedCase=0 resizeStressAllocatedBytes=0 detail= identityStress=1000 validResizes=500 invalidGeometry=250 staleGeneration=250 lifecycles=100 result=PASS\s*$')
if ($exitCode -ne 0 -or -not $match.Success) {
    throw "C188 hosted test suite failed with exit code $exitCode. See $logPath"
}
$manifest = [ordered]@{
    result='PASS'; outputRoot=$root; exitCode=$exitCode
    focusedCases=25; identityStressOperations=1000
    validResizeOperations=500; invalidGeometryRequests=250
    staleGenerationAttempts=250; launchCloseLifecycles=100
    resizeHotPathAllocatedBytes=0
    qualificationLogSha256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash
    restoreLogSha256=(Get-FileHash -LiteralPath (Join-Path $root 'restore.log') -Algorithm SHA256).Hash
    independentBuildLogSha256=(Get-FileHash -LiteralPath (Join-Path $root 'independent-builds.log') -Algorithm SHA256).Hash
}
$manifestPath = Join-Path $root 'c188-host-test-manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "C188_HOST_TESTS=PASS CASES=25 IDENTITY=1000 RESIZES=500 INVALID=250 STALE=250 LIFECYCLES=100 ALLOCATIONS=0"
Write-Output "C188_HOST_TEST_MANIFEST=$manifestPath"
