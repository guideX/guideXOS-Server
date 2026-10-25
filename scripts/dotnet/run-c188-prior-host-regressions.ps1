param(
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188 prior hosted regressions require PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoots = @(
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188-external-app-identity')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r4-notes-startup-operation')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r5-notes-proof-invocation')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r6-notes-postrender')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r7-notes-proof-authorization')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r8-notes-input-routing')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r9-registry-input-reentry')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r10-nativeaot-state-reentry')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r11-managed-state-provenance')))
$root = [System.IO.Path]::GetFullPath($OutputRoot)
$allowedRoot = @($phaseRoots | Where-Object {
    $relative = [System.IO.Path]::GetRelativePath($_, $root)
    $relative -ne '.' -and -not $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -and
        -not [System.IO.Path]::IsPathRooted($relative)
} | Select-Object -First 1)
if ($allowedRoot.Count -eq 0) {
    throw "OutputRoot must be a unique directory below one of: $($phaseRoots -join ', ')."
}
if (Test-Path -LiteralPath $root) {
    throw "Refusing to overwrite C188 regression output: $root"
}
$null = New-Item -ItemType Directory -Path $root
$artifacts = Join-Path $root 'managed-artifacts'
$hostOutput = Join-Path $root 'managed-output'
$null = New-Item -ItemType Directory -Path $artifacts, $hostOutput
$commonProperties = @(
    '-p:C188ArtifactIsolation=true',
    '-p:C188BuildScope=hosted-baselines',
    "-p:C188HostOutputRoot=$hostOutput"
)
$dotnet = (Get-Command dotnet.exe -ErrorAction Stop | Select-Object -First 1).Source

function Invoke-C188Captured([string]$Name, [string[]]$CommandArguments, [string]$LogPath, [string]$ExpectedPattern) {
    $output = (& $dotnet @CommandArguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    Set-Content -LiteralPath $LogPath -Value $output -Encoding utf8
    if ($exitCode -ne 0 -or $output -notmatch $ExpectedPattern) {
        throw "C188 prior regression '$Name' failed with exit code $exitCode. See $LogPath"
    }
    return [ordered]@{
        name = $Name
        exitCode = $exitCode
        log = $LogPath
        logSha256 = (Get-FileHash -LiteralPath $LogPath -Algorithm SHA256).Hash
    }
}

$c182Project = Join-Path $repo 'samples/host/ManagedStarterC182/ManagedStarterC182.csproj'
$c182Artifacts = Join-Path $artifacts 'c182'
$c182Build = (& $dotnet build $c182Project --configuration Release --artifacts-path $c182Artifacts @commonProperties 2>&1 | Out-String)
$c182BuildExit = $LASTEXITCODE
$c182BuildLog = Join-Path $root 'c182-build.log'
Set-Content -LiteralPath $c182BuildLog -Value $c182Build -Encoding utf8
if ($c182BuildExit -ne 0) { throw "C182 host build failed with exit code $c182BuildExit. See $c182BuildLog" }
$c182Dll = Get-ChildItem -LiteralPath (Join-Path $c182Artifacts 'bin') -Recurse -File -Filter 'ManagedStarterC182Host.dll' | Select-Object -First 1
if ($null -eq $c182Dll) { throw "ManagedStarterC182Host.dll was not produced under $c182Artifacts" }
$c182Run = Invoke-C188Captured 'C182 starter' @($c182Dll.FullName) `
    (Join-Path $root 'c182-host.log') 'C182-MANAGED-STARTER cases=15 .*result=PASS'

$c187Project = Join-Path $repo 'samples/host/ManagedHostServicesC187/ManagedHostServicesC187.csproj'
$c187Artifacts = Join-Path $artifacts 'c187'
$contractsProject = Join-Path $repo 'samples/managed/ManagedUiContracts/GuideXos.ManagedUi.Contracts.csproj'
$counterProject = Join-Path $repo 'samples/managed/C185CounterApp/GuideXos.C185.CounterApp.csproj'
foreach ($project in @($contractsProject,$counterProject,$c187Project)) {
    $restore = (& $dotnet restore $project --artifacts-path $c187Artifacts @commonProperties 2>&1 | Out-String)
    $restoreExitCode = $LASTEXITCODE
    $restoreLog = Join-Path $root (('c187-restore-' + [System.IO.Path]::GetFileNameWithoutExtension($project) + '.log'))
    Set-Content -LiteralPath $restoreLog -Value $restore -Encoding utf8
    if ($restoreExitCode -ne 0) { throw "C187 dependency restore failed for $project with exit code $restoreExitCode. See $restoreLog" }
}
$c187Build = (& $dotnet build $c187Project --configuration Release --no-restore --artifacts-path $c187Artifacts @commonProperties 2>&1 | Out-String)
$c187BuildExit = $LASTEXITCODE
$c187BuildLog = Join-Path $root 'c187-build.log'
Set-Content -LiteralPath $c187BuildLog -Value $c187Build -Encoding utf8
if ($c187BuildExit -ne 0) { throw "C187 host build failed with exit code $c187BuildExit. See $c187BuildLog" }
$c187Dll = Get-ChildItem -LiteralPath (Join-Path $c187Artifacts 'bin') -Recurse -File -Filter 'ManagedHostServicesC187Host.dll' | Select-Object -First 1
if ($null -eq $c187Dll) { throw "ManagedHostServicesC187Host.dll was not produced under $c187Artifacts" }
$c187Run = Invoke-C188Captured 'C185/C186/C187 hosted suites' @($c187Dll.FullName) `
    (Join-Path $root 'c185-c186-c187-host.log') `
    '(?s)C185-HOST-SERVICES cases=28 .*result=PASS.*C186-HOST-SERVICES cases=15 .*result=PASS.*C187-GUEST-CANDIDATE cases=22 .*result=PASS'

$manifest = [ordered]@{
    result = 'PASS'
    outputRoot = $root
    commonOutputIsolation = 'C188ArtifactIsolation'
    suites = @(
        [ordered]@{ name='C182 starter'; cases=15; buildLog=(Get-FileHash -LiteralPath $c182BuildLog -Algorithm SHA256).Hash; runLog=$c182Run.log; runLogSha256=$c182Run.logSha256 },
        [ordered]@{ name='C185 host services'; cases=28; runLog=$c187Run.log; runLogSha256=$c187Run.logSha256 },
        [ordered]@{ name='C186 lifecycle'; cases=15; runLog=$c187Run.log; runLogSha256=$c187Run.logSha256 },
        [ordered]@{ name='C187 external guest integration'; cases=22; runLog=$c187Run.log; runLogSha256=$c187Run.logSha256 }
    )
}
$manifestPath = Join-Path $root 'prior-host-regressions.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output 'C188_PRIOR_HOST_REGRESSIONS=PASS C182=15 C185=28 C186=15 C187=22'
Write-Output "C188_PRIOR_HOST_MANIFEST=$manifestPath"
