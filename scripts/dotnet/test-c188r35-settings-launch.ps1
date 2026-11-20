param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$PackageRoot = [System.IO.Path]::GetFullPath($PackageRoot)
$packagePath = Join-Path $PackageRoot 'r35-execution-package.json'
$evidencePath = Join-Path $PackageRoot 'r35-offline-rehearsal.evidence.json'
if (-not (Test-Path -LiteralPath $packagePath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $evidencePath -PathType Leaf)) {
    throw 'R35 test requires a staged package and completed offline rehearsal.'
}
$package = Get-Content -LiteralPath $packagePath -Raw | ConvertFrom-Json -ErrorAction Stop
$evidence = Get-Content -LiteralPath $evidencePath -Raw | ConvertFrom-Json -ErrorAction Stop
$wrapperPath = Join-Path $repo 'scripts\dotnet\run-c188r35-settings-launch.ps1'
$wrapperSource = Get-Content -LiteralPath $wrapperPath -Raw
$nestedRunnerSource = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\run-c188r12r1-settings-handoff.ps1') -Raw
$python = (Get-Command python.exe -ErrorAction Stop | Select-Object -First 1).Source
$visualTool = Join-Path $repo 'scripts\dotnet\compare-c188r12r1-screenshots.py'
$referencePpm = [string]$package.r34Evidence.screenshotPpmPath
$assertions = 0
function Assert-C188R35([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw "R35 Settings launch test failed: $Message" }
    $script:assertions++
}

Assert-C188R35 ([string]$evidence.result -ceq 'PASS_OFFLINE_REHEARSAL') 'complete parent-to-nested rehearsal passed'
Assert-C188R35 ((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash -ceq [string]$evidence.executionPackageManifestSha256) 'rehearsal binds the exact package manifest'
Assert-C188R35 ([string]$package.executionPackageId -ceq [string]$evidence.executionPackageId -and
    [string]$package.executionAttemptId -ceq [string]$evidence.executionAttemptId) 'package and fresh attempt identities match rehearsal'
Assert-C188R35 ([string]$package.candidateManifestSha256 -ceq [string]$evidence.candidateManifestSha256 -and
    [string]$package.candidateIntegritySha256 -ceq [string]$evidence.candidateIntegritySha256 -and
    [int]$package.candidateInventoryFileCount -eq 349 -and [int]$evidence.candidateInventoryFileCount -eq 349) 'all 349 candidate inventory entries are bound'
Assert-C188R35 ([bool]$evidence.nestedRehearsal.precreatedEvidenceRootAccepted -and
    [string]$evidence.nestedRehearsal.readinessProfile -ceq 'C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS' -and
    [uint32]$evidence.nestedRehearsal.expectedDescriptorCount -eq 2 -and
    (@($evidence.nestedRehearsal.expectedDescriptorSelectors) -join ',') -ceq '4,5') 'nested runner accepted the candidate-bound two-descriptor profile'
Assert-C188R35 ([bool]$evidence.readiness.accepted -and [bool]$evidence.settingsInteractionStageEligible -and
    [bool]$evidence.readiness.mainLoopObserved -and [bool]$evidence.readiness.notesRenderAndFocusObserved) 'authentic Notes readiness reaches the Settings interaction stage'
Assert-C188R35 ([int]$evidence.readiness.screenWidth -eq 1280 -and [int]$evidence.readiness.screenHeight -eq 800 -and
    [bool]$evidence.readiness.settingsTarget.accepted -and [int]$evidence.readiness.settingsTarget.x -eq 815 -and
    [int]$evidence.readiness.settingsTarget.y -eq 550) 'source-validated button center matches the observed display layout'
$target = $evidence.readiness.settingsTarget
Assert-C188R35 ([int]$target.buttonScreenX -le [int]$target.x -and
    [int]$target.buttonScreenY -le [int]$target.y -and
    [int]$target.buttonScreenX + [int]$target.buttonWidth -ge [int]$target.x -and
    [int]$target.buttonScreenY + [int]$target.buttonHeight -ge [int]$target.y) 'click center lies within the actual Settings button bounds'
Assert-C188R35 ($nestedRunnerSource.Contains("'--verify-settings-target'") -and
    $nestedRunnerSource.Contains('PASS_TARGET_VISIBLE_UNOBSTRUCTED') -and
    $nestedRunnerSource.Contains('Live Notes layout does not match the source-validated Settings target at (815, 550).')) `
    'live target layout and unobstructed button pixels are checked before input'
$visualOutput = & $python $visualTool '--verify-settings-target' $referencePpm $referencePpm '815' '550' 2>&1 | Out-String
$visualExitCode = $LASTEXITCODE
$visualProof = $visualOutput | ConvertFrom-Json -ErrorAction Stop
Assert-C188R35 ($visualExitCode -eq 0 -and [string]$visualProof.result -ceq 'PASS_TARGET_VISIBLE_UNOBSTRUCTED' -and
    [int]$visualProof.matchingPixels -eq 2520 -and [int]$visualProof.totalPixels -eq 2520 -and
    [int]$visualProof.targetBounds.left -eq 770 -and [int]$visualProof.targetBounds.top -eq 536) `
    'source screenshot confirms the full Settings button target patch'
Assert-C188R35 (-not [bool]$evidence.readiness.qmpInputSubmitted -and
    -not [bool]$evidence.readiness.settingsLaunchAlreadyRequested -and
    -not [bool]$evidence.settingsClickSubmitted) 'rehearsal issued no guest input or synthetic factory activation'
Assert-C188R35 (-not [bool]$evidence.qacInvoked -and -not [bool]$evidence.qemuLaunched -and
    -not [bool]$evidence.guestExecuted -and -not [bool]$evidence.realAttemptGuardConsumed -and
    [bool]$evidence.nestedRehearsal.syntheticGuardOnly) 'rehearsal did not consume a real one-boot guard or start a guest'
foreach ($component in @($package.runnerComponents)) {
    $source = Join-Path $repo ([string]$component.path.Replace('/','\'))
    Assert-C188R35 ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ceq [string]$component.sha256 -and
        (Get-FileHash -LiteralPath ([string]$component.packagedPath) -Algorithm SHA256).Hash -ceq [string]$component.sha256) `
        "runner source and package hashes match: $($component.path)"
}
Assert-C188R35 ($wrapperSource.Contains('R35 package identity or candidate-root binding is invalid.') -and
    $wrapperSource.Contains('R35 candidate-specific descriptor profile or genuine Settings factory metadata is invalid.')) 'invalid execution package and candidate profiles fail closed'
Assert-C188R35 ($wrapperSource.Contains('R35 native launch is blocked until the complete offline rehearsal passes.') -and
    $wrapperSource.IndexOf('R35 native launch is blocked until the complete offline rehearsal passes.') -lt
    $wrapperSource.IndexOf('$preflightRoot = ')) 'missing readiness evidence stops before fresh QAC preflight'
Assert-C188R35 ($wrapperSource.Contains('R35 execution package already has a consumed candidate guard; automatic retry is forbidden.') -and
    $wrapperSource.Contains('$selector5Stages[$i] -in @(') -and
    $wrapperSource.Contains("'type-init-enter','constructor-enter'") -and
    $wrapperSource.Contains('$i -gt $selector5InvokeIndex') -and
    -not $wrapperSource.Contains('[bool]$parentEvidence.milestones.M11') ) 'consumed guard reuse is rejected and genuine entry is based on selector-5 trace stages'

$tempBase = [System.IO.Path]::GetFullPath($PackageRoot)
$tempRoot = Join-Path $tempBase ('r35-negative-tests-' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $tempRoot) { throw "Refusing to overwrite R35 negative-test path: $tempRoot" }
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    $pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
    $referenceBytes = [System.IO.File]::ReadAllBytes($referencePpm)
    $ppmHeaderPrefix = [System.Text.Encoding]::ASCII.GetString($referenceBytes,0,[Math]::Min(128,$referenceBytes.Length))
    $ppmHeaderMatch = [regex]::Match($ppmHeaderPrefix, '^P6\s+\d+\s+\d+\s+255\s')
    if (-not $ppmHeaderMatch.Success) { throw 'R34 visual reference has an invalid PPM header.' }
    $ppmPixelOffset = $ppmHeaderMatch.Length
    if ($referenceBytes[$ppmPixelOffset - 1] -eq 13 -and $referenceBytes[$ppmPixelOffset] -eq 10) { $ppmPixelOffset++ }
    $mutatedPpmPath = Join-Path $tempRoot 'target-pixel-mutated.ppm'
    $mutatedBytes = [byte[]]$referenceBytes.Clone()
    $mutatedPixelOffset = $ppmPixelOffset + ((550 * 1280 + 815) * 3)
    $mutatedBytes[$mutatedPixelOffset] = [byte]($mutatedBytes[$mutatedPixelOffset] -bxor 1)
    [System.IO.File]::WriteAllBytes($mutatedPpmPath,$mutatedBytes)
    $negativeVisualOutput = & $python $visualTool '--verify-settings-target' $referencePpm $mutatedPpmPath '815' '550' 2>&1 | Out-String
    $negativeVisualExit = $LASTEXITCODE
    $negativeVisualProof = $negativeVisualOutput | ConvertFrom-Json -ErrorAction Stop
    Assert-C188R35 ($negativeVisualExit -ne 0 -and [string]$negativeVisualProof.result -ceq 'FAIL_TARGET_MISMATCH' -and
        [int]$negativeVisualProof.matchingPixels -eq 2519) 'occlusion or target-pixel mismatch fails closed before click'
    $invalidRoot = Join-Path $tempRoot 'invalid-manifest'
    $null = New-Item -ItemType Directory -Path $invalidRoot
    Set-Content -LiteralPath (Join-Path $invalidRoot 'r35-execution-package.json') -Value '{"schema":"wrong"}' -Encoding utf8
    $invalidOutput = (& $pwsh -NoProfile -File $wrapperPath -PackageRoot $invalidRoot -Mode Rehearse 2>&1 | Out-String)
    $invalidExit = $LASTEXITCODE
    Assert-C188R35 ($invalidExit -ne 0 -and $invalidOutput.Contains('R35 package identity or candidate-root binding is invalid.')) 'invalid package manifest is rejected before execution'

    $scenarioRoot = Join-Path $tempRoot 'candidate-guard-cases'
    $candidateClone = Join-Path $scenarioRoot 'candidate-fixture'
    $null = New-Item -ItemType Directory -Path $scenarioRoot,$candidateClone -Force
    Copy-Item -LiteralPath (Join-Path ([string]$package.candidateRoot) 'candidate-manifest.json') -Destination $candidateClone
    Copy-Item -LiteralPath (Join-Path ([string]$package.candidateRoot) 'candidate-integrity.json') -Destination $candidateClone
    $inventory = Get-Content -LiteralPath (Join-Path $candidateClone 'candidate-integrity.json') -Raw | ConvertFrom-Json -ErrorAction Stop
    foreach ($entry in @($inventory.files)) {
        $source = Join-Path ([string]$package.candidateRoot) ([string]$entry.path.Replace('/','\'))
        $destination = Join-Path $candidateClone ([string]$entry.path.Replace('/','\'))
        $parent = [System.IO.Path]::GetDirectoryName($destination)
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) { $null = New-Item -ItemType Directory -Path $parent -Force }
        Copy-Item -LiteralPath $source -Destination $destination
    }
    $candidateManifestClone = Join-Path $candidateClone 'candidate-manifest.json'
    $candidateIntegrityClone = Join-Path $candidateClone 'candidate-integrity.json'
    $sidecarRoot = Join-Path $scenarioRoot 'r25-sidecars'
    $null = New-Item -ItemType Directory -Path $sidecarRoot
    $sidecarPath = Join-Path $sidecarRoot 'candidate-r25-manifest.json'
    $sidecarSource = Get-Content -LiteralPath ([string]$package.r25CandidateManifestPath) -Raw | ConvertFrom-Json -ErrorAction Stop
    $sidecarClone = [ordered]@{}
    foreach ($property in $sidecarSource.PSObject.Properties) { $sidecarClone[$property.Name] = $property.Value }
    $sidecarClone.candidateRoot = $candidateClone
    $sidecarClone.candidateManifestPath = $candidateManifestClone
    $sidecarClone.candidateIntegrityPath = $candidateIntegrityClone
    $sidecarClone | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $sidecarPath -Encoding utf8
    $sidecarHash = (Get-FileHash -LiteralPath $sidecarPath -Algorithm SHA256).Hash
    $clonePackage = [ordered]@{}
    foreach ($property in $package.PSObject.Properties) { $clonePackage[$property.Name] = $property.Value }
    $clonePackage.candidateRoot = $candidateClone
    $clonePackage.r25CandidateManifestPath = $sidecarPath
    $clonePackage.r25CandidateManifestSha256 = $sidecarHash
    $clonePackage.rehearsalEvidencePath = Join-Path $scenarioRoot 'missing-rehearsal.evidence.json'
    $clonePackage.executionPackageId = [guid]::NewGuid().ToString('N')
    $clonePackage.executionAttemptId = [guid]::NewGuid().ToString('N')
    $cloneManifestPath = Join-Path $scenarioRoot 'r35-execution-package.json'
    $clonePackage | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $cloneManifestPath -Encoding utf8
    $missingOutput = (& $pwsh -NoProfile -File $wrapperPath -PackageRoot $scenarioRoot -Mode RunNative 2>&1 | Out-String)
    $missingExit = $LASTEXITCODE
    Assert-C188R35 ($missingExit -ne 0 -and $missingOutput.Contains('R35 native launch is blocked until the complete offline rehearsal passes.')) 'missing rehearsal readiness is rejected before QAC admission'

    $cloneManifestHash = (Get-FileHash -LiteralPath $cloneManifestPath -Algorithm SHA256).Hash
    [ordered]@{result='PASS_OFFLINE_REHEARSAL';executionPackageManifestSha256=$cloneManifestHash
        executionAttemptId=[string]$clonePackage.executionAttemptId} |
        ConvertTo-Json | Set-Content -LiteralPath ([string]$clonePackage.rehearsalEvidencePath) -Encoding utf8
    $cloneGuardRoot = Join-Path $scenarioRoot 'one-boot-guards'
    $null = New-Item -ItemType Directory -Path $cloneGuardRoot
    [ordered]@{candidateRoot=$candidateClone;consumed=$true;automaticRetry=$false;retryAllowed=$false} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $cloneGuardRoot ('attempt-r25-' + [guid]::NewGuid().ToString('N') + '.json')) -Encoding utf8
    $consumedOutput = (& $pwsh -NoProfile -File $wrapperPath -PackageRoot $scenarioRoot -Mode RunNative 2>&1 | Out-String)
    $consumedExit = $LASTEXITCODE
    Assert-C188R35 ($consumedExit -ne 0 -and $consumedOutput.Contains('R35 execution package already has a consumed candidate guard; automatic retry is forbidden.')) 'consumed guard cannot authorize a second guest attempt'
} finally {
    $resolvedTemp = [System.IO.Path]::GetFullPath($tempRoot)
    $tempPrefix = [System.IO.Path]::GetFullPath($tempBase).TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedTemp.StartsWith($tempPrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
        [System.IO.Path]::GetFileName($resolvedTemp) -notmatch '^r35-negative-tests-[a-f0-9]{32}$') {
        throw "Refusing unsafe R35 negative-test cleanup path: $resolvedTemp"
    }
    if (Test-Path -LiteralPath $resolvedTemp) { Remove-Item -LiteralPath $resolvedTemp -Recurse -Force }
}
Write-Output "C188R35_SETTINGS_LAUNCH_TESTS=$assertions RESULT=PASS"
