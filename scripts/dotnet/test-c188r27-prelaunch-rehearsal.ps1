$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$artifactRoot = [System.IO.Path]::GetFullPath('E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT')
$nestedRunner = Join-Path $repo 'scripts\dotnet\run-c188r12r1-settings-handoff.ps1'
$parentRunner = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'run-c188r24-initializemodules.ps1') -Raw
. (Join-Path $PSScriptRoot 'c188r26-precreated-evidence-root.ps1')
$pwsh = (Get-Command pwsh.exe -ErrorAction Stop | Select-Object -First 1).Source
$script:checks = 0
function Assert-R27([bool]$Condition,[string]$Name) {
    $script:checks++
    if (-not $Condition) { throw "R27 prelaunch rehearsal failed: $Name" }
}
function Invoke-NestedRehearsal([string]$Candidate,[string]$BootRoot,[string]$GuardPath = '') {
    $arguments = @('-NoProfile','-File',$nestedRunner,'-CandidateRoot',$Candidate,
        '-ExpectedKernelSha256',$script:kernelHash,'-BootEvidencePath',$BootRoot,'-PrelaunchRehearsal')
    if ($GuardPath) { $arguments += @('-BootEvidenceAuthorizationPath',$GuardPath) }
    $previousErrorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
    $output = & $pwsh @arguments 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousErrorPreference }
    $jsonLine = @($output -split "`r?`n" | Where-Object { $_ -match '^\{"schema":"GUIDEXOS_C188R27_PRELAUNCH_REHEARSAL_V1"' } | Select-Object -Last 1)
    $parsed = $null
    if ($jsonLine.Count -gt 0) { $parsed = $jsonLine[0] | ConvertFrom-Json -ErrorAction Stop }
    return [pscustomobject]@{exitCode=$exitCode;output=$output;proof=$parsed}
}

$fixtureRoot = Join-Path $artifactRoot ('r27-prelaunch-fixture-' + [guid]::NewGuid().ToString('N'))
$candidate = Join-Path $fixtureRoot 'candidate'
$bootRoot = Join-Path $fixtureRoot 'boot-authorized-config'
$emptyRoot = Join-Path $fixtureRoot 'boot-authorized-empty'
$absentRoot = Join-Path $fixtureRoot 'boot-runner-created'
$guardRoot = Join-Path $fixtureRoot 'one-boot-guards'
$null = New-Item -ItemType Directory -Path (Join-Path $candidate 'esp\EFI\BOOT'),$guardRoot -Force
try {
    $kernel = Join-Path $candidate 'esp\kernel.elf'
    $ramdisk = Join-Path $candidate 'esp\ramdisk.img'
    $bootloader = Join-Path $candidate 'esp\EFI\BOOT\BOOTX64.EFI'
    [System.IO.File]::WriteAllBytes($kernel,[byte[]](0x4B,0x45,0x52,0x4E))
    [System.IO.File]::WriteAllBytes($ramdisk,[byte[]](0x52,0x41,0x4D,0x44))
    [System.IO.File]::WriteAllBytes($bootloader,[byte[]](0x42,0x4F,0x4F,0x54))
    $script:kernelHash = (Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash
    $ramdiskHash = (Get-FileHash -LiteralPath $ramdisk -Algorithm SHA256).Hash
    Set-Content -LiteralPath (Join-Path $candidate 'ramdisk-staging.json') -Value (@{result='PASS';ramdiskSha256=$ramdiskHash} | ConvertTo-Json) -Encoding utf8
    Set-Content -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Value '{"candidate":"r27-prelaunch-fixture"}' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $candidate 'candidate-integrity.json') -Value '{"integrity":"fixture"}' -Encoding utf8
    $manifestHash = (Get-FileHash -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Algorithm SHA256).Hash
    $integrityHash = (Get-FileHash -LiteralPath (Join-Path $candidate 'candidate-integrity.json') -Algorithm SHA256).Hash
    $guardPath = Join-Path $guardRoot ('attempt-r25-' + [guid]::NewGuid().ToString('N') + '.json')
    $guard = [ordered]@{
        schema='GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1';candidateRoot=$candidate
        candidateManifestSha256=$manifestHash;candidateIntegritySha256=$integrityHash
        bootEvidencePath=$bootRoot;attemptNumber=1;maxGuestBoots=1
        automaticRetry=$false;retryAllowed=$false;consumed=$true;syntheticFixture=$true
    }
    $config = [ordered]@{
        schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1';recordAddress='0x00000001001E14E0'
        recordBytes=80;candidateManifestSha256=$manifestHash
    }
    $null = New-Item -ItemType Directory -Path $bootRoot
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bootRoot 'r25-failfast-capture.json') -Encoding utf8

    $authorizedConfig = Invoke-NestedRehearsal $candidate $bootRoot $guardPath
    Assert-R27 ($authorizedConfig.exitCode -eq 0 -and $null -ne $authorizedConfig.proof -and
        [bool]$authorizedConfig.proof.precreatedEvidenceRoot -and
        -not [bool]$authorizedConfig.proof.qacInvoked -and -not [bool]$authorizedConfig.proof.qemuLaunched) `
        'the actual nested runner accepts its parent-created capture directory and exits before QAC'
    Assert-R27 ((Get-FileHash -LiteralPath (Join-Path $bootRoot 'esp\kernel.elf') -Algorithm SHA256).Hash -eq $script:kernelHash) `
        'parent-to-child handoff copied the exact candidate kernel before its offline return'

    $candidateR29 = Join-Path $fixtureRoot 'candidate-r29-schema3'
    $bootRootR29 = Join-Path $fixtureRoot 'boot-authorized-r29-schema3'
    $null = New-Item -ItemType Directory -Path $candidateR29
    Copy-Item -LiteralPath (Join-Path $candidate 'esp') -Destination $candidateR29 -Recurse
    Copy-Item -LiteralPath (Join-Path $candidate 'ramdisk-staging.json') -Destination $candidateR29
    $runtimeManifestR29 = Join-Path $candidateR29 'runtime-pack.manifest.json'
    [ordered]@{
        c188r25FailFastOriginCapture=$true
        c188r28GcStateGuardDiscriminator=$true
        c188r28GcStateGuardDiscriminatorMarker='GUIDEXOS_NATIVEAOT_C188R28_GC_STATE_GUARD_DISCRIMINATOR'
        c188r29PreallocationGcStateGuard=$true
        c188r29PreallocationGcStateGuardMarker='GUIDEXOS_NATIVEAOT_C188R29_PREALLOCATION_GC_STATE_GUARD'
        failFastOriginRecordSchema=3;failFastOriginRecordBytes=192
    } | ConvertTo-Json | Set-Content -LiteralPath $runtimeManifestR29 -Encoding utf8
    $candidateManifestR29Path = Join-Path $candidateR29 'candidate-manifest.json'
    [ordered]@{candidate='r29-schema3-fixture';runtimePackManifest=$runtimeManifestR29} |
        ConvertTo-Json | Set-Content -LiteralPath $candidateManifestR29Path -Encoding utf8
    $candidateIntegrityR29Path = Join-Path $candidateR29 'candidate-integrity.json'
    Set-Content -LiteralPath $candidateIntegrityR29Path -Value '{"fixture":"r29-schema3"}' -Encoding utf8
    $candidateManifestR29Hash = (Get-FileHash -LiteralPath $candidateManifestR29Path -Algorithm SHA256).Hash
    $candidateIntegrityR29Hash = (Get-FileHash -LiteralPath $candidateIntegrityR29Path -Algorithm SHA256).Hash
    $r25ManifestR29Path = Join-Path $fixtureRoot 'candidate-r29-schema3-r25-candidate-manifest.json'
    [ordered]@{
        schema='GUIDEXOS_C188R25_CANDIDATE_V1';candidateRoot=$candidateR29
        candidateManifestSha256=$candidateManifestR29Hash
        runtimePackManifest=$runtimeManifestR29
        runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimeManifestR29 -Algorithm SHA256).Hash
        diagnostics=[ordered]@{
            recordAddress='0x00000001001E14E0';recordBytes=192;schemaVersion=3
            gcStateGuardDiscriminator=$true;preallocationGcStateGuardDiscriminator=$true
        }
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $r25ManifestR29Path -Encoding utf8
    $guardPathR29 = Join-Path $guardRoot ('attempt-r25-' + [guid]::NewGuid().ToString('N') + '.json')
    $guardR29 = [ordered]@{
        schema='GUIDEXOS_C188R25_ONE_BOOT_GUARD_V1';candidateRoot=$candidateR29
        candidateManifestSha256=$candidateManifestR29Hash;candidateIntegritySha256=$candidateIntegrityR29Hash
        bootEvidencePath=$bootRootR29;attemptNumber=1;maxGuestBoots=1
        automaticRetry=$false;retryAllowed=$false;consumed=$true;syntheticFixture=$true
    }
    $guardR29 | ConvertTo-Json | Set-Content -LiteralPath $guardPathR29 -Encoding utf8
    $null = New-Item -ItemType Directory -Path $bootRootR29
    [ordered]@{
        schema='GUIDEXOS_C188R25_FAILFAST_CAPTURE_CONFIG_V1'
        recordAddress='0x00000001001E14E0';recordBytes=192;recordSchema=3
        candidateManifestSha256=$candidateManifestR29Hash
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bootRootR29 'r25-failfast-capture.json') -Encoding utf8
    $authorizedR29Config = Invoke-NestedRehearsal $candidateR29 $bootRootR29 $guardPathR29
    Assert-R27 ($authorizedR29Config.exitCode -eq 0 -and $null -ne $authorizedR29Config.proof -and
        [bool]$authorizedR29Config.proof.precreatedEvidenceRoot -and
        -not [bool]$authorizedR29Config.proof.qacInvoked -and -not [bool]$authorizedR29Config.proof.qemuLaunched -and
        (Get-FileHash -LiteralPath (Join-Path $bootRootR29 'esp\kernel.elf') -Algorithm SHA256).Hash -eq $script:kernelHash) `
        'the nested runner accepts a candidate-bound R29 schema-3 evidence directory and completes the offline media handoff'

    Remove-Item -LiteralPath (Join-Path $bootRoot 'r25-failfast-capture.json') -Force
    $guard.bootEvidencePath = $emptyRoot
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $null = New-Item -ItemType Directory -Path $emptyRoot
    $authorizedEmpty = Invoke-NestedRehearsal $candidate $emptyRoot $guardPath
    Assert-R27 ($authorizedEmpty.exitCode -eq 0 -and $null -ne $authorizedEmpty.proof -and
        [bool]$authorizedEmpty.proof.precreatedEvidenceRoot) 'a parent-authorized empty evidence directory passes the actual nested runner'

    $null = New-Item -ItemType Directory -Path $absentRoot
    Remove-Item -LiteralPath $absentRoot -Force
    $runnerCreated = Invoke-NestedRehearsal $candidate $absentRoot
    Assert-R27 ($runnerCreated.exitCode -eq 0 -and $null -ne $runnerCreated.proof -and
        -not [bool]$runnerCreated.proof.precreatedEvidenceRoot -and (Test-Path -LiteralPath $absentRoot -PathType Container)) `
        'an absent directory is created by the nested runner and qualifies offline'

    $badRoot = Join-Path $fixtureRoot 'boot-extra-file'
    $null = New-Item -ItemType Directory -Path $badRoot
    $guard.bootEvidencePath = $badRoot
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    Set-Content -LiteralPath (Join-Path $badRoot 'foreign.txt') -Value 'do not overwrite' -Encoding utf8
    $unexpected = Invoke-NestedRehearsal $candidate $badRoot $guardPath
    Assert-R27 ($unexpected.exitCode -ne 0 -and $unexpected.output -match 'unexpected entries') 'unexpected nonempty evidence remains rejected'

    $mismatchRoot = Join-Path $fixtureRoot 'boot-manifest-mismatch'
    $null = New-Item -ItemType Directory -Path $mismatchRoot
    $guard.bootEvidencePath = $mismatchRoot
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $badConfig = [ordered]@{}
    foreach ($key in $config.Keys) { $badConfig[$key] = $config[$key] }
    $badConfig.candidateManifestSha256 = ('0' * 64)
    $badConfig | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $mismatchRoot 'r25-failfast-capture.json') -Encoding utf8
    $mismatch = Invoke-NestedRehearsal $candidate $mismatchRoot $guardPath
    Assert-R27 ($mismatch.exitCode -ne 0 -and $mismatch.output -match 'does not bind the R25 record') 'candidate and capture-config mismatch is rejected before QAC'

    $historicalRoot = Join-Path $fixtureRoot 'boot-historical-replay'
    $null = New-Item -ItemType Directory -Path $historicalRoot
    $guard.bootEvidencePath = $bootRoot
    $guard | ConvertTo-Json | Set-Content -LiteralPath $guardPath -Encoding utf8
    $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $historicalRoot 'r25-failfast-capture.json') -Encoding utf8
    $historical = Invoke-NestedRehearsal $candidate $historicalRoot $guardPath
    Assert-R27 ($historical.exitCode -ne 0 -and $historical.output -match 'does not authorize this candidate') 'historical guard replay cannot authorize a new directory'

    $unsafeRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('r27-prelaunch-unsafe-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $unsafeRoot
    try {
        $unsafe = Invoke-NestedRehearsal $candidate $unsafeRoot $guardPath
        Assert-R27 ($unsafe.exitCode -ne 0 -and $unsafe.output -match 'unique sibling runs') 'unsafe evidence paths are rejected before copy or QAC'
    } finally { Remove-Item -LiteralPath $unsafeRoot -Recurse -Force }

    Assert-R27 (-not (Test-C188R26EvidenceOwner 'DOMAIN\foreign' 'DOMAIN\parent')) 'foreign evidence-root owner is not accepted'
    Assert-R27 ($parentRunner.Contains("Join-Path `$repo 'scripts\dotnet\run-c188r12r1-settings-handoff.ps1'") -and
        $parentRunner.Contains("'-BootEvidenceAuthorizationPath',`$guardPath")) 'the production R25 parent invokes the corrected nested runner and passes its one-boot authority'
    Assert-R27 ($parentRunner -match 'This exact R24 candidate already has a one-boot guard; automatic retries are forbidden' -and
        $parentRunner -match '\[System\.IO\.Path\]::GetFullPath\(\[string\]\$existingGuard\.candidateRoot\) -eq \$candidate') `
        'a consumed candidate attempt remains blocked on repeat invocation'
    Assert-R27 ($parentRunner.IndexOf('if (-not $clear)') -lt $parentRunner.IndexOf('$null = New-Item -ItemType Directory -Path $guardRoot -Force')) `
        'the production parent does not consume a guard when QAC admission is not clear'
    Write-Output "R27_PRELAUNCH_REHEARSAL_ASSERTIONS=$script:checks RESULT=PASS"
}
finally {
    $resolved = [System.IO.Path]::GetFullPath($fixtureRoot)
    $expectedPrefix = $artifactRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar
    if ($resolved.StartsWith($expectedPrefix,[System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved) -like 'r27-prelaunch-fixture-*') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
