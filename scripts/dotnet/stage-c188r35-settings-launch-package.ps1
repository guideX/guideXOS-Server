param(
    [Parameter()][string]$SourceCandidateRoot='E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\candidate-20261117T050157105Z-0fcf664c63df40e7ae3c8d3d759d5378',
    [Parameter()][string]$ArtifactRoot='E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$artifactRootFull = [System.IO.Path]::GetFullPath($ArtifactRoot)
$sourceCandidate = [System.IO.Path]::GetFullPath($SourceCandidateRoot)
$expectedSourceCandidate = [System.IO.Path]::GetFullPath(
    'E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r33-settings-handoff\candidate-20261117T050157105Z-0fcf664c63df40e7ae3c8d3d759d5378')
if ($sourceCandidate -cne $expectedSourceCandidate) {
    throw 'R35 accepts only the exact R33 candidate identified in the R34 report.'
}

$sourceManifestPath = Join-Path $sourceCandidate 'candidate-manifest.json'
$sourceIntegrityPath = Join-Path $sourceCandidate 'candidate-integrity.json'
$sourcePhaseRoot = [System.IO.Path]::GetDirectoryName($sourceCandidate)
$sourceLeaf = [System.IO.Path]::GetFileName($sourceCandidate)
$sourceR25ManifestPath = Join-Path $sourcePhaseRoot ($sourceLeaf + '-r25-candidate-manifest.json')
$sourceR33ManifestPath = Join-Path $sourcePhaseRoot ($sourceLeaf + '-r33-candidate-manifest.json')
$expectedCandidateManifestSha256 = '787A46FFFF09C23355E585F6A7C02BC1ABA19429DED156BC64C19F28A6F510E2'
$expectedCandidateIntegritySha256 = '72B913375CA2438C4FB3705D3B1F6AA514DBA0DDB1A46F5F964DEA44BBF89A94'
$expectedOriginalR25ManifestSha256 = '818D8724D900BE34A6EFD73160B00F9278A164BB05495925E070F37283F087DE'
$expectedOriginalR33ManifestSha256 = '303F989980BA1EC7E737C6D9B96469ED4BE995F0FE18AFA86F6286CDCE39A817'
foreach ($path in @($sourceManifestPath,$sourceIntegrityPath,$sourceR25ManifestPath,$sourceR33ManifestPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R35 source provenance is missing: $path" }
}
if ((Get-FileHash -LiteralPath $sourceManifestPath -Algorithm SHA256).Hash -ine $expectedCandidateManifestSha256 -or
    (Get-FileHash -LiteralPath $sourceIntegrityPath -Algorithm SHA256).Hash -ine $expectedCandidateIntegritySha256 -or
    (Get-FileHash -LiteralPath $sourceR25ManifestPath -Algorithm SHA256).Hash -ine $expectedOriginalR25ManifestSha256 -or
    (Get-FileHash -LiteralPath $sourceR33ManifestPath -Algorithm SHA256).Hash -ine $expectedOriginalR33ManifestSha256) {
    throw 'R33 candidate or original sidecar hashes differ from the R34 recorded provenance.'
}

$candidateManifest = Get-Content -LiteralPath $sourceManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$candidateIntegrity = Get-Content -LiteralPath $sourceIntegrityPath -Raw | ConvertFrom-Json -ErrorAction Stop
$sourceR25Manifest = Get-Content -LiteralPath $sourceR25ManifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
if ($candidateIntegrity.files.Count -ne 349 -or
    [string]$candidateManifest.kernelSha256 -ine '9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B' -or
    [string]$candidateManifest.managedElfSha256 -ine '14C61298B00665B9981C7FE177670C529C69B4F8AF2D9EA96BF4C53F888C40C8' -or
    [string]$candidateManifest.runtimePackManifestSha256 -ine 'D4AB05BAB6D2B8ED8859BF11B41A8B173B49C910D9C4D3FAA14AE44191C36FAD' -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
    [string]$candidateManifest.actualFactoryConstructorSymbol -cne 'ManagedSettingsCenter___ctor_0' -or
    [string]$sourceR25Manifest.candidateManifestSha256 -ine $expectedCandidateManifestSha256 -or
    [string]$sourceR25Manifest.candidateIntegritySha256 -ine $expectedCandidateIntegritySha256) {
    throw 'R33 candidate identity, 349-file inventory, runtime, or genuine Settings factory binding is invalid.'
}

function Assert-C188R35CandidateInventory([string]$Root,[object]$Inventory,[string]$Context) {
    $expectedPaths = @($Inventory.files | ForEach-Object { [string]$_.path } | Sort-Object)
    $actualPaths = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
        $_.Name -notin @('candidate-manifest.json','candidate-integrity.json')
    } | ForEach-Object { [System.IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/') } | Sort-Object)
    if ($expectedPaths.Count -ne 349 -or $actualPaths.Count -ne $expectedPaths.Count -or
        @($expectedPaths | Where-Object { $_ -notin $actualPaths }).Count -gt 0 -or
        @($actualPaths | Where-Object { $_ -notin $expectedPaths }).Count -gt 0) {
        throw "$Context candidate file set does not match the original 349-file inventory."
    }
    foreach ($entry in @($Inventory.files)) {
        $path = Join-Path $Root ([string]$entry.path.Replace('/','\'))
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$entry.sha256) {
            throw "$Context candidate inventory hash mismatch: $($entry.path)"
        }
    }
}
Assert-C188R35CandidateInventory $sourceCandidate $candidateIntegrity 'Source'

$protectedObjects = [ordered]@{
    'c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj'='3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD'
    'c188r12r4r5r4r5r2r2_native_alloc_record_test.obj'='605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3'
    'guidexos_nativeaot_native_alloc_record.obj'='2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183'
}
foreach ($objectName in $protectedObjects.Keys) {
    $path = Join-Path $repo $objectName
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine [string]$protectedObjects[$objectName]) {
        throw "Protected pre-existing object hash mismatch: $objectName"
    }
}

$executionPackageId = [guid]::NewGuid().ToString('N')
$executionAttemptId = [guid]::NewGuid().ToString('N')
$packageRoot = Join-Path $artifactRootFull ('c188r35-settings-launch\package-' + $executionPackageId)
$candidateLeaf = 'candidate-r35-' + $executionPackageId
$candidateRoot = Join-Path $packageRoot $candidateLeaf
foreach ($path in @($packageRoot,$candidateRoot)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite R35 package path: $path" }
}
$null = New-Item -ItemType Directory -Path $packageRoot -Force
Copy-Item -LiteralPath $sourceCandidate -Destination $candidateRoot -Recurse
Assert-C188R35CandidateInventory $candidateRoot $candidateIntegrity 'R35 copied'
$candidateManifestPath = Join-Path $candidateRoot 'candidate-manifest.json'
$candidateIntegrityPath = Join-Path $candidateRoot 'candidate-integrity.json'
$candidateManifestSha256 = (Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash
$candidateIntegritySha256 = (Get-FileHash -LiteralPath $candidateIntegrityPath -Algorithm SHA256).Hash

$r25ManifestPath = Join-Path $packageRoot ($candidateLeaf + '-r25-candidate-manifest.json')
$r25Manifest = [ordered]@{}
foreach ($property in $sourceR25Manifest.PSObject.Properties) { $r25Manifest[$property.Name] = $property.Value }
$r25Manifest.candidateRoot = $candidateRoot
$r25Manifest.candidateManifestPath = $candidateManifestPath
$r25Manifest.candidateIntegrityPath = $candidateIntegrityPath
$r25Manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $r25ManifestPath -Encoding utf8
$r25ManifestSha256 = (Get-FileHash -LiteralPath $r25ManifestPath -Algorithm SHA256).Hash
$r33SidecarCopyPath = Join-Path $packageRoot 'r33-source-candidate-manifest.json'
Copy-Item -LiteralPath $sourceR33ManifestPath -Destination $r33SidecarCopyPath

$runnerComponents = @(
    'scripts/dotnet/stage-c188r35-settings-launch-package.ps1',
    'scripts/dotnet/run-c188r35-settings-launch.ps1',
    'scripts/dotnet/run-c188r24-initializemodules.ps1',
    'scripts/dotnet/c188r24-initializemodules-predicates.ps1',
    'scripts/dotnet/run-c188r12r1-settings-handoff.ps1',
    'scripts/dotnet/run-c188-qac-guest.ps1',
    'scripts/dotnet/c188r11-runner-predicates.ps1',
    'scripts/dotnet/c188r11r4r2-runner-predicates.ps1',
    'scripts/dotnet/c188r11qac-ownership-predicates.ps1',
    'scripts/dotnet/c188r12r1-settings-handoff-predicates.ps1',
    'scripts/dotnet/c188r12r2r1-settings-dispatch-predicates.ps1',
    'scripts/dotnet/c188r12r3-lifetime-predicates.ps1',
    'scripts/dotnet/c188r12r4-factory-predicates.ps1',
    'scripts/dotnet/c188r22-bootstrap-handoff-predicates.ps1',
    'scripts/dotnet/c188r26-precreated-evidence-root.ps1',
    'scripts/dotnet/c188r33-settings-handoff-predicates.ps1',
    'scripts/dotnet/c188r25-failfast-runner-predicates.ps1',
    'scripts/dotnet/c181-qmp.psm1',
    'scripts/dotnet/c188-qmp-control.py',
    'scripts/dotnet/find-c188r12r1-cursor.py',
    'scripts/dotnet/compare-c188r12r1-screenshots.py',
    'scripts/dotnet/analyze-c188r25-failfast-origin.py',
    'scripts/dotnet/run-c188r25-failfast-origin.ps1'
)
$runnerPackageRoot = Join-Path $packageRoot 'runner-package'
$null = New-Item -ItemType Directory -Path $runnerPackageRoot -Force
$runnerComponentRecords = [System.Collections.Generic.List[object]]::new()
foreach ($relative in $runnerComponents) {
    $sourcePath = Join-Path $repo ($relative.Replace('/','\'))
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "R35 runner input is missing: $relative" }
    $packagePath = Join-Path $runnerPackageRoot ($relative.Replace('/','\'))
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $packagePath) -Force
    Copy-Item -LiteralPath $sourcePath -Destination $packagePath
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
    if ($sourceHash -ine $packageHash) { throw "R35 runner package copy hash mismatch: $relative" }
    $runnerComponentRecords.Add([ordered]@{path=$relative;sha256=$sourceHash;packagedPath=$packagePath})
}

$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$r25AnalyzerPath = Join-Path $repo 'scripts\dotnet\analyze-c188r25-failfast-origin.py'
$r25AnalyzerText = & $python $r25AnalyzerPath '--candidate-root' $candidateRoot 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "R35 candidate analyzer failed: $r25AnalyzerText" }
$r25AnalyzerResultPath = Join-Path $packageRoot 'r25-analyzer.json'
Set-Content -LiteralPath $r25AnalyzerResultPath -Value $r25AnalyzerText -Encoding utf8
$r25AnalyzerResult = $r25AnalyzerText | ConvertFrom-Json -ErrorAction Stop
if ([string]$r25AnalyzerResult.result -cne 'PASS' -or
    [string]$r25AnalyzerResult.candidate.managedElfSha256 -ine [string]$candidateManifest.managedElfSha256) {
    throw 'R35 candidate analyzer output does not match the genuine linked R33 managed ELF.'
}

$readinessPredicates = Join-Path $repo 'scripts\dotnet\c188r33-settings-handoff-predicates.ps1'
. (Join-Path $repo 'scripts\dotnet\c188r11-runner-predicates.ps1')
. (Join-Path $repo 'scripts\dotnet\c188r11r4r2-runner-predicates.ps1')
. (Join-Path $repo 'scripts\dotnet\c188r12r1-settings-handoff-predicates.ps1')
. $readinessPredicates
$readinessProfile = Get-C188R33ManagedReadinessProfile $candidateRoot
if ([string]$readinessProfile.profile -cne 'C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS' -or
    [uint32]$readinessProfile.expectedDescriptorCount -ne 2 -or
    ($readinessProfile.expectedDescriptorSelectors -join ',') -cne '4,5' -or
    [string]$candidateManifest.actualManagedSettingsFactory -cne
        'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter') {
    throw 'R35 candidate readiness profile or factory identity is incorrect.'
}

$sourceR34PhaseRoot = Join-Path $artifactRootFull 'c188r33-settings-handoff'
$r34BootRoot = Join-Path $sourceR34PhaseRoot 'qac-guest-r25-0daceb0b87f64f9ca45a0340ceacce26'
$r34SerialPath = Join-Path $r34BootRoot 'serial.log'
$r34ScreenshotPath = Join-Path $r34BootRoot 'desktop-ready.ppm'
$r34ScreenshotPngPath = Join-Path $sourceR34PhaseRoot 'r34-desktop-ready.png'
$r34ActivePath = Join-Path $r34BootRoot 'active.json'
$r34QmpPath = Join-Path $r34BootRoot 'qmp.log'
foreach ($path in @($r34SerialPath,$r34ScreenshotPath,$r34ActivePath,$r34QmpPath,$r34ScreenshotPngPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "R34 readiness evidence is missing: $path" }
}
$r34SerialSha256 = (Get-FileHash -LiteralPath $r34SerialPath -Algorithm SHA256).Hash
if ($r34SerialSha256 -ine 'C57ACEC13AEE9B85B1E4C5EF0C02D9629918A199EDBF92618114713356A5FE28') {
    throw 'R34 serial evidence changed from its preserved hash.'
}
$r34Active = Get-Content -LiteralPath $r34ActivePath -Raw | ConvertFrom-Json -ErrorAction Stop
$r34Serial = Get-Content -LiteralPath $r34SerialPath -Raw
$readinessFailure = Get-C188R11ReadinessFailureReason $r34Serial 2 ([uint32[]]@(4,5))
if (-not [string]::IsNullOrWhiteSpace($readinessFailure) -or
    -not (Test-C188R33SettingsClickGate $r34Serial $readinessProfile.profile 2 `
        ([int]$r34Active.screenWidth) ([int]$r34Active.screenHeight) ([uint32[]]@(4,5)))) {
    throw 'R34 live Notes rendering/input-loop evidence no longer satisfies the corrected R35 readiness gate.'
}
$clickTarget = Get-C188R12R1NotesSettingsClickTarget $r34Serial $repo `
    ([int]$r34Active.screenWidth) ([int]$r34Active.screenHeight)
if (-not [bool]$clickTarget.accepted -or [int]$clickTarget.x -ne 815 -or
    [int]$clickTarget.y -ne 550 -or [int]$r34Active.screenWidth -ne 1280 -or
    [int]$r34Active.screenHeight -ne 800) {
    throw 'R34 screenshot-bound layout, dimensions, or source-validated Settings target changed.'
}
$r34Qmp = Get-Content -LiteralPath $r34QmpPath -Raw
if ($r34Qmp -match 'input-send-event|button-down|button-up') {
    throw 'R34 readiness evidence unexpectedly contains a Settings click.'
}

$r34PpmBytes = [System.IO.File]::ReadAllBytes($r34ScreenshotPath)
$ppmHeader = [System.Text.Encoding]::ASCII.GetString($r34PpmBytes,0,[Math]::Min(128,$r34PpmBytes.Length))
$ppmHeaderMatch = [regex]::Match($ppmHeader,'^P6\s+(?<width>\d+)\s+(?<height>\d+)\s+255\s')
if (-not $ppmHeaderMatch.Success -or [int]$ppmHeaderMatch.Groups['width'].Value -ne 1280 -or
    [int]$ppmHeaderMatch.Groups['height'].Value -ne 800) {
    throw 'R34 framebuffer screenshot is not the expected 1280x800 PPM image.'
}

$runnerHashMap = @{}
foreach ($entry in $runnerComponentRecords) { $runnerHashMap[[string]$entry.path] = [string]$entry.sha256 }
$packageManifestPath = Join-Path $packageRoot 'r35-execution-package.json'
$r25AnalyzerSha256 = (Get-FileHash -LiteralPath $r25AnalyzerPath -Algorithm SHA256).Hash
$packageManifest = [ordered]@{
    schema='GUIDEXOS_C188R35_SETTINGS_LAUNCH_PACKAGE_V1'
    result='PASS_PACKAGE_STAGED'
    executionPackageId=$executionPackageId;executionAttemptId=$executionAttemptId
    createdUtc=[DateTime]::UtcNow.ToString('O')
    sourceCandidateRoot=$sourceCandidate;candidateRoot=$candidateRoot
    sourceR33CandidateManifestPath=$sourceR33ManifestPath
    sourceR33CandidateManifestSha256=$expectedOriginalR33ManifestSha256
    candidateManifestPath=$candidateManifestPath;candidateManifestSha256=$candidateManifestSha256
    candidateIntegrityPath=$candidateIntegrityPath;candidateIntegritySha256=$candidateIntegritySha256
    candidateInventoryFileCount=349
    candidateHashes=[ordered]@{
        kernelSha256=(Get-FileHash -LiteralPath (Join-Path $candidateRoot 'esp/kernel.elf') -Algorithm SHA256).Hash
        ramdiskSha256=(Get-FileHash -LiteralPath (Join-Path $candidateRoot 'esp/ramdisk.img') -Algorithm SHA256).Hash
        bootloaderSha256=(Get-FileHash -LiteralPath (Join-Path $candidateRoot 'esp/EFI/BOOT/BOOTX64.EFI') -Algorithm SHA256).Hash
        managedElfSha256=[string]$candidateManifest.managedElfSha256
        runtimePackManifestSha256=[string]$candidateManifest.runtimePackManifestSha256
    }
    readinessProfile=[string]$readinessProfile.profile
    expectedManagedDescriptorCount=[uint32]$readinessProfile.expectedDescriptorCount
    expectedManagedDescriptorSelectors=@($readinessProfile.expectedDescriptorSelectors)
    settingsFactory=[string]$candidateManifest.actualManagedSettingsFactory
    settingsFactoryConstructor=[string]$candidateManifest.actualFactoryConstructorSymbol
    originalR25CandidateManifestPath=$sourceR25ManifestPath
    originalR25CandidateManifestSha256=$expectedOriginalR25ManifestSha256
    r25CandidateManifestPath=$r25ManifestPath;r25CandidateManifestSha256=$r25ManifestSha256
    r25AnalyzerPath=$r25AnalyzerPath;r25AnalyzerSha256=$r25AnalyzerSha256
    r25AnalyzerResultPath=$r25AnalyzerResultPath
    r25AnalyzerResultSha256=(Get-FileHash -LiteralPath $r25AnalyzerResultPath -Algorithm SHA256).Hash
    originalR33SidecarCopyPath=$r33SidecarCopyPath
    originalR33SidecarCopySha256=(Get-FileHash -LiteralPath $r33SidecarCopyPath -Algorithm SHA256).Hash
    r34RunnerSourceHead='928206cb767077a4940115284bb76d916a726d8b'
    r34CorrectedNestedRunnerSha256='D79C5CCB90F68F348E7F53A110C77CAEE6AA13425E318A23684585B1A574B7BC'
    correctedR35NestedRunnerSha256=[string]$runnerHashMap['scripts/dotnet/run-c188r12r1-settings-handoff.ps1']
    runnerComponents=@($runnerComponentRecords)
    r34Evidence=[ordered]@{
        serialPath=$r34SerialPath;serialSha256=$r34SerialSha256
        screenshotPpmPath=$r34ScreenshotPath;screenshotPpmSha256=(Get-FileHash -LiteralPath $r34ScreenshotPath -Algorithm SHA256).Hash
        screenshotPngPath=$r34ScreenshotPngPath;screenshotPngSha256=(Get-FileHash -LiteralPath $r34ScreenshotPngPath -Algorithm SHA256).Hash
        qmpLogPath=$r34QmpPath;qmpLogSha256=(Get-FileHash -LiteralPath $r34QmpPath -Algorithm SHA256).Hash
        activeStatePath=$r34ActivePath;screenWidth=[int]$r34Active.screenWidth;screenHeight=[int]$r34Active.screenHeight
        observedSettingsTarget=$clickTarget;clickSubmitted=$false
    }
    protectedModifiedObjects=$protectedObjects
    rehearsalEvidencePath=(Join-Path $packageRoot 'r35-offline-rehearsal.evidence.json')
    actualOneBootGuardCreated=$false;noNativeExecutionDuringStaging=$true
}
$packageManifest | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $packageManifestPath -Encoding utf8

[ordered]@{
    schema='GUIDEXOS_C188R35_PACKAGE_STAGING_RESULT_V1';result='PASS_PACKAGE_STAGED'
    executionPackageId=$executionPackageId;executionAttemptId=$executionAttemptId
    packageRoot=$packageRoot;candidateRoot=$candidateRoot
    packageManifestPath=$packageManifestPath
    packageManifestSha256=(Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash
    candidateManifestSha256=$candidateManifestSha256;candidateIntegritySha256=$candidateIntegritySha256
    candidateInventoryFileCount=349;r25CandidateManifestSha256=$r25ManifestSha256
    r25AnalyzerSha256=$r25AnalyzerSha256
    protectedModifiedObjectsPreserved=$true;nativeExecutionAttempted=$false
} | ConvertTo-Json -Depth 6 -Compress | Write-Output
