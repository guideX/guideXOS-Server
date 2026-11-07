param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$CandidateRoot = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [System.IO.Path]::GetFullPath($RepoRoot)
$phaseRoot = [System.IO.Path]::GetFullPath((Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4r2r3-transition-attribution'))
if ([string]::IsNullOrWhiteSpace($CandidateRoot)) {
    $CandidateRoot = Join-Path $phaseRoot 'candidate-20261106T042749067Z'
}
$candidate = [System.IO.Path]::GetFullPath($CandidateRoot)
$candidateManifestPath = Join-Path $candidate 'candidate-manifest.json'
$inventoryPath = Join-Path $candidate 'candidate-integrity-inventory.json'
$runnerPath = Join-Path $root 'scripts\dotnet\run-c188r12r4r5-gc-ownership-qac-guest.ps1'
$admissionPath = Join-Path $root 'scripts\dotnet\c188r12r4r5r4-qac-admission.psm1'
$ErrorActionPreference = 'Stop'
Import-Module $admissionPath -Force

$results = [System.Collections.Generic.List[object]]::new()
function Assert-Contract([bool]$Condition,[string]$Name,[string]$Evidence='hosted') {
    if (-not $Condition) { throw "Transition runner contract failed: $Name" }
    $results.Add([pscustomobject]@{name=$Name;result='PASS';evidenceKind=$Evidence})
}
function Assert-Rejected([scriptblock]$Action,[string]$Name) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-Contract $rejected $Name
}
function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

foreach ($path in @($runnerPath,$candidateManifestPath,$inventoryPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required transition runner contract input is missing: $path" }
}
$manifestHashBefore = Get-Hash $candidateManifestPath
$inventoryHashBefore = Get-Hash $inventoryPath
$sourceRamdisk = Join-Path $root 'ESP\ramdisk.img'
$sourceRamdiskHashBefore = Get-Hash $sourceRamdisk
$qemuCode = 'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
$varsTemplate = 'C:\Program Files\qemu\share\edk2-i386-vars.fd'
$qemuCodeHashBefore = Get-Hash $qemuCode
$varsTemplateHashBefore = Get-Hash $varsTemplate

$fixtureRoot = Join-Path $phaseRoot ('hosted-runner-contract-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot -ErrorAction Stop | Out-Null
try {
    # The actual runner validates all inventory hashes, stages disposable state,
    # then exits before querying QAC or creating the one-boot guard.
    $runnerOutput = & pwsh -NoProfile -ExecutionPolicy Bypass -File $runnerPath `
        -CandidateRoot $candidate -CandidateIntegrityInventory $inventoryPath -HostedPreflightOnly 2>&1
    $runnerExit = $LASTEXITCODE
    $runnerText = ($runnerOutput | Out-String)
    Assert-Contract ($runnerExit -eq 0 -and $runnerText -match 'C188R12R4R5R2R3_TRANSITION_HOSTED_PREFLIGHT=PASS') `
        'valid transition candidate accepted by the shared runner'
    $preflightLine = @($runnerText -split "`r?`n" | Where-Object { $_ -match '^C188R12R4R5R2R3_TRANSITION_HOSTED_PREFLIGHT_MANIFEST=' }) | Select-Object -First 1
    if ($null -eq $preflightLine) { throw 'Runner hosted preflight did not emit its manifest path.' }
    $preflightPath = $preflightLine -replace '^C188R12R4R5R2R3_TRANSITION_HOSTED_PREFLIGHT_MANIFEST=',''
    $hosted = Get-Content -LiteralPath $preflightPath -Raw | ConvertFrom-Json -ErrorAction Stop
    Assert-Contract ([string]$hosted.result -ceq 'TRANSITION_PROFILE_HOSTED_PREFLIGHT_PASS' -and
        [string]$hosted.candidateRoot -ceq $candidate -and [int]$hosted.candidateIntegrityFileCount -eq 1482) `
        'candidate manifest and 1,482-entry integrity inventory verified'
    Assert-Contract (-not [bool]$hosted.qacQueried -and -not [bool]$hosted.qacReserved -and
        -not [bool]$hosted.qemuLaunched -and -not [bool]$hosted.attemptGuardCreated -and [int]$hosted.guestBoots -eq 0) `
        'hosted preflight cannot query QAC, reserve, create a guard, or launch QEMU'
    Assert-Contract ([string]$hosted.candidateEsp -cne [string]$hosted.isolatedGuestEsp -and
        [string]$hosted.sourceRamdiskSha256 -ceq [string]$hosted.isolatedRamdiskSha256 -and
        [int]$hosted.isolatedEspFileCount -eq 3) 'transition guest ESP is a verified independent writable copy'
    Assert-Contract ([string]$hosted.writableUefiVarsImage -notlike ($candidate + '*') -and
        [string]$hosted.writableUefiVarsImage -like ([string]$hosted.runRoot + '*') -and
        [string]$hosted.uefiVarsTemplateSha256 -ceq [string]$hosted.writableUefiVarsImageSha256) `
        'fresh writable UEFI variable image is copied into this run-owned state'
    Assert-Contract ([string]$hosted.serialLog -like ([string]$hosted.runRoot + '*') -and
        [string]$hosted.qmpLog -like ([string]$hosted.runRoot + '*') -and
        -not [string]::IsNullOrWhiteSpace([string]$hosted.qualificationCorrelationId)) `
        'serial, QMP, and run correlation paths are unique to the qualification'

    $missingCandidate = Join-Path $fixtureRoot 'missing-candidate'
    Assert-Rejected { Test-QacTransitionCandidateProfile -CandidateRoot $missingCandidate `
        -PhaseRoot $phaseRoot -RepoRoot $root } 'missing candidate rejected before QAC'

    $safeHandoffRoot = Join-Path $root 'out\dotnet\c188r12r4r5r4r5r2r2r4-safe-handoff'
    Assert-Contract (-not (Test-QacCandidatePath -CandidateRoot $safeHandoffRoot -PhaseRoot $phaseRoot -RequireExisting)) `
        'candidate outside the authorized transition profile boundary rejected'

    $invalidManifestRoot = Join-Path $fixtureRoot 'invalid-manifest'
    New-Item -ItemType Directory -Path $invalidManifestRoot | Out-Null
    $alteredManifest = Get-Content -LiteralPath $candidateManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
    $alteredManifest.mode = 'UnauthorizedProfile'
    $alteredManifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $invalidManifestRoot 'candidate-manifest.json') -Encoding utf8
    Assert-Rejected { Test-QacTransitionCandidateProfile -CandidateRoot $invalidManifestRoot `
        -PhaseRoot $phaseRoot -RepoRoot $root } 'invalid or altered transition manifest rejected'

    $integrityRoot = Join-Path $fixtureRoot 'integrity-mismatch'
    New-Item -ItemType Directory -Path $integrityRoot | Out-Null
    $fixtureManifest = Join-Path $integrityRoot 'candidate-manifest.json'
    $fixturePayload = Join-Path $integrityRoot 'payload.bin'
    [System.IO.File]::WriteAllText($fixtureManifest,'{"fixture":true}',[System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllBytes($fixturePayload,[byte[]](1,2,3,4))
    $fixtureInventory = [ordered]@{
        schema='C188R12R4R5R1_CANDIDATE_INTEGRITY_INVENTORY_V1'
        candidateRoot=$integrityRoot;candidateManifest='candidate-manifest.json'
        candidateManifestSha256=(Get-Hash $fixtureManifest);fileCount=2
        files=@(
            [ordered]@{path='candidate-manifest.json';sizeBytes=(Get-Item $fixtureManifest).Length;sha256=(Get-Hash $fixtureManifest)},
            [ordered]@{path='payload.bin';sizeBytes=(Get-Item $fixturePayload).Length;sha256=(Get-Hash $fixturePayload)}
        )
    }
    $fixtureInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $integrityRoot 'candidate-integrity-inventory.json') -Encoding utf8
    [System.IO.File]::WriteAllBytes($fixturePayload,[byte[]](9,8,7,6))
    Assert-Rejected { Test-QacCandidateIntegrityInventory -CandidateRoot $integrityRoot `
        -InventoryPath (Join-Path $integrityRoot 'candidate-integrity-inventory.json') } `
        'integrity mismatch rejected without modifying the verified candidate'

    $missingEsp = Join-Path $fixtureRoot 'missing-esp-artifact'
    New-Item -ItemType Directory -Path $missingEsp | Out-Null
    Assert-Rejected { Test-QacTransitionGuestArtifacts -Esp $missingEsp `
        -KernelSha256 ('0' * 64) -RamdiskSha256 ('0' * 64) -BootloaderSha256 ('0' * 64) } `
        'missing required guest artifact rejected'

    $sourceCandidateRoot = Join-Path $fixtureRoot 'state-source-candidate'
    $sourceEsp = Join-Path $sourceCandidateRoot 'esp'
    New-Item -ItemType Directory -Path (Join-Path $sourceEsp 'EFI\BOOT') -Force | Out-Null
    [System.IO.File]::WriteAllBytes((Join-Path $sourceEsp 'kernel.elf'),[byte[]](10,11,12))
    [System.IO.File]::WriteAllBytes((Join-Path $sourceEsp 'ramdisk.img'),[byte[]](20,21,22,23))
    [System.IO.File]::WriteAllBytes((Join-Path $sourceEsp 'EFI\BOOT\BOOTX64.EFI'),[byte[]](30,31))
    $varsFixture = Join-Path $fixtureRoot 'uefi-vars-template.fd'
    [System.IO.File]::WriteAllBytes($varsFixture,[byte[]](0..127))
    $varsFixtureHash = Get-Hash $varsFixture
    $isolatedRunRoot = Join-Path $fixtureRoot 'isolated-state-test'
    $state = New-QacTransitionGuestState -CandidateEsp $sourceEsp -CandidateRoot $sourceCandidateRoot `
        -PhaseRoot $phaseRoot -RunRoot $isolatedRunRoot -UefiVarsTemplate $varsFixture
    Assert-Contract ($state.Esp -ne $sourceEsp -and $state.UefiVarsImage -ne $varsFixture -and
        $state.SourceEspSha256 -ceq $state.IsolatedRamdiskSha256 -and
        $state.UefiVarsImageSha256 -ceq $varsFixtureHash) 'writable guest state is isolated from the source and baseline'
    Assert-Rejected { New-QacTransitionGuestState -CandidateEsp $sourceEsp -CandidateRoot $sourceCandidateRoot `
        -PhaseRoot $phaseRoot -RunRoot (Join-Path $fixtureRoot 'failed-state-not-created') `
        -UefiVarsTemplate (Join-Path $fixtureRoot 'missing-vars.fd') } 'state-preparation failure does not create or overwrite foreign state'

    $runnerSource = Get-Content -LiteralPath $runnerPath -Raw
    Assert-Contract ($runnerSource.Contains("Join-Path `$PSScriptRoot 'analyze-c188r12r4r5r2r3-transition-attribution.ps1'") -and
        $runnerSource -notmatch '\$candidateManifest\.transitionAnalyzer') `
        'transition analyzer resolves from the runner directory; candidate metadata hash-pins its identity' 'runner source contract'
    Assert-Contract ($runnerSource -match 'transitionRuntimeStartupObserved=.*rhinitialize_return') `
        'transition run manifest recognizes the observed runtime initialization marker' 'runner source contract'
    $censusIndex = $runnerSource.IndexOf("`$before = Get-QacSnapshot 'qac-before'",[System.StringComparison]::Ordinal)
    $guardIndex = $runnerSource.IndexOf('[System.IO.File]::Open($attemptPath,[System.IO.FileMode]::CreateNew',[System.StringComparison]::Ordinal)
    $launchIndex = $runnerSource.IndexOf('[System.Diagnostics.Process]::Start($psi)',[System.StringComparison]::Ordinal)
    $qmpQuitIndex = $runnerSource.IndexOf("[void](Invoke-Qmp 'quit')",[System.StringComparison]::Ordinal)
    $releaseIndex = $runnerSource.IndexOf('$releaseVerified = $true; break',$qmpQuitIndex,[System.StringComparison]::Ordinal)
    Assert-Contract ($censusIndex -ge 0 -and $guardIndex -gt $censusIndex -and $launchIndex -gt $guardIndex -and
        $qmpQuitIndex -gt $launchIndex -and $releaseIndex -gt $qmpQuitIndex -and
        $runnerSource.Contains('finally {') -and $runnerSource -notmatch '\[System\.Diagnostics\.Process\]::Start\(\$qemu') `
        'QAC census gates the one-boot guard; QMP shutdown and release verification follow owned launch' 'runner source contract'

    Assert-Contract ((Get-Hash $candidateManifestPath) -ceq $manifestHashBefore -and
        (Get-Hash $inventoryPath) -ceq $inventoryHashBefore -and
        (Get-Hash $sourceRamdisk) -ceq $sourceRamdiskHashBefore -and
        (Get-Hash $qemuCode) -ceq $qemuCodeHashBefore -and
        (Get-Hash $varsTemplate) -ceq $varsTemplateHashBefore) `
        'candidate manifest, inventory, protected ramdisk, and firmware baselines remain unchanged'
} finally {
    $fixtureFull = [System.IO.Path]::GetFullPath($fixtureRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $phasePrefix = $phaseRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fixtureFull.StartsWith($phasePrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
        $fixtureFull -eq $candidate -or -not (Test-Path -LiteralPath $fixtureFull -PathType Container)) {
        throw 'Refusing to clean hosted runner fixtures outside their task-owned directory.'
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $fixtureFull -Force -Recurse)) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing recursive fixture cleanup through a reparse point: $($item.FullName)"
        }
    }
    Remove-Item -LiteralPath $fixtureFull -Recurse -Force
}

$results | ConvertTo-Json -Depth 5
Write-Output ("C188R12R4R5R2R3 transition runner contract: PASS assertions={0}; native guest execution was not performed." -f $results.Count)
