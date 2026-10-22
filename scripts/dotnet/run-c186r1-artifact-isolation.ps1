[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$isolationRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet\c186r1-artifact-isolation'))
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$evidenceRoot = Join-Path $isolationRoot "evidence\run-$runId"
$beforeRoot = Join-Path $evidenceRoot 'protected-before'
$afterRoot = Join-Path $evidenceRoot 'protected-after'
$propsPath = Join-Path $isolationRoot 'msbuild\Directory.Build.props'
$hostBin = Join-Path $isolationRoot 'nativeaot\build\host-bin'
$hostObj = Join-Path $isolationRoot 'nativeaot\build\host-obj'
$buildTempRoot = Join-Path $isolationRoot 'temp'
$expectedRamdiskSha256 = 'AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8'
$script:failures = [System.Collections.Generic.List[string]]::new()
$script:steps = [System.Collections.Generic.List[object]]::new()

$protectedTrees = [ordered]@{
    'c185-managed-host-services' = 'out\dotnet\c185-managed-host-services'
    'c186-managed-host-contract' = 'out\dotnet\c186-managed-host-contract'
    'c182-managed-application-authoring' = 'out\dotnet\c182-managed-application-authoring'
    'c181-managed-application-residency' = 'out\dotnet\c181-managed-application-residency'
    'samples-host-c185-bin' = 'samples\host\ManagedHostServicesC185\bin'
    'samples-host-c185-obj' = 'samples\host\ManagedHostServicesC185\obj'
    'samples-host-c182-obj' = 'samples\host\ManagedStarterC182\obj'
    'samples-host-regression-obj' = 'samples\host\ManagedUiRegression\obj'
    'samples-counterapp-bin' = 'samples\managed\C185CounterApp\bin'
    'samples-counterapp-obj' = 'samples\managed\C185CounterApp\obj'
    'samples-c182-smoke' = 'samples\managed\HostLogProof\c182-host-smoke'
    'samples-contracts-bin' = 'samples\managed\ManagedUiContracts\bin'
    'samples-contracts-obj' = 'samples\managed\ManagedUiContracts\obj'
}

function Get-TreeInventory([string]$RootPath) {
    $fullRoot = [System.IO.Path]::GetFullPath($RootPath)
    if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) {
        throw "Protected directory is missing: $fullRoot"
    }
    $files = @(Get-ChildItem -LiteralPath $fullRoot -Recurse -File -Force | Sort-Object FullName)
    $records = foreach ($file in $files) {
        [pscustomobject]@{
            path = $file.FullName.Substring($fullRoot.TrimEnd('\', '/').Length + 1).Replace('\', '/')
            bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
        }
    }
    return [pscustomobject]@{
        root = $fullRoot
        capturedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        fileCount = $files.Count
        totalBytes = [long](($files | Measure-Object -Property Length -Sum).Sum)
        files = @($records)
    }
}

function Save-Json([object]$Value, [string]$Path) {
    $json = $Value | ConvertTo-Json -Depth 8
    Set-Content -LiteralPath $Path -Value $json -Encoding utf8
}

function Invoke-CapturedStep(
    [string]$Name,
    [string]$Executable,
    [string[]]$Arguments,
    [string]$LogPath,
    [string]$ExpectedPattern = ''
) {
    $outputText = ''
    $exitCode = 1
    try {
        $output = @(& $Executable @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
        $outputText = $output | Out-String
    } catch {
        $outputText = ($_ | Out-String)
        $exitCode = 1
    }
    Set-Content -LiteralPath $LogPath -Value $outputText -Encoding utf8
    $passed = $exitCode -eq 0
    if ($passed -and -not [string]::IsNullOrWhiteSpace($ExpectedPattern)) {
        $passed = $outputText -match $ExpectedPattern
    }
    $status = if ($passed) { 'PASS' } else { 'FAIL' }
    $step = [pscustomobject]@{ name = $Name; status = $status; exitCode = $exitCode; log = $LogPath }
    $script:steps.Add($step)
    Write-Host "C186R1 step=$Name status=$status exitCode=$exitCode"
    if (-not $passed) {
        $script:failures.Add("$Name failed with exit code $exitCode; see $LogPath")
    }
    return [pscustomobject]@{ status = $status; exitCode = $exitCode; output = $outputText; step = $step }
}

function Compare-TreeInventory([object]$Before, [object]$After) {
    $beforeByPath = @{}
    foreach ($record in $Before.files) { $beforeByPath[$record.path] = $record }
    $afterByPath = @{}
    foreach ($record in $After.files) { $afterByPath[$record.path] = $record }
    $added = @($After.files | Where-Object { -not $beforeByPath.ContainsKey($_.path) } | ForEach-Object { $_.path })
    $removed = @($Before.files | Where-Object { -not $afterByPath.ContainsKey($_.path) } | ForEach-Object { $_.path })
    $changed = @(
        foreach ($record in $After.files) {
            if ($beforeByPath.ContainsKey($record.path)) {
                $prior = $beforeByPath[$record.path]
                if ($prior.bytes -ne $record.bytes -or $prior.sha256 -cne $record.sha256) { $record.path }
            }
        }
    )
    return [pscustomobject]@{
        root = $Before.root
        beforeFiles = $Before.fileCount
        afterFiles = $After.fileCount
        added = $added
        removed = $removed
        changed = $changed
        status = if ($added.Count -eq 0 -and $removed.Count -eq 0 -and $changed.Count -eq 0) { 'PASS' } else { 'FAIL' }
    }
}

function ConvertTo-XmlText([string]$Value) {
    return [System.Security.SecurityElement]::Escape($Value)
}

if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'Directory.Build.props') -PathType Leaf)) {
    throw "Repository root does not contain Directory.Build.props: $RepoRoot"
}
$isolationPrefix = $isolationRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
$expectedIsolationRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet\c186r1-artifact-isolation'))
if (-not $isolationRoot.Equals($expectedIsolationRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected C186R1 output root: $isolationRoot"
}
foreach ($root in @($propsPath, $hostBin, $hostObj)) {
    $full = [System.IO.Path]::GetFullPath($root)
    if (-not $full.StartsWith($isolationPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "C186R1 destination escaped its dedicated output tree: $full"
    }
}
$guardPaths = @($isolationRoot, (Split-Path -Parent $propsPath), $hostBin, $hostObj, $buildTempRoot)
foreach ($guardPath in $guardPaths) {
    $walk = [System.IO.DirectoryInfo][System.IO.Path]::GetFullPath($guardPath)
    while ($null -ne $walk) {
        if (Test-Path -LiteralPath $walk.FullName) {
            $item = Get-Item -LiteralPath $walk.FullName -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "C186R1 output tree traverses a reparse point: $($walk.FullName)"
            }
        }
        if ($walk.FullName.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
        $walk = $walk.Parent
    }
}
if (Test-Path -LiteralPath $propsPath -PathType Leaf) {
    $existingProps = Get-Item -LiteralPath $propsPath -Force
    if (($existingProps.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "C186R1 MSBuild props file is a reparse point: $propsPath"
    }
}

New-Item -ItemType Directory -Force -Path $evidenceRoot, $beforeRoot, $afterRoot, (Split-Path -Parent $propsPath), $hostBin, $hostObj, $buildTempRoot | Out-Null
$env:TEMP = $buildTempRoot
$env:TMP = $buildTempRoot
$beforeStatus = @(& git -C $RepoRoot status --short --branch 2>&1) | Out-String
Set-Content -LiteralPath (Join-Path $evidenceRoot 'git-status-before.txt') -Value $beforeStatus -Encoding utf8
$ramdiskPath = Join-Path $RepoRoot 'ESP\ramdisk.img'
$ramdiskBefore = if (Test-Path -LiteralPath $ramdiskPath -PathType Leaf) {
    (Get-FileHash -LiteralPath $ramdiskPath -Algorithm SHA256).Hash.ToUpperInvariant()
} else { '' }
if ($ramdiskBefore -cne $expectedRamdiskSha256) {
    throw "Protected ESP/ramdisk.img hash does not match its required identity: $ramdiskBefore"
}

$beforeInventories = @{}
foreach ($entry in $protectedTrees.GetEnumerator()) {
    $directory = Join-Path $RepoRoot $entry.Value
    $inventory = Get-TreeInventory $directory
    $beforeInventories[$entry.Key] = $inventory
    $inventoryPath = Join-Path $beforeRoot ($entry.Key + '.json')
    Save-Json $inventory $inventoryPath
}
$beforeSummary = foreach ($entry in $protectedTrees.GetEnumerator()) {
    $inventory = $beforeInventories[$entry.Key]
    [pscustomobject]@{
        id = $entry.Key
        path = $entry.Value
        fileCount = $inventory.fileCount
        totalBytes = $inventory.totalBytes
        manifest = (Join-Path $beforeRoot ($entry.Key + '.json'))
        manifestSha256 = (Get-FileHash -LiteralPath (Join-Path $beforeRoot ($entry.Key + '.json')) -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}
Save-Json ([pscustomobject]@{ phase = 'C186R1-protected-before'; trees = @($beforeSummary) }) (Join-Path $evidenceRoot 'protected-before.json')

$c185Root = Join-Path $RepoRoot $protectedTrees['c185-managed-host-services']
$impactFiles = @()
foreach ($relativeRoot in @('host-bin\release_win-x64', 'host-obj\release_win-x64')) {
    $subtree = Join-Path $c185Root $relativeRoot
    if (-not (Test-Path -LiteralPath $subtree -PathType Container)) {
        throw "Expected output subtree from the initial C186 routing failure is missing: $subtree"
    }
    $impactFiles += @(Get-ChildItem -LiteralPath $subtree -Recurse -File -Force)
}
$historicalHashes = @{}
$c186NativeRoot = Join-Path $RepoRoot 'out\dotnet\c186-managed-host-contract\nativeaot'
$counterpartRoots = @{
    'host-bin\release_win-x64' = Join-Path $c186NativeRoot 'isolated-host-bin\release_win-x64'
    'host-obj\release_win-x64' = Join-Path $c186NativeRoot 'isolated-host-obj\release_win-x64'
}
$c185Report = Join-Path $RepoRoot 'docs\dotnet\NATIVEAOT_C185_MANAGED_HOST_SERVICE_BOUNDARY.md'
foreach ($line in Get-Content -LiteralPath $c185Report) {
    if ($line -match '^\|\s*`([^`]+)`\s*\|\s*[\d,]+\s*B\s*\|\s*`([0-9A-Fa-f]{64})`\s*\|') {
        $historicalHashes[$Matches[1]] = $Matches[2].ToUpperInvariant()
    }
}
$impactRows = foreach ($file in $impactFiles | Sort-Object FullName) {
    $relativeToRepo = $file.FullName.Substring($RepoRoot.TrimEnd('\', '/').Length + 1).Replace('\', '/')
    $relativeUnderAffectedRoot = ''
    $counterpartRoot = ''
    foreach ($mapping in $counterpartRoots.GetEnumerator()) {
        $affectedPrefix = Join-Path $c185Root $mapping.Key
        $affectedPrefixWithSeparator = [System.IO.Path]::GetFullPath($affectedPrefix).TrimEnd('\', '/') + '\'
        if ($file.FullName.StartsWith($affectedPrefixWithSeparator, [StringComparison]::OrdinalIgnoreCase)) {
            $relativeUnderAffectedRoot = $file.FullName.Substring($affectedPrefixWithSeparator.Length)
            $counterpartRoot = $mapping.Value
            break
        }
    }
    $counterpartPath = if ($counterpartRoot) { Join-Path $counterpartRoot $relativeUnderAffectedRoot } else { '' }
    $currentHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
    $counterpartHash = if ($counterpartPath -and (Test-Path -LiteralPath $counterpartPath -PathType Leaf)) {
        (Get-FileHash -LiteralPath $counterpartPath -Algorithm SHA256).Hash.ToUpperInvariant()
    } else { '' }
    $counterpartRelative = if ($counterpartPath -and $counterpartHash) {
        $counterpartPath.Substring($RepoRoot.TrimEnd('\', '/').Length + 1).Replace('\', '/')
    } else { '' }
    $purpose = if ($relativeToRepo -match '/host-obj/') {
        if ($relativeToRepo -match '/native/') { 'NativeAOT compiler object, linker response, or native definition intermediate' }
        else { 'MSBuild intermediate, generated source, reference assembly, or cache' }
    } elseif ($relativeToRepo -match '/native/HostLogProof\.(exe|pdb)$') {
        'NativeAOT linked PE and matching debug symbols copied into the project output tree'
    } elseif ($relativeToRepo -match '/HostLogProof\.(dll|pdb)$') {
        'HostLogProof managed compilation output and debug symbols'
    } elseif ($relativeToRepo -match '/(GuideXos\.|HostLogProof\.).*\.(dll|pdb)$') {
        'Managed dependency output copied beside HostLogProof'
    } else {
        'HostLogProof publish dependency, runtime component, or apphost output'
    }
    $historical = if ($historicalHashes.ContainsKey($relativeToRepo)) { $historicalHashes[$relativeToRepo] } else { '' }
    [pscustomobject]@{
        Path = $relativeToRepo
        Purpose = $purpose
        CurrentSHA256 = $currentHash
        HistoricalSHA256 = $historical
        ComparisonResult = if ($historical) { if ($historical -ceq $currentHash) { 'MATCH' } else { 'MISMATCH' } } else { 'historical contents unverified' }
        RetainedC186Counterpart = $counterpartRelative
        CounterpartSHA256 = $counterpartHash
        CounterpartComparison = if (-not $counterpartHash) { 'no corresponding retained copy' } elseif ($counterpartHash -ceq $currentHash) { 'same current hash' } else { 'different current hash' }
        Confidence = 'High that this current path is within the first C186 NativeAOT output roots; prior existence and bytes are unrecorded.'
        Limitation = 'The first C186 run has no pre-run inventory for this exact path; current SHA-256 cannot establish its original bytes.'
    }
}
$impactPath = Join-Path $evidenceRoot 'c185-artifact-impact-matrix.csv'
$impactRows | Export-Csv -LiteralPath $impactPath -NoTypeInformation -Encoding utf8

$historicalRows = foreach ($historicalPath in $historicalHashes.Keys | Sort-Object) {
    $fullPath = Join-Path $RepoRoot $historicalPath.Replace('/', '\')
    if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
        $currentHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToUpperInvariant()
        [pscustomobject]@{ Path = $historicalPath; HistoricalSHA256 = $historicalHashes[$historicalPath]; CurrentSHA256 = $currentHash; Result = if ($currentHash -ceq $historicalHashes[$historicalPath]) { 'MATCH' } else { 'MISMATCH' } }
    } else {
        [pscustomobject]@{ Path = $historicalPath; HistoricalSHA256 = $historicalHashes[$historicalPath]; CurrentSHA256 = ''; Result = 'MISSING' }
    }
}
$historicalComparisonPath = Join-Path $evidenceRoot 'c185-documented-hash-comparison.csv'
$historicalRows | Export-Csv -LiteralPath $historicalComparisonPath -NoTypeInformation -Encoding utf8
if (@($historicalRows | Where-Object Result -ne 'MATCH').Count -gt 0) {
    $script:failures.Add("One or more documented C185 artifact hashes do not match; see $historicalComparisonPath")
}
$counterpartComparisonPath = Join-Path $evidenceRoot 'c185-retained-c186-counterpart-comparison.csv'
$impactRows | Select-Object Path, CurrentSHA256, RetainedC186Counterpart, CounterpartSHA256, CounterpartComparison |
    Export-Csv -LiteralPath $counterpartComparisonPath -NoTypeInformation -Encoding utf8
$counterpartCounts = [ordered]@{
    sameCurrentHash = @($impactRows | Where-Object CounterpartComparison -eq 'same current hash').Count
    differentCurrentHash = @($impactRows | Where-Object CounterpartComparison -eq 'different current hash').Count
    noCorrespondingCopy = @($impactRows | Where-Object CounterpartComparison -eq 'no corresponding retained copy').Count
}

$hostBinXml = ConvertTo-XmlText (($hostBin.TrimEnd('\', '/') + '\$(C186R1BuildScope)\'))
$hostObjXml = ConvertTo-XmlText (($hostObj.TrimEnd('\', '/') + '\$(C186R1BuildScope)\'))
$propsContent = @"
<Project>
  <PropertyGroup>
    <ResolveNuGetPackages>false</ResolveNuGetPackages>
    <C186R1BuildScope Condition="'`$(C186R1BuildScope)' == ''">hosted</C186R1BuildScope>
  </PropertyGroup>
  <PropertyGroup Condition="'`$(MSBuildProjectName)' == 'HostLogProof'">
    <BaseOutputPath>$hostBinXml</BaseOutputPath>
    <BaseIntermediateOutputPath>$hostObjXml</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$hostObjXml</MSBuildProjectExtensionsPath>
  </PropertyGroup>
</Project>
"@
Set-Content -LiteralPath $propsPath -Value $propsContent -Encoding utf8

$dotnet = Get-Command dotnet.exe -ErrorAction Stop | Select-Object -First 1
$powershell = Get-Command powershell.exe -ErrorAction Stop | Select-Object -First 1
$contractsProject = Join-Path $RepoRoot 'samples\managed\ManagedUiContracts\GuideXos.ManagedUi.Contracts.csproj'
$counterProject = Join-Path $RepoRoot 'samples\managed\C185CounterApp\GuideXos.C185.CounterApp.csproj'
$qualificationRoot = Join-Path $isolationRoot 'qualifications'
New-Item -ItemType Directory -Force -Path $qualificationRoot | Out-Null

$startingHead = @(& git -C $RepoRoot rev-parse HEAD 2>&1) | Out-String
$startingHead = $startingHead.Trim()
$startingBranch = @(& git -C $RepoRoot branch --show-current 2>&1) | Out-String
$startingBranch = $startingBranch.Trim()
if ($startingBranch -cne 'v1.1_DOTNET_SUPPORT') {
    $script:failures.Add("Validation must stay on v1.1_DOTNET_SUPPORT; found branch=$startingBranch")
}
foreach ($requiredCommit in @('d14846a5eda85a59b4fdd05492b74ae11bfd9d39', '417d5168bfa3aeca331e7111b6e915fb001ae69f')) {
    & git -C $RepoRoot merge-base --is-ancestor $requiredCommit $startingHead 2>$null
    if ($LASTEXITCODE -ne 0) {
        $script:failures.Add("Required C186 lineage commit is not an ancestor of HEAD: $requiredCommit")
    }
}

try {
    $null = Invoke-CapturedStep 'independent-managed-ui-contracts-build' $dotnet.Source @(
        'build', $contractsProject, '--configuration', 'Release', '--artifacts-path', (Join-Path $qualificationRoot 'contracts\artifacts')
    ) (Join-Path $evidenceRoot 'managed-ui-contracts-build.log') 'Build succeeded'

    $null = Invoke-CapturedStep 'independent-counterapp-build' $dotnet.Source @(
        'build', $counterProject, '--configuration', 'Release', '--artifacts-path', (Join-Path $qualificationRoot 'counterapp\artifacts')
    ) (Join-Path $evidenceRoot 'counterapp-independent-build.log') 'Build succeeded'

    $c182Project = Join-Path $RepoRoot 'samples\host\ManagedStarterC182\ManagedStarterC182.csproj'
    $c182Artifacts = Join-Path $qualificationRoot 'c182\artifacts'
    $c182Build = Invoke-CapturedStep 'c182-starter-build' $dotnet.Source @(
        'build', $c182Project, '--configuration', 'Release', '--artifacts-path', $c182Artifacts,
        "-p:DirectoryBuildPropsPath=$propsPath", '-p:C186R1BuildScope=c182-host'
    ) (Join-Path $evidenceRoot 'c182-starter-build.log') 'Build succeeded'
    if ($c182Build.exitCode -eq 0) {
        $c182Dll = Get-ChildItem -LiteralPath (Join-Path $c182Artifacts 'bin') -Recurse -File -Filter 'ManagedStarterC182Host.dll' | Select-Object -First 1
        if ($null -eq $c182Dll) {
            $script:failures.Add('ManagedStarterC182Host.dll was not found under the isolated C182 artifacts root.')
        } else {
            $null = Invoke-CapturedStep 'c182-starter-host-qualification' $dotnet.Source @($c182Dll.FullName) `
                (Join-Path $evidenceRoot 'c182-starter-host-qualification.log') 'C182-MANAGED-STARTER cases=15 .*result=PASS'
        }
    }

    $c185Project = Join-Path $RepoRoot 'samples\host\ManagedHostServicesC185\ManagedHostServicesC185.csproj'
    $c185Artifacts = Join-Path $qualificationRoot 'c185-c186\artifacts'
    $null = Invoke-CapturedStep 'c185-composition-contracts-build' $dotnet.Source @(
        'build', $contractsProject, '--configuration', 'Release', '--artifacts-path', $c185Artifacts,
        "-p:DirectoryBuildPropsPath=$propsPath", '-p:C186R1BuildScope=c185-c186-host'
    ) (Join-Path $evidenceRoot 'c185-composition-contracts-build.log') 'Build succeeded'
    $null = Invoke-CapturedStep 'c185-composition-counterapp-build' $dotnet.Source @(
        'build', $counterProject, '--configuration', 'Release', '--artifacts-path', $c185Artifacts,
        "-p:DirectoryBuildPropsPath=$propsPath", '-p:C186R1BuildScope=c185-c186-host'
    ) (Join-Path $evidenceRoot 'c185-composition-counterapp-build.log') 'Build succeeded'
    $c185Build = Invoke-CapturedStep 'c185-c186-host-build' $dotnet.Source @(
        'build', $c185Project, '--configuration', 'Release', '--artifacts-path', $c185Artifacts,
        "-p:DirectoryBuildPropsPath=$propsPath", '-p:C186R1BuildScope=c185-c186-host'
    ) (Join-Path $evidenceRoot 'c185-c186-host-build.log') 'Build succeeded'
    if ($c185Build.exitCode -eq 0) {
        $c185Dll = Get-ChildItem -LiteralPath (Join-Path $c185Artifacts 'bin') -Recurse -File -Filter 'ManagedHostServicesC185Host.dll' | Select-Object -First 1
        if ($null -eq $c185Dll) {
            $script:failures.Add('ManagedHostServicesC185Host.dll was not found under the isolated C185/C186 artifacts root.')
        } else {
            $hostRun = Invoke-CapturedStep 'c185-c186-host-qualification' $dotnet.Source @($c185Dll.FullName) `
                (Join-Path $evidenceRoot 'c185-c186-host-qualification.log') '(?s)C185-HOST-SERVICES cases=28 .*result=PASS.*C186-HOST-SERVICES cases=15 .*result=PASS'
        }
    }

    $aggregateScript = Join-Path $PSScriptRoot 'run-managed-ui-regressions.ps1'
    $aggregateEvidence = Join-Path $qualificationRoot 'aggregate\evidence'
    $aggregateArtifacts = Join-Path $qualificationRoot 'aggregate\artifacts'
    $aggregateRun = Invoke-CapturedStep 'managed-ui-aggregate-regression' $powershell.Source @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $aggregateScript,
        '-RepoRoot', $RepoRoot, '-EvidenceRoot', $aggregateEvidence, '-ArtifactsRoot', $aggregateArtifacts
    ) (Join-Path $evidenceRoot 'managed-ui-aggregate-run.log') 'result=PASS suites=38 cases=930'
    $aggregateManifestPath = Join-Path $aggregateEvidence 'managed-ui-host-manifest.json'
    if (Test-Path -LiteralPath $aggregateManifestPath -PathType Leaf) {
        $aggregateManifest = Get-Content -LiteralPath $aggregateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
        if ($aggregateManifest.status -cne 'PASS' -or $aggregateManifest.focusedSummary.suites -ne 38 -or
            $aggregateManifest.focusedSummary.cases -ne 930 -or $aggregateManifest.focusedSummary.failed -ne 0) {
            $script:failures.Add("Managed UI aggregate manifest is failed or incomplete: $aggregateManifestPath")
        }
    } else {
        $script:failures.Add("Managed UI aggregate manifest was not generated: $aggregateManifestPath")
    }

    $inventoryTest = Join-Path $PSScriptRoot 'test-managed-ui-regression-inventory.ps1'
    $null = Invoke-CapturedStep 'aggregate-manifest-negative-tests' $powershell.Source @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $inventoryTest
    ) (Join-Path $evidenceRoot 'managed-ui-manifest-negative-tests.log') 'incorrectManifestsRejected=6 result=PASS'

    $routingTest = Join-Path $PSScriptRoot 'test-c186r1-output-routing.ps1'
    $null = Invoke-CapturedStep 'unsafe-output-path-negative-tests' $powershell.Source @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $routingTest, '-RepoRoot', $RepoRoot
    ) (Join-Path $evidenceRoot 'output-routing-negative-tests.log') 'unsafeRootsRejected=3 createdPaths=0 result=PASS'

    $nativeScript = Join-Path $PSScriptRoot 'run-c186r1-nativeaot.ps1'
    $nativeOutput = Join-Path $isolationRoot 'nativeaot'
    $null = Invoke-CapturedStep 'isolated-nativeaot-and-elf' $powershell.Source @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $nativeScript,
        '-RepoRoot', $RepoRoot, '-OutputRoot', $nativeOutput, '-DirectoryBuildPropsPath', $propsPath
    ) (Join-Path $evidenceRoot 'nativeaot-validation.log') 'result=PASS'
} catch {
    $script:failures.Add("C186R1 validation orchestration error: $($_ | Out-String)")
} finally {
    $afterInventories = @{}
    $comparisons = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in $protectedTrees.GetEnumerator()) {
        try {
            $directory = Join-Path $RepoRoot $entry.Value
            $inventory = Get-TreeInventory $directory
            $afterInventories[$entry.Key] = $inventory
            $afterFile = Join-Path $afterRoot ($entry.Key + '.json')
            Save-Json $inventory $afterFile
            $comparison = Compare-TreeInventory $beforeInventories[$entry.Key] $inventory
            $comparisons.Add($comparison)
            if ($comparison.status -ne 'PASS') {
                $script:failures.Add("Protected tree changed during C186R1: $($entry.Value)")
            }
        } catch {
            $comparison = [pscustomobject]@{ root = (Join-Path $RepoRoot $entry.Value); status = 'ERROR'; error = ($_ | Out-String) }
            $comparisons.Add($comparison)
            $script:failures.Add("Protected tree could not be re-inventoried: $($entry.Value)")
        }
    }
    $afterSummary = foreach ($entry in $protectedTrees.GetEnumerator()) {
        $inventoryPath = Join-Path $afterRoot ($entry.Key + '.json')
        $inventory = $afterInventories[$entry.Key]
        [pscustomobject]@{
            id = $entry.Key
            path = $entry.Value
            fileCount = $inventory.fileCount
            totalBytes = $inventory.totalBytes
            manifest = $inventoryPath
            manifestSha256 = (Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash.ToUpperInvariant()
        }
    }
    Save-Json ([pscustomobject]@{ phase = 'C186R1-protected-after'; trees = @($afterSummary) }) (Join-Path $evidenceRoot 'protected-after.json')
    Save-Json ([pscustomobject]@{ phase = 'C186R1-protected-tree-comparison'; comparisons = @($comparisons) }) (Join-Path $evidenceRoot 'protected-tree-comparison.json')
    $ramdiskAfter = if (Test-Path -LiteralPath $ramdiskPath -PathType Leaf) {
        (Get-FileHash -LiteralPath $ramdiskPath -Algorithm SHA256).Hash.ToUpperInvariant()
    } else { '' }
    if ($ramdiskAfter -cne $expectedRamdiskSha256 -or $ramdiskAfter -cne $ramdiskBefore) {
        $script:failures.Add("Protected ESP/ramdisk.img changed or does not match its recorded hash: $ramdiskAfter")
    }
    Set-Content -LiteralPath (Join-Path $evidenceRoot 'git-status-after.txt') -Value ((@(& git -C $RepoRoot status --short --branch 2>&1) | Out-String)) -Encoding utf8
    $statusBeforePath = Join-Path $evidenceRoot 'git-status-before.txt'
    $statusAfterPath = Join-Path $evidenceRoot 'git-status-after.txt'
    if ((Get-Content -LiteralPath $statusBeforePath -Raw) -cne (Get-Content -LiteralPath $statusAfterPath -Raw)) {
        $script:failures.Add('Git worktree status changed during validation.')
    }
}

$endingHead = @(& git -C $RepoRoot rev-parse HEAD 2>&1) | Out-String
$endingHead = $endingHead.Trim()
$endingBranch = @(& git -C $RepoRoot branch --show-current 2>&1) | Out-String
$endingBranch = $endingBranch.Trim()
if ($endingHead -cne $startingHead -or $endingBranch -cne $startingBranch) {
    $script:failures.Add("Git identity changed during validation: branch=$endingBranch HEAD=$endingHead")
}
$preservationPass = @($comparisons | Where-Object status -ne 'PASS').Count -eq 0 -and
    $ramdiskAfter -ceq $expectedRamdiskSha256
$overallStatus = if ($script:failures.Count -eq 0 -and $preservationPass) { 'PASS' } else { 'FAIL' }
$summary = [ordered]@{
    schemaVersion = 1
    phase = 'C186R1-artifact-isolation-validation'
    status = $overallStatus
    startingBranch = $startingBranch
    startingHead = $startingHead
    endingBranch = $endingBranch
    endingHead = $endingHead
    ramdiskSha256Before = $ramdiskBefore
    ramdiskSha256After = $ramdiskAfter
    temporaryDirectory = $buildTempRoot
    protectedPreservation = if ($preservationPass) { 'PASS' } else { 'FAIL' }
    artifactImpactMatrix = $impactPath
    historicalC185Comparisons = $historicalComparisonPath
    retainedC186CounterpartComparison = $counterpartComparisonPath
    retainedC186CounterpartCounts = $counterpartCounts
    steps = @($script:steps)
    failures = @($script:failures)
}
Save-Json $summary (Join-Path $evidenceRoot 'validation-summary.json')
if ($overallStatus -eq 'PASS') {
    Write-Output "C186R1-ARTIFACT-ISOLATION result=PASS protectedTrees=$($protectedTrees.Count) qualificationSteps=$($script:steps.Count)"
    exit 0
}
Write-Output "C186R1-ARTIFACT-ISOLATION result=FAIL failures=$($script:failures.Count) protected=$($summary.protectedPreservation)"
exit 1
