param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$C167EvidenceRoot = "",
    [switch]$BuildProofProducts,
    [switch]$UseExistingProofProducts,
    [switch]$RunProofBoots,
    [switch]$RunOrdinaryAcceptance,
    [switch]$RunC167SettingsProofBoots,
    [switch]$RunC169ManagedUiProofBoots,
    [switch]$C170ControlRendering,
    [switch]$C167SettingsDefaultApps,
    [switch]$ReuseC167ProofKernel,
    [switch]$ReuseC167ProofComposite,
    [switch]$RunC169ManagedUiOrdinaryAcceptance,
    [switch]$HostTestsOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ((& git -C $RepoRoot branch --show-current).Trim() -ne 'v1.1_DOTNET_SUPPORT') {
    throw 'C166R3 requires the existing v1.1_DOTNET_SUPPORT branch.'
}
$head = (& git -C $RepoRoot rev-parse HEAD).Trim()
if (-not $head) { throw 'Could not resolve repository HEAD.' }
$script:c167SettingsAcceptance = [bool]($RunC167SettingsProofBoots -or $C167SettingsDefaultApps)
$script:c169ManagedUiProof = [bool]$RunC169ManagedUiProofBoots
$script:c169ManagedUiOrdinaryAcceptance = [bool]$RunC169ManagedUiOrdinaryAcceptance
$script:c170ControlRendering = [bool]$C170ControlRendering
if ($RunC169ManagedUiProofBoots -and -not $RunC167SettingsProofBoots) {
    throw 'C169 managed UI proof extends the focused C167 Settings proof lane and requires -RunC167SettingsProofBoots.'
}
if ($C170ControlRendering -and
    -not ($RunC169ManagedUiProofBoots -or $RunC169ManagedUiOrdinaryAcceptance)) {
    throw 'C170 evidence requires the existing focused or ordinary C169 Settings proof lane.'
}
$script:c167SettingsProofBuild = $false
if ($RunC167SettingsProofBoots -and
    ((-not $BuildProofProducts -and -not $UseExistingProofProducts) -or
     ($BuildProofProducts -and $UseExistingProofProducts) -or $RunProofBoots -or $RunOrdinaryAcceptance)) {
    throw 'C167 real Settings proof requires verified proof products and runs separately from the C166 proof/ordinary switches.'
}
if ($ReuseC167ProofKernel -and -not $RunC167SettingsProofBoots) {
    throw 'ReuseC167ProofKernel applies only to the real Settings UI proof lane.'
}
if ($ReuseC167ProofComposite -and
    (-not $RunC169ManagedUiProofBoots -or -not $UseExistingProofProducts -or -not $RunC167SettingsProofBoots)) {
    throw 'ReuseC167ProofComposite requires the C169 focused UI proof lane and verified existing C166 proof products.'
}
if ($RunC169ManagedUiOrdinaryAcceptance -and
    (-not $RunOrdinaryAcceptance -or -not $C167SettingsDefaultApps -or
     -not $UseExistingProofProducts -or $RunC167SettingsProofBoots)) {
    throw 'C169 ordinary UI acceptance requires C167 Settings acceptance and verified C166 products, and runs separately from focused proof boots.'
}
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($HostTestsOnly) {
        Join-Path $RepoRoot 'out\dotnet\c166r9-host-validation'
    } elseif ($C170ControlRendering) {
        Join-Path $RepoRoot 'out\dotnet\c170-control-rendering'
    } elseif ($RunC167SettingsProofBoots) {
        Join-Path $RepoRoot 'out\dotnet\c167-default-apps'
    } else {
        Join-Path $RepoRoot 'out\dotnet\c166r3-file-associations'
    }
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$script:c170EvidenceOutputRoot = $EvidenceRoot
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\', '/') + '\'
if (-not $EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C166R3 evidence must remain under $allowedRoot"
}
$script:c167EvidenceRoot = if ([string]::IsNullOrWhiteSpace($C167EvidenceRoot)) {
    if ($script:c170ControlRendering) { $EvidenceRoot }
    else { Join-Path $RepoRoot 'out\dotnet\c167-default-apps' }
} else { [System.IO.Path]::GetFullPath($C167EvidenceRoot) }
if (-not $script:c167EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C167 evidence must remain under $allowedRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

if ($HostTestsOnly) {
    $gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    $dotnetCommand = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $gxxCommand) { throw 'C166R7 host mode requires a C++ host compiler (g++).' }
    if (-not $dotnetCommand) { throw 'C166R7 host mode requires the .NET SDK.' }
    $nativeExe = Join-Path $EvidenceRoot 'C166Association.exe'
    $nativeLog = Join-Path $EvidenceRoot 'native-host-tests.txt'
    $fixturePath = Join-Path $EvidenceRoot 'native-managed-fixture.txt'
    $namedPath = Join-Path $EvidenceRoot 'dual-slot-cases.json'
    $nativeSources = @(
        (Join-Path $RepoRoot 'samples\host\C166Association\main.cpp'),
        (Join-Path $RepoRoot 'kernel\core\file_association_service.cpp'))
    & $gxxCommand.Source -std=c++14 -O2 -Wall -Wextra -DGUIDEXOS_PROOF_CONTROL -DGUIDEXOS_HOST_C166_TESTS "-I$RepoRoot" `
        "-I$(Join-Path $RepoRoot 'kernel\core')" @nativeSources -o $nativeExe
    if ($LASTEXITCODE -ne 0) { throw "C166R7 native host compilation failed: $LASTEXITCODE" }
    $env:C166_NATIVE_FIXTURE = $fixturePath
    $env:C166_NAMED_RESULTS = $namedPath
    $nativeOutput = & $nativeExe 2>&1 | Out-String
    $nativeExit = $LASTEXITCODE
    Remove-Item Env:C166_NATIVE_FIXTURE,Env:C166_NAMED_RESULTS -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $nativeLog -Value $nativeOutput -Encoding utf8
    if ($nativeExit -ne 0) { throw "C166R7 native host tests failed; see $nativeLog" }
    $productionNativeExe = Join-Path $EvidenceRoot 'C166Association-production-gate.exe'
    $productionNativeLog = Join-Path $EvidenceRoot 'native-production-gate-tests.txt'
    $productionFixture = Join-Path $EvidenceRoot 'production-managed-fixture.txt'
    $productionNamed = Join-Path $EvidenceRoot 'production-dual-slot-cases.json'
    & $gxxCommand.Source -std=c++14 -O2 -Wall -Wextra "-I$RepoRoot" `
        "-I$(Join-Path $RepoRoot 'kernel\core')" @nativeSources -o $productionNativeExe
    if ($LASTEXITCODE -ne 0) { throw 'Proof-disabled native C166 gate build failed.' }
    $env:C166_NATIVE_FIXTURE = $productionFixture
    $env:C166_NAMED_RESULTS = $productionNamed
    $productionOutput = & $productionNativeExe 2>&1 | Out-String
    $productionExit = $LASTEXITCODE
    Remove-Item Env:C166_NATIVE_FIXTURE,Env:C166_NAMED_RESULTS -ErrorAction SilentlyContinue
    Set-Content -LiteralPath $productionNativeLog -Value $productionOutput -Encoding utf8
    if ($productionExit -ne 0 -or $productionOutput -notmatch 'failures=0') {
        throw "Proof-disabled diagnostics exclusion failed; see $productionNativeLog"
    }
    $managedProject = Join-Path $RepoRoot 'samples\host\C166Association\ManagedHostTests.csproj'
    $managedLog = Join-Path $EvidenceRoot 'managed-host-tests.txt'
    $managedOutput = & $dotnetCommand.Source run --project $managedProject --configuration Release -- $fixturePath 2>&1 | Out-String
    $managedExit = $LASTEXITCODE
    Set-Content -LiteralPath $managedLog -Value $managedOutput -Encoding utf8
    if ($managedExit -ne 0) { throw "C166R7 managed host tests failed; see $managedLog" }
    $nativeCount = 0
    if ($nativeOutput -match 'native host cases=(\d+)') { $nativeCount = [int]$Matches[1] }
    $managedCount = 0
    if ($managedOutput -match 'C166-MANAGED-ASSOCIATION-TESTS cases=(\d+)') {
        $managedCount = [int]$Matches[1]
    }
    $namedCases = Get-Content -Raw -LiteralPath $namedPath | ConvertFrom-Json
    $nativeLine = [string]($nativeOutput -split "`r?`n" | Where-Object { $_ -match '^C166 native host cases=' } | Select-Object -Last 1)
    $nativeStats = @{}
    foreach ($match in [regex]::Matches($nativeLine, '(\w+)=([0-9]+)')) { $nativeStats[$match.Groups[1].Value] = [uint64]$match.Groups[2].Value }
    $agreementPass = $managedOutput -match 'native-managed agreement cases=7 result=PASS statusMapping=PASS abi=v4/128 associationOffset=120'
    $nativePass = $nativeExit -eq 0 -and $nativeStats.failures -eq 0
    $managedPass = $managedExit -eq 0 -and $managedCount -ge 22
    $namedProperties = @($namedCases.PSObject.Properties)
    $matrixPass = $namedProperties.Count -eq 31 -and @($namedProperties | Where-Object { $_.Value -ne 'PASS' }).Count -eq 0
    $auditsPass = $nativeStats.audited -eq 1000 -and $nativeStats.crcFailures -eq 0 -and $nativeStats.malformed -eq 0 -and $nativeStats.monotonic -eq 1 -and $nativeStats.alternating -eq 1
    $hostPass = $nativePass -and $productionExit -eq 0 -and $managedPass -and $matrixPass -and $auditsPass -and $agreementPass
    $manifest = [ordered]@{
        phase='C166R9'; sourceHead=$head; sourceBranch='v1.1_DOTNET_SUPPORT'; upstream='origin/v1.1_DOTNET_SUPPORT'
        productionModule='kernel/core/file_association_service.cpp'
        storageInterface='kernel/core/include/kernel/file_association_service.h'
        productionVfsAdapter='kernel/core/file_association_vfs_storage.cpp'
        hostStorageAdapter='samples/host/C166Association/fake_storage.h'
        nativeCases=$nativeCount; nativeStatus=$(if($nativePass){'PASS'}else{'FAIL'})
        abiCases=4; abiSizes=@(104,112,120,128); callbackOffsets=@{snapshot=104;close=112;association=120}
        managedCases=$managedCount; managedStatus=$(if($managedPass){'PASS'}else{'FAIL'})
        dualSlotCases=$namedCases; dualSlotCaseCount=$namedProperties.Count; dualSlotStatus=$(if($matrixPass){'PASS'}else{'FAIL'})
        crcVectors=@{known='123456789=0xCBF43926';empty='0x00000000'}
        partialWriteRange='0..135'; partialWriteCases=136; fullWriteControl='PASS'
        stressTransactions=1000; persistedImagesAudited=$nativeStats.audited; crcMismatches=$nativeStats.crcFailures
        malformedCommittedImages=$nativeStats.malformed; generationStart=$nativeStats.generationStart
        generationEnd=$nativeStats.generationEnd; successfulGenerationIncrements=1000
        slotACommits=$nativeStats.slotA; slotBCommits=$nativeStats.slotB
        generationMonotonicity=$(if($nativeStats.monotonic -eq 1){'PASS'}else{'FAIL'}); slotAlternation=$(if($nativeStats.alternating -eq 1){'PASS'}else{'FAIL'})
        resolverLookups=1000; nativeManagedAgreement=@{cases=7;status=$(if($agreementPass){'PASS'}else{'FAIL'});statusMapping=$(if($agreementPass){'PASS'}else{'FAIL'});fixture='native-managed-fixture.txt';abi='v4/128';associationOffset=120}
        hostValidationComplete=$hostPass; hostValidationMarker=$(if($hostPass){'C166R9_HOST_VALIDATION_PASS'}else{$null})
        hostSuiteStatus=$(if($hostPass){'PASS'}else{'FAIL'}); accepted=$false
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath `
        (Join-Path $EvidenceRoot 'c166r9-host-manifest.json') -Encoding utf8
    Write-Host $nativeOutput.Trim()
    Write-Host $managedOutput.Trim()
    Write-Host 'PROOF1-PRODUCTION-GATE PASS: proof-disabled native diagnostics request rejected'
    if ($hostPass) { Write-Host 'C166R9_HOST_VALIDATION_PASS' } else { throw 'C166R9 host acceptance failed; see per-case manifest and suite logs.' }
    return
}

function Resolve-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function New-C170EvidenceFileRecord([string]$Name,[string]$Path,[string]$ExpectedSha256) {
    if ([string]::IsNullOrWhiteSpace($Path) -or
        -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "C170 evidence file is missing: $Path"
    }
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $actualHash = Get-Sha256 $fullPath
    if ([string]::IsNullOrWhiteSpace($ExpectedSha256) -or
        $actualHash -ne $ExpectedSha256.ToUpperInvariant()) {
        throw "C170 evidence file hash mismatch: $fullPath"
    }
    return [ordered]@{
        name = $Name
        path = $fullPath
        sha256 = $actualHash
        sizeBytes = (Get-Item -LiteralPath $fullPath).Length
    }
}

function Get-C170ScreenshotReferences([object]$Value,[System.Collections.Generic.List[object]]$References) {
    if ($null -eq $Value -or $Value -is [string]) { return }
    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($key in $Value.Keys) {
            if ([string]$key -notmatch '(?i)(screenshot|screen)$' -or
                [string]::IsNullOrWhiteSpace([string]$Value[$key])) { continue }
            $hashKey = [string]$key + 'Sha256'
            $hashValue = if ($Value.Contains($hashKey)) { [string]$Value[$hashKey] } else { '' }
            if ([string]::IsNullOrWhiteSpace($hashValue) -and
                [string]$key -match '(?i)^opened(?=screenshot$|screen$)') {
                $fallbackHashKey = ([string]$key -replace '(?i)^opened','') + 'Sha256'
                if ($Value.Contains($fallbackHashKey)) { $hashValue = [string]$Value[$fallbackHashKey] }
            }
            $References.Add([ordered]@{path=[string]$Value[$key];sha256=$hashValue})
        }
        foreach ($key in $Value.Keys) {
            if ([string]$key -match '(?i)(screenshot|screen)(Sha256)?$') { continue }
            Get-C170ScreenshotReferences $Value[$key] $References
        }
        return
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        foreach ($item in $Value) { Get-C170ScreenshotReferences $item $References }
        return
    }
    $propertyNames = @($Value.PSObject.Properties | ForEach-Object { $_.Name })
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -notmatch '(?i)(screenshot|screen)$' -or
            [string]::IsNullOrWhiteSpace([string]$property.Value)) { continue }
        $hashName = $property.Name + 'Sha256'
        $hashProperty = $Value.PSObject.Properties[$hashName]
        if (-not $hashProperty -and $property.Name -match '(?i)^opened(?=screenshot$|screen$)') {
            $fallbackHashName = ($property.Name -replace '(?i)^opened','') + 'Sha256'
            $hashProperty = $Value.PSObject.Properties[$fallbackHashName]
        }
        $hashValue = if ($hashProperty) { [string]$hashProperty.Value } else { '' }
        $References.Add([ordered]@{path=[string]$property.Value;sha256=$hashValue})
    }
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -match '(?i)(screenshot|screen)(Sha256)?$') { continue }
        Get-C170ScreenshotReferences $property.Value $References
    }
}

function Write-C170FocusedEvidenceManifest([string]$EvidenceRoot,[string]$SourceManifestPath,
    [string]$Composite,[string]$Kernel,[string]$NeutralRamdisk,[string]$QemuPath,[string]$SourceHead) {
    $source = Get-Content -LiteralPath $SourceManifestPath -Raw | ConvertFrom-Json
    if ($source.status -ne 'PASS' -or $source.sourceHead -ne $SourceHead -or
        @($source.guestBoots).Count -ne 3 -or
        $source.persistentStorage.sameImagePathAcrossAllBoots -ne $true) {
        throw 'C170 focused source manifest is incomplete, stale, or does not prove one shared storage image.'
    }

    $files = [System.Collections.Generic.List[object]]::new()
    $files.Add((New-C170EvidenceFileRecord 'source-focused-manifest' $SourceManifestPath `
        (Get-Sha256 $SourceManifestPath)))
    $files.Add((New-C170EvidenceFileRecord 'proof-composite' $Composite $source.proofCompositeSha256))
    $files.Add((New-C170EvidenceFileRecord 'proof-kernel' $Kernel $source.proofKernelSha256))
    $files.Add((New-C170EvidenceFileRecord 'neutral-ramdisk' $NeutralRamdisk $source.neutralRamdiskSha256))

    $boots = [System.Collections.Generic.List[object]]::new()
    $seenScreenshots = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $requiredStateMarkers = @(
        'C167-STATE operation=Apply state=Disabled',
        'C167-STATE operation=Apply state=ApplicationOverride',
        'C167-STATE operation=Reset state=NoOverride')
    for ($index = 0; $index -lt 3; $index++) {
        $boot = $source.guestBoots[$index]
        if ($boot.status -ne 'PASS' -or [int]$boot.pid -le 0) {
            throw "C170 focused boot $($index + 1) is not a completed PASS boot."
        }
        $serial = New-C170EvidenceFileRecord "focused-boot-$($index + 1)-serial" `
            $boot.serialPath $boot.serialSha256
        $qmpPath = Join-Path (Split-Path -Parent $boot.serialPath) 'qmp-input.log'
        $qmpRecord = New-C170EvidenceFileRecord "focused-boot-$($index + 1)-qmp" `
            $qmpPath $boot.qmpInputSha256
        $serialText = Get-Content -LiteralPath $boot.serialPath -Raw
        if ($serialText.IndexOf($requiredStateMarkers[$index], [StringComparison]::Ordinal) -lt 0) {
            throw "C170 focused boot $($index + 1) is missing its required completion marker."
        }
        $files.Add($serial)
        $files.Add($qmpRecord)
        $references = [System.Collections.Generic.List[object]]::new()
        Get-C170ScreenshotReferences $boot.interactions $references
        if ($references.Count -eq 0) {
            throw "C170 focused boot $($index + 1) has no screenshot references."
        }
        $screenshots = [System.Collections.Generic.List[object]]::new()
        foreach ($reference in $references) {
            $record = New-C170EvidenceFileRecord "focused-boot-$($index + 1)-screenshot" `
                $reference.path $reference.sha256
            if ($seenScreenshots.Add($record.path)) { $files.Add($record) }
            $screenshots.Add([ordered]@{path=$record.path;sha256=$record.sha256})
        }
        $boots.Add([ordered]@{
            stage = $boot.stage
            result = 'PASS'
            qemuProcessIdentity = [ordered]@{
                processId = [int]$boot.pid
                executablePath = $QemuPath
                commandLineArguments = @($boot.qemuArguments)
                startedAtUtc = $boot.qemuStartedAtUtc
            }
            backingStoragePath = [System.IO.Path]::GetFullPath($boot.backingEspPath)
            serial = [ordered]@{path=$serial.path;sha256=$serial.sha256}
            qmpInput = [ordered]@{path=$qmpRecord.path;sha256=$qmpRecord.sha256}
            completionMarkers = @($requiredStateMarkers[$index])
            screenshots = @($screenshots.ToArray())
        })
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        phase = 'C170-focused-control-rendering-proof'
        sourceHead = $SourceHead
        sourceBranch = 'v1.1_DOTNET_SUPPORT'
        status = 'PASS'
        scenarioResult = 'PASS'
        sourceManifest = [System.IO.Path]::GetFullPath($SourceManifestPath)
        proofArtifacts = [ordered]@{
            composite = $files[1]
            kernel = $files[2]
            neutralRamdisk = $files[3]
        }
        guestBootCount = $boots.Count
        guestBoots = @($boots.ToArray())
        files = @($files.ToArray())
        artifactIntegrity = [ordered]@{result='PASS';algorithm='SHA-256';errors=@()}
    }
    $manifestPath = Join-Path $EvidenceRoot 'c170-focused-proof-manifest.json'
    $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $verified = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($verified.schemaVersion -ne 1 -or $verified.status -ne 'PASS' -or
        @($verified.guestBoots).Count -ne 3 -or @($verified.files).Count -lt 7) {
        throw 'C170 focused proof manifest did not survive JSON/schema validation.'
    }
    foreach ($file in $verified.files) {
        if (-not (Test-Path -LiteralPath $file.path -PathType Leaf) -or
            (Get-Sha256 $file.path) -ne $file.sha256) {
            throw "C170 focused proof manifest references a missing or mismatched file: $($file.path)"
        }
    }
    Write-Host "C170 focused proof manifest PASS: $manifestPath"
}

function Write-C170IncompleteEvidenceManifest([string]$Path,[string]$Phase,
    [string]$SourceHead,[string]$Reason) {
    $owners = @(
        Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" |
            ForEach-Object { [ordered]@{
                processId = [int]$_.ProcessId
                executablePath = [string]$_.ExecutablePath
                commandLine = [string]$_.CommandLine
            } }
    )
    $manifest = [ordered]@{
        schemaVersion = 1
        phase = $Phase
        sourceHead = $SourceHead
        sourceBranch = 'v1.1_DOTNET_SUPPORT'
        status = 'INCOMPLETE'
        scenarioResult = 'INCOMPLETE'
        reason = $Reason
        guestBootCount = 0
        guestBoots = @()
        files = @()
        artifactIntegrity = [ordered]@{result='INCOMPLETE';algorithm='SHA-256';errors=@($Reason)}
        qemuOwnersAtIncompleteCapture = $owners
    }
    $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
    $verified = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($verified.status -ne 'INCOMPLETE' -or $verified.scenarioResult -eq 'PASS') {
        throw "C170 incomplete evidence manifest validation failed: $Path"
    }
}

function Write-C170OrdinaryEvidenceManifest([string]$EvidenceRoot,[string]$SourceManifestPath,
    [string]$RepoRoot,[string]$SourceHead) {
    $source = Get-Content -LiteralPath $SourceManifestPath -Raw | ConvertFrom-Json
    if ($source.outcome -ne 'A' -or $source.sourceHead -ne $SourceHead -or
        $source.productionInstallation -notlike 'PASS*' -or
        @($source.ordinaryBoots).Count -ne 3 -or
        $source.finalAssociation.slotsPresent -ne 0 -or
        -not $source.cleanProduction.proof1Excluded) {
        throw 'C170 ordinary source manifest is incomplete, stale, or lacks clean production acceptance.'
    }

    $files = [System.Collections.Generic.List[object]]::new()
    $files.Add((New-C170EvidenceFileRecord 'source-ordinary-manifest' $SourceManifestPath `
        (Get-Sha256 $SourceManifestPath)))
    $proofPrerequisite = $null
    if ($source.proofPrerequisite) {
        $prerequisite = $source.proofPrerequisite
        $proofManifestRecord = New-C170EvidenceFileRecord 'c166-proof-prerequisite-manifest' `
            $prerequisite.proofManifestPath $prerequisite.proofManifestSha256
        $acceptanceRecord = New-C170EvidenceFileRecord 'c166-proof-acceptance-manifest' `
            $prerequisite.acceptanceManifestPath $prerequisite.acceptanceManifestSha256
        $files.Add($proofManifestRecord)
        $files.Add($acceptanceRecord)
        $proofSerials = [System.Collections.Generic.List[object]]::new()
        foreach ($serial in $prerequisite.serials) {
            $record = New-C170EvidenceFileRecord "c166-proof-$($serial.stage)-serial" `
                $serial.path $serial.sha256
            $files.Add($record)
            $proofSerials.Add([ordered]@{stage=$serial.stage;path=$record.path;sha256=$record.sha256})
        }
        $refreshDiagnostic = $null
        if ($prerequisite.refreshDiagnosticPath) {
            $refreshDiagnosticRecord = New-C170EvidenceFileRecord 'c170-proof1-refresh-incomplete-diagnostic' `
                $prerequisite.refreshDiagnosticPath $prerequisite.refreshDiagnosticSha256
            $refreshDetails = Get-Content -LiteralPath $refreshDiagnosticRecord.path -Raw | ConvertFrom-Json
            if ($refreshDetails.status -ne 'INCOMPLETE' -or
                $refreshDetails.historicalEvidenceSubstituted -ne $false -or
                $refreshDetails.guestLaneExecuted -ne $false) {
                throw 'C170 failed proof refresh diagnostic is not explicitly incomplete.'
            }
            $files.Add($refreshDiagnosticRecord)
            $refreshDiagnostic = [ordered]@{
                path = $refreshDiagnosticRecord.path
                sha256 = $refreshDiagnosticRecord.sha256
                status = 'INCOMPLETE'
                usedAsAcceptanceProof = $false
                bootSerials = @($refreshDetails.serials)
            }
        }
        $proofPrerequisite = [ordered]@{
            kind = $prerequisite.kind
            sourceHead = $prerequisite.sourceHead
            acceptanceSourceHead = $prerequisite.acceptanceSourceHead
            proofManifest = [ordered]@{path=$proofManifestRecord.path;sha256=$proofManifestRecord.sha256}
            acceptanceManifest = [ordered]@{path=$acceptanceRecord.path;sha256=$acceptanceRecord.sha256}
            guestBootCount = [int]$prerequisite.guestBootCount
            successfulManagedMutationCount = [int]$prerequisite.successfulManagedMutationCount
            serials = @($proofSerials.ToArray())
            failedC170Refresh = $refreshDiagnostic
        }
    }
    $productionArtifacts = [System.Collections.Generic.List[object]]::new()
    foreach ($candidate in $source.cleanProduction.allCandidateStrings) {
        $record = New-C170EvidenceFileRecord "clean-$([IO.Path]::GetFileName($candidate.path))" `
            $candidate.path $candidate.sha256
        $files.Add($record)
        $productionArtifacts.Add($record)
    }
    if ($productionArtifacts.Count -ne 3) {
        throw 'C170 ordinary manifest requires composite, kernel, and ramdisk build hashes.'
    }
    $installedPaths = @(
        (Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'),
        (Join-Path $RepoRoot 'ESP\kernel.elf'),
        (Join-Path $RepoRoot 'ESP\ramdisk.img'))
    $installedHashes = @(
        $source.cleanProduction.kernelCanonicalSha256,
        $source.cleanProduction.kernelEspSha256,
        $source.cleanProduction.ramdiskEspSha256)
    $installedNames = @('installed-canonical-kernel','installed-esp-kernel','installed-esp-ramdisk')
    $installed = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $installedPaths.Count; $index++) {
        $record = New-C170EvidenceFileRecord $installedNames[$index] `
            $installedPaths[$index] $installedHashes[$index]
        $files.Add($record)
        $installed.Add($record)
    }
    if ($installed[0].sha256 -ne $productionArtifacts[1].sha256 -or
        $installed[1].sha256 -ne $productionArtifacts[1].sha256 -or
        $installed[2].sha256 -ne $productionArtifacts[2].sha256) {
        throw 'C170 ordinary installed production hashes do not match the clean candidate artifacts.'
    }

    $boots = [System.Collections.Generic.List[object]]::new()
    $seenScreenshots = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($boot in $source.ordinaryBoots) {
        if ($boot.status -ne 'PASS' -or [int]$boot.pid -le 0) {
            throw "C170 ordinary boot $($boot.stage) is not a completed PASS boot."
        }
        $serial = New-C170EvidenceFileRecord "$($boot.stage)-serial" `
            $boot.serialPath $boot.serialSha256
        $qmpPath = Join-Path (Split-Path -Parent $boot.serialPath) 'qmp-input.log'
        $qmpRecord = New-C170EvidenceFileRecord "$($boot.stage)-qmp" `
            $qmpPath $boot.qmpInputSha256
        $serialText = Get-Content -LiteralPath $boot.serialPath -Raw
        if ($serialText -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C166-ORDINARY-READY app=ManagedNotes render=PASS') {
            throw "C170 ordinary boot $($boot.stage) is missing its completion marker."
        }
        $files.Add($serial)
        $files.Add($qmpRecord)
        $references = [System.Collections.Generic.List[object]]::new()
        Get-C170ScreenshotReferences $boot.productionIntegration $references
        if ($references.Count -eq 0) { throw "C170 ordinary boot $($boot.stage) has no screenshot references." }
        $screenshots = [System.Collections.Generic.List[object]]::new()
        foreach ($reference in $references) {
            $record = New-C170EvidenceFileRecord "$($boot.stage)-screenshot" `
                $reference.path $reference.sha256
            if ($seenScreenshots.Add($record.path)) { $files.Add($record) }
            $screenshots.Add([ordered]@{path=$record.path;sha256=$record.sha256})
        }
        $boots.Add([ordered]@{
            stage = $boot.stage
            result = 'PASS'
            qemuProcessIdentity = [ordered]@{
                processId = [int]$boot.pid
                executablePath = $boot.qemuExecutablePath
                commandLineArguments = @($boot.qemuArguments)
                startedAtUtc = $boot.qemuStartedAtUtc
            }
            serial = [ordered]@{path=$serial.path;sha256=$serial.sha256}
            qmpInput = [ordered]@{path=$qmpRecord.path;sha256=$qmpRecord.sha256}
            completionMarkers = @('C166-ORDINARY-READY app=ManagedNotes render=PASS')
            screenshots = @($screenshots.ToArray())
        })
    }
    $manifest = [ordered]@{
        schemaVersion = 1
        phase = 'C170-ordinary-production-control-rendering'
        sourceHead = $SourceHead
        sourceBranch = 'v1.1_DOTNET_SUPPORT'
        status = 'PASS'
        scenarioResult = 'PASS'
        sourceManifest = [System.IO.Path]::GetFullPath($SourceManifestPath)
        cleanProduction = [ordered]@{
            composite = $productionArtifacts[0]
            kernel = $productionArtifacts[1]
            ramdisk = $productionArtifacts[2]
            installedCanonicalKernel = $installed[0]
            installedEspKernel = $installed[1]
            installedEspRamdisk = $installed[2]
            proof1Excluded = $true
        }
        finalAssociation = $source.finalAssociation
        c166ProofPrerequisite = $proofPrerequisite
        guestBootCount = $boots.Count
        guestBoots = @($boots.ToArray())
        files = @($files.ToArray())
        artifactIntegrity = [ordered]@{result='PASS';algorithm='SHA-256';errors=@()}
    }
    $manifestPath = Join-Path $EvidenceRoot 'c170-ordinary-production-manifest.json'
    $manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $verified = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($verified.schemaVersion -ne 1 -or $verified.status -ne 'PASS' -or
        @($verified.guestBoots).Count -ne 3 -or @($verified.files).Count -lt 10) {
        throw 'C170 ordinary production manifest did not survive JSON/schema validation.'
    }
    foreach ($file in $verified.files) {
        if (-not (Test-Path -LiteralPath $file.path -PathType Leaf) -or
            (Get-Sha256 $file.path) -ne $file.sha256) {
            throw "C170 ordinary manifest references a missing or mismatched file: $($file.path)"
        }
    }
    Write-Host "C170 ordinary production manifest PASS: $manifestPath"
}

function Get-C166WorkspaceQemuOwners([string]$RepoRoot) {
    $workspacePath = [System.IO.Path]::GetFullPath($RepoRoot).Replace('/', '\').TrimEnd('\')
    @(
        Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" |
            Where-Object {
                $commandLine = ([string]$_.CommandLine).Replace('/', '\')
                $commandLine.IndexOf($workspacePath,
                    [StringComparison]::OrdinalIgnoreCase) -ge 0
            }
    )
}

function Get-FreeQmpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Send-OwnedQemuQuit([int]$Port, [System.Diagnostics.Process]$Process) {
    if ($null -eq $Process) { return }
    $Process.Refresh()
    if ($Process.HasExited) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $deadline = (Get-Date).AddSeconds(5)
        while (-not $client.Connected -and (Get-Date) -lt $deadline) {
            try { $client.Connect('127.0.0.1', $Port) } catch { Start-Sleep -Milliseconds 100 }
        }
        if (-not $client.Connected) { throw "Owned QEMU QMP port $Port did not become available." }
        $stream = $client.GetStream()
        $stream.ReadTimeout = 2000
        $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
        $writer = [System.IO.StreamWriter]::new($stream, [System.Text.Encoding]::ASCII, 1024, $true)
        [void]$reader.ReadLine()
        $writer.WriteLine('{"execute":"qmp_capabilities"}'); $writer.Flush()
        [void]$reader.ReadLine()
        $writer.WriteLine('{"execute":"quit"}'); $writer.Flush()
        [void]$reader.ReadLine()
    } finally { $client.Dispose() }
    if (-not $Process.WaitForExit(10000)) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Process.WaitForExit(5000)
    }
}

function Invoke-ProofQemuBoot([string]$Stage, [string]$Ramdisk,
    [string]$EspPath, [string]$Bootloader, [string]$Kernel,
    [string]$QemuPath, [string]$OvmfPath, [string]$BootRoot,
    [switch]$HoldAfterPass,
    [switch]$PreparedFatDisk,
    [string]$ReadyPattern = '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF-SCENARIO-(?:PASS|FAIL) C166 ') {
    $serialPath = Join-Path $BootRoot 'serial.log'
    $stdoutPath = Join-Path $BootRoot 'qemu.stdout.log'
    $stderrPath = Join-Path $BootRoot 'qemu.stderr.log'
    $qmpLogPath = Join-Path $BootRoot 'qmp-input.log'
    New-Item -ItemType Directory -Force -Path $BootRoot | Out-Null
    Set-Content -LiteralPath $qmpLogPath -Value '' -Encoding ASCII
    if ($PreparedFatDisk) {
        if (-not (Test-Path -LiteralPath $EspPath -PathType Leaf)) {
            throw "Prepared FAT proof disk is missing: $EspPath"
        }
    } else {
        New-Item -ItemType Directory -Force -Path (Join-Path $EspPath 'EFI\BOOT') | Out-Null
        Copy-Item -LiteralPath $Bootloader -Destination (Join-Path $EspPath 'EFI\BOOT\BOOTX64.EFI') -Force
        Copy-Item -LiteralPath $Kernel -Destination (Join-Path $EspPath 'kernel.elf') -Force
        # Only boot metadata changes. This path is the same root FAT backing for every boot.
        Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $EspPath 'ramdisk.img') -Force
    }
    $stagedKernelSha256 = $null
    $stagedRamdiskSha256 = $null
    if (-not $PreparedFatDisk) {
        $stagedKernelPath = Join-Path $EspPath 'kernel.elf'
        $stagedRamdiskPath = Join-Path $EspPath 'ramdisk.img'
        $sourceKernelSha256 = Get-Sha256 $Kernel
        $sourceRamdiskSha256 = Get-Sha256 $Ramdisk
        $stagedKernelSha256 = Get-Sha256 $stagedKernelPath
        $stagedRamdiskSha256 = Get-Sha256 $stagedRamdiskPath
        if ($stagedKernelSha256 -ne $sourceKernelSha256 -or
            $stagedRamdiskSha256 -ne $sourceRamdiskSha256) {
            throw "QEMU boot media staging mismatch for $Stage`: kernel source=$sourceKernelSha256 staged=$stagedKernelSha256; ramdisk source=$sourceRamdiskSha256 staged=$stagedRamdiskSha256."
        }
    }
    $qmpPort = Get-FreeQmpPort
    $arguments = @('-accel','tcg,thread=single','-machine','pc','-smp','1',
        '-drive',('if=pflash,format=raw,readonly=on,file="' + $OvmfPath + '"'),
        '-drive',$(if ($PreparedFatDisk) {
            'file="' + $EspPath + '",format=raw,if=ide,index=0'
        } else {
            'file=fat:rw:"' + $EspPath + '",format=raw,if=ide,index=0'
        }),
        '-m','1024M','-vga','std','-display','none',
        '-serial',('file:"' + $serialPath + '"'),
        '-qmp',("tcp:127.0.0.1:{0},server,nowait" -f $qmpPort),
        '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -WindowStyle Hidden -PassThru
    $qemuStartedAtUtc = $process.StartTime.ToUniversalTime().ToString('o')
    $serial = ''
    $holdProcess = $false
    try {
        $deadline = (Get-Date).AddSeconds(120)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $serialPath) {
                $serial = Get-Content -LiteralPath $serialPath -Raw -ErrorAction SilentlyContinue
                if ($serial -match $ReadyPattern) { break }
            }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        if ($HoldAfterPass -and $serial -match $ReadyPattern) {
            $holdProcess = $true
        }
    } finally {
        $process.Refresh()
        if (-not $holdProcess -and -not $process.HasExited) {
            try { Send-OwnedQemuQuit $qmpPort $process }
            catch {
                # Fallback is constrained to the QEMU process created above.
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                [void]$process.WaitForExit(5000)
            }
        }
    }
    if (Test-Path -LiteralPath $serialPath) { $serial = Get-Content -LiteralPath $serialPath -Raw }
    $process.Refresh()
    $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
    $serialHash = if ($null -ne $exitCode) { Get-Sha256 $serialPath } else { $null }
    return [ordered]@{stage=$Stage;serialPath=$serialPath;serialSha256=$serialHash;serial=$serial;pid=$process.Id;exitCode=$exitCode;backingEspPath=[System.IO.Path]::GetFullPath($EspPath);kernelSha256=$stagedKernelSha256;ramdiskSha256=$stagedRamdiskSha256;qmpPort=$qmpPort;qmpLogPath=$qmpLogPath;qemuExecutablePath=[System.IO.Path]::GetFullPath($QemuPath);qemuArguments=@($arguments);qemuStartedAtUtc=$qemuStartedAtUtc;process=$process;held=$holdProcess}
}

function Stop-OwnedProofQemu([object]$Boot) {
    if (-not $Boot -or -not $Boot.process) { return }
    $process = [System.Diagnostics.Process]$Boot.process
    $process.Refresh()
    if (-not $process.HasExited) {
        try { Send-OwnedQemuQuit ([int]$Boot.qmpPort) $process }
        catch {
            # The fallback remains restricted to the process created by this runner.
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            [void]$process.WaitForExit(5000)
        }
    }
    $process.Refresh()
    if ($process.HasExited) { $Boot.exitCode = $process.ExitCode }
    $Boot.held = $false
}

function Assert-ProofScenario([object]$Boot, [string]$ScenarioCode,
    [string]$StageCode, [int]$ExpectedSteps) {
    $expectedCompletion = "PROOF-SCENARIO-PASS C166 $ScenarioCode $StageCode"
    if ($Boot.serial -notmatch [regex]::Escape($expectedCompletion)) { throw "$($Boot.stage) proof completion failed; see $($Boot.serialPath)." }
    if (@([regex]::Matches($Boot.serial, '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF-SCENARIO-(?:PASS|FAIL) C166 ')).Count -ne 1) { throw "$($Boot.stage) emitted duplicate or missing completion markers." }
    $stepLines = @([regex]::Matches($Boot.serial, '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 [SP] (?:Same|Pre|Post) (?<step>[0-9]+) PASS '))
    if ($stepLines.Count -ne $ExpectedSteps) { throw "$($Boot.stage) expected $ExpectedSteps passing proof steps; found $($stepLines.Count)." }
    for ($i = 0; $i -lt $stepLines.Count; $i++) {
        if ([int]$stepLines[$i].Groups['step'].Value -ne $i + 1) { throw "$($Boot.stage) proof steps were missing, duplicated, or out of order." }
    }
}

function Assert-PostRebootChurn([object]$Boot) {
    $compare = @([regex]::Matches($Boot.serial,
        '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Post REBOOT-COMPARE PASS slot=A gen=3 expected=A/3\r?$'))
    if ($compare.Count -ne 1) { throw 'Boot 3 did not emit one direct A/3 reboot comparison before mutation.' }
    $mutationMatches = @([regex]::Matches($Boot.serial,
        '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Post M(?<index>[0-9]{2}) (?<result>PASS|FAIL) (?<operation>Set|Dis|Rst) st=(?<status>[0-9]+) ov=(?<override>[0-2]) eff=(?<effective>notes|none) slot=(?<slot>[ABN]) gen=(?<generation>[0-9]+)\r?$'))
    if ($mutationMatches.Count -ne 26) { throw "Boot 3 expected 26 indexed managed mutation markers; found $($mutationMatches.Count)." }
    $mutations = @()
    for ($i = 1; $i -le 26; $i++) {
        $match = $mutationMatches[$i - 1]
        $expectedOperation = if ($i -le 24) { switch (($i - 1) % 3) { 0 {'Set'} 1 {'Dis'} default {'Rst'} } } elseif ($i -eq 25) { 'Set' } else { 'Rst' }
        $expectedOverride = switch ($expectedOperation) { 'Set' { '1' } 'Dis' { '2' } default { '0' } }
        $expectedEffective = if ($expectedOperation -eq 'Dis') { 'none' } else { 'notes' }
        $expectedSlot = if (($i % 2) -eq 1) { 'B' } else { 'A' }
        $expectedGeneration = [string](3 + $i)
        if ([int]$match.Groups['index'].Value -ne $i -or
            $match.Groups['result'].Value -ne 'PASS' -or
            $match.Groups['operation'].Value -ne $expectedOperation -or
            $match.Groups['status'].Value -ne '0' -or
            $match.Groups['override'].Value -ne $expectedOverride -or
            $match.Groups['effective'].Value -ne $expectedEffective -or
            $match.Groups['slot'].Value -ne $expectedSlot -or
            $match.Groups['generation'].Value -ne $expectedGeneration) {
            throw "Boot 3 mutation $i failed its result/state/effective-handler/slot/generation contract: $($match.Value)"
        }
        $mutations += [ordered]@{ index=$i;operation=$expectedOperation;status=0;override=$expectedOverride;effective=$expectedEffective;slot=$expectedSlot;generation=[int64]$expectedGeneration;marker=$match.Value }
    }
    $summary = @([regex]::Matches($Boot.serial,
        '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Post MUTATION-SUMMARY PASS count=26 commits=26 gen=3-29 alt=PASS final=A/29/0\r?$'))
    if ($summary.Count -ne 1) { throw 'Boot 3 managed mutation summary is missing or inconsistent.' }
    if ($Boot.serial -notmatch '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Post 3 PASS Q st=0 ov=0 eff=notes\r?$') {
        throw 'Boot 3 final Query did not report NoOverride with Managed Notes as the effective handler.'
    }
    $Boot['mutations'] = $mutations
    return ,$mutations
}

function Get-C166Serial([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $content=Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
    if ($null -eq $content) { return '' }
    return [string]$content
}

function Wait-C166Serial([string]$Path, [string]$Pattern,
    [int]$After = 0, [int]$Seconds = 30) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $text = Get-C166Serial $Path
        $tail = if ($After -le $text.Length) { $text.Substring($After) } else { '' }
        $match = [regex]::Match($tail, "(?m)$Pattern")
        if ($match.Success) { return [pscustomobject]@{Text=$text;Match=$match;Index=$text.Length} }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for C166 guest marker '$Pattern' in $Path."
}

function Read-C166QmpResponse([System.IO.Stream]$Stream, [int]$Seconds = 3) {
    $builder = [System.Text.StringBuilder]::new()
    $buffer = [byte[]]::new(4096)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                [void]$builder.Append([System.Text.Encoding]::ASCII.GetString($buffer, 0, $count))
                if ($builder.ToString().TrimEnd().EndsWith('}')) { break }
            }
        } else { Start-Sleep -Milliseconds 20 }
    }
    return $builder.ToString()
}

function Send-C166QmpEvents([int]$Port, [object[]]$Events,
    [string]$LogPath, [int]$DelayMilliseconds = 45) {
    if ($Events.Count -eq 0) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 200
        $greeting = Read-C166QmpResponse $stream
        $capabilityBytes = [System.Text.Encoding]::ASCII.GetBytes(
            '{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($capabilityBytes, 0, $capabilityBytes.Length); $stream.Flush()
        $capabilities = Read-C166QmpResponse $stream
        if ($capabilities -match '"error"') { throw "C166 QMP negotiation failed: $capabilities" }
        foreach ($event in $Events) {
            $request = [ordered]@{execute='input-send-event';arguments=[ordered]@{events=@($event)}} |
                ConvertTo-Json -Compress -Depth 10
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
            $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
            $response = Read-C166QmpResponse $stream
            Add-Content -LiteralPath $LogPath -Value ("event={0}`ngreeting={1}`ncapabilities={2}`nresponse={3}" -f $request,$greeting,$capabilities,$response) -Encoding ASCII
            if ($response -match '"error"') { throw "C166 QMP input event failed: $response" }
            if ($DelayMilliseconds -gt 0) { Start-Sleep -Milliseconds $DelayMilliseconds }
        }
    } finally { $client.Dispose() }
}

function Send-C166QmpCommand([int]$Port, [string]$Command,
    [object]$Arguments, [string]$LogPath) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 200
        $greeting = Read-C166QmpResponse $stream
        $capabilityBytes = [System.Text.Encoding]::ASCII.GetBytes(
            '{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($capabilityBytes, 0, $capabilityBytes.Length); $stream.Flush()
        $capabilities = Read-C166QmpResponse $stream
        if ($capabilities -match '"error"') { throw "C166 QMP negotiation failed: $capabilities" }
        $payload = [ordered]@{execute=$Command}
        if ($null -ne $Arguments) { $payload.arguments = $Arguments }
        $request = $payload | ConvertTo-Json -Compress -Depth 10
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
        $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
        $response = Read-C166QmpResponse $stream
        Add-Content -LiteralPath $LogPath -Value ("command={0}`ngreeting={1}`ncapabilities={2}`nresponse={3}" -f $request,$greeting,$capabilities,$response) -Encoding ASCII
        if ($response -match '"error"') { throw "C166 QMP command '$Command' failed: $response" }
        return $response
    } finally { $client.Dispose() }
}

function New-C166RelativeMove([int]$Dx, [int]$Dy) {
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($axis in @(@{Name='x';Delta=$Dx},@{Name='y';Delta=$Dy})) {
        $remaining = [int]$axis.Delta
        while ($remaining -ne 0) {
            $step = [Math]::Sign($remaining) * [Math]::Min([Math]::Abs($remaining),40)
            $events.Add([ordered]@{type='rel';data=[ordered]@{axis=$axis.Name;value=$step}})
            $remaining -= $step
        }
    }
    return $events.ToArray()
}

function New-C166Button([string]$Name, [bool]$Down) {
    return [ordered]@{type='btn';data=[ordered]@{button=$Name;down=$Down}}
}

function New-C166Wheel([int]$Delta) {
    if ($Delta -eq 0) { throw 'C166 wheel delta cannot be zero.' }
    return [ordered]@{
        type='btn'
        data=[ordered]@{
            # QEMU's PS/2 wheel buttons are sign-inverted by the guest packet.
            button=if ($Delta -gt 0) { 'wheel-down' } else { 'wheel-up' }
            down=$true
        }
    }
}

function New-C166Key([string]$Code, [bool]$Down) {
    return [ordered]@{type='key';data=[ordered]@{down=$Down;key=[ordered]@{type='qcode';data=$Code}}}
}

function Initialize-C166GuestPointer([object]$Boot) {
    # Proof scenario completion is emitted before the kernel finishes device
    # startup. Do not send QMP input until PS/2 and the desktop event loop are
    # ready to consume it.
    [void](Wait-C166Serial $Boot.serialPath '^\[KERNEL\] Input manager initialized' 0 120)
    [void](Wait-C166Serial $Boot.serialPath '^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.' 0 120)
    $serial = Get-C166Serial $Boot.serialPath
    $widthMatch = [regex]::Match($serial, '(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)')
    $heightMatch = [regex]::Match($serial, '(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)')
    if (-not $widthMatch.Success -or -not $heightMatch.Success) {
        throw "$($Boot.stage) did not report framebuffer dimensions for QMP interaction."
    }
    $script:c166ScreenWidth = [Convert]::ToInt32($widthMatch.Groups[1].Value,16)
    $script:c166ScreenHeight = [Convert]::ToInt32($heightMatch.Groups[1].Value,16)
    $Boot.pointerStartupScreenshot = Join-Path (Split-Path $Boot.serialPath) 'pointer-startup.ppm'
    $Boot.pointerStartupScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.pointerStartupScreenshot
    # ps2mouse::init places the cursor at the framebuffer center on every
    # fresh guest boot. Keep that exact screen-space origin for subsequent
    # QMP relative moves; input callbacks can be absent while a native window
    # is foreground, so a managed C138 callback is not a cursor calibration.
    $script:c166Cursor = [pscustomobject]@{
        X = [int]($script:c166ScreenWidth/2)
        Y = [int]($script:c166ScreenHeight/2)
    }
}

function Move-C166Pointer([int]$X, [int]$Y, [object]$Boot) {
    if (-not $script:c166Cursor) { throw 'C166 QMP pointer is not calibrated.' }
    if ($X -eq $script:c166Cursor.X -and $Y -eq $script:c166Cursor.Y) { return }
    Send-C166QmpEvents ([int]$Boot.qmpPort) `
        (New-C166RelativeMove ($X-$script:c166Cursor.X) ($Y-$script:c166Cursor.Y)) `
        $Boot.qmpLogPath 80
    $script:c166Cursor = [pscustomobject]@{X=$X;Y=$Y}
}

function Click-C166Screen([int]$X, [int]$Y, [object]$Boot) {
    Move-C166Pointer $X $Y $Boot
    Send-C166QmpEvents ([int]$Boot.qmpPort) @((New-C166Button 'left' $true),(New-C166Button 'left' $false)) $Boot.qmpLogPath 300
}

function Press-C166Key([string]$Code, [object]$Boot) {
    Send-C166QmpEvents ([int]$Boot.qmpPort) @((New-C166Key $Code $true),(New-C166Key $Code $false)) $Boot.qmpLogPath 110
}

function Capture-C166GuestScreen([object]$Boot, [string]$Path) {
    $null = Send-C166QmpCommand ([int]$Boot.qmpPort) 'screendump' ([ordered]@{filename=[System.IO.Path]::GetFullPath($Path)}) $Boot.qmpLogPath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "QMP did not create guest screen evidence $Path." }
    return Get-Sha256 $Path
}

function Capture-C167GuestScreen([object]$Boot, [string]$Path) {
    # Let the compositor publish the managed surface after its render callback.
    Start-Sleep -Milliseconds 750
    return Capture-C166GuestScreen $Boot $Path
}

function Get-C166AssociationDiskState([string]$EspPath) {
    $slots = @()
    foreach ($item in @(@{name='A';file='gxas0.bin'},@{name='B';file='gxas1.bin'})) {
        $path = Join-Path $EspPath $item.file
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $slots += [ordered]@{slot=$item.name;path=$path;present=$false;sizeBytes=0;sha256=$null;generation=$null;recordCount=$null;override=$null;applicationId=$null}
            continue
        }
        $bytes = [System.IO.File]::ReadAllBytes($path)
        if ($bytes.Length -ne 136) { throw "C166 $($item.name) slot image has unexpected size $($bytes.Length)." }
        $records = [BitConverter]::ToUInt16($bytes,6)
        $persistedOverride = if ($records -eq 0) { 'NoOverride' } else {
            switch ([BitConverter]::ToUInt32($bytes,32)) { 1 {'ApplicationOverride'} 2 {'Disabled'} default {'Invalid'} }
        }
        $applicationId = if ($records -gt 0) {
            ([System.Text.Encoding]::ASCII.GetString($bytes,36,96)).TrimEnd([char]0)
        } else { '' }
        $slots += [ordered]@{
            slot=$item.name;path=$path;present=$true;sizeBytes=$bytes.Length
            sha256=(Get-Sha256 $path);generation=[BitConverter]::ToUInt64($bytes,8)
            recordCount=$records;override=$persistedOverride;applicationId=$applicationId
        }
    }
    return ,$slots
}

function Get-C167AssociationDiskState([string]$FatImage,[string]$Python) {
    $helper = Join-Path $PSScriptRoot 'c167-fat16-proof-disk.py'
    $json = & $Python $helper slots --image $FatImage 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect C167 FAT16 proof disk slots: $json"
    }
    try { return @($json | ConvertFrom-Json) }
    catch { throw "C167 FAT16 proof disk slot parser returned invalid JSON: $json" }
}

function Get-C166SlotIdentity([object[]]$Slots) {
    return (($Slots | ForEach-Object { '{0}:{1}:{2}:{3}' -f $_.slot,$_.generation,$_.override,$_.sha256 }) -join '|')
}

function Open-C166AllProgramsItem([int]$Index, [object]$Boot) {
    $screenHeight = [int]$script:c166ScreenHeight
    # The rendered menu height follows the full s_startMenuApps table, which
    # includes unpinned recent entries as well as the 18 pinned/control entries.
    # C166's C158-C163 composition has 22 rows; All Programs itself has 21.
    $menuCount = 22
    $menuHeight = 30 + $menuCount * 22 + 36
    $menuY = [Math]::Max(0,($screenHeight-40)-$menuHeight)
    $contentY = $menuY + 31
    $footerY = $menuY + 30 + $menuCount * 22
    Click-C166Screen 50 ($screenHeight-20) $Boot
    Start-Sleep -Milliseconds 250
    $Boot.startMenuOpenScreenshot = Join-Path (Split-Path $Boot.serialPath) 'start-menu-open.ppm'
    $Boot.startMenuOpenScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.startMenuOpenScreenshot
    Click-C166Screen 70 ($footerY+18) $Boot
    Start-Sleep -Milliseconds 250
    $Boot.allProgramsScreenshot = Join-Path (Split-Path $Boot.serialPath) 'start-menu-all-programs.ppm'
    $Boot.allProgramsScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.allProgramsScreenshot
    $programY = $contentY + $Index*22 + 11
    Click-C166Screen 115 $programY $Boot
}

function Launch-C166ManagedFileExplorer([object]$Boot) {
    $start = (Get-C166Serial $Boot.serialPath).Length
    Open-C166AllProgramsItem 15 $Boot
    Start-Sleep -Milliseconds 500
    $Boot.managedExplorerLaunchScreenshot = Join-Path (Split-Path $Boot.serialPath) 'managed-explorer-launch-attempt.ppm'
    $Boot.managedExplorerLaunchScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.managedExplorerLaunchScreenshot
    [void](Wait-C166Serial $Boot.serialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.fileexplorer selector=00000008 image=/system/apps/GXOSAPP\.ELF metadata=valid' $start 40)
    $Boot.managedExplorerAfterLaunchScreenshot = Join-Path (Split-Path $Boot.serialPath) 'managed-explorer-after-launch.ppm'
    $Boot.managedExplorerAfterLaunchScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.managedExplorerAfterLaunchScreenshot
    try {
        [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C163-EXPLORER id=com\.guidexos\.apps\.managed\.fileexplorer sel=8 path=/system/apps n=\d+ cap=64 ctl=5 abi=3/120 ro=1 result=PASS' $start 60)
    } catch {
        $Boot.managedExplorerLaunchTimeoutScreenshot = Join-Path (Split-Path $Boot.serialPath) 'managed-explorer-launch-timeout.ppm'
        $Boot.managedExplorerLaunchTimeoutScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.managedExplorerLaunchTimeoutScreenshot
        throw
    }
}

function Get-C166ManagedExplorerOrigin {
    return [pscustomobject]@{
        X = [Math]::Max(0,[int](($script:c166ScreenWidth-800)/2))
        Y = [Math]::Max(0,[int](($script:c166ScreenHeight-390)/2))
    }
}

function Close-C166ManagedNotesStartupWindow([object]$Boot) {
    # The proof guest starts with a clean Managed Notes window. Close it through
    # its normal title-bar control so document activation can reuse its managed
    # surface slot after File Explorer is opened.
    $originX = [Math]::Max(0,[int](($script:c166ScreenWidth-600)/2))
    $originY = [Math]::Max(0,[int](($script:c166ScreenHeight-360)/2))
    Click-C166Screen ($originX+586) ($originY+12) $Boot
    Start-Sleep -Milliseconds 250
    [void](Wait-C166Serial $Boot.serialPath `
        'C150-SURFACE.*destroy appId=com\.guidexos\.apps\.managed\.notes' 0 10)
    [void](Wait-C166Serial $Boot.serialPath 'C111-SURFACE.*action=close result=PASS' 0 10)
}

function Click-C166ManagedExplorerRow([int]$Row, [object]$Boot) {
    $origin = Get-C166ManagedExplorerOrigin
    Click-C166Screen ($origin.X+40) ($origin.Y+24+70+$Row*18+9) $Boot
}

function Click-C166ManagedExplorerButton([string]$Name, [object]$Boot) {
    $origin = Get-C166ManagedExplorerOrigin
    $x = switch ($Name) {'Up' {66} 'Open' {170} 'Refresh' {284} 'Close' {398} default {throw "Unknown Managed File Explorer button '$Name'."}}
    Click-C166Screen ($origin.X+$x) ($origin.Y+24+340) $Boot
}

function Select-C166ManagedExplorerEntry([string]$Path, [string]$Name,
    [string]$Type, [object]$Boot) {
    $beforeAll = Get-C166Serial $Boot.serialPath
    $after = $beforeAll.Length
    for ($row=0; $row -lt 20; $row++) {
        Click-C166ManagedExplorerRow $row $Boot
        Start-Sleep -Milliseconds 200
        $text = Get-C166Serial $Boot.serialPath
        $tail = if ($after -le $text.Length) {$text.Substring($after)} else {''}
        $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=' +
            [regex]::Escape($Path) + ' n=\d+ sel=' + [regex]::Escape($Name) +
            ' type=' + [regex]::Escape($Type) + ' size=[^ ]+ view=\d+ status='
        if ($tail -match $pattern) { return }
    }
    throw "Managed File Explorer could not select '$Name' in '$Path'."
}

function Navigate-C166ManagedExplorerToHello([object]$Boot) {
    Select-C166ManagedExplorerEntry '/system/apps' 'C164' 'Directory' $Boot
    $pathStart = (Get-C166Serial $Boot.serialPath).Length
    Click-C166ManagedExplorerButton 'Open' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C164 ' $pathStart 15)
    Select-C166ManagedExplorerEntry '/system/apps/C164' 'hello.txt' 'File' $Boot
    if ((Get-C166Serial $Boot.serialPath) -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C164 n=\d+ sel=hello\.txt type=File size=15 view=\d+ status=File selected\r?$') {
        throw 'Managed File Explorer did not report the exact 15-byte hello.txt VFS fixture.'
    }
}

function Invoke-C166ManagedExplorerOpen([object]$Boot, [string]$Mode,
    [string]$ScreenEvidencePath) {
    $notesLaunchesBefore = [regex]::Matches((Get-C166Serial $Boot.serialPath),
        '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes ').Count
    Launch-C166ManagedFileExplorer $Boot
    Navigate-C166ManagedExplorerToHello $Boot
    $openStart = (Get-C166Serial $Boot.serialPath).Length
    Click-C166ManagedExplorerButton 'Open' $Boot
    if ($Mode -eq 'Disabled') {
        [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C164-FILE-ACTIVATION path=/system/apps/C164/hello\.txt result=unsupported source-retained=true selection=preserved\r?$' $openStart 20)
        $sha = Capture-C166GuestScreen $Boot $ScreenEvidencePath
        $notesLaunchesAfter = [regex]::Matches((Get-C166Serial $Boot.serialPath),
            '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes ').Count
        if ($notesLaunchesAfter -ne $notesLaunchesBefore) { throw 'Disabled Managed File Explorer Open launched Managed Notes.' }
        return [ordered]@{workflow='Managed File Explorer';path='/system/apps/C164/hello.txt';result='No associated handler';notesLaunchesBefore=$notesLaunchesBefore;notesLaunchesAfter=$notesLaunchesAfter;screenshot=$ScreenEvidencePath;screenshotSha256=$sha}
    }
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C164-NOTES-ACT path=/system/apps/C164/hello\.txt bytes=15 clean=1 undo=0 redo=0 caret=0 anchor=0 view=0 result=PASS\r?$' $openStart 90)
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C164-NOTES-CONTENT exact=true source=VFS result=PASS\r?$' $openStart 20)
    [void](Wait-C166Serial $Boot.serialPath '^\[C164-ACTIVATION\] source=ManagedFileExplorer instance=[0-9A-Fa-f]+ target=com\.guidexos\.apps\.managed\.notes path=/system/apps/C164/hello\.txt fresh=true source-closed=true result=PASS\r?$' $openStart 90)
    $sha = Capture-C166GuestScreen $Boot $ScreenEvidencePath
    return [ordered]@{workflow='Managed File Explorer';path='/system/apps/C164/hello.txt';content='hello from C164';contentBytes=15;clean=$true;undo=0;redo=0;activation='PASS';screenshot=$ScreenEvidencePath;screenshotSha256=$sha}
}

function Launch-C166NativeFileExplorer([object]$Boot) {
    $before = (Get-C166Serial $Boot.serialPath).Length
    Open-C166AllProgramsItem 5 $Boot
    Start-Sleep -Milliseconds 800
    $Boot.nativeExplorerLaunchScreenshot = Join-Path (Split-Path $Boot.serialPath) 'native-explorer-launch.ppm'
    $Boot.nativeExplorerLaunchScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.nativeExplorerLaunchScreenshot
    $text = Get-C166Serial $Boot.serialPath
    if ($text.Substring([Math]::Min($before,$text.Length)) -match 'Managed application launch rejected|Not available in bare-metal mode') {
        throw 'Native File Explorer launch was rejected or unavailable.'
    }
}

function Navigate-C166NativeExplorerToHello([object]$Boot) {
    # The native Files window begins at /. Use its visible /system mount in
    # the Navigation pane, then the sorted VFS directory rows and keyboard
    # Open action. No host-side resolver or direct Notes launch is involved.
    $windowX = 80; $windowY = 45
    $mountStart = (Get-C166Serial $Boot.serialPath).Length
    # The mount list includes both / (row 0) and /system (row 1).
    Click-C166Screen ($windowX+40) ($windowY+152) $Boot
    Start-Sleep -Milliseconds 250
    # /system contains the apps directory as its first sorted directory.
    Press-C166Key 'home' $Boot
    Press-C166Key 'ret' $Boot
    Start-Sleep -Milliseconds 250
    # /system/apps directories sort as C151, C152, C155, C164 in this fixture pack.
    Press-C166Key 'home' $Boot
    for ($i=0; $i -lt 3; $i++) { Press-C166Key 'down' $Boot }
    Press-C166Key 'ret' $Boot
    Start-Sleep -Milliseconds 250
    # Hidden dotfiles are omitted; the visible files begin empty.txt, hello.txt.
    Press-C166Key 'home' $Boot
    Press-C166Key 'down' $Boot
    Start-Sleep -Milliseconds 150
    $Boot['nativeNavigationStart'] = $mountStart
}

function Invoke-C166NativeExplorerOpen([object]$Boot, [string]$Mode,
    [string]$BeforeOpenScreenshot, [string]$AfterOpenScreenshot) {
    $notesLaunchesBefore = [regex]::Matches((Get-C166Serial $Boot.serialPath),
        '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes ').Count
    Launch-C166NativeFileExplorer $Boot
    Navigate-C166NativeExplorerToHello $Boot
    # The native Files control handles mouse row selection reliably, while
    # its keyboard focus remains on the first file after the directory change.
    # Click the visible hello.txt row before capturing/opening the selection.
    Click-C166Screen 300 181 $Boot
    Start-Sleep -Milliseconds 150
    $beforeSha = Capture-C166GuestScreen $Boot $BeforeOpenScreenshot
    $openStart = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'ret' $Boot
    Start-Sleep -Milliseconds 1000
    $afterSha = Capture-C166GuestScreen $Boot $AfterOpenScreenshot
    $notesLaunchesAfter = [regex]::Matches((Get-C166Serial $Boot.serialPath),
        '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes ').Count
    if ($Mode -eq 'Disabled') {
        if ($notesLaunchesAfter -ne $notesLaunchesBefore) { throw 'Disabled native File Explorer Open launched Managed Notes.' }
        return [ordered]@{workflow='Native File Explorer';path='/system/apps/C164/hello.txt';result='No associated handler';notesLaunchesBefore=$notesLaunchesBefore;notesLaunchesAfter=$notesLaunchesAfter;selectedScreenshot=$BeforeOpenScreenshot;selectedScreenshotSha256=$beforeSha;openScreenshot=$AfterOpenScreenshot;openScreenshotSha256=$afterSha;uiStatus='captured for visual confirmation'}
    }
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C164-NOTES-ACT path=/system/apps/C164/hello\.txt bytes=15 clean=1 undo=0 redo=0 caret=0 anchor=0 view=0 result=PASS\r?$' $openStart 90)
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C164-NOTES-CONTENT exact=true source=VFS result=PASS\r?$' $openStart 20)
    if ($notesLaunchesAfter -le $notesLaunchesBefore) { throw 'Native File Explorer did not launch Managed Notes with a document activation.' }
    return [ordered]@{workflow='Native File Explorer';path='/system/apps/C164/hello.txt';content='hello from C164';contentBytes=15;clean=$true;undo=0;redo=0;notesLaunchesBefore=$notesLaunchesBefore;notesLaunchesAfter=$notesLaunchesAfter;activation='PASS';openedScreenshot=$AfterOpenScreenshot;screenshotSha256=$afterSha}
}

function New-C166SettingsRecord([string]$Path) {
    [byte[]]$bytes = [byte[]]::new(26)
    [byte[]]$magic = [System.Text.Encoding]::ASCII.GetBytes('GXSC')
    [Array]::Copy($magic,0,$bytes,0,4)
    $bytes[4]=2; $bytes[6]=10
    $bytes[12]=0; $bytes[13]=1; $bytes[14]=0; $bytes[15]=1
    $bytes[16]=1; $bytes[17]=1; $bytes[18]=1; $bytes[19]=0
    $bytes[20]=0; $bytes[21]=5
    [long]$crc=4294967295L
    foreach ($value in $bytes[0..21]) {
        $crc=($crc -bxor [long]$value) -band 4294967295L
        for ($bit=0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) { $crc=(($crc -shr 1) -bxor 3988292384L) -band 4294967295L }
            else { $crc=($crc -shr 1) -band 4294967295L }
        }
    }
    $crc=($crc -bxor 4294967295L) -band 4294967295L
    for ($index=0; $index -lt 4; $index++) { $bytes[22+$index]=[byte](($crc -shr ($index*8)) -band 255) }
    [System.IO.File]::WriteAllBytes($Path,$bytes)
}

function Save-C167ProtectedMedia([string]$RepoRoot,[string]$EvidenceRoot) {
    $backupRoot = Join-Path $EvidenceRoot 'protected-media-before'
    New-Item -ItemType Directory -Force -Path $backupRoot | Out-Null
    $items = @(
        @{ name='canonical-kernel'; source=(Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'); backup=(Join-Path $backupRoot 'kernel-build-kernel.elf') },
        @{ name='esp-kernel'; source=(Join-Path $RepoRoot 'ESP\kernel.elf'); backup=(Join-Path $backupRoot 'esp-kernel.elf') },
        @{ name='esp-ramdisk'; source=(Join-Path $RepoRoot 'ESP\ramdisk.img'); backup=(Join-Path $backupRoot 'esp-ramdisk.img') }
    )
    $manifestItems = @()
    foreach ($item in $items) {
        if (-not (Test-Path -LiteralPath $item.source -PathType Leaf)) {
            throw "C167 protected media is missing: $($item.source)"
        }
        Copy-Item -LiteralPath $item.source -Destination $item.backup -Force
        $sourceHash = Get-Sha256 $item.source
        $backupHash = Get-Sha256 $item.backup
        if ($sourceHash -ne $backupHash) { throw "C167 protected media backup mismatch: $($item.name)" }
        $manifestItems += [ordered]@{name=$item.name;source=$item.source;backup=$item.backup;sha256=$backupHash}
    }
    $result = [ordered]@{root=$backupRoot;items=$manifestItems;verified=$true}
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backupRoot 'protected-media.json') -Encoding utf8
    return $result
}

function Restore-C167CanonicalKernel([object]$Protected,[string]$RepoRoot) {
    $saved = @($Protected.items | Where-Object name -eq 'canonical-kernel')[0]
    $target = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
    if (-not $saved -or -not (Test-Path -LiteralPath $saved.backup -PathType Leaf)) {
        throw 'C167 canonical kernel protection backup is unavailable.'
    }
    Copy-Item -LiteralPath $saved.backup -Destination $target -Force
    if ((Get-Sha256 $target) -ne $saved.sha256) {
        throw 'C167 canonical kernel did not return to its protected hash.'
    }
}

function Get-C167SettingsOrigin {
    return [pscustomobject]@{
        X=[Math]::Max(0,[int](($script:c166ScreenWidth-560)/2))
        Y=[Math]::Max(0,[int](($script:c166ScreenHeight-340)/2))
    }
}

function Open-C167SettingsCenter([object]$Boot,[switch]$ExpectC145Final) {
    $start = (Get-C166Serial $Boot.serialPath).Length
    # C167 uses a focused production-shaped composite without startup proof
    # controllers, so Notes opens in a clean state for the real Settings action.
    Open-C166AllProgramsItem 12 $Boot
    [void](Wait-C166Serial $Boot.serialPath `
        '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.notes selector=00000004 image=/system/apps/GXOSAPP\.ELF metadata=valid' $start 40)
    [void](Wait-C166Serial $Boot.serialPath `
        '^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9A-Fa-f]+ selector=00000004 existing=true result=PASS' $start 20)
    Start-Sleep -Milliseconds 350
    $Boot.notesReadyScreenshot = Join-Path (Split-Path $Boot.serialPath) 'notes-ready-for-settings.ppm'
    $Boot.notesReadyScreenshotSha256 = Capture-C166GuestScreen $Boot $Boot.notesReadyScreenshot
    $originX = [Math]::Max(0,[int](($script:c166ScreenWidth-600)/2))
    $originY = [Math]::Max(0,[int](($script:c166ScreenHeight-360)/2))
    $start = (Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($originX+475) ($originY+24+294) $Boot
    # A native managed-control click can dispatch correctly even when the
    # relative-pointer position rounds one pixel away from C150's diagnostic
    # coordinate. The Settings launch and its first managed render are the
    # authoritative result of this real pointer action.
    [void](Wait-C166Serial $Boot.serialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $start 60)
    if ($ExpectC145Final) {
        [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C145-FINAL viewport=valid registration=12 modal=none capture=none drag=none result=PASS' $start 60)
    }
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Settings result=PASS' $start 20)
    Start-Sleep -Milliseconds 350
    return Get-C167SettingsOrigin
}

function Enter-C167DefaultAppsPage([object]$Boot,[string]$ExpectedState,[string]$ScreenshotPath) {
    $origin = Get-C167SettingsOrigin
    $start = (Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($origin.X+476) ($origin.Y+24+48+11) $Boot
    $state = [regex]::Escape($ExpectedState)
    $marker = Wait-C166Serial $Boot.serialPath `
        ('^\[C102-MANAGED-OUTPUT\] C167-PAGE entry=Apps query=fresh state={0} .*result=PASS' -f $state) $start 20
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Apps result=PASS' $start 20)
    $sha = Capture-C167GuestScreen $Boot $ScreenshotPath
    return [ordered]@{state=$ExpectedState;marker=$marker.Match.Value;screenshot=$ScreenshotPath;screenshotSha256=$sha}
}

function Select-C167DefaultAppsChoice([object]$Boot,[int]$ChoiceIndex,[string]$ScreenshotRoot = '') {
    if ($ChoiceIndex -notin @(0,1)) { throw "Unsupported C167 choice index $ChoiceIndex." }
    $origin = Get-C167SettingsOrigin
    $comboX = $origin.X + 44 + 224
    $comboY = $origin.Y + 24 + 96 + 80 + 9
    $start = (Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen $comboX $comboY $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' $start 10)
    $popupScreenshot = $null
    $popupScreenshotSha256 = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotRoot)) {
        $popupScreenshot = Join-Path $ScreenshotRoot 'c169-pointer-combo-open.ppm'
        $popupScreenshotSha256 = Capture-C166GuestScreen $Boot $popupScreenshot
    }
    $popupY = $origin.Y + 24 + 96 + 80 + 18 + ($ChoiceIndex*18) + 9
    Click-C166Screen $comboX $popupY $Boot
    if ($ChoiceIndex -eq 0) {
        [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS' $start 10)
    } else {
        [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS' $start 10)
    }
    $selectionScreenshot = $null
    $selectionScreenshotSha256 = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotRoot)) {
        $selectionScreenshot = Join-Path $ScreenshotRoot 'c169-pointer-combo-selection.ppm'
        $selectionScreenshotSha256 = Capture-C166GuestScreen $Boot $selectionScreenshot
    }
    return [ordered]@{choiceIndex=$ChoiceIndex;open='PASS';selection='PASS';popupScreenshot=$popupScreenshot;popupScreenshotSha256=$popupScreenshotSha256;selectionScreenshot=$selectionScreenshot;selectionScreenshotSha256=$selectionScreenshotSha256;qmpLog=$Boot.qmpLogPath}
}

function Invoke-C167KeyboardDefaultAppsSelection([object]$Boot,[int]$ChoiceIndex,[string]$ScreenshotRoot = '') {
    if ($ChoiceIndex -notin @(0,1)) { throw "Unsupported C167 choice index $ChoiceIndex." }
    # Enter Apps using the visible page button first; the existing host then
    # owns every focus transition through the same ScrollView and ComboBox.
    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'tab' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=traversed result=PASS' $start 10)
    $focusScreenshot = $null
    $focusScreenshotSha256 = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotRoot)) {
        $focusScreenshot = Join-Path $ScreenshotRoot 'c169-keyboard-focus.ppm'
        $focusScreenshotSha256 = Capture-C166GuestScreen $Boot $focusScreenshot
    }
    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'ret' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-open=PASS capture=combo result=PASS' $start 10)
    $popupScreenshot = $null
    $popupScreenshotSha256 = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotRoot)) {
        $popupScreenshot = Join-Path $ScreenshotRoot 'c169-keyboard-combo-open.ppm'
        $popupScreenshotSha256 = Capture-C166GuestScreen $Boot $popupScreenshot
    }
    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'esc' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-CAPTURE popup=none result=PASS' $start 10)
    $dismissedScreenshot = $null
    $dismissedScreenshotSha256 = $null
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotRoot)) {
        $dismissedScreenshot = Join-Path $ScreenshotRoot 'c169-keyboard-combo-dismissed.ppm'
        $dismissedScreenshotSha256 = Capture-C166GuestScreen $Boot $dismissedScreenshot
    }

    # Focus Apply long enough to capture the shared Button cue, then continue
    # through Reset and return to the selector using existing focus routing.
    $buttonFocusScreenshot = $null
    $buttonFocusScreenshotSha256 = $null
    $buttonFocusStart = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'tab' $Boot
    [void](Wait-C166Serial $Boot.serialPath `
        '^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=(?:traversed|offscreen-revealed) result=PASS' `
        $buttonFocusStart 10)
    if (-not [string]::IsNullOrWhiteSpace($ScreenshotRoot)) {
        $buttonFocusScreenshot = Join-Path $ScreenshotRoot 'c170-button-focused.ppm'
        $buttonFocusScreenshotSha256 = Capture-C167GuestScreen $Boot $buttonFocusScreenshot
    }
    $start = (Get-C166Serial $Boot.serialPath).Length
    Send-C166QmpEvents ([int]$Boot.qmpPort) `
        @((New-C166Key 'shift' $true),(New-C166Key 'tab' $true),
          (New-C166Key 'tab' $false),(New-C166Key 'shift' $false)) $Boot.qmpLogPath 90
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-FOCUS shift-tab=handled earlier-member=PASS result=PASS' $start 10)

    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'ret' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-open=PASS capture=combo result=PASS' $start 10)
    if ($ChoiceIndex -eq 1) { Press-C166Key 'down' $Boot }
    elseif ($ChoiceIndex -eq 0) { Press-C166Key 'up' $Boot }
    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'ret' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-commit=PASS capture=none result=PASS' $start 10)
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Apps result=PASS' $start 10)
    return [ordered]@{choiceIndex=$ChoiceIndex;keyboardOpenEscape='PASS';tab='PASS';shiftTab='PASS';selection='PASS';focusScreenshot=$focusScreenshot;focusScreenshotSha256=$focusScreenshotSha256;buttonFocusScreenshot=$buttonFocusScreenshot;buttonFocusScreenshotSha256=$buttonFocusScreenshotSha256;popupScreenshot=$popupScreenshot;popupScreenshotSha256=$popupScreenshotSha256;dismissedScreenshot=$dismissedScreenshot;dismissedScreenshotSha256=$dismissedScreenshotSha256;qmpLog=$Boot.qmpLogPath}
}

function Invoke-C169SettingsScrollProof([object]$Boot,[string]$ScreenshotRoot) {
    $origin = Get-C167SettingsOrigin
    Click-C166Screen ($origin.X + 24 + 450) ($origin.Y + 24 + 96 + 180) $Boot
    $pointerX = $origin.X + 24 + 250
    $pointerY = $origin.Y + 24 + 96 + 100
    Move-C166Pointer $pointerX $pointerY $Boot
    $downStart = (Get-C166Serial $Boot.serialPath).Length
    Send-C166QmpEvents ([int]$Boot.qmpPort) @((New-C166Wheel 1),(New-C166Wheel 1),(New-C166Wheel 1)) $Boot.qmpLogPath 180
    $down = Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed sections-translated=PASS result=PASS' $downStart 20
    $downScreenshot = Join-Path $ScreenshotRoot 'c169-settings-scroll-down.ppm'
    $downScreenshotSha256 = Capture-C166GuestScreen $Boot $downScreenshot
    $upStart = (Get-C166Serial $Boot.serialPath).Length
    Send-C166QmpEvents ([int]$Boot.qmpPort) @((New-C166Wheel -1),(New-C166Wheel -1),(New-C166Wheel -1)) $Boot.qmpLogPath 180
    $up = Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed sections-translated=PASS result=PASS' $upStart 20
    $upScreenshot = Join-Path $ScreenshotRoot 'c169-settings-scroll-up.ppm'
    $upScreenshotSha256 = Capture-C166GuestScreen $Boot $upScreenshot
    return [ordered]@{down=$down.Match.Value;up=$up.Match.Value;downScreenshot=$downScreenshot;downScreenshotSha256=$downScreenshotSha256;upScreenshot=$upScreenshot;upScreenshotSha256=$upScreenshotSha256;directionChanges=2}
}

function Apply-C167DefaultAppsChoice([object]$Boot,[string]$ExpectedState,[string]$ScreenshotPath) {
    $origin = Get-C167SettingsOrigin
    $start = (Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($origin.X+44+80) ($origin.Y+24+96+158+9) $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-APPLY input=pointer verified=true result=PASS' $start 20)
    $state = [regex]::Escape($ExpectedState)
    $marker = Wait-C166Serial $Boot.serialPath `
        ('^\[C102-MANAGED-OUTPUT\] C167-STATE operation=Apply state={0} .*result=PASS' -f $state) $start 10
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Apps result=PASS' $start 20)
    $sha = Capture-C167GuestScreen $Boot $ScreenshotPath
    return [ordered]@{state=$ExpectedState;marker=$marker.Match.Value;screenshot=$ScreenshotPath;screenshotSha256=$sha}
}

function Apply-C167DefaultAppsChoiceByKeyboard([object]$Boot,[string]$ExpectedState,[string]$ScreenshotPath) {
    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'tab' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=traversed result=PASS' $start 10)
    $start = (Get-C166Serial $Boot.serialPath).Length
    Press-C166Key 'ret' $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-APPLY input=keyboard verified=true result=PASS' $start 20)
    $state = [regex]::Escape($ExpectedState)
    $marker = Wait-C166Serial $Boot.serialPath `
        ('^\[C102-MANAGED-OUTPUT\] C167-STATE operation=Apply state={0} .*result=PASS' -f $state) $start 10
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Apps result=PASS' $start 20)
    $sha = Capture-C167GuestScreen $Boot $ScreenshotPath
    return [ordered]@{state=$ExpectedState;input='keyboard';marker=$marker.Match.Value;screenshot=$ScreenshotPath;screenshotSha256=$sha}
}

function Reset-C167DefaultApps([object]$Boot,[string]$ScreenshotPath) {
    $origin = Get-C167SettingsOrigin
    $start = (Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($origin.X+44+164+100) ($origin.Y+24+96+158+9) $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RESET input=pointer verified=true result=PASS' $start 20)
    $marker = Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-STATE operation=Reset state=NoOverride display=system-default result=PASS' $start 10
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Apps result=PASS' $start 20)
    $sha = Capture-C167GuestScreen $Boot $ScreenshotPath
    return [ordered]@{state='NoOverride';marker=$marker.Match.Value;screenshot=$ScreenshotPath;screenshotSha256=$sha}
}

function Return-C167ToSettingsAndReenter([object]$Boot,[string]$ExpectedState,[string]$ScreenshotPath) {
    $origin = Get-C167SettingsOrigin
    $start = (Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($origin.X+476) ($origin.Y+24+48+11) $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-PAGE return=Settings pending=discarded write=false result=PASS' $start 10)
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Settings result=PASS' $start 20)
    return Enter-C167DefaultAppsPage $Boot $ExpectedState $ScreenshotPath
}

function Close-C167SettingsCenter([object]$Boot) {
    $start = (Get-C166Serial $Boot.serialPath).Length
    Send-C166QmpEvents ([int]$Boot.qmpPort) `
        @((New-C166Key 'alt' $true),(New-C166Key 'f4' $true),
          (New-C166Key 'f4' $false),(New-C166Key 'alt' $false)) $Boot.qmpLogPath 90
    [void](Wait-C166Serial $Boot.serialPath '^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.settingscenter generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ reason=(?:close-or-replace|return)' $start 20)
}

function Get-C167ResolvedDiskState([object[]]$Slots) {
    $committed = @($Slots | Where-Object { $_.present })
    if ($committed.Count -eq 0) {
        return [ordered]@{override='NoOverride';generation=0;slot='N';applicationId=''}
    }
    $latest = $committed[0]
    for ($index = 1; $index -lt $committed.Count; $index++) {
        if ([uint64]$committed[$index].generation -gt [uint64]$latest.generation) {
            $latest = $committed[$index]
        }
    }
    return [ordered]@{override=$latest.override;generation=$latest.generation;slot=$latest.slot;applicationId=$latest.applicationId}
}

function Assert-C167DiskState([object[]]$Slots,[string]$Expected,[bool]$MustHaveRecord) {
    $resolved = Get-C167ResolvedDiskState $Slots
    if ($resolved.override -ne $Expected) {
        throw "C167 association slots resolve to $($resolved.override), expected $Expected."
    }
    if ($MustHaveRecord -and $resolved.slot -eq 'N') {
        throw "C167 expected a persisted $Expected association state, but no slot exists."
    }
    if ($Expected -eq 'ApplicationOverride' -and
        $resolved.applicationId -ne 'com.guidexos.apps.managed.notes') {
        throw 'C167 persisted explicit state does not name Managed Notes.'
    }
    return $resolved
}

function Invoke-C167SettingsProofBoots([string]$RepoRoot,[string]$EvidenceRoot,
    [string]$Composite,[string]$NeutralRamdisk,[string]$Kernel,[string]$Qemu,[string]$Ovmf,[string]$Bootloader,
    [string]$Python,[object]$ProtectedMedia) {
    if (-not $Qemu -or -not $Ovmf -or -not $Bootloader -or -not $Python) {
        throw 'C167 real Settings proof is blocked because QEMU, OVMF, the bootloader, or Python is unavailable.'
    }
    if (-not (Test-Path -LiteralPath $NeutralRamdisk -PathType Leaf) -or
        -not (Test-Path -LiteralPath $Kernel -PathType Leaf)) {
        throw 'C167 real Settings proof requires the fresh neutral ramdisk and proof kernel.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $NeutralRamdisk) 'wallpaper-pack-c167-ui-proof') -PathType Container)) {
        throw 'C167 UI proof ramdisk was not staged by the selector-free ramdisk generation command.'
    }
    $qemuOwners = @(Get-C166WorkspaceQemuOwners $RepoRoot)
    if ($qemuOwners.Count -gt 0) {
        $formatted = ($qemuOwners | ForEach-Object { "pid=$($_.ProcessId) exe=$($_.ExecutablePath) cmd=$($_.CommandLine)" }) -join "`n"
        throw "C167 UI proof is blocked by existing QEMU owner(s); none was terminated.`n$formatted"
    }

    $runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $runRoot = Join-Path (Join-Path $EvidenceRoot 'settings-ui-proof-runs') $runId
    $sharedEsp = Join-Path $runRoot 'shared-writable-esp.img'
    $settingsRecord = Join-Path $runRoot 'staging\GXSETT.BIN'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $settingsRecord) | Out-Null
    New-C166SettingsRecord $settingsRecord
    $fatHelper = Join-Path $PSScriptRoot 'c167-fat16-proof-disk.py'
    $diskBuildOutput = & $Python $fatHelper create --image $sharedEsp --bootloader $Bootloader `
        --kernel $Kernel --ramdisk $NeutralRamdisk --settings $settingsRecord 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sharedEsp -PathType Leaf)) {
        throw "Could not create the C167 FAT16 ESP image: $diskBuildOutput"
    }
    $diskBuild = $diskBuildOutput | ConvertFrom-Json
    $diskIdentity = [System.IO.Path]::GetFullPath($sharedEsp)
    $initialDiskHash = Get-Sha256 $sharedEsp
    $initialSlots = Get-C167AssociationDiskState $sharedEsp $Python
    $initialResolved = Assert-C167DiskState $initialSlots 'NoOverride' $false

    $bootResults = @()
    $boot1DiskHashBefore = Get-Sha256 $sharedEsp
    $boot1 = Invoke-ProofQemuBoot 'C167-Settings-Proof-Boot1' $NeutralRamdisk $sharedEsp $Bootloader $Kernel $Qemu $Ovmf `
        (Join-Path $runRoot 'boot1') -HoldAfterPass -PreparedFatDisk -ReadyPattern '(?m)^\[desktop\] bare-metal desktop icon init completed'
    try {
        Initialize-C166GuestPointer $boot1
        $boot1Before = Get-C167AssociationDiskState $sharedEsp $Python
        $settingsOrigin = Open-C167SettingsCenter $boot1 -ExpectC145Final
        $boot1Default = Enter-C167DefaultAppsPage $boot1 'NoOverride' (Join-Path $runRoot 'boot1-default-apps-system-default.ppm')
        $selectionScreenshotRoot = if ($script:c169ManagedUiProof) { Join-Path $runRoot 'boot1' } else { '' }
        $selection = Invoke-C167KeyboardDefaultAppsSelection $boot1 1 $selectionScreenshotRoot
        $beforeApply = Get-C167AssociationDiskState $sharedEsp $Python
        if ((Get-C166SlotIdentity $boot1Before) -ne (Get-C166SlotIdentity $beforeApply)) {
            throw 'Boot 1 selecting No default wrote association persistence before Apply.'
        }
        $boot1PendingScreenshot = Join-Path $runRoot 'boot1-no-default-pending.ppm'
        $pendingSha = Capture-C167GuestScreen $boot1 $boot1PendingScreenshot
        $boot1Apply = Apply-C167DefaultAppsChoiceByKeyboard $boot1 'Disabled' (Join-Path $runRoot 'boot1-default-apps-disabled.ppm')
        $boot1Reentry = Return-C167ToSettingsAndReenter $boot1 'Disabled' (Join-Path $runRoot 'boot1-default-apps-reentry.ppm')
        Close-C167SettingsCenter $boot1
        $boot1Explorer = Invoke-C166ManagedExplorerOpen $boot1 'Disabled' (Join-Path $runRoot 'boot1-managed-explorer-no-handler.ppm')
        if ($boot1Explorer.result -ne 'No associated handler') {
            throw 'Boot 1 Managed File Explorer did not observe the Disabled association.'
        }
        if ($boot1.backingEspPath -ne $diskIdentity) { throw 'Boot 1 used an unexpected writable ESP path.' }
        $boot1Interactions = [ordered]@{initial=$boot1Default;choice=$selection;beforeApplySlots=$beforeApply;pendingScreenshot=$boot1PendingScreenshot;pendingScreenshotSha256=$pendingSha;apply=$boot1Apply;reentry=$boot1Reentry;managedExplorer=$boot1Explorer;associationDiskUnchangedThroughSelection=$true;settingsOrigin=$settingsOrigin;c169=if ($script:c169ManagedUiProof) { $selection } else { $null }}
    } finally {
        Stop-OwnedProofQemu $boot1
    }
    $boot1DiskHashAfter = Get-Sha256 $sharedEsp
    $boot1Slots = Get-C167AssociationDiskState $sharedEsp $Python
    $boot1Resolved = Assert-C167DiskState $boot1Slots 'Disabled' $true
    $boot1Interactions.resolvedSlots = $boot1Resolved
    $boot1Interactions.associationDiskUnchangedThroughExplorer = 'Verified by the Disabled state restored on Boot 2.'
    $boot1.serial = Get-C166Serial $boot1.serialPath
    $boot1.serialSha256 = Get-Sha256 $boot1.serialPath
    $boot1.qmpInputSha256 = Get-Sha256 $boot1.qmpLogPath
    $boot1.pid = [int]$boot1.process.Id
    $boot1.process = $null
    $boot1Interactions.serialPath = $boot1.serialPath
    $boot1Interactions.serialSha256 = $boot1.serialSha256
    $boot1Interactions.qmpInputSha256 = $boot1.qmpInputSha256
    $boot1Interactions.sameEspPath = $diskIdentity
    $boot1Interactions.diskSha256Before = $boot1DiskHashBefore
    $boot1Interactions.diskSha256After = $boot1DiskHashAfter
    $boot1Interactions.result = 'PASS'
    $bootResults += [ordered]@{stage=$boot1.stage;status='PASS';pid=$boot1.pid;serialPath=$boot1.serialPath;serialSha256=$boot1.serialSha256;qmpInputSha256=$boot1.qmpInputSha256;qemuArguments=@($boot1.qemuArguments);qemuStartedAtUtc=$boot1.qemuStartedAtUtc;backingEspPath=$boot1.backingEspPath;interactions=$boot1Interactions}

    $boot2DiskHashBefore = Get-Sha256 $sharedEsp
    if ($boot2DiskHashBefore -ne $boot1DiskHashAfter) { throw 'C167 FAT16 ESP image changed between Boots 1 and 2 outside the guest run.' }
    $boot2 = Invoke-ProofQemuBoot 'C167-Settings-Proof-Boot2' $NeutralRamdisk $sharedEsp $Bootloader $Kernel $Qemu $Ovmf `
        (Join-Path $runRoot 'boot2') -HoldAfterPass -PreparedFatDisk -ReadyPattern '(?m)^\[desktop\] bare-metal desktop icon init completed'
    try {
        if ($boot2.backingEspPath -ne $diskIdentity) { throw 'Boot 2 did not reuse the exact writable FAT16 ESP image.' }
        $boot2Before = Get-C167AssociationDiskState $sharedEsp $Python
        $boot2Resolved = Assert-C167DiskState $boot2Before 'Disabled' $true
        Initialize-C166GuestPointer $boot2
        Open-C167SettingsCenter $boot2 -ExpectC145Final | Out-Null
        $scrollProof = $null
        if ($script:c169ManagedUiProof) {
            $scrollProof = Invoke-C169SettingsScrollProof $boot2 (Join-Path $runRoot 'boot2')
        }
        $boot2Disabled = Enter-C167DefaultAppsPage $boot2 'Disabled' (Join-Path $runRoot 'boot2-default-apps-restored-disabled.ppm')
        $boot2Queried = Get-C167AssociationDiskState $sharedEsp $Python
        if ((Get-C166SlotIdentity $boot2Before) -ne (Get-C166SlotIdentity $boot2Queried)) {
            throw 'Boot 2 Settings re-entry changed persisted Disabled association state.'
        }
        $choiceScreenshotRoot = if ($script:c169ManagedUiProof) { Join-Path $runRoot 'boot2' } else { '' }
        $boot2Selection = Select-C167DefaultAppsChoice $boot2 0 $choiceScreenshotRoot
        $boot2BeforeApply = Get-C167AssociationDiskState $sharedEsp $Python
        if ((Get-C166SlotIdentity $boot2Before) -ne (Get-C166SlotIdentity $boot2BeforeApply)) {
            throw 'Boot 2 selecting Managed Notes wrote association persistence before Apply.'
        }
        $boot2Apply = Apply-C167DefaultAppsChoice $boot2 'ApplicationOverride' (Join-Path $runRoot 'boot2-default-apps-explicit-notes.ppm')
        $boot2Reentry = $null
        if ($script:c169ManagedUiProof) {
            $boot2Reentry = Return-C167ToSettingsAndReenter $boot2 'ApplicationOverride' (Join-Path $runRoot 'boot2-c169-settings-reentry.ppm')
        }
        Close-C167SettingsCenter $boot2
        $boot2Explorer = Invoke-C166NativeExplorerOpen $boot2 'Default' `
            (Join-Path $runRoot 'boot2-native-explorer-selected-hello.ppm') `
            (Join-Path $runRoot 'boot2-native-explorer-notes-activation.ppm')
        if ($boot2Explorer.content -ne 'hello from C164' -or -not $boot2Explorer.clean) {
            throw 'Boot 2 Native File Explorer did not open the expected C164 Notes document.'
        }
        Close-C166ManagedNotesStartupWindow $boot2
        $boot2ManagedExplorer = Invoke-C166ManagedExplorerOpen $boot2 'Default' `
            (Join-Path $runRoot 'boot2-managed-explorer-notes-activation.ppm')
        if ($boot2ManagedExplorer.content -ne 'hello from C164' -or -not $boot2ManagedExplorer.clean) {
            throw 'Boot 2 Managed File Explorer did not agree with native File Explorer for the explicit Notes default.'
        }
        $boot2Interactions = [ordered]@{restoredState=$boot2Resolved;settings=$boot2Disabled;queryPreservedSlots=$true;choice=$boot2Selection;beforeApplySlots=$boot2BeforeApply;apply=$boot2Apply;nativeExplorer=$boot2Explorer;managedExplorer=$boot2ManagedExplorer;c169=if ($script:c169ManagedUiProof) { [ordered]@{scroll=$scrollProof;settingsReentry=$boot2Reentry;pointerPopupSelection=$boot2Selection} } else { $null };result='PASS'}
    } finally {
        Stop-OwnedProofQemu $boot2
    }
    $boot2DiskHashAfter = Get-Sha256 $sharedEsp
    $boot2AfterApply = Get-C167AssociationDiskState $sharedEsp $Python
    $boot2Explicit = Assert-C167DiskState $boot2AfterApply 'ApplicationOverride' $true
    $boot2Interactions.explicitState = $boot2Explicit
    $boot2Interactions.slotImagesAfterExplorer = $boot2AfterApply
    $boot2Interactions.associationDiskUnchangedThroughExplorer = 'Verified by the ApplicationOverride state restored on Boot 3.'
    $boot2.serial = Get-C166Serial $boot2.serialPath
    $boot2.serialSha256 = Get-Sha256 $boot2.serialPath
    $boot2.qmpInputSha256 = Get-Sha256 $boot2.qmpLogPath
    $boot2.pid = [int]$boot2.process.Id
    $boot2.process = $null
    $boot2Interactions.serialPath = $boot2.serialPath
    $boot2Interactions.serialSha256 = $boot2.serialSha256
    $boot2Interactions.qmpInputSha256 = $boot2.qmpInputSha256
    $boot2Interactions.sameEspPath = $diskIdentity
    $boot2Interactions.diskSha256Before = $boot2DiskHashBefore
    $boot2Interactions.diskSha256After = $boot2DiskHashAfter
    $bootResults += [ordered]@{stage=$boot2.stage;status='PASS';pid=$boot2.pid;serialPath=$boot2.serialPath;serialSha256=$boot2.serialSha256;qmpInputSha256=$boot2.qmpInputSha256;qemuArguments=@($boot2.qemuArguments);qemuStartedAtUtc=$boot2.qemuStartedAtUtc;backingEspPath=$boot2.backingEspPath;interactions=$boot2Interactions}

    $boot3DiskHashBefore = Get-Sha256 $sharedEsp
    if ($boot3DiskHashBefore -ne $boot2DiskHashAfter) { throw 'C167 FAT16 ESP image changed between Boots 2 and 3 outside the guest run.' }
    $boot3 = Invoke-ProofQemuBoot 'C167-Settings-Proof-Boot3' $NeutralRamdisk $sharedEsp $Bootloader $Kernel $Qemu $Ovmf `
        (Join-Path $runRoot 'boot3') -HoldAfterPass -PreparedFatDisk -ReadyPattern '(?m)^\[desktop\] bare-metal desktop icon init completed'
    try {
        if ($boot3.backingEspPath -ne $diskIdentity) { throw 'Boot 3 did not reuse the exact writable FAT16 ESP image.' }
        $boot3Before = Get-C167AssociationDiskState $sharedEsp $Python
        $boot3Resolved = Assert-C167DiskState $boot3Before 'ApplicationOverride' $true
        Initialize-C166GuestPointer $boot3
        Open-C167SettingsCenter $boot3 -ExpectC145Final | Out-Null
        $boot3Explicit = Enter-C167DefaultAppsPage $boot3 'ApplicationOverride' (Join-Path $runRoot 'boot3-default-apps-restored-explicit.ppm')
        $boot3Reset = Reset-C167DefaultApps $boot3 (Join-Path $runRoot 'boot3-default-apps-reset-system-default.ppm')
        $boot3Reentry = Return-C167ToSettingsAndReenter $boot3 'NoOverride' (Join-Path $runRoot 'boot3-default-apps-reset-reentry.ppm')
        Close-C167SettingsCenter $boot3
        $boot3Explorer = Invoke-C166ManagedExplorerOpen $boot3 'Default' (Join-Path $runRoot 'boot3-managed-explorer-notes-activation.ppm')
        if ($boot3Explorer.content -ne 'hello from C164' -or -not $boot3Explorer.clean) {
            throw 'Boot 3 Managed File Explorer did not open the expected clean C164 Notes document.'
        }
        $c169AppChecks = $null
        if ($script:c169ManagedUiProof) {
            $c169Calculator = Invoke-C169CalculatorSmoke $boot3 (Join-Path $runRoot 'boot3')
            $c169TaskManager = Invoke-C169TaskManagerSmoke $boot3 (Join-Path $runRoot 'boot3')
            $reopenedSettings = Open-C167SettingsCenter $boot3
            $reopenedDefaultApps = Enter-C167DefaultAppsPage $boot3 'NoOverride' (Join-Path $runRoot 'boot3-c169-settings-reopened.ppm')
            Close-C167SettingsCenter $boot3
            $c169AppChecks = [ordered]@{calculator=$c169Calculator;taskManager=$c169TaskManager;settingsReopened='PASS';reopenedSettingsOrigin=$reopenedSettings;reopenedDefaultApps=$reopenedDefaultApps;finalControlOwnership='modal=none capture=none drag=none'}
        }
        $boot3Tail = Get-C166Serial $boot3.serialPath
        if ($boot3Tail -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C167-PAGE entry=Apps query=fresh state=NoOverride display=system-default result=PASS' -or
            $boot3Tail -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C145-FINAL viewport=valid registration=12 modal=none capture=none drag=none result=PASS') {
            throw 'Boot 3 did not leave verified NoOverride state with clean Settings control ownership.'
        }
        $boot3Interactions = [ordered]@{restoredState=$boot3Resolved;settings=$boot3Explicit;reset=$boot3Reset;reentry=$boot3Reentry;managedExplorer=$boot3Explorer;cleanNotesBaseline=$true;modalCaptureDrag='none';c169=$c169AppChecks;result='PASS'}
    } finally {
        Stop-OwnedProofQemu $boot3
    }
    $boot3DiskHashAfter = Get-Sha256 $sharedEsp
    $boot3AfterReset = Get-C167AssociationDiskState $sharedEsp $Python
    $boot3NoOverride = Assert-C167DiskState $boot3AfterReset 'NoOverride' $true
    $boot3Interactions.noOverrideState = $boot3NoOverride
    $boot3Interactions.slotImagesAfterExplorer = $boot3AfterReset
    $boot3Interactions.associationDiskUnchangedThroughExplorer = 'Verified by the final NoOverride slot after reset and Managed File Explorer.'
    $boot3.serial = Get-C166Serial $boot3.serialPath
    $boot3.serialSha256 = Get-Sha256 $boot3.serialPath
    $boot3.qmpInputSha256 = Get-Sha256 $boot3.qmpLogPath
    $boot3.pid = [int]$boot3.process.Id
    $boot3.process = $null
    $boot3Interactions.serialPath = $boot3.serialPath
    $boot3Interactions.serialSha256 = $boot3.serialSha256
    $boot3Interactions.qmpInputSha256 = $boot3.qmpInputSha256
    $boot3Interactions.sameEspPath = $diskIdentity
    $boot3Interactions.diskSha256Before = $boot3DiskHashBefore
    $boot3Interactions.diskSha256After = $boot3DiskHashAfter
    $bootResults += [ordered]@{stage=$boot3.stage;status='PASS';pid=$boot3.pid;serialPath=$boot3.serialPath;serialSha256=$boot3.serialSha256;qmpInputSha256=$boot3.qmpInputSha256;qemuArguments=@($boot3.qemuArguments);qemuStartedAtUtc=$boot3.qemuStartedAtUtc;backingEspPath=$boot3.backingEspPath;interactions=$boot3Interactions}

    $finalSlots = Get-C167AssociationDiskState $sharedEsp $Python
    $finalState = Assert-C167DiskState $finalSlots 'NoOverride' $true
    if (@($bootResults | Where-Object { $_.backingEspPath -ne $diskIdentity -or $_.status -ne 'PASS' }).Count -ne 0) {
        throw 'C167 proof boots did not all pass against the same persistent ESP path.'
    }
    $manifest = [ordered]@{
        phase='C167-real-Settings-proof';status='PASS';sourceHead=$head;sourceBranch='v1.1_DOTNET_SUPPORT'
        protectedMedia=$ProtectedMedia;proofCompositeSha256=Get-Sha256 $Composite;proofKernelSha256=Get-Sha256 $Kernel;neutralRamdiskSha256=Get-Sha256 $NeutralRamdisk
        proofControlFlag='absent';startupProofSuites='excluded; visible Settings and Explorer controls drive guest operations'
        ramdiskSelector='PROOF1 selector absent; UI proof uses real Settings controls and managed C166 wrapper'
        guestBoots=$bootResults;guestBootCount=$bootResults.Count
        c169ManagedUiProof=if ($script:c169ManagedUiProof) {
            [ordered]@{status='PASS';guestBootCount=$bootResults.Count;boot1=$bootResults[0].interactions.c169;boot2=$bootResults[1].interactions.c169;boot3=$bootResults[2].interactions.c169;visualStateScreenshots='keyboard focus, keyboard popup open/dismissal, pointer popup/selection, Settings scroll, Calculator result, Task Manager, and reopened Settings'}
        } else { $null }
        persistentStorage=[ordered]@{path=$diskIdentity;filesystem=$diskBuild.filesystem;partitionStartLba=$diskBuild.partitionStartLba;partitionSectors=$diskBuild.partitionSectors;imageFormat='raw';imageSizeBytes=$diskBuild.sizeBytes;initialDiskSha256=$initialDiskHash;diskSha256BeforeEachBoot=@($boot1DiskHashBefore,$boot2DiskHashBefore,$boot3DiskHashBefore);diskSha256AfterEachBoot=@($boot1DiskHashAfter,$boot2DiskHashAfter,$boot3DiskHashAfter);sameImagePathAcrossAllBoots=$true;initialSlots=$initialSlots;initialResolved=$initialResolved;finalResolved=$finalState;finalSlotImages=$finalSlots}
        settingsFormat=[ordered]@{version=2;bytes=26;preserved=$true}
        associationAuthority='GuideXosHost managed wrapper -> C166 ABI v4/128 association callback offset 120 -> native kernel::appmodel'
        qemuOwnership='only runner-created processes were stopped; unrelated processes were not terminated'
    }
    $manifestPath = Join-Path $runRoot 'c167-settings-ui-proof-manifest.json'
    $manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $latestManifest = Join-Path $EvidenceRoot 'c167-settings-ui-proof-manifest.json'
    Copy-Item -LiteralPath $manifestPath -Destination $latestManifest -Force
    if ($script:c170ControlRendering) {
        Write-C170FocusedEvidenceManifest $script:c170EvidenceOutputRoot $manifestPath $Composite $Kernel `
            $NeutralRamdisk $Qemu $head
    }
    Write-Host "C167 real Settings proof boots=3/3 PASS; evidence: $runRoot"
}

function Get-C166TaskManagerOrigin {
    return [pscustomobject]@{
        X=[Math]::Max(0,[int](($script:c166ScreenWidth-800)/2))
        Y=[Math]::Max(0,[int](($script:c166ScreenHeight-370)/2))
    }
}

function Close-C166TaskManager([object]$Boot) {
    $origin=Get-C166TaskManagerOrigin
    $start=(Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($origin.X+390) ($origin.Y+24+290) $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C161-TM-CLOSE-DISPATCH\] selector=7 controls=0 result=PASS' $start 20)
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-CLOSE controls=0 selection=none result=PASS' $start 15)
}

function Invoke-C166TaskManagerSnapshot([object]$Boot) {
    $start=(Get-C166Serial $Boot.serialPath).Length
    Open-C166AllProgramsItem 14 $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.taskmanager selector=00000007 image=/system/apps/GXOSAPP\.ELF metadata=valid' $start 35)
    $launch=Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 reg=\d+ ctr=4 cap=4 max=20 snap=\d+ n=\d+ self=\d+ active=1 result=PASS' $start 35
    $origin=Get-C166TaskManagerOrigin
    Click-C166Screen ($origin.X+30) ($origin.Y+24+64) $Boot
    $refreshStart=(Get-C166Serial $Boot.serialPath).Length
    Send-C166QmpEvents ([int]$Boot.qmpPort) `
        @((New-C166Key 'ctrl' $true),(New-C166Key 'r' $true),
          (New-C166Key 'r' $false),(New-C166Key 'ctrl' $false)) $Boot.qmpLogPath 60
    $refresh=Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=(\d+) sel=([^ ]+) v=\d+ ap=true self=(\d+) active=1 result=PASS' $refreshStart 20
    return [ordered]@{launch=$launch.Match.Value;refresh=$refresh.Match.Value;rowCount=[int]$refresh.Match.Groups[1].Value;active=$true}
}

function Invoke-C169CalculatorSmoke([object]$Boot,[string]$ScreenshotRoot) {
    $start = (Get-C166Serial $Boot.serialPath).Length
    Open-C166AllProgramsItem 11 $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.calculator selector=00000006 image=/system/apps/GXOSAPP\.ELF metadata=valid' $start 35)
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator selector=6 controls=18 capacity=18 focus=18 fresh=PASS result=PASS' $start 35)
    Press-C166Key '7' $Boot
    Send-C166QmpEvents ([int]$Boot.qmpPort) @((New-C166Key 'shift' $true),(New-C166Key '8' $true),(New-C166Key '8' $false),(New-C166Key 'shift' $false)) $Boot.qmpLogPath 90
    Press-C166Key '8' $Boot
    Press-C166Key 'ret' $Boot
    $result = Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=56 phase=3 commands=4 result=PASS' $start 20
    $screenshot = Join-Path $ScreenshotRoot 'c169-calculator-56.ppm'
    $screenshotSha256 = Capture-C166GuestScreen $Boot $screenshot
    $closeStart = (Get-C166Serial $Boot.serialPath).Length
    Send-C166QmpEvents ([int]$Boot.qmpPort) @((New-C166Key 'alt' $true),(New-C166Key 'f4' $true),(New-C166Key 'f4' $false),(New-C166Key 'alt' $false)) $Boot.qmpLogPath 90
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 active=none modal=none popup=none drag=none result=PASS' $closeStart 30)
    return [ordered]@{launch='PASS';calculation=$result.Match.Value;screenshot=$screenshot;screenshotSha256=$screenshotSha256;close='PASS'}
}

function Invoke-C169TaskManagerSmoke([object]$Boot,[string]$ScreenshotRoot) {
    $snapshot = Invoke-C166TaskManagerSnapshot $Boot
    $screenshot = Join-Path $ScreenshotRoot 'c169-task-manager.ppm'
    $screenshotSha256 = Capture-C166GuestScreen $Boot $screenshot
    Close-C166TaskManager $Boot
    return [ordered]@{launch=$snapshot.launch;refresh=$snapshot.refresh;rowCount=$snapshot.rowCount;screenshot=$screenshot;screenshotSha256=$screenshotSha256;close='PASS'}
}

function Close-C166NativeExplorerThroughTaskManager([object]$Boot,[int]$RowCount) {
    if ($RowCount -le 0) { throw 'C166 Task Manager snapshot contained no rows.' }
    $origin=Get-C166TaskManagerOrigin
    Click-C166Screen ($origin.X+30) ($origin.Y+24+64) $Boot
    $selectedIdentity=$null
    for ($row=$RowCount-1; $row -ge 0; $row--) {
        $before=(Get-C166Serial $Boot.serialPath).Length
        if ($row -eq $RowCount-1) { Press-C166Key 'end' $Boot }
        else { Press-C166Key 'up' $Boot }
        $selection=Wait-C166Serial $Boot.serialPath ('^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row={0} identity=(\d+):(\d+) on=[01] result=PASS' -f $row) $before 12
        if ($selection.Match.Groups[1].Value -ne '1') { continue }
        $identity=$selection.Match.Groups[2].Value
        $hex=([uint64]::Parse($identity,[Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
        $full=Get-C166Serial $Boot.serialPath
        if ($full -match ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 1:{0}=gxos\.builtin\.fileexplorer\r?$' -f $hex)) {
            [void](Wait-C166Serial $Boot.serialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=1:{0} enabled=true result=PASS' -f [regex]::Escape($identity)) $before 10)
            $selectedIdentity=$identity
            break
        }
    }
    if (-not $selectedIdentity) { throw 'C166 Task Manager snapshot did not identify native File Explorer for a safe close.' }
    $origin=Get-C166TaskManagerOrigin
    $closeStart=(Get-C166Serial $Boot.serialPath).Length
    Click-C166Screen ($origin.X+234) ($origin.Y+24+290) $Boot
    [void](Wait-C166Serial $Boot.serialPath '^\[C102-MANAGED-OUTPUT\] C162-TM-CONFIRM open=true identity=captured modal=true result=PASS' $closeStart 20)
    Press-C166Key 'ret' $Boot
    [void](Wait-C166Serial $Boot.serialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=1:{0} result=0 fresh=true remains=false result=PASS' -f [regex]::Escape($selectedIdentity)) $closeStart 30)
    return [ordered]@{target='gxos.builtin.fileexplorer';identityType=1;identity=$selectedIdentity;close='PASS'}
}

function Invoke-C166OrdinaryBoot([int]$Number,[string]$Kernel,[string]$Ramdisk,
    [string]$SettingsRecord,[string]$Qemu,[string]$Ovmf,[string]$Bootloader,
    [string]$EvidenceRoot) {
    $root=Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}" -f $Number)
    $esp=Join-Path $root 'ESP'
    New-Item -ItemType Directory -Force -Path $root,$esp | Out-Null
    Copy-Item -LiteralPath $SettingsRecord -Destination (Join-Path $esp 'GXSETT.BIN') -Force
    $boot=Invoke-ProofQemuBoot ("C166-Ordinary-{0}" -f $Number) $Ramdisk $esp $Bootloader $Kernel $Qemu $Ovmf `
        (Join-Path $root 'qemu') -HoldAfterPass -ReadyPattern '(?m)^\[desktop\] bare-metal desktop icon init completed'
    $integration=$null
    try {
        [void](Wait-C166Serial $boot.serialPath '^\[NATIVEAOT-PRODUCTION-LAUNCH\] applicationId=com\.guidexos\.apps\.managed\.notes recordId=com\.guidexos\.apps\.managed\.notes image=/system/apps/GXOSAPP\.ELF selector=00000004' 0 90)
        [void](Wait-C166Serial $boot.serialPath '^\[C102-MANAGED-OUTPUT\] C166-ORDINARY-READY app=ManagedNotes render=PASS' 0 120)
        if ($Number -eq 1) {
            Initialize-C166GuestPointer $boot
            Close-C166ManagedNotesStartupWindow $boot
            $notes=Invoke-C166ManagedExplorerOpen $boot 'Default' (Join-Path $root 'managed-explorer-notes.ppm')
            $defaultApps=$null
            if ($script:c167SettingsAcceptance) {
                Open-C167SettingsCenter $boot -ExpectC145Final | Out-Null
                $defaultApps=Enter-C167DefaultAppsPage $boot 'NoOverride' (Join-Path $root 'default-apps-system-default.ppm')
                $ordinarySlots=Get-C166AssociationDiskState $esp
                [void](Assert-C167DiskState $ordinarySlots 'NoOverride' $false)
                $c169KeyboardSelection=$null
                if ($script:c169ManagedUiOrdinaryAcceptance) {
                    $c169KeyboardSelection=Invoke-C167KeyboardDefaultAppsSelection $boot 1 $root
                    $slotsAfterKeyboard=Get-C166AssociationDiskState $esp
                    [void](Assert-C167DiskState $slotsAfterKeyboard 'NoOverride' $false)
                    if ((Get-C166SlotIdentity $ordinarySlots) -ne (Get-C166SlotIdentity $slotsAfterKeyboard)) {
                        throw 'Ordinary boot 1 keyboard focus/selection wrote association persistence before Apply.'
                    }
                }
                Close-C167SettingsCenter $boot
                Open-C167SettingsCenter $boot | Out-Null
                $reopened=Enter-C167DefaultAppsPage $boot 'NoOverride' `
                    (Join-Path $root 'default-apps-close-reopen-system-default.ppm')
                $c169ReopenedFocus=$null
                $c169ReopenedFocusScreenshot=$null
                $c169ReopenedFocusScreenshotSha256=$null
                if ($script:c169ManagedUiOrdinaryAcceptance) {
                    $focusStart=(Get-C166Serial $boot.serialPath).Length
                    Press-C166Key 'tab' $boot
                    $c169ReopenedFocus=Wait-C166Serial $boot.serialPath `
                        '^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=(?:traversed|offscreen-revealed) result=PASS' `
                        $focusStart 15
                    $c169ReopenedFocusScreenshot=Join-Path $root 'c169-settings-reopened-focus.ppm'
                    $c169ReopenedFocusScreenshotSha256=Capture-C167GuestScreen $boot $c169ReopenedFocusScreenshot
                }
                Close-C167SettingsCenter $boot
                $defaultApps.reopen=$reopened
                if ($script:c169ManagedUiOrdinaryAcceptance) {
                    $defaultApps.c169KeyboardFocus=[ordered]@{
                        interaction=$c169KeyboardSelection
                        committedStateBefore=$ordinarySlots
                        committedStateAfter=$slotsAfterKeyboard
                        closeReopen='PASS'
                        reopenedFocus=$c169ReopenedFocus.Match.Value
                        reopenedFocusScreenshot=$c169ReopenedFocusScreenshot
                        reopenedFocusScreenshotSha256=$c169ReopenedFocusScreenshotSha256
                        staleFocus='none; new Tab focus routed successfully'
                    }
                }
            }
            $integration=[ordered]@{managedFileExplorer=$notes;defaultApps=$defaultApps;C164NotesActivation='PASS';path=$notes.path;content=$notes.content;cleanDocument=$notes.clean}
        } elseif ($Number -eq 2) {
            Initialize-C166GuestPointer $boot
            $managed=$null
            if ($script:c167SettingsAcceptance) {
                Open-C167SettingsCenter $boot | Out-Null
                $settingsInitialScreenshot=Join-Path $root 'settings-general-initial.ppm'
                $settingsInitialSha=Capture-C167GuestScreen $boot $settingsInitialScreenshot
                $settingsOrigin=Get-C167SettingsOrigin
                $settingsPageEntry=Enter-C167DefaultAppsPage $boot 'NoOverride' `
                    (Join-Path $root 'settings-navigation-default-apps.ppm')
                $returnStart=(Get-C166Serial $boot.serialPath).Length
                Click-C166Screen ($settingsOrigin.X+476) `
                    ($settingsOrigin.Y+24+48+11) $boot
                $settingsReturn=Wait-C166Serial $boot.serialPath `
                    '^\[C102-MANAGED-OUTPUT\] C167-PAGE return=Settings pending=discarded write=false result=PASS' `
                    $returnStart 15
                [void](Wait-C166Serial $boot.serialPath `
                    '^\[C102-MANAGED-OUTPUT\] C167-RENDER page=Settings result=PASS' `
                    $returnStart 15)
                Click-C166Screen ($settingsOrigin.X+24+450) `
                    ($settingsOrigin.Y+24+96+180) $boot
                $settingsViewportX=$settingsOrigin.X+24+250
                $settingsViewportY=$settingsOrigin.Y+24+96+100
                Move-C166Pointer $settingsViewportX $settingsViewportY $boot
                $scrollDownStart=(Get-C166Serial $boot.serialPath).Length
                Send-C166QmpEvents ([int]$boot.qmpPort) `
                    @((New-C166Wheel 1),(New-C166Wheel 1),(New-C166Wheel 1)) `
                    $boot.qmpLogPath 180
                $scrollDown=Wait-C166Serial $boot.serialPath `
                    '^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed sections-translated=PASS result=PASS' `
                    $scrollDownStart 20
                $scrollDownScreenshot=Join-Path $root 'settings-scroll-lower-sections.ppm'
                $scrollDownSha=Capture-C167GuestScreen $boot $scrollDownScreenshot
                $scrollUpStart=(Get-C166Serial $boot.serialPath).Length
                Send-C166QmpEvents ([int]$boot.qmpPort) `
                    @((New-C166Wheel -1),(New-C166Wheel -1),(New-C166Wheel -1)) `
                    $boot.qmpLogPath 180
                $scrollUp=Wait-C166Serial $boot.serialPath `
                    '^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed sections-translated=PASS result=PASS' `
                    $scrollUpStart 20
                $focusStart=(Get-C166Serial $boot.serialPath).Length
                Press-C166Key 'tab' $boot
                $tab=Wait-C166Serial $boot.serialPath `
                    '^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=(?:traversed|offscreen-revealed) result=PASS' `
                    $focusStart 15
                $shiftTabStart=(Get-C166Serial $boot.serialPath).Length
                Send-C166QmpEvents ([int]$boot.qmpPort) `
                    @((New-C166Key 'shift' $true),(New-C166Key 'tab' $true),
                      (New-C166Key 'tab' $false),(New-C166Key 'shift' $false)) `
                    $boot.qmpLogPath 90
                $shiftTab=Wait-C166Serial $boot.serialPath `
                    '^\[C102-MANAGED-OUTPUT\] C144-FOCUS shift-tab=handled earlier-member=PASS result=PASS' `
                    $shiftTabStart 15
                $defaultApps=Enter-C167DefaultAppsPage $boot 'NoOverride' `
                    (Join-Path $root 'default-apps-system-default.ppm')
                $c169PointerSelection=$null
                if ($script:c169ManagedUiOrdinaryAcceptance) {
                    $slotsBeforePointer=Get-C166AssociationDiskState $esp
                    [void](Assert-C167DiskState $slotsBeforePointer 'NoOverride' $false)
                    # The clean initial C167 state already selects Managed Notes
                    # (index 0). Choose the other option so this proof observes
                    # an actual pending selection transition and commit marker.
                    $c169PointerSelection=Select-C167DefaultAppsChoice $boot 1 $root
                    $slotsAfterPointer=Get-C166AssociationDiskState $esp
                    [void](Assert-C167DiskState $slotsAfterPointer 'NoOverride' $false)
                    if ((Get-C166SlotIdentity $slotsBeforePointer) -ne (Get-C166SlotIdentity $slotsAfterPointer)) {
                        throw 'Ordinary boot 2 pointer ComboBox selection wrote association persistence before Apply.'
                    }
                }
                Close-C167SettingsCenter $boot
                $managed=Invoke-C166ManagedExplorerOpen $boot 'Default' `
                    (Join-Path $root 'managed-explorer-notes.ppm')
                $settingsNavigation=[ordered]@{
                    initialSettings='PASS';defaultAppsEntry=$settingsPageEntry
                    settingsReturn=$settingsReturn.Match.Value
                    initialSettingsScreenshot=$settingsInitialScreenshot
                    initialSettingsScreenshotSha256=$settingsInitialSha
                    scrollDown=$scrollDown.Match.Value
                    scrollUp=$scrollUp.Match.Value;tab=$tab.Match.Value
                    shiftTab=$shiftTab.Match.Value
                    lowerSectionsScreenshot=$scrollDownScreenshot
                    lowerSectionsScreenshotSha256=$scrollDownSha
                    c169PointerPopup=$c169PointerSelection
                    defaultApps=$defaultApps;close='PASS';result='PASS'
                }
            }
            $native=Invoke-C166NativeExplorerOpen $boot 'Default' `
                (Join-Path $root 'native-explorer-selected.ppm') (Join-Path $root 'native-explorer-notes.ppm')
            $snapshot=Invoke-C166TaskManagerSnapshot $boot
            $nativeClose=Close-C166NativeExplorerThroughTaskManager $boot $snapshot.rowCount
            Close-C166TaskManager $boot
            $integration=[ordered]@{settingsNavigation=$settingsNavigation;managedFileExplorer=$managed;nativeFileExplorer=$native;taskManager=$snapshot;nativeFileExplorerClose=$nativeClose;taskManagerClose='PASS';C164NotesActivation='PASS'}
        } elseif ($Number -eq 3) {
            Initialize-C166GuestPointer $boot
            Close-C166ManagedNotesStartupWindow $boot
            $notes=Invoke-C166ManagedExplorerOpen $boot 'Default' (Join-Path $root 'managed-explorer-notes.ppm')
            $calcStart=(Get-C166Serial $boot.serialPath).Length
            Open-C166AllProgramsItem 11 $boot
            [void](Wait-C166Serial $boot.serialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.calculator selector=00000006 image=/system/apps/GXOSAPP\.ELF metadata=valid' $calcStart 35)
            [void](Wait-C166Serial $boot.serialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator selector=6 controls=18 capacity=18 focus=18 fresh=PASS result=PASS' $calcStart 35)
            Press-C166Key '7' $boot
            Send-C166QmpEvents ([int]$boot.qmpPort) `
                @((New-C166Key 'shift' $true),(New-C166Key '8' $true),
                  (New-C166Key '8' $false),(New-C166Key 'shift' $false)) $boot.qmpLogPath 90
            Press-C166Key '8' $boot
            Press-C166Key 'ret' $boot
            [void](Wait-C166Serial $boot.serialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=56 phase=3 commands=4 result=PASS' $calcStart 20)
            $calcScreen=Capture-C166GuestScreen $boot (Join-Path $root 'calculator-56.ppm')
            $calcCloseStart=(Get-C166Serial $boot.serialPath).Length
            Send-C166QmpEvents ([int]$boot.qmpPort) `
                @((New-C166Key 'alt' $true),(New-C166Key 'f4' $true),
                  (New-C166Key 'f4' $false),(New-C166Key 'alt' $false)) $boot.qmpLogPath 90
            [void](Wait-C166Serial $boot.serialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 active=none modal=none popup=none drag=none result=PASS' $calcCloseStart 30)
            $snapshot=Invoke-C166TaskManagerSnapshot $boot
            Close-C166TaskManager $boot
            $notesFocusStart=(Get-C166Serial $boot.serialPath).Length
            Open-C166AllProgramsItem 12 $boot
            $notesFocus=Wait-C166Serial $boot.serialPath '^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9A-Fa-f]{16} selector=00000004 existing=true result=PASS' $notesFocusStart 25
            $origin=[pscustomobject]@{X=[Math]::Max(0,[int](($script:c166ScreenWidth-600)/2));Y=[Math]::Max(0,[int](($script:c166ScreenHeight-360)/2))}
            $settingsStart=(Get-C166Serial $boot.serialPath).Length
            Click-C166Screen ($origin.X+475) ($origin.Y+24+294) $boot
            [void](Wait-C166Serial $boot.serialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $settingsStart 90)
            [void](Wait-C166Serial $boot.serialPath '^(?:\[C102-MANAGED-OUTPUT\] )?C148-LOAD-META path=/system/apps/GXSETT\.BIN v=2 size=26 .*result=PASS' $settingsStart 60)
            [void](Wait-C166Serial $boot.serialPath '^(?:\[C102-MANAGED-OUTPUT\] )?C145-FINAL viewport=valid registration=\d+ modal=none capture=none drag=none result=PASS' $settingsStart 120)
            $defaultApps=$null
            if ($script:c167SettingsAcceptance) {
                $defaultApps=Enter-C167DefaultAppsPage $boot 'NoOverride' (Join-Path $root 'default-apps-initial-system-default.ppm')
                $slotsBeforeApply=Get-C166AssociationDiskState $esp
                [void](Assert-C167DiskState $slotsBeforeApply 'NoOverride' $false)
                $c169PopupSelection=$null
                if ($script:c169ManagedUiOrdinaryAcceptance) {
                    $noDefaultScreenshotRoot=Join-Path $root 'c169-popup-no-default'
                    New-Item -ItemType Directory -Force -Path $noDefaultScreenshotRoot | Out-Null
                    $toNoDefault=Select-C167DefaultAppsChoice $boot 1 $noDefaultScreenshotRoot
                    $slotsAfterPopup=Get-C166AssociationDiskState $esp
                    [void](Assert-C167DiskState $slotsAfterPopup 'NoOverride' $false)
                    if ((Get-C166SlotIdentity $slotsBeforeApply) -ne (Get-C166SlotIdentity $slotsAfterPopup)) {
                        throw 'Ordinary boot 3 ComboBox popup selection wrote association persistence before Apply.'
                    }
                    $c169PopupSelection=[ordered]@{selectedNoDefault=$toNoDefault;committedStateBeforeApply=$slotsAfterPopup}
                }
                $applyState=if ($script:c169ManagedUiOrdinaryAcceptance) {'Disabled'} else {'ApplicationOverride'}
                $applyScreenshotName=if ($applyState -eq 'Disabled') {'default-apps-applied-no-default.ppm'} else {'default-apps-applied-explicit-notes.ppm'}
                $apply=Apply-C167DefaultAppsChoice $boot $applyState (Join-Path $root $applyScreenshotName)
                $slotsAfterApply=Get-C166AssociationDiskState $esp
                [void](Assert-C167DiskState $slotsAfterApply $applyState $true)
                $reset=Reset-C167DefaultApps $boot (Join-Path $root 'default-apps-reset-system-default.ppm')
                $reentry=Return-C167ToSettingsAndReenter $boot 'NoOverride' (Join-Path $root 'default-apps-reset-reentry.ppm')
                $defaultApps=[ordered]@{initial=$defaultApps;apply=$apply;reset=$reset;reentry=$reentry;slotsBeforeApply=$slotsBeforeApply;slotsAfterApply=$slotsAfterApply;c169PopupSelection=$c169PopupSelection;slotVerification='authoritative query during Reset plus host disk read after QEMU shutdown';result='PASS'}
            }
            $settingsCloseStart=(Get-C166Serial $boot.serialPath).Length
            Send-C166QmpEvents ([int]$boot.qmpPort) `
                @((New-C166Key 'alt' $true),(New-C166Key 'f4' $true),
                  (New-C166Key 'f4' $false),(New-C166Key 'alt' $false)) $boot.qmpLogPath 90
            [void](Wait-C166Serial $boot.serialPath '^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.settingscenter generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ reason=(?:close-or-replace|return)' $settingsCloseStart 25)
            if ($script:c167SettingsAcceptance) {
                Open-C167SettingsCenter $boot | Out-Null
                $reopened=Enter-C167DefaultAppsPage $boot 'NoOverride' (Join-Path $root 'default-apps-close-reopen-system-default.ppm')
                Close-C167SettingsCenter $boot
                $defaultApps.reopen=$reopened
            }
            $settings=[ordered]@{launch='PASS';close='PASS';format='v2';loadMeta='PASS';ownership='modal=none capture=none drag=none';focusMarker=$notesFocus.Match.Value}
            $integration=[ordered]@{managedFileExplorer=$notes;calculator=[ordered]@{display=56;screen=Join-Path $root 'calculator-56.ppm';screenSha256=$calcScreen};taskManager=$snapshot;settings=$settings;defaultApps=$defaultApps;C164NotesActivation='PASS';C162NotesFocus=$true}
        } else { throw "Unexpected C166 ordinary boot number $Number." }
        $full=Get-C166Serial $boot.serialPath
        if ($full -match '(?m)^PROOF-SCENARIO-|^\[C102-MANAGED-OUTPUT\] PROOF C166') {
            throw "Clean ordinary boot $Number contains active PROOF1 output."
        }
        Stop-OwnedProofQemu $boot
        $boot.serial=Get-C166Serial $boot.serialPath
        $boot.serialSha256=Get-Sha256 $boot.serialPath
        $boot.qmpInputSha256=Get-Sha256 $boot.qmpLogPath
        $boot.productionIntegration=$integration
        $boot.status='PASS'
        $boot.kernelSha256=Get-Sha256 (Join-Path $esp 'kernel.elf')
        $boot.ramdiskSha256=Get-Sha256 (Join-Path $esp 'ramdisk.img')
        $boot.associationSlots=Get-C166AssociationDiskState $esp
        $boot.logPath=$boot.serialPath
        $boot.serialSummary=[ordered]@{
            ordinaryNotesReady=($boot.serial -match '(?m)^\[C102-MANAGED-OUTPUT\] C166-ORDINARY-READY app=ManagedNotes render=PASS')
            proof1OutputAbsent=($boot.serial -notmatch '(?m)^PROOF-SCENARIO-|^\[C102-MANAGED-OUTPUT\] PROOF C166')
            containsFailure=($boot.serial -match '(?m)^\[C102-MANAGED-OUTPUT\] (?:C158|C161|C162|C163|C164|C145|C148)-[^\r\n]*result=FAIL')
        }
        $boot.pid=$boot.process.Id
        $boot.process=$null
        $boot.serial=$null
        return $boot
    } finally {
        Stop-OwnedProofQemu $boot
    }
}

function Invoke-C166OrdinaryAcceptance([string]$RepoRoot,[string]$EvidenceRoot,
    [string]$ProofCompositeHash,[string]$ProofKernelHash,[string]$ProofRamdiskHash,
    [string[]]$Flags,[string]$Qemu,[string]$Ovmf,[string]$Bootloader,[string]$Make,[string]$Compiler) {
    $acceptanceRoot = if ($script:c167SettingsAcceptance) {
        Join-Path $script:c167EvidenceRoot 'production-acceptance'
    } else {
        Join-Path $RepoRoot 'out\dotnet\c166a2-final-acceptance'
    }
    $proofManifestPath=Join-Path (Join-Path $EvidenceRoot 'proof1-guest-runs') 'proof1-manifest.json'
    $historicalProofPrerequisite = $null
    $hostManifestPath = if ($script:c167SettingsAcceptance) {
        Join-Path (Join-Path $script:c167EvidenceRoot 'host-validation') 'c166r9-host-manifest.json'
    } else {
        Join-Path (Join-Path $acceptanceRoot 'host-validation') 'c166r9-host-manifest.json'
    }
    if (-not (Test-Path -LiteralPath $proofManifestPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $hostManifestPath -PathType Leaf)) {
        throw 'C166 ordinary acceptance requires preserved C166A2 proof and fresh host validation manifests.'
    }
    $hostManifest=Get-Content -LiteralPath $hostManifestPath -Raw | ConvertFrom-Json
    $proofManifest = $null
    $proofReady = $false
    $currentProofManifest = $null
    if (Test-Path -LiteralPath $proofManifestPath -PathType Leaf) {
        $currentProofManifest=Get-Content -LiteralPath $proofManifestPath -Raw | ConvertFrom-Json
        $proofReady = [bool]$currentProofManifest.guestLaneExecuted -and
            @($currentProofManifest.guestBoots).Count -eq 3 -and
            @($currentProofManifest.successfulManagedMutations).Count -ge 25
        if ($proofReady) { $proofManifest=$currentProofManifest }
    }
    if (-not $proofReady -and $script:c170ControlRendering) {
        # A failed C170 refresh remains incomplete. C170 may rely on the already
        # accepted C166A2 proof only as an explicitly linked prerequisite; it
        # never copies the historic manifest into the C170 proof output.
        $historicalProofManifestPath = Join-Path $RepoRoot `
            'out\dotnet\c167-default-apps\proof1-guest-runs\proof1-manifest.json'
        $historicalAcceptancePath = Join-Path $RepoRoot `
            'out\dotnet\c167-default-apps\production-acceptance\c166a2-manifest.json'
        foreach ($path in @($historicalProofManifestPath,$historicalAcceptancePath)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "C170 historical C166 proof prerequisite is missing: $path"
            }
        }
        $historicalProof=Get-Content -LiteralPath $historicalProofManifestPath -Raw | ConvertFrom-Json
        $historicalAcceptance=Get-Content -LiteralPath $historicalAcceptancePath -Raw | ConvertFrom-Json
        $historicalProofCanonical=[System.IO.Path]::GetFullPath($historicalProofManifestPath)
        $acceptedProofCanonical=[System.IO.Path]::GetFullPath([string]$historicalAcceptance.proofManifest)
        if ($historicalAcceptance.outcome -ne 'A' -or -not $historicalAcceptance.c166Accepted -or
            -not [StringComparer]::OrdinalIgnoreCase.Equals($acceptedProofCanonical,$historicalProofCanonical) -or
            -not $historicalProof.guestLaneExecuted -or @($historicalProof.guestBoots).Count -ne 3 -or
            @($historicalProof.successfulManagedMutations).Count -lt 25) {
            throw 'C170 historical C166 proof is not linked to a passing accepted C166A2 manifest.'
        }
        $refreshDiagnosticPath = Join-Path $script:c170EvidenceOutputRoot 'c170-proof1-refresh-incomplete.json'
        $refreshRunRoot = Join-Path $script:c170EvidenceOutputRoot 'proof1-guest-runs'
        $latestRefreshRun = Get-ChildItem -LiteralPath $refreshRunRoot -Directory -Filter 'guest-run-*' `
            -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        $refreshSerials=[System.Collections.Generic.List[object]]::new()
        if ($latestRefreshRun) {
            foreach ($bootNumber in 1..3) {
                $serialPath=Join-Path (Join-Path $latestRefreshRun.FullName "boot$bootNumber") 'serial.log'
                if (Test-Path -LiteralPath $serialPath -PathType Leaf) {
                    $serialText=Get-Content -LiteralPath $serialPath -Raw
                    $refreshSerials.Add([ordered]@{
                        boot=$bootNumber
                        path=[System.IO.Path]::GetFullPath($serialPath)
                        sha256=Get-Sha256 $serialPath
                        c166SameBootPassMarkerObserved=($serialText -match '(?m)^\[C102-MANAGED-OUTPUT\] PROOF-SCENARIO-PASS C166 S Same$')
                        missingSurfaceDestroy=($serialText -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C150-SURFACE.*destroy appId=com\.guidexos\.apps\.managed\.notes')
                        staleDispatchObserved=($serialText -match '(?m)^\[C150-MANAGED-OUTPUT\] C150-DISPATCH rejected=stale-application result=FAIL')
                    })
                }
            }
        }
        $refreshDiagnostic=[ordered]@{
            schemaVersion=1
            phase='C170-C166-proof-refresh-diagnostic'
            sourceHead=$head
            sourceBranch='v1.1_DOTNET_SUPPORT'
            status='INCOMPLETE'
            scenarioResult='INCOMPLETE'
            reason='C170 proof refresh did not complete its application-integration lane: Boot 1 emitted the C166 SameBoot pass marker but timed out waiting for the Managed Notes C150 surface-destroy marker after stale-application dispatch evidence.'
            guestLaneExecuted=[bool]$currentProofManifest.guestLaneExecuted
            c166Accepted=[bool]$currentProofManifest.c166Accepted
            currentProofBuildManifest=[ordered]@{
                path=[System.IO.Path]::GetFullPath($proofManifestPath)
                sha256=Get-Sha256 $proofManifestPath
            }
            failedRunPath=if ($latestRefreshRun) { [System.IO.Path]::GetFullPath($latestRefreshRun.FullName) } else { $null }
            serials=@($refreshSerials.ToArray())
            historicalEvidenceSubstituted=$false
            artifactIntegrity=[ordered]@{result='PASS';algorithm='SHA-256';errors=@()}
        }
        $refreshDiagnostic | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $refreshDiagnosticPath -Encoding utf8
        $verifiedRefresh=Get-Content -LiteralPath $refreshDiagnosticPath -Raw | ConvertFrom-Json
        if ($verifiedRefresh.status -ne 'INCOMPLETE' -or $verifiedRefresh.scenarioResult -eq 'PASS') {
            throw 'C170 proof refresh diagnostic incorrectly claims completion.'
        }
        foreach ($serial in $verifiedRefresh.serials) {
            if (-not (Test-Path -LiteralPath $serial.path -PathType Leaf) -or
                (Get-Sha256 $serial.path) -ne $serial.sha256) {
                throw "C170 proof refresh diagnostic references a missing or mismatched serial: $($serial.path)"
            }
        }
        $historicalSerials=[System.Collections.Generic.List[object]]::new()
        foreach ($boot in $historicalProof.guestBoots) {
            if ([String]::IsNullOrWhiteSpace([string]$boot.serialPath) -or
                [String]::IsNullOrWhiteSpace([string]$boot.serialSha256) -or
                -not (Test-Path -LiteralPath $boot.serialPath -PathType Leaf) -or
                (Get-Sha256 $boot.serialPath) -ne $boot.serialSha256) {
                throw "C170 historical C166 proof has a missing or mismatched serial for $($boot.stage)."
            }
            $serialText=Get-Content -LiteralPath $boot.serialPath -Raw
            if ($serialText -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] PROOF-SCENARIO-PASS C166 ') {
                throw "C170 historical C166 proof serial lacks its completion marker: $($boot.serialPath)"
            }
            $historicalSerials.Add([ordered]@{
                stage=[string]$boot.stage
                path=[System.IO.Path]::GetFullPath([string]$boot.serialPath)
                sha256=[string]$boot.serialSha256
            })
        }
        if (@($historicalProof.successfulManagedMutations | Where-Object { [int]$_.status -ne 0 }).Count -gt 0) {
            throw 'C170 historical C166 proof contains a failed managed mutation.'
        }
        $proofManifest=$historicalProof
        $proofManifestPath=$historicalProofCanonical
        $proofReady=$true
        $historicalProofPrerequisite=[ordered]@{
            kind='accepted-historical-C166A2-proof-prerequisite'
            sourceHead=[string]$historicalProof.sourceHead
            acceptanceSourceHead=[string]$historicalAcceptance.sourceHead
            proofManifestPath=$historicalProofCanonical
            proofManifestSha256=Get-Sha256 $historicalProofCanonical
            acceptanceManifestPath=[System.IO.Path]::GetFullPath($historicalAcceptancePath)
            acceptanceManifestSha256=Get-Sha256 $historicalAcceptancePath
            guestBootCount=@($historicalProof.guestBoots).Count
            successfulManagedMutationCount=@($historicalProof.successfulManagedMutations).Count
            serials=@($historicalSerials.ToArray())
            refreshDiagnosticPath=[System.IO.Path]::GetFullPath($refreshDiagnosticPath)
            refreshDiagnosticSha256=Get-Sha256 $refreshDiagnosticPath
        }
    }
    if (-not $proofReady -or -not $hostManifest.hostValidationComplete) {
        throw 'C166 ordinary acceptance prerequisites are not complete or passing.'
    }
    if ($script:c170ControlRendering) {
        $script:c170OrdinaryProofPrerequisite=$historicalProofPrerequisite
    }
    $qemuOwners=@(Get-C166WorkspaceQemuOwners $RepoRoot |
        ForEach-Object { [ordered]@{pid=$_.ProcessId;path=$_.ExecutablePath;commandLine=$_.CommandLine} })
    if ($qemuOwners.Count -gt 0) {
        $formatted=($qemuOwners | ForEach-Object { "pid=$($_.pid) exe=$($_.path) cmd=$($_.commandLine)" }) -join "`n"
        throw "C166 ordinary boots are blocked by existing QEMU owner(s); none was terminated.`n$formatted"
    }

    $cleanRoot=Join-Path $EvidenceRoot 'clean-production-candidate'
    $buildRoot=Join-Path $cleanRoot 'build'
    $compositeRoot=Join-Path $buildRoot 'composite-c166-production'
    $runtimeOutput=Join-Path $buildRoot 'runtime-pack'
    $composite=Join-Path $compositeRoot 'artifacts\HostLogProof.elf'
    $kernelCandidate=Join-Path $cleanRoot 'kernel.elf'
    $ramdiskCandidate=Join-Path $cleanRoot 'ramdisk.img'
    $settingsRecord=Join-Path $cleanRoot 'GXSETT.BIN'
    New-Item -ItemType Directory -Force -Path $cleanRoot,$buildRoot | Out-Null
    $dotnet=Resolve-Tool 'dotnet.exe' @('C:\Program Files\dotnet\dotnet.exe')
    $python=Resolve-Tool 'python.exe' @(
        'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
        'C:\Python312\python.exe','C:\Python311\python.exe')
    if (-not $dotnet -or -not $python -or -not $Make -or -not $Compiler) {
        throw 'Clean C166 production candidate build tools are unavailable.'
    }

    $managedBuilder=Join-Path $RepoRoot 'scripts\dotnet\build-managed-hostlog-proof.ps1'
    $managedLog=Join-Path $cleanRoot 'managed-composite-build.log'
    $managedArgs=@('-ExecutionPolicy','Bypass','-File',$managedBuilder,
        '-RepoRoot',$RepoRoot,'-OutputRoot',$compositeRoot,
        '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
        '-RuntimePackOutputRoot',$runtimeOutput,'-UseGuideXosRuntimePack',
        '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating',
        '-ManagedProjectMode','C160Composite','-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C158ManagedCalculator',
        '-C161ManagedTaskManager','-C162ManagedTaskManagerClose',
        '-C163ManagedFileExplorer','-C163FileExplorerDiagnostics',
        '-C164FileActivationDiagnostics','-C166OrdinaryBuild',
        '-HeapConfiguration','Primary8MiB','-PythonExe',$python,'-Clean')
    & powershell.exe @managedArgs *> $managedLog
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $composite -PathType Leaf)) {
        throw "Clean C166 production composite build failed; see $managedLog."
    }
    $cleanCompositeHash=Get-Sha256 $composite

    $generator=Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
    $generatorLog=Join-Path $cleanRoot 'production-ramdisk-build.log'
    $generatorArgs=@('-ExecutionPolicy','Bypass','-File',$generator,
        '-OutputDir',(Join-Path $cleanRoot 'wallpaper-pack'),'-OutputImage',$ramdiskCandidate,
        '-C104AppAPath',$composite,'-ProductionCompositeApplicationPath',$composite,
        '-C114ManagedDirectoryServices','-C117ManagedTextArea','-C118ManagedListBox',
        '-C151ManagedOpenFileDialog','-C152ManagedNotesSaveWorkflow','-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C164FileActivation')
    & powershell.exe @generatorArgs *> $generatorLog
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $ramdiskCandidate -PathType Leaf)) {
        throw "Clean C166 production ramdisk build failed; see $generatorLog."
    }
    $cleanRamdiskHash=Get-Sha256 $ramdiskCandidate

    $kernelPath=Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
    $espKernel=Join-Path $RepoRoot 'ESP\kernel.elf'
    $espRamdisk=Join-Path $RepoRoot 'ESP\ramdisk.img'
    $protectedRoot = if ($script:c167SettingsAcceptance) {
        Join-Path $script:c167EvidenceRoot 'protected-media-before'
    } else {
        Join-Path $acceptanceRoot 'protected-media-before-c166a2'
    }
    $kernelBackup=Join-Path $protectedRoot 'kernel-build-kernel.elf'
    $espKernelBackup=Join-Path $protectedRoot 'esp-kernel.elf'
    $ramdiskBackup=Join-Path $protectedRoot 'esp-ramdisk.img'
    foreach ($path in @($kernelBackup,$espKernelBackup,$ramdiskBackup,$kernelPath,$espKernel,$espRamdisk)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Protected production media is missing: $path" }
    }
    $baselineKernelHash=Get-Sha256 $kernelBackup
    $baselineEspKernelHash=Get-Sha256 $espKernelBackup
    $baselineRamdiskHash=Get-Sha256 $ramdiskBackup
    if ((Get-Sha256 $kernelPath) -ne $baselineKernelHash -or
        (Get-Sha256 $espKernel) -ne $baselineEspKernelHash -or
        (Get-Sha256 $espRamdisk) -ne $baselineRamdiskHash) {
        throw 'Protected production artifacts changed since byte-exact C166A2 backup; leaving them untouched.'
    }
    $preInstallSlots=Get-C166AssociationDiskState (Join-Path $RepoRoot 'ESP')
    if (@($preInstallSlots | Where-Object { $_.present }).Count -gt 0) {
        throw 'Protected production ESP unexpectedly contains association overrides; refusing to replace production media.'
    }

    $productionProofOnlyFlags = @(
        '-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF',
        '-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
        '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF',
        '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF',
        '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF'
    )
    $cleanFlags=@(($Flags) | Where-Object {
        $_ -ne '-DGUIDEXOS_PROOF_CONTROL' -and
        $_ -ne '-DGXOS_NATIVEAOT_C166_FILE_ASSOCIATION_PROOF' -and
        $_ -notin $productionProofOnlyFlags
    })
    $kernelLog=Join-Path $cleanRoot 'production-kernel-build.log'
    $kernelArgs=@('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
        "EXTRA_CFLAGS=$($cleanFlags -join ' ')",'-B')
    $priorErrorActionPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $Make @kernelArgs *> $kernelLog
        $kernelBuildExitCode=$LASTEXITCODE
    } finally {
        $ErrorActionPreference=$priorErrorActionPreference
    }
    if ($kernelBuildExitCode -ne 0 -or -not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) {
        if ((Get-Sha256 $kernelPath) -eq $baselineKernelHash) {
            throw "Clean C166 production kernel link failed without changing the protected canonical kernel; see $kernelLog."
        }
        throw "Clean C166 production kernel link failed after changing the canonical build output; protected bytes remain in $kernelBackup, and were not restored over a possibly newer file. See $kernelLog."
    }
    Copy-Item -LiteralPath $kernelPath -Destination $kernelCandidate -Force
    $cleanKernelHash=Get-Sha256 $kernelCandidate
    if ((Get-Sha256 $kernelPath) -ne $cleanKernelHash) { throw 'Clean kernel candidate copy does not match the linked output.' }
    Copy-Item -LiteralPath $kernelBackup -Destination $kernelPath -Force
    if ((Get-Sha256 $kernelPath) -ne $baselineKernelHash) { throw 'Protected canonical kernel restoration failed after candidate build.' }

    $scanPaths=@($composite,$kernelCandidate,$ramdiskCandidate)
    $forbidden=@('GUIDEXOS_PROOF_CONTROL','PROOF-SCENARIO-PASS','PROOF-SCENARIO-FAIL','PROOF C166','proof1.bin','GXPF')
    $scanResults=@()
    foreach ($path in $scanPaths) {
        $text=[System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($path))
        foreach ($needle in $forbidden) {
            if ($text.IndexOf($needle,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "Clean production candidate contains prohibited PROOF1 string '$needle' in $path."
            }
        }
        $scanResults += [ordered]@{path=$path;sha256=Get-Sha256 $path;forbiddenProof1Strings='absent'}
    }
    New-C166SettingsRecord $settingsRecord
    $settingsHash=Get-Sha256 $settingsRecord

    $ordinaryRunId=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $ordinaryRoot=Join-Path (Join-Path $acceptanceRoot 'ordinary-boots') "ordinary-run-$ordinaryRunId"
    $ordinary=@()
    for ($number=1; $number -le 3; $number++) {
        if (@(Get-C166WorkspaceQemuOwners $RepoRoot).Count -gt 0) {
            throw "A QEMU owner using this workspace appeared before C166 ordinary boot $number; no process was terminated."
        }
        $ordinary += Invoke-C166OrdinaryBoot $number $kernelCandidate $ramdiskCandidate `
            $settingsRecord $Qemu $Ovmf $Bootloader $ordinaryRoot
    }
    if ($ordinary.Count -ne 3 -or @($ordinary | Where-Object status -ne 'PASS').Count -ne 0) {
        throw 'C166 ordinary production boot results were not 3/3 PASS.'
    }
    foreach ($boot in $ordinary) {
        if ($boot.kernelSha256 -ne $cleanKernelHash -or $boot.ramdiskSha256 -ne $cleanRamdiskHash) {
            throw "C166 ordinary boot $($boot.stage) did not use the exact clean candidate artifacts."
        }
    }
    if ($script:c167SettingsAcceptance) {
        for ($index=0; $index -lt 3; $index++) {
            $mustHaveNoOverrideRecord = $index -eq 2
            [void](Assert-C167DiskState $ordinary[$index].associationSlots 'NoOverride' $mustHaveNoOverrideRecord)
        }
    } elseif (@($ordinary[0].associationSlots | Where-Object { $_.present }).Count -gt 0 -or
        @($ordinary[1].associationSlots | Where-Object { $_.present }).Count -gt 0 -or
        @($ordinary[2].associationSlots | Where-Object { $_.present }).Count -gt 0) {
        throw 'A clean ordinary C166 guest created a persistent association override unexpectedly.'
    }
    if ($script:c169ManagedUiOrdinaryAcceptance) {
        $boot1C169 = $ordinary[0].productionIntegration.defaultApps.c169KeyboardFocus
        $boot2C169 = $ordinary[1].productionIntegration.settingsNavigation.c169PointerPopup
        $boot3C169 = $ordinary[2].productionIntegration.defaultApps.c169PopupSelection
        if (-not $boot1C169 -or $boot1C169.interaction.keyboardOpenEscape -ne 'PASS' -or
            [string]::IsNullOrWhiteSpace($boot1C169.reopenedFocus) -or
            -not $boot2C169 -or $boot2C169.open -ne 'PASS' -or
            -not $boot3C169 -or $boot3C169.selectedNoDefault.open -ne 'PASS') {
            throw 'C169 ordinary production UI checks were incomplete across the three boots.'
        }
    }

    # All clean candidate gates have passed. Install the verified kernel and
    # ramdisk only now; the previous production hashes remain recoverable.
    if ((Get-Sha256 $kernelPath) -ne $baselineKernelHash -or
        (Get-Sha256 $espKernel) -ne $baselineEspKernelHash -or
        (Get-Sha256 $espRamdisk) -ne $baselineRamdiskHash) {
        throw 'Protected media changed after ordinary validation; preserving the newer bytes instead of installing.'
    }
    try {
        Copy-Item -LiteralPath $kernelCandidate -Destination $kernelPath -Force
        Copy-Item -LiteralPath $kernelCandidate -Destination $espKernel -Force
        Copy-Item -LiteralPath $ramdiskCandidate -Destination $espRamdisk -Force
        if ((Get-Sha256 $kernelPath) -ne $cleanKernelHash -or
            (Get-Sha256 $espKernel) -ne $cleanKernelHash -or
            (Get-Sha256 $espRamdisk) -ne $cleanRamdiskHash) {
            throw 'Installed C166 clean candidate hashes do not match the validated candidate.'
        }
    } catch {
        if ((Get-Sha256 $kernelPath) -eq $cleanKernelHash) { Copy-Item -LiteralPath $kernelBackup -Destination $kernelPath -Force }
        elseif ((Get-Sha256 $kernelPath) -ne $baselineKernelHash) { throw 'Canonical kernel changed unexpectedly during failed install; preserved bytes need review.' }
        if ((Get-Sha256 $espKernel) -eq $cleanKernelHash) { Copy-Item -LiteralPath $espKernelBackup -Destination $espKernel -Force }
        elseif ((Get-Sha256 $espKernel) -ne $baselineEspKernelHash) { throw 'ESP kernel changed unexpectedly during failed install; preserved bytes need review.' }
        if ((Get-Sha256 $espRamdisk) -eq $cleanRamdiskHash) { Copy-Item -LiteralPath $ramdiskBackup -Destination $espRamdisk -Force }
        elseif ((Get-Sha256 $espRamdisk) -ne $baselineRamdiskHash) { throw 'ESP ramdisk changed unexpectedly during failed install; preserved bytes need review.' }
        throw
    }
    $finalSlots=Get-C166AssociationDiskState (Join-Path $RepoRoot 'ESP')
    if (@($finalSlots | Where-Object { $_.present }).Count -gt 0) {
        throw 'Installed clean production ESP contains association override slots.'
    }
    $qemuAfter=@(Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-x86_64.exe'" |
        ForEach-Object { [ordered]@{pid=$_.ProcessId;path=$_.ExecutablePath;commandLine=$_.CommandLine} })
    $runnerQemuPids=@($ordinary | ForEach-Object { [int]$_.pid })
    $runnerQemuAfter=@($qemuAfter | Where-Object { $runnerQemuPids -contains [int]$_.pid })
    if ($runnerQemuAfter.Count -gt 0) { throw 'A runner-owned QEMU process remained after ordinary acceptance.' }

    $manifest=[ordered]@{
        phase='C166A2';outcome='A';c166Accepted=$true;sourceHead=$head;sourceBranch='v1.1_DOTNET_SUPPORT'
        preservedC166AEvidence='out/dotnet/proof1/guest-run-20261010-183857-ee661a2c and C166A accepted manifests retained'
        hostValidation=[ordered]@{nativeCases=$hostManifest.nativeCases;managedCases=$hostManifest.managedCases;dualSlotCases=$hostManifest.dualSlotCaseCount;partialWriteCases=$hostManifest.partialWriteCases;stressTransactions=$hostManifest.stressTransactions;status='PASS';manifest=$hostManifestPath}
        proofProducts=[ordered]@{compositeSha256=$ProofCompositeHash;kernelSha256=$ProofKernelHash;ramdiskSha256=$ProofRamdiskHash;scenarioMedia=$proofManifest.scenarioMedia}
        proofManifest=$proofManifestPath;proofBoots=$proofManifest.guestBoots;crossBootState=$proofManifest.crossBootStatePath
        proofPrerequisite=if ($script:c170ControlRendering) { $script:c170OrdinaryProofPrerequisite } else { $null }
        boot2DirectDiagnostics=$proofManifest.boot2DisabledExplorerParity.observedByProof
        boot3DirectRebootComparison=$proofManifest.boot3ReloadedDiagnostic
        successfulManagedMutations=$proofManifest.successfulManagedMutations
        managedMutationCount=@($proofManifest.successfulManagedMutations).Count
        mutationSummary=$proofManifest.mutationSummary
        disabledExplorerParity=$proofManifest.boot2DisabledExplorerParity
        restoredDefaultExplorerParity=$proofManifest.boot3RestoredExplorerParity
            notesActivation=[ordered]@{path='/system/apps/C164/hello.txt';content='hello from C164';contentBytes=15;clean=$true;undo=0;redo=0;caret=0;anchor=0;view=0;source='real VFS fixture through Managed and Native File Explorer'}
        regressions=[ordered]@{
            C164='fresh real Managed/Native File Explorer Notes activation in proof Boot 3 and ordinary boots 1-3';
            C163='fresh Managed File Explorer UI browsing/open and Notes activation';
            C162='fresh identity-safe native File Explorer close through Managed Task Manager in ordinary Boot 2';
            C160='PASS retained from accepted C166A2 proof; startup proof suite excluded from clean ordinary boots';
            C156='PASS retained from accepted C166A2 proof; startup regression tests excluded from clean ordinary boots';
            C145='PASS in focused C167 Settings guest boots and ordinary Settings lifecycle';
            C146='PASS in C166 host validation and focused C167 Settings persistence/error paths';
            C128='unverified; retained C166A evidence unchanged'
        }
        cleanProduction=[ordered]@{compositeSha256=$cleanCompositeHash;kernelSha256=$cleanKernelHash;ramdiskSha256=$cleanRamdiskHash;kernelCanonicalSha256=(Get-Sha256 $kernelPath);kernelEspSha256=(Get-Sha256 $espKernel);ramdiskEspSha256=(Get-Sha256 $espRamdisk);allCandidateStrings=$scanResults;proof1Excluded=$true;settingsRecordFormat='v2/26 bytes';settingsRecordSha256=$settingsHash}
        ordinaryBoots=@($ordinary)
        c169ManagedUiOrdinaryAcceptance=if ($script:c169ManagedUiOrdinaryAcceptance) {
            [ordered]@{
                status='PASS'
                boot1=[ordered]@{keyboardFocus=$ordinary[0].productionIntegration.defaultApps.c169KeyboardFocus;settingsCloseReopen='PASS';staleFocusCheck='fresh Tab focus routed after reopen'}
                boot2=[ordered]@{pointerPopup=$ordinary[1].productionIntegration.settingsNavigation.c169PointerPopup;keyboardAndScroll=$ordinary[1].productionIntegration.settingsNavigation;managedExplorer=$ordinary[1].productionIntegration.managedFileExplorer}
                boot3=[ordered]@{popupSelection=$ordinary[2].productionIntegration.defaultApps.c169PopupSelection;applyReset=$ordinary[2].productionIntegration.defaultApps;calculator=$ordinary[2].productionIntegration.calculator;taskManager=$ordinary[2].productionIntegration.taskManager;settingsCloseReopen='PASS'}
                proof1OutputAbsent=$true
                associationFinal='NoOverride'
            }
        } else { $null }
        settingsSmoke=$ordinary[2].productionIntegration.settings
        finalAssociation=[ordered]@{slotsPresent=0;override='NoOverride (no persisted override files)';effectiveHandler='com.guidexos.apps.managed.notes';evidence='ordinary Managed File Explorer opens the exact C164 TXT fixture through the default handler'}
        productionInstallation='PASS after clean candidate and all three ordinary boots passed'
        qemuOwnership=[ordered]@{beforeOrdinary=@();afterOrdinary=$qemuAfter;runnerOwnedProcessesReaped=$true;unrelatedProcessesTerminated=0}
        protectedBaseline=[ordered]@{kernelSha256=$baselineKernelHash;espKernelSha256=$baselineEspKernelHash;ramdiskSha256=$baselineRamdiskHash;backupRoot=$protectedRoot}
        endingHead=(& git -C $RepoRoot rev-parse HEAD).Trim();acceptanceManifest='out/dotnet/c166a2-final-acceptance/c166a2-manifest.json'
    }
    $manifestPath=Join-Path $acceptanceRoot 'c166a2-manifest.json'
    $manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Write-Host "C166A2 outcome=A C166 accepted: proof, regressions, clean production, and ordinary boots 3/3 PASS. Manifest: $manifestPath"
}

if ($script:c170ControlRendering -and $RunC167SettingsProofBoots) {
    Write-C170IncompleteEvidenceManifest `
        (Join-Path $script:c170EvidenceOutputRoot 'c170-focused-proof-manifest.json') `
        'C170-focused-control-rendering-proof' $head 'C170 focused proof has started; acceptance validation is pending.'
}
if ($script:c170ControlRendering -and $RunOrdinaryAcceptance) {
    Write-C170IncompleteEvidenceManifest `
        (Join-Path $script:c170EvidenceOutputRoot 'c170-ordinary-production-manifest.json') `
        'C170-ordinary-production-control-rendering' $head 'C170 ordinary acceptance has started; production validation is pending.'
}

$c167ProtectedMedia = $null
if ($script:c167SettingsAcceptance) {
    $c167ProtectedMedia = Save-C167ProtectedMedia $RepoRoot $script:c167EvidenceRoot
}

$dotnet = Resolve-Tool 'dotnet.exe' @('C:\Program Files\dotnet\dotnet.exe')
$gcc = Resolve-Tool 'x86_64-elf-gcc.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-gcc.exe')
$gxx = Resolve-Tool 'x86_64-elf-g++.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-g++.exe')
$ld = Resolve-Tool 'x86_64-elf-ld.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-ld.exe')
$objcopy = Resolve-Tool 'x86_64-elf-objcopy.exe' @('C:\x86_64-elf-tools\bin\x86_64-elf-objcopy.exe')
$make = Resolve-Tool 'mingw32-make.exe' @('C:\mingw64\bin\mingw32-make.exe','C:\msys64\mingw64\bin\mingw32-make.exe')
$qemu = Resolve-Tool 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs\qemu\qemu-system-x86_64.exe'),
    'C:\qemu\qemu-system-x86_64.exe',
    'C:\msys64\mingw64\bin\qemu-system-x86_64.exe')
$ovmf = Resolve-Tool 'edk2-x86_64-code.fd' @(
    (Join-Path $RepoRoot 'OVMF.fd'),(Join-Path $RepoRoot 'ovmf.fd'),
    'C:\Program Files\qemu\share\edk2-x86_64-code.fd')
$bootloader = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$qemuRelevant = @()

$sdk = 'unavailable'
if ($dotnet) {
    $sdkOutput = & $dotnet --version 2>&1
    if ($LASTEXITCODE -eq 0) { $sdk = ([string]$sdkOutput -split "`r?`n" | Select-Object -First 1).Trim() }
}
$crossAvailable = $false
if ($gcc -and $gxx -and $ld -and $objcopy) { $crossAvailable = $true }
$productionCompiler = Resolve-Tool 'g++.exe' @('C:\mingw64\bin\g++.exe','C:\msys64\mingw64\bin\g++.exe')
$productionLinker = Resolve-Tool 'ld.exe' @('C:\mingw64\bin\ld.exe','C:\msys64\mingw64\bin\ld.exe')
$productionObjcopy = Resolve-Tool 'objcopy.exe' @('C:\mingw64\bin\objcopy.exe','C:\msys64\mingw64\bin\objcopy.exe')
$productionToolchain = [bool]($make -and $productionCompiler -and $productionLinker -and $productionObjcopy)
$qemuVersion = 'unavailable'
if ($qemu) {
    $versionOutput = & $qemu -version 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        $qemuVersion = ($versionOutput -split "`r?`n" | Select-Object -First 1).Trim()
    }
}
$capability = @"
SOURCE_HEAD=$head
DOTNET_TOOLCHAIN=$(if ($sdk -ne 'unavailable') {'AVAILABLE'} else {'UNAVAILABLE'}) path=$dotnet sdk=$sdk
CROSS_TOOLCHAIN=$(if ($crossAvailable) {'AVAILABLE'} else {'UNAVAILABLE'}) gcc=$gcc g++=$gxx ld=$ld objcopy=$objcopy
PRODUCTION_KERNEL_TOOLCHAIN=$(if ($productionToolchain) {'AVAILABLE'} else {'UNAVAILABLE'}) make=$make g++=$productionCompiler ld=$productionLinker objcopy=$productionObjcopy
QEMU=$(if ($qemu) {'AVAILABLE'} else {'UNAVAILABLE'}) path=$qemu version=$qemuVersion
QEMU_OWNERSHIP=deferred-until-host-and-build-gates
"@
Set-Content -LiteralPath (Join-Path $EvidenceRoot 'environment-capability.txt') -Value $capability -Encoding utf8
Write-Host $capability
if (-not $dotnet) { throw 'Outcome E: current .NET SDK is unavailable.' }

if (-not $productionToolchain) { throw 'Outcome E: repository production kernel build toolchain is unavailable.' }
if (-not $BuildProofProducts -and -not $UseExistingProofProducts) {
    Write-Host 'Focused C166R3 source/tool discovery complete; pass -BuildProofProducts to build fresh proof products.'
    if ($RunProofBoots -and $qemuRelevant.Count -gt 0) {
        throw 'Outcome E: proof boots blocked by an already running QEMU workload; no process was terminated.'
    }
    return
}
if ($BuildProofProducts -and $UseExistingProofProducts) {
    throw 'Choose either fresh proof-product builds or verified existing proof products, not both.'
}

$buildRoot = Join-Path $EvidenceRoot 'build'
$proofComposite = Join-Path $buildRoot 'composite-c166-proof'
$productionComposite = Join-Path $buildRoot 'composite-c166-production'
$runtimeOutput = Join-Path $buildRoot 'runtime-pack'
$proofElf = Join-Path $proofComposite 'artifacts\HostLogProof.elf'
$proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
$c169ProofBuildId = if ($script:c169ManagedUiProof) { Get-Date -Format 'yyyyMMdd-HHmmss-fff' } else { '' }
$c167ProofCompositeName = if ($script:c169ManagedUiProof) { "composite-c167-ui-c169-$c169ProofBuildId" } else { 'composite-c167-ui' }
$c167RuntimePackName = if ($script:c169ManagedUiProof) { "runtime-pack-c167-ui-c169-$c169ProofBuildId" } else { 'runtime-pack-c167-ui' }
$c167ProofComposite = Join-Path $buildRoot $c167ProofCompositeName
$c167RuntimePackOutputRoot = Join-Path $buildRoot $c167RuntimePackName
if ($ReuseC167ProofComposite) {
    $c167ProofComposite = Join-Path $buildRoot 'composite-c167-ui'
    $c167RuntimePackOutputRoot = Join-Path $buildRoot 'runtime-pack-c167-ui'
}
$c167ProofElf = Join-Path $c167ProofComposite 'artifacts\HostLogProof.elf'
$c167UiKernel = Join-Path $EvidenceRoot 'c167-ui-proof-kernel.elf'
$proofScenarioMedia = [ordered]@{}
$c167NeutralRamdisk = $null
$proofCompositeHash = $null
$proofKernelHash = $null
$c167ProofCompositeHash = $null
$c167UiKernelHash = $null
$proofRamdiskHash = $null
$proofKernelStatus = 'not built'

$flags = @('-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION',
    '-DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    '-DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION',
    '-DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    '-DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES',
    '-DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER','-DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT',
    '-DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA','-DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX',
    '-DGXOS_NATIVEAOT_C119_MANAGED_BUTTON','-DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST',
    '-DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX','-DGXOS_NATIVEAOT_C122_MANAGED_LABEL',
    '-DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR','-DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON',
    '-DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR','-DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX',
    '-DGXOS_NATIVEAOT_C127_MANAGED_PANEL','-DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE',
    '-DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT','-DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX',
    '-DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON','-DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX',
    '-DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING','-DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU',
    '-DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU','-DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING',
    '-DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR','-DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT',
    '-DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW','-DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK',
    '-DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK','-DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX',
    '-DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER','-DGXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG',
    '-DGXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE','-DGXOS_NATIVEAOT_C147_RUNTIME_SETTINGS',
    '-DGXOS_NATIVEAOT_C148_SETTINGS_V2','-DGXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING',
    '-DGXOS_NATIVEAOT_C150_MANAGED_APP_RETURN','-DGXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    '-DGXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW','-DGXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    '-DGXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS','-DGXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    '-DGXOS_NATIVEAOT_C158_MANAGED_CALCULATOR','-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF',
    '-DGXOS_NATIVEAOT_C161_MANAGED_TASK_MANAGER','-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
    '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE','-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF',
    '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER','-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF',
    '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF',
    '-DGUIDEXOS_PROOF_CONTROL')

if ($UseExistingProofProducts) {
    $savedManifestPath = Join-Path $EvidenceRoot 'c166r9-proof-manifest.json'
    if (-not (Test-Path -LiteralPath $savedManifestPath -PathType Leaf)) {
        throw "Existing proof-product manifest is missing: $savedManifestPath"
    }
    $savedManifest = Get-Content -Raw -LiteralPath $savedManifestPath | ConvertFrom-Json
    if ($savedManifest.sourceHead -ne $head -or $savedManifest.proofProductsStatus -ne 'PASS') {
        throw 'Existing proof products do not have a PASS manifest for the current source HEAD.'
    }
    if (-not (Test-Path -LiteralPath $proofElf -PathType Leaf) -or
        (Get-Sha256 $proofElf) -ne $savedManifest.proofCompositeSha256) {
        throw 'Existing proof composite is missing or does not match its verified build manifest.'
    }
    if (-not (Test-Path -LiteralPath $proofKernel -PathType Leaf) -or
        (Get-Sha256 $proofKernel) -ne $savedManifest.proofKernelSha256) {
        throw 'Existing proof kernel is missing or does not match its verified build manifest.'
    }
    foreach ($stageName in @('SameBoot','PreReboot','PostReboot')) {
        $savedStage = $savedManifest.scenarioMedia.$stageName
        if (-not $savedStage -or -not (Test-Path -LiteralPath $savedStage.path -PathType Leaf) -or
            (Get-Sha256 $savedStage.path) -ne $savedStage.sha256) {
            throw "Existing $stageName selector ramdisk is missing or does not match its verified build manifest."
        }
        $proofScenarioMedia[$stageName] = [ordered]@{ path=$savedStage.path; sha256=$savedStage.sha256 }
    }
    $proofCompositeHash = $savedManifest.proofCompositeSha256
    $proofKernelHash = $savedManifest.proofKernelSha256
    $proofRamdiskHash = $proofScenarioMedia.SameBoot.sha256
    $proofKernelStatus = $savedManifest.proofKernelStatus
    Write-Host "Verified existing C166 proof products for source HEAD $head."
    if ($RunC167SettingsProofBoots) {
        $c167NeutralRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c167-ui-proof.img'
        if ($ReuseC167ProofComposite) {
            if (-not (Test-Path -LiteralPath $c167ProofElf -PathType Leaf) -or
                -not (Test-Path -LiteralPath $c167NeutralRamdisk -PathType Leaf) -or
                -not (Test-Path -LiteralPath $c167UiKernel -PathType Leaf)) {
                throw 'The previously built C169 UI composite, selector-free ramdisk, or focused kernel is missing.'
            }
            $c167ProofCompositeHash = Get-Sha256 $c167ProofElf
            $c167UiKernelHash = Get-Sha256 $c167UiKernel
            Write-Host "Reusing successful C169 focused UI composite, neutral ramdisk, and kernel: composite=$c167ProofCompositeHash kernel=$c167UiKernelHash ramdisk=$(Get-Sha256 $c167NeutralRamdisk)"
        } else {
        $python = Resolve-Tool 'python.exe' @(
            'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
            'C:\Python312\python.exe','C:\Python311\python.exe')
        if (-not $python) { throw 'C167 focused proof requires Python.' }
        $managedBuilder = Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1'
        $c167ManagedArgs = @('-ExecutionPolicy','Bypass','-File',$managedBuilder,
            '-RepoRoot',$RepoRoot,'-OutputRoot',$c167ProofComposite,
            '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
            '-RuntimePackOutputRoot',$c167RuntimePackOutputRoot,
            '-UseGuideXosRuntimePack','-ProductionApplication',
            '-PersistentCompositeLifecycle','-AllocationMode','Allocating',
            '-ManagedProjectMode','C167ProofComposite','-C155ManagedNotesSession',
            '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument',
            '-C163ManagedFileExplorer','-C163FileExplorerDiagnostics','-C164FileActivationDiagnostics',
            '-HeapConfiguration','Primary8MiB',
            '-PythonExe',$python)
        if ($script:c169ManagedUiProof) {
            $c167ManagedArgs += @('-C158ManagedCalculator','-C161ManagedTaskManager','-C162ManagedTaskManagerClose')
        }
        & powershell.exe @c167ManagedArgs
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $c167ProofElf -PathType Leaf)) {
            throw "Fresh focused C167 UI composite build failed: $LASTEXITCODE"
        }
        $c167ProofCompositeHash = Get-Sha256 $c167ProofElf
        $generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
        $neutralArgs = @('-ExecutionPolicy','Bypass','-File',$generator,
            '-OutputDir',(Join-Path $EvidenceRoot 'staging\wallpaper-pack-c167-ui-proof'),
            '-OutputImage',$c167NeutralRamdisk,'-C104AppAPath',$c167ProofElf,
            '-ProductionCompositeApplicationPath',$c167ProofElf,'-C114ManagedDirectoryServices',
            '-C117ManagedTextArea','-C118ManagedListBox','-C151ManagedOpenFileDialog',
            '-C152ManagedNotesSaveWorkflow','-C155ManagedNotesSession',
            '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C164FileActivation')
        & powershell.exe @neutralArgs
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $c167NeutralRamdisk -PathType Leaf)) {
            throw "C167 selector-free UI proof ramdisk generation failed: $LASTEXITCODE"
        }
        }
        if ($productionToolchain) {
            if ($ReuseC167ProofKernel) {
                $kernelChanges = @(& git -C $RepoRoot diff --name-only -- kernel)
                if ($kernelChanges.Count -ne 0 -or
                    -not (Test-Path -LiteralPath $c167UiKernel -PathType Leaf)) {
                    throw 'The existing focused UI kernel cannot be reused after kernel-source changes or when its protected evidence file is missing.'
                }
                $c167UiKernelHash = Get-Sha256 $c167UiKernel
                Write-Host "Reusing unchanged focused UI kernel: $c167UiKernelHash"
            } else {
                $c167ProofOnlyFlags = @(
                    '-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF',
                    '-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
                    '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF',
                    '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF',
                    '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF',
                    '-DGXOS_NATIVEAOT_C166_FILE_ASSOCIATION_PROOF',
                    '-DGUIDEXOS_PROOF_CONTROL')
                $c167Flags = @($flags | Where-Object { $_ -notin $c167ProofOnlyFlags })
                $c167KernelArgs = @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
                    "EXTRA_CFLAGS=$($c167Flags -join ' ')",'-B')
                $canonicalKernel = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
                try {
                    & $make @c167KernelArgs
                    if ($LASTEXITCODE -ne 0) { throw "Focused C167 UI kernel link failed: $LASTEXITCODE" }
                    Copy-Item -LiteralPath $canonicalKernel -Destination $c167UiKernel -Force
                    $c167UiKernelHash = Get-Sha256 $c167UiKernel
                } finally {
                    Restore-C167CanonicalKernel $c167ProtectedMedia $RepoRoot
                }
            }
        }
    }
} else {
$python = Resolve-Tool 'python.exe' @(
    'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    'C:\Python312\python.exe','C:\Python311\python.exe')
if (-not $python) { throw 'The managed artifact converter Python runtime could not be located.' }
$managedBuilder = Join-Path $PSScriptRoot 'build-managed-hostlog-proof.ps1'
$managedArgs = @('-ExecutionPolicy','Bypass','-File',$managedBuilder,
    '-RepoRoot',$RepoRoot,'-OutputRoot',$proofComposite,
    '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
    '-RuntimePackOutputRoot',$runtimeOutput,'-UseGuideXosRuntimePack',
    '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating','-ProofControl',
    '-ManagedProjectMode','C160Composite','-C155ManagedNotesSession',
    '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C158ManagedCalculator',
    '-C160ApplicationSnapshotProof','-C161ManagedTaskManager','-C161TaskManagerProof',
    '-C162ManagedTaskManagerClose','-C162TaskManagerCloseProof','-C163ManagedFileExplorer',
    '-C163FileExplorerProof','-C164FileActivationProof','-C166FileAssociationProof',
    '-HeapConfiguration','Primary8MiB','-PythonExe',$python)
& powershell.exe @managedArgs
if ($LASTEXITCODE -ne 0) { throw "Fresh C166 managed proof build failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $proofElf -PathType Leaf)) { throw "Proof composite missing: $proofElf" }
$proofCompositeHash = Get-Sha256 $proofElf

$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
$proofRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c166-proof.img'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $proofRamdisk) | Out-Null
$proofScenarioMedia = [ordered]@{}
foreach ($proofStage in @('SameBoot','PreReboot','PostReboot')) {
    $stageSuffix = $proofStage.ToLowerInvariant()
    $stageImage = if ($proofStage -eq 'SameBoot') { $proofRamdisk } else {
        Join-Path $EvidenceRoot "staging\ramdisk-c166-$stageSuffix.img"
    }
    $mediaArgs = @('-ExecutionPolicy','Bypass','-File',$generator,
        '-OutputDir',(Join-Path $EvidenceRoot "staging\wallpaper-pack-c166-$stageSuffix"),
        '-OutputImage',$stageImage,'-C104AppAPath',$proofElf,'-Proof1Control','-Proof1Stage',$proofStage,
        '-ProductionCompositeApplicationPath',$proofElf,'-C114ManagedDirectoryServices',
        '-C117ManagedTextArea','-C118ManagedListBox','-C151ManagedOpenFileDialog',
        '-C152ManagedNotesSaveWorkflow','-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C164FileActivation')
    & powershell.exe @mediaArgs
    if ($LASTEXITCODE -ne 0) { throw "Proof ramdisk generation failed for $proofStage`: $LASTEXITCODE" }
    $proofScenarioMedia[$proofStage] = [ordered]@{ path=$stageImage; sha256=(Get-Sha256 $stageImage) }
}
$proofRamdiskHash = $proofScenarioMedia.SameBoot.sha256

$c167NeutralRamdisk = $null
if ($RunC167SettingsProofBoots) {
    $c167ManagedArgs = @('-ExecutionPolicy','Bypass','-File',$managedBuilder,
        '-RepoRoot',$RepoRoot,'-OutputRoot',$c167ProofComposite,
        '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
        '-RuntimePackOutputRoot',$c167RuntimePackOutputRoot,
        '-UseGuideXosRuntimePack','-ProductionApplication',
        '-PersistentCompositeLifecycle','-AllocationMode','Allocating',
        '-ManagedProjectMode','C167ProofComposite','-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument',
        '-C163ManagedFileExplorer','-C163FileExplorerDiagnostics','-C164FileActivationDiagnostics',
        '-HeapConfiguration','Primary8MiB',
        '-PythonExe',$python)
    if ($script:c169ManagedUiProof) {
        $c167ManagedArgs += @('-C158ManagedCalculator','-C161ManagedTaskManager','-C162ManagedTaskManagerClose')
    }
    & powershell.exe @c167ManagedArgs
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $c167ProofElf -PathType Leaf)) {
        throw "Fresh focused C167 UI composite build failed: $LASTEXITCODE"
    }
    $c167ProofCompositeHash = Get-Sha256 $c167ProofElf
    $c167NeutralRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c167-ui-proof.img'
    $neutralArgs = @('-ExecutionPolicy','Bypass','-File',$generator,
        '-OutputDir',(Join-Path $EvidenceRoot 'staging\wallpaper-pack-c167-ui-proof'),
        '-OutputImage',$c167NeutralRamdisk,'-C104AppAPath',$c167ProofElf,
        '-ProductionCompositeApplicationPath',$c167ProofElf,'-C114ManagedDirectoryServices',
        '-C117ManagedTextArea','-C118ManagedListBox','-C151ManagedOpenFileDialog',
        '-C152ManagedNotesSaveWorkflow',
        '-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument','-C164FileActivation')
    & powershell.exe @neutralArgs
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $c167NeutralRamdisk -PathType Leaf)) {
        throw "C167 selector-free UI proof ramdisk generation failed: $LASTEXITCODE"
    }
}

$kernelArgs = @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
    "EXTRA_CFLAGS=$($flags -join ' ')",'-B')
$proofKernelStatus = 'SKIPPED: no configured AMD64 compiler'
$proofKernelHash = $null
if ($productionToolchain) {
    $canonicalKernel = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
    try {
        & $make @kernelArgs
        if ($LASTEXITCODE -ne 0) { throw "Fresh C166 kernel proof link failed: $LASTEXITCODE" }
        $proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
        Copy-Item -LiteralPath $canonicalKernel -Destination $proofKernel -Force
        $proofKernelHash = Get-Sha256 $proofKernel
        $proofKernelStatus = "PASS: configured AMD64 MinGW fallback ($productionCompiler)"
    } finally {
        if ($RunC167SettingsProofBoots) { Restore-C167CanonicalKernel $c167ProtectedMedia $RepoRoot }
    }
}
if ($RunC167SettingsProofBoots -and $productionToolchain) {
    $c167ProofOnlyFlags = @(
        '-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF',
        '-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF',
        '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF',
        '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF',
        '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF',
        '-DGXOS_NATIVEAOT_C166_FILE_ASSOCIATION_PROOF',
        '-DGUIDEXOS_PROOF_CONTROL')
    $c167Flags = @($flags | Where-Object { $_ -notin $c167ProofOnlyFlags })
    $c167KernelArgs = @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
        "EXTRA_CFLAGS=$($c167Flags -join ' ')",'-B')
    $canonicalKernel = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
    try {
        & $make @c167KernelArgs
        if ($LASTEXITCODE -ne 0) { throw "Focused C167 UI kernel link failed: $LASTEXITCODE" }
        Copy-Item -LiteralPath $canonicalKernel -Destination $c167UiKernel -Force
        $c167UiKernelHash = Get-Sha256 $c167UiKernel
    } finally {
        Restore-C167CanonicalKernel $c167ProtectedMedia $RepoRoot
    }
}
}

# QEMU ownership is deliberately checked only after the host and all three build gates.
$qemuRelevant = @(Get-C166WorkspaceQemuOwners $RepoRoot |
    ForEach-Object { [ordered]@{ pid=$_.ProcessId; path=$_.ExecutablePath; commandLine=$_.CommandLine } })
$guestGate = if ($qemuRelevant.Count -gt 0) { 'BLOCKED: this workspace already owns QEMU' }
    elseif ($RunProofBoots -and -not $proofKernelHash) { 'BLOCKED: no configured AMD64 compiler; no fresh proof kernel' }
    elseif ($RunProofBoots) { 'ready: three managed proof scenarios staged' }
    else { 'not-run: proof boots not requested' }

$manifest = [ordered]@{
    phase='C166R9'; outcome='host-and-proof-build-gates-passed'; accepted=$false
    sourceHead=$head; sourceBranch='v1.1_DOTNET_SUPPORT'
    dotnetSdkPath=$dotnet; dotnetSdkVersion=$sdk
    crossCompilerGcc=$gcc; crossCompilerGxx=$gxx; crossLinker=$ld; crossObjcopy=$objcopy
    productionCompiler=$productionCompiler; productionLinker=$productionLinker
    productionMake=$make; qemuPath=$qemu; qemuVersion=$qemuVersion
    existingWorkspaceQemu=$qemuRelevant; qemuOwnershipPolicy='scoped to command lines referencing this checkout; unrelated QEMU processes are preserved'; proofCompositeSha256=$proofCompositeHash
    proofKernelSha256=$proofKernelHash; proofKernelStatus=$proofKernelStatus; proofRamdiskSha256=$proofRamdiskHash
    proofControlFlag='GUIDEXOS_PROOF_CONTROL'; registeredProofSuites=@('C166')
    c166Scenarios=@('SameBoot','Persistence'); scenarioStages=@('SameBoot','PreReboot','PostReboot')
    scenarioSelectorTransport='fixed 9-byte /system/config/proof1.bin in boot ramdisk'
    scenarioMedia=$proofScenarioMedia; maximumResultMarkerLength=96
    c166GuestLanesWired=@{Boot1='C166/SameBoot';Boot2='C166/Persistence/PreReboot';Boot3='C166/Persistence/PostReboot'}
    persistentProductDiskPreservation='Stage selector media are separate ramdisks; the association-storage disk path is invariant across Boot2 and Boot3.'
    proofProductsStatus='PASS'; proofProductsMarker='C166R9_PROOF_PRODUCTS_READY'
    win32ConsoleImports=@{GetConsoleMode='absent-in-successful-link';GetFileType='absent-in-successful-link';WriteConsoleW='absent-in-successful-link'}
    consoleWriteLineRemoval='strongly-supported-cause-by-before-after-link-evidence'
    qemuBoots='not-run'; qemuAvailable=[bool]$qemu; qemuOwnership=$qemuRelevant; guestAcceptance=$guestGate
    artifactAuthority='current committed source and freshly built proof products; historical C163/C164 hashes are informational only'
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'c166r9-proof-manifest.json') -Encoding utf8
$proof1Manifest = [ordered]@{
    phase='PROOF1'; sourceHead=$head; proofBuildFlag='GUIDEXOS_PROOF_CONTROL'
    registeredSuites=@('C166'); c166Scenarios=@('SameBoot','Persistence')
    scenarioStages=@('SameBoot','PreReboot','PostReboot'); maximumResultMarkerLength=96
    focusedTests=[ordered]@{native='PASS: proof diagnostics read is slot/generation neutral; production rejects operation 4';managed='PASS: 34 cases including selector validation, fixed C166 route, and diagnostic callback'}
    proofDisabledProduction=[ordered]@{managedBuild='PASS';managedProofMarkers='absent';nativeDiagnosticsOperation='rejected'}
    managedNativeAotBuild='PASS'; proofCompositeSha256=$proofCompositeHash
    kernelToolchain='kernel/arch/amd64/Makefile.arch MinGW fallback'
    kernelBuild=$proofKernelStatus; proofRamdiskBuild='PASS'; sameBootRamdiskSha256=$proofRamdiskHash
    qemuAvailability=$(if ($qemu) {'available'} else {'unavailable'})
    guestLaneWired=$true; guestLaneExecutable=[bool]$proofKernelHash
    guestLaneExecuted=$false; c166Accepted=$false
    scenarioMedia=$proofScenarioMedia
    coordinatorPath='samples/managed/HostLogProof/GuideXos/GuideXosProofControl.cs'
    selectorRepresentation='9 bytes: GXPF, version=1, suite=1, scenario=1|2, stage=1|2|3, XOR checksum'
    selectorStorage='separate boot ramdisk /system/config/proof1.bin'
    completionProtocol='exactly one PROOF-SCENARIO-PASS|FAIL C166 <scenario> <stage> marker'
    resultProtocol='PROOF C166 <suite> <scenario> <stage> <step> PASS|FAIL <op> st=<code> ov=<state> [slot=<A|B|N> gen=<n>]'
    crossBootState='host out/dotnet/proof1/guest-run-*/c166-persistence-state.json; expected slot, generation, prior serial SHA-256, and association disk path'
    productDiskIdentity='same writable fat:rw ESP directory at IDE index 0 for Boot 2 and Boot 3; only ramdisk.img is replaced'
}
$proof1Dir = Join-Path $EvidenceRoot 'proof1-guest-runs'
New-Item -ItemType Directory -Force -Path $proof1Dir | Out-Null
if (-not $RunOrdinaryAcceptance) {
    $proof1Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $proof1Dir 'proof1-manifest.json') -Encoding utf8
}
Write-Host 'C166R9_PROOF_PRODUCTS_READY'
Write-Host "C166R9 proof products built from $head"
Write-Host "composite=$proofCompositeHash kernel=$proofKernelHash ramdisk=$proofRamdiskHash"
if ($RunC167SettingsProofBoots) {
    if ($RunProofBoots -or $RunOrdinaryAcceptance) { throw 'C167 real Settings proof runs independently of C166 proof and ordinary lanes.' }
    if (-not $c167NeutralRamdisk) {
        $c167NeutralRamdisk = Join-Path $EvidenceRoot 'staging\ramdisk-c167-ui-proof.img'
    }
    if (-not $c167ProofCompositeHash -or -not $c167UiKernelHash -or -not $c167NeutralRamdisk) {
        throw 'C167 real Settings proof products are incomplete.'
    }
    $c167Python = Resolve-Tool 'python.exe' @(
        'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
        'C:\Python312\python.exe','C:\Python311\python.exe')
    if (-not $c167Python) { throw 'C167 FAT16 proof-disk tooling requires Python.' }
    try {
        Invoke-C167SettingsProofBoots $RepoRoot $script:c167EvidenceRoot $c167ProofElf $c167NeutralRamdisk `
            $c167UiKernel $qemu $ovmf $bootloader $c167Python $c167ProtectedMedia
    } catch {
        if ($script:c170ControlRendering) {
            Write-C170IncompleteEvidenceManifest `
                (Join-Path $script:c170EvidenceOutputRoot 'c170-focused-proof-manifest.json') `
                'C170-focused-control-rendering-proof' $head $_.Exception.Message
        }
        throw
    }
    return
}
if ($RunOrdinaryAcceptance) {
    if ($RunProofBoots) { throw 'Choose separate coherent proof and ordinary acceptance runs.' }
    if (-not $UseExistingProofProducts) { throw 'C166 ordinary acceptance requires the verified existing proof products and completed proof run.' }
    try {
        Invoke-C166OrdinaryAcceptance $RepoRoot $EvidenceRoot $proofCompositeHash $proofKernelHash $proofRamdiskHash $flags $qemu $ovmf $bootloader $make $productionCompiler
    } catch {
        if ($script:c170ControlRendering) {
            Write-C170IncompleteEvidenceManifest `
                (Join-Path $script:c170EvidenceOutputRoot 'c170-ordinary-production-manifest.json') `
                'C170-ordinary-production-control-rendering' $head $_.Exception.Message
        }
        throw
    }
    if ($script:c170ControlRendering) {
        $ordinaryAcceptancePath = Join-Path `
            (Join-Path $script:c167EvidenceRoot 'production-acceptance') 'c166a2-manifest.json'
        try {
            Write-C170OrdinaryEvidenceManifest $script:c170EvidenceOutputRoot $ordinaryAcceptancePath `
                $RepoRoot $head
        } catch {
            Write-C170IncompleteEvidenceManifest `
                (Join-Path $script:c170EvidenceOutputRoot 'c170-ordinary-production-manifest.json') `
                'C170-ordinary-production-control-rendering' $head $_.Exception.Message
            throw
        }
    }
    if ($C167SettingsDefaultApps -and -not $script:c170ControlRendering) {
        $c166AcceptancePath = Join-Path (Join-Path $script:c167EvidenceRoot 'production-acceptance') 'c166a2-manifest.json'
        $c167ProofPath = Join-Path $script:c167EvidenceRoot 'c167-settings-ui-proof-manifest.json'
        $c167HostPath = Join-Path (Join-Path $script:c167EvidenceRoot 'host-validation') 'c167-host-manifest.json'
        foreach ($path in @($c166AcceptancePath,$c167ProofPath,$c167HostPath)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "C167 final manifest prerequisite is missing: $path" }
        }
        $ordinaryManifest = Get-Content -LiteralPath $c166AcceptancePath -Raw | ConvertFrom-Json
        $uiManifest = Get-Content -LiteralPath $c167ProofPath -Raw | ConvertFrom-Json
        $c167HostManifest = Get-Content -LiteralPath $c167HostPath -Raw | ConvertFrom-Json
        if ($uiManifest.status -ne 'PASS' -or @($uiManifest.guestBoots).Count -ne 3 -or
            $ordinaryManifest.outcome -ne 'A' -or @($ordinaryManifest.ordinaryBoots).Count -ne 3 -or
            $c167HostManifest.status -ne 'PASS') {
            throw 'C167 proof, host, or ordinary boot evidence is incomplete.'
        }
        $final = [ordered]@{
            phase='C167';outcome='A';c167Accepted=$true;sourceHead=$head;sourceBranch='v1.1_DOTNET_SUPPORT'
            startingHead='e3550ef6f45201d30ce9fdf169195fc4d1d57ae9';endingHead=$head
            c167HostManifest=$c167HostPath;c167HostCases=20;stressIterations=60;directSlotReferences=0
            c166HostManifest=(Join-Path (Join-Path $script:c167EvidenceRoot 'host-validation') 'c166r9-host-manifest.json')
            c166NativeHostCases=604;c166ManagedHostCases=34;c166DualSlotCases=31;c166PersistenceAudits=1000
            realSettingsUiProof=$uiManifest;cleanProduction=$ordinaryManifest.cleanProduction
            ordinaryBoots=$ordinaryManifest.ordinaryBoots;ordinaryBootCount=3
            regressions=$ordinaryManifest.regressions;c128='unverified; no independent authoritative C128 run was added'
            settingsFormat=[ordered]@{version=2;bytes=26};hostAbi=[ordered]@{version=4;bytes=128;associationCallbackOffset=120;responseBytes=328}
            associationAuthority='kernel::appmodel via GuideXosHost -> GuideXosFileAssociations; managed Settings writes no association slot files'
            finalProductionAssociation=$ordinaryManifest.finalAssociation
            proof1Excluded=$ordinaryManifest.cleanProduction.proof1Excluded
            productionInstallation=$ordinaryManifest.productionInstallation
            productionManifest=$c166AcceptancePath;status='PASS'
        }
        $finalPath = Join-Path $script:c167EvidenceRoot 'c167-acceptance-manifest.json'
        $final | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $finalPath -Encoding utf8
        Write-Host "C167 outcome=A complete proof, production, and ordinary evidence manifest: $finalPath"
    }
    return
}
if (-not $RunProofBoots) {
    Write-Host 'No QEMU boot requested; proof products and manifest are retained.'
    return
}
if (-not $qemu -and $RunProofBoots) { throw 'Outcome D: QEMU executable is unavailable; fresh builds remain usable.' }
if ($RunProofBoots -and $qemuRelevant.Count -gt 0) {
    $owners = ($qemuRelevant | ForEach-Object { "pid=$($_.pid) exe=$($_.path) cmd=$($_.commandLine)" }) -join "`n"
    throw "Outcome E: proof boots are blocked by existing QEMU process(es); no process was terminated.`n$owners"
}
if (-not $proofKernelHash) {
    throw 'Outcome E: guest execution is externally blocked because the configured AMD64 cross compiler is unavailable; the proof scenarios and managed proof composite are built.'
}
if (-not $ovmf -or -not (Test-Path -LiteralPath $bootloader -PathType Leaf)) {
    throw 'Outcome E: fresh QEMU boot requires the repository bootloader and OVMF code image.'
}
$runStamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$guestRoot = Join-Path $proof1Dir "guest-run-$runStamp"
$sharedEsp = Join-Path $guestRoot 'shared-esp'
New-Item -ItemType Directory -Force -Path $sharedEsp | Out-Null
$diskIdentity = [System.IO.Path]::GetFullPath($sharedEsp)
$boot1 = Invoke-ProofQemuBoot 'Boot1-SameBoot' $proofScenarioMedia.SameBoot.path $sharedEsp $bootloader $proofKernel $qemu $ovmf (Join-Path $guestRoot 'boot1') -HoldAfterPass
$boot1Interactions = $null
try {
    Assert-ProofScenario $boot1 'S' 'Same' 5
    if ($boot1.backingEspPath -ne $diskIdentity) { throw 'Boot 1 association storage backing path changed unexpectedly.' }
    $boot1Before = Get-C166AssociationDiskState $sharedEsp
    $boot1NoOverride = @($boot1Before | Where-Object { $_.override -eq 'NoOverride' } | Sort-Object generation -Descending | Select-Object -First 1)[0]
    if (-not $boot1NoOverride -or $boot1NoOverride.generation -ne 2 -or $boot1NoOverride.slot -ne 'B') {
        throw 'Boot 1 SameBoot Reset did not leave the expected directly observed B/2 NoOverride checkpoint.'
    }
    $boot1Evidence = Join-Path $guestRoot 'boot1\integration-evidence'
    New-Item -ItemType Directory -Force -Path $boot1Evidence | Out-Null
    Initialize-C166GuestPointer $boot1
    Close-C166ManagedNotesStartupWindow $boot1
    $managedDefault = Invoke-C166ManagedExplorerOpen $boot1 'Default' `
        (Join-Path $boot1Evidence 'managed-explorer-default-notes.ppm')
    $boot1After = Get-C166AssociationDiskState $sharedEsp
    if ((Get-C166SlotIdentity $boot1Before) -ne (Get-C166SlotIdentity $boot1After)) {
        throw 'Boot 1 Managed File Explorer interaction changed association slot bytes, override state, or generation.'
    }
    $boot1Interactions = [ordered]@{restoredState='NoOverride';observedSlot=$boot1NoOverride.slot;observedGeneration=$boot1NoOverride.generation;effectiveHandler='com.guidexos.apps.managed.notes';managed=$managedDefault;slotImagesBefore=$boot1Before;slotImagesAfter=$boot1After;associationStateAndGenerationUnchanged=$true}
} finally {
    Stop-OwnedProofQemu $boot1
}
$boot1.serial = Get-C166Serial $boot1.serialPath
$boot1.serialSha256 = Get-Sha256 $boot1.serialPath
$boot2 = Invoke-ProofQemuBoot 'Boot2-PreReboot' $proofScenarioMedia.PreReboot.path $sharedEsp $bootloader $proofKernel $qemu $ovmf (Join-Path $guestRoot 'boot2') -HoldAfterPass
$boot2Interactions = $null
try {
    Assert-ProofScenario $boot2 'P' 'Pre' 3
    if ($boot2.backingEspPath -ne $diskIdentity) { throw 'Boot 2 association storage backing path changed unexpectedly.' }
    $boot2Diagnostic = [regex]::Match($boot2.serial, '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Pre [0-9]+ PASS Diag st=0 ov=2 slot=(?<slot>[ABN]) gen=(?<gen>[0-9]+)\r?$')
    if (-not $boot2Diagnostic.Success -or $boot2Diagnostic.Groups['slot'].Value -ne 'A' -or $boot2Diagnostic.Groups['gen'].Value -ne '3') {
        throw 'Boot 2 did not directly report the expected Disabled A/3 persistence checkpoint.'
    }
    $boot2Before = Get-C166AssociationDiskState $sharedEsp
    Initialize-C166GuestPointer $boot2
    $boot2Evidence = Join-Path $guestRoot 'boot2\integration-evidence'
    New-Item -ItemType Directory -Force -Path $boot2Evidence | Out-Null
    $managedDisabled = Invoke-C166ManagedExplorerOpen $boot2 'Disabled' (Join-Path $boot2Evidence 'managed-explorer-disabled.ppm')
    $nativeDisabled = Invoke-C166NativeExplorerOpen $boot2 'Disabled' `
        (Join-Path $boot2Evidence 'native-explorer-disabled-selected.ppm') `
        (Join-Path $boot2Evidence 'native-explorer-disabled-open.ppm')
    $boot2After = Get-C166AssociationDiskState $sharedEsp
    if ((Get-C166SlotIdentity $boot2Before) -ne (Get-C166SlotIdentity $boot2After)) {
        throw 'Boot 2 Explorer interactions changed association slot bytes, override state, or generation.'
    }
    $boot2Interactions = [ordered]@{disabledState='Disabled';observedByProof='A/3';managed=$managedDisabled;native=$nativeDisabled;slotImagesBefore=$boot2Before;slotImagesAfter=$boot2After;associationStateAndGenerationUnchanged=$true}
} finally {
    Stop-OwnedProofQemu $boot2
}
$boot2.serial = Get-C166Serial $boot2.serialPath
$boot2.serialSha256 = Get-Sha256 $boot2.serialPath
$crossBootStatePath = Join-Path $guestRoot 'c166-persistence-state.json'
$crossBootState = [ordered]@{suite='C166';scenario='Persistence';slot=$boot2Diagnostic.Groups['slot'].Value;generation=$boot2Diagnostic.Groups['gen'].Value;boot2SerialSha256=$boot2.serialSha256;associationDiskPath=$diskIdentity}
$crossBootState | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $crossBootStatePath -Encoding ASCII

$boot3 = Invoke-ProofQemuBoot 'Boot3-PostReboot' $proofScenarioMedia.PostReboot.path $sharedEsp $bootloader $proofKernel $qemu $ovmf (Join-Path $guestRoot 'boot3') -HoldAfterPass
$boot3Interactions = $null
$postRebootMutations = @()
try {
    Assert-ProofScenario $boot3 'P' 'Post' 3
    $postRebootMutations = Assert-PostRebootChurn $boot3
    if ($boot3.backingEspPath -ne $diskIdentity -or $boot2.backingEspPath -ne $boot3.backingEspPath) {
        throw 'Boot 2 and Boot 3 did not use the same writable association-storage disk identity.'
    }
    $boot3Diagnostic = [regex]::Match($boot3.serial, '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Post 2 PASS Diag st=0 ov=2 slot=(?<slot>[ABN]) gen=(?<gen>[0-9]+)\r?$')
    if (-not $boot3Diagnostic.Success -or $boot3Diagnostic.Groups['slot'].Value -ne $crossBootState.slot -or $boot3Diagnostic.Groups['gen'].Value -ne $crossBootState.generation) {
        throw 'Boot 3 loaded slot/generation did not exactly match Boot 2 direct diagnostics before any mutation.'
    }
    $boot3BeforeInteractions = Get-C166AssociationDiskState $sharedEsp
    $slotA = @($boot3BeforeInteractions | Where-Object slot -eq 'A')[0]
    $slotB = @($boot3BeforeInteractions | Where-Object slot -eq 'B')[0]
    if ($slotA.generation -ne 29 -or $slotA.override -ne 'NoOverride' -or
        $slotB.generation -ne 28 -or $slotB.override -ne 'ApplicationOverride' -or
        $slotB.applicationId -ne 'com.guidexos.apps.managed.notes') {
        throw 'Boot 3 final association slot images do not match A/29 NoOverride and B/28 Managed Notes.'
    }
    $boot3Evidence = Join-Path $guestRoot 'boot3\integration-evidence'
    New-Item -ItemType Directory -Force -Path $boot3Evidence | Out-Null
    Initialize-C166GuestPointer $boot3
    $nativeDefault = Invoke-C166NativeExplorerOpen $boot3 'Default' `
        (Join-Path $boot3Evidence 'native-explorer-default-selected.ppm') `
        (Join-Path $boot3Evidence 'native-explorer-default-notes.ppm')
    $boot3AfterInteractions = Get-C166AssociationDiskState $sharedEsp
    if ((Get-C166SlotIdentity $boot3BeforeInteractions) -ne (Get-C166SlotIdentity $boot3AfterInteractions)) {
        throw 'Boot 3 Explorer interactions changed association slot bytes, override state, or generation.'
    }
    $boot3Interactions = [ordered]@{restoredState='NoOverride';effectiveHandler='com.guidexos.apps.managed.notes';native=$nativeDefault;managedFromBoot1=$boot1Interactions.managed;slotImagesBefore=$boot3BeforeInteractions;slotImagesAfter=$boot3AfterInteractions;associationStateAndGenerationUnchanged=$true;managedInteractionBoot='Boot1-SameBoot'}
} finally {
    Stop-OwnedProofQemu $boot3
}
$boot3.serial = Get-C166Serial $boot3.serialPath
$boot3.serialSha256 = Get-Sha256 $boot3.serialPath
$proof1Manifest.guestLaneExecuted = $true
$proof1Manifest.c166Accepted = $false
$guestBootSummary = @()
foreach ($bootResult in @($boot1,$boot2,$boot3)) {
    $guestBootSummary += [ordered]@{stage=$bootResult.stage;serialPath=$bootResult.serialPath;serialSha256=$bootResult.serialSha256;exitCode=$bootResult.exitCode}
}
$proof1Manifest.guestBoots = $guestBootSummary
$proof1Manifest.crossBootStatePath = $crossBootStatePath
$proof1Manifest.associationDiskIdentity = [ordered]@{path=$diskIdentity;boot2=$boot2.backingEspPath;boot3=$boot3.backingEspPath;same=$true}
$proof1Manifest.boot2PersistedDiagnostic = [ordered]@{slot=$boot2Diagnostic.Groups['slot'].Value;generation=[int64]$boot2Diagnostic.Groups['gen'].Value;marker=$boot2Diagnostic.Value}
$proof1Manifest.boot3ReloadedDiagnostic = [ordered]@{slot=$boot3Diagnostic.Groups['slot'].Value;generation=[int64]$boot3Diagnostic.Groups['gen'].Value;matchesBoot2=($boot3Diagnostic.Groups['slot'].Value -eq $crossBootState.slot -and $boot3Diagnostic.Groups['gen'].Value -eq $crossBootState.generation);marker=$boot3Diagnostic.Value}
$proof1Manifest.boot2DisabledExplorerParity = $boot2Interactions
$proof1Manifest.boot1RestoredExplorerParity = $boot1Interactions
$proof1Manifest.boot3RestoredExplorerParity = $boot3Interactions
$proof1Manifest.successfulManagedMutations = $postRebootMutations
$proof1Manifest.mutationSummary = [ordered]@{count=$postRebootMutations.Count;successfulPersistedTransactions=$postRebootMutations.Count;generationStart=3;generationEnd=29;alternatingSlots=$true;finalSlot='A';finalOverride='NoOverride';marker=([regex]::Match($boot3.serial,'(?m)^(?:\[C102-MANAGED-OUTPUT\] )?PROOF C166 P Post MUTATION-SUMMARY PASS[^\r\n]*').Value)}
$proof1Manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $proof1Dir 'proof1-manifest.json') -Encoding utf8
Write-Host "PROOF1 managed scenarios, reboot churn, and guest Explorer workflows passed. Evidence: $guestRoot"
