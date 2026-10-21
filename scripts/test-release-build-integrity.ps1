Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ModulePath = Join-Path $PSScriptRoot 'release-build-integrity.psm1'
Import-Module -Name $ModulePath -Force
$TestRoot = Join-Path $env:TEMP ('gxos-release-build-integrity-' + [Guid]::NewGuid().ToString('N'))
$EspRoot = Join-Path $TestRoot 'ESP'
$IdentityPath = Join-Path $EspRoot 'build-identity.txt'
$Assertions = 0
$Failures = New-Object System.Collections.Generic.List[string]

function Assert-True([bool]$Condition, [string]$Description) {
    $script:Assertions++
    if (-not $Condition) { $script:Failures.Add($Description) }
}

function Assert-Throws([scriptblock]$Action, [string]$Description) {
    $threw = $false
    try { & $Action } catch { $threw = $true }
    Assert-True $threw $Description
}

function Write-TestIdentity([string]$Status = 'success', [bool]$CompleteStages = $true, [bool]$UseCurrentDeveloperStudio = $false, [bool]$UseHistoricalV021DeveloperStudio = $false) {
    $lines = @(
        'schemaVersion=2',
        'buildId=2e3a5996-7f24-458d-a5d8-0398b6934f8b',
        "buildStatus=$Status",
        'sourceCommit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
        'architecture=amd64',
        'startedAtUtc=2026-10-19T12:00:00.0000000Z'
    )
    foreach ($stage in @('bootloader', 'kernel', 'symbolAudit', 'ramdisk', 'pacman', 'developerStudio', 'appPackages')) {
        $stageValue = if ($CompleteStages) { 'passed' } else { 'pending' }
        $lines += "stage.$stage=$stageValue"
    }
    $artifacts = @(
        @{ Name = 'bootloader'; Path = 'EFI/BOOT/BOOTX64.EFI'; Bytes = [byte[]](1,2,3) },
        @{ Name = 'kernel'; Path = 'kernel.elf'; Bytes = [byte[]](4,5,6) },
        @{ Name = 'ramdisk'; Path = 'ramdisk.img'; Bytes = [byte[]](7,8,9) },
        @{ Name = 'kernelSymbolAudit'; Path = 'kernel-symbol-audit.txt'; Bytes = [byte[]](10,11,12) },
        @{ Name = 'pacman.manifest'; Path = 'Apps/PacMan/app.json'; Bytes = [byte[]](13,14,15) },
        @{ Name = 'pacman.elf'; Path = 'Apps/PacMan/bin/amd64/pacman.elf'; Bytes = [byte[]](16,17,18) },
        @{ Name = 'developerStudio.manifest'; Path = 'Apps/DeveloperStudio/app.json'; Bytes = [byte[]](19,20,21) },
        @{ Name = 'developerStudio.elf'; Path = 'Apps/DeveloperStudio/bin/amd64/developerstudio.elf'; Bytes = [byte[]](22,23,24) }
    )
    foreach ($artifact in $artifacts) {
        $artifactPath = Join-Path $EspRoot ($artifact.Path -replace '/', '\')
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $artifactPath) | Out-Null
        if ($artifact.Name -eq 'developerStudio.manifest') {
            if ($UseHistoricalV021DeveloperStudio) {
                $manifestSource = Join-Path $PSScriptRoot '..\docs\evidence\settings-s22r3-v03-package-20261019\historical-v021-DeveloperStudio-app.json'
            } elseif ($UseCurrentDeveloperStudio) {
                $manifestSource = Join-Path $PSScriptRoot '..\Apps\DeveloperStudio\app.json'
            } else {
                $manifestSource = Join-Path $PSScriptRoot '..\tests\fixtures\developerstudio-phase25-app.json'
            }
            Copy-Item -LiteralPath $manifestSource -Destination $artifactPath -Force
        } elseif ($artifact.Name -eq 'developerStudio.elf') {
            $elfSource = if ($UseCurrentDeveloperStudio) { Join-Path $PSScriptRoot '..\Apps\DeveloperStudio\bin\amd64\developerstudio.elf' } else { Join-Path $PSScriptRoot '..\ESP\Apps\DeveloperStudio\bin\amd64\developerstudio.elf' }
            Copy-Item -LiteralPath $elfSource -Destination $artifactPath -Force
        } else {
            [IO.File]::WriteAllBytes($artifactPath, $artifact.Bytes)
        }
        $artifactBytes = (Get-Item -LiteralPath $artifactPath).Length
        $lines += "artifact.$($artifact.Name).path=$($artifact.Path)"
        $lines += "artifact.$($artifact.Name).bytes=$artifactBytes"
        $lines += "artifact.$($artifact.Name).sha256=$((Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant())"
    }
    [IO.File]::WriteAllLines($IdentityPath, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

try {
    New-Item -ItemType Directory -Path $EspRoot -Force | Out-Null

    Assert-Throws { Assert-ReleaseCommandCompleted -Command 'required-stage' -ExitCode 7 -Output '' } 'rejects a required command returning nonzero'
    Assert-Throws { Assert-ReleaseCommandCompleted -Command 'required-stage' -ExitCode 0 -Output 'ERROR: stage failed' } 'rejects a child that prints an error but exits zero'

    $historicalV021Manifest = Join-Path $PSScriptRoot '..\docs\evidence\settings-s22r3-v03-package-20261019\historical-v021-DeveloperStudio-app.json'
    Assert-Throws { Assert-DeveloperStudioMinimumServerVersion -ManifestPath $historicalV021Manifest -TargetServerVersion '0.3.0' } 'rejects the exact v0.2.1 media manifest because it requires Server 0.5.0'
    $phase25Manifest = Join-Path $PSScriptRoot '..\tests\fixtures\developerstudio-phase25-app.json'
    Assert-Throws { Assert-DeveloperStudioMinimumServerVersion -ManifestPath $phase25Manifest -TargetServerVersion '0.3.0' } 'rejects the Phase 25 manifest because it requires Server 0.5.0'
    $pacmanManifest = Join-Path $PSScriptRoot '..\Apps\PacMan\app.json'
    $pacmanCompatibility = Assert-ReleaseNativeAppManifest -ManifestPath $pacmanManifest -PackageName 'PacMan' -Architecture 'amd64' -TargetServerVersion '0.3.0'
    Assert-True ($pacmanCompatibility.MinimumServerVersion -eq '0.1.0' -and $pacmanCompatibility.Architecture -eq 'amd64') 'accepts the qualified PacMan package contract for the v0.3 amd64 target'
    $modernDeveloperStudioManifest = Join-Path $PSScriptRoot '..\Apps\DeveloperStudio\app.json'
    Assert-Throws { Assert-ReleaseNativeAppManifest -ManifestPath $modernDeveloperStudioManifest -PackageName 'DeveloperStudio' -Architecture 'amd64' -TargetServerVersion '0.3.0' } 'rejects the unmodified current DeveloperStudio package because its declared minimum is Server 0.5.0'

    $unsupportedArchitectureManifest = Join-Path $TestRoot 'pacman-unsupported-architecture.json'
    $unsupportedArchitecture = Get-Content -LiteralPath $pacmanManifest -Raw | ConvertFrom-Json
    $unsupportedArchitecture.supportedArchitectures = @('arm64')
    $unsupportedArchitecture.entries = @([PSCustomObject]@{
        architecture = 'arm64'
        path = 'bin/arm64/pacman.elf'
        entryPoint = 'gx_main'
        abi = 'guidexos-c-abi-v1'
        runtime = 'native-elf'
    })
    [IO.File]::WriteAllText($unsupportedArchitectureManifest, ($unsupportedArchitecture | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))
    Assert-Throws { Assert-ReleaseNativeAppManifest -ManifestPath $unsupportedArchitectureManifest -PackageName 'PacMan' -Architecture 'amd64' -TargetServerVersion '0.3.0' } 'rejects a package that declares only an unsupported architecture'

    $packageSetRoot = Join-Path $TestRoot 'package-set-esp'
    $packageSetPacman = Join-Path $packageSetRoot 'Apps\PacMan'
    New-Item -ItemType Directory -Path (Join-Path $packageSetPacman 'bin\amd64') -Force | Out-Null
    Copy-Item -LiteralPath $pacmanManifest -Destination (Join-Path $packageSetPacman 'app.json')
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\Apps\PacMan\bin\amd64\pacman.elf') -Destination (Join-Path $packageSetPacman 'bin\amd64\pacman.elf')
    $selectedPackageSet = Assert-ReleaseNativeAppShipSet -EspRoot $packageSetRoot -RequiredPackageNames @('PacMan') -Architecture 'amd64' -TargetServerVersion '0.3.0'
    Assert-True ($selectedPackageSet.PackageNames.Count -eq 1 -and $selectedPackageSet.PackageNames[0] -eq 'PacMan') 'accepts the selected PacMan-only Native ELF ship-set'
    New-Item -ItemType Directory -Path (Join-Path $packageSetRoot 'Apps\UnlistedPackage') -Force | Out-Null
    Assert-Throws { Assert-ReleaseNativeAppShipSet -EspRoot $packageSetRoot -RequiredPackageNames @('PacMan') -Architecture 'amd64' -TargetServerVersion '0.3.0' } 'rejects an extra package directory copied into ESP\Apps outside the release selection'

    $compatibleManifest = Join-Path $TestRoot 'developerstudio-v03-compatible.json'
    [IO.File]::WriteAllText($compatibleManifest, '{"id":"com.guidexos.developerstudio","minGuideXOSVersion":"0.3.0"}')
    $compatibility = Assert-DeveloperStudioMinimumServerVersion -ManifestPath $compatibleManifest -TargetServerVersion '0.3.0'
    Assert-True ($compatibility.MinimumServerVersion -eq '0.3.0' -and $compatibility.TargetServerVersion -eq '0.3.0') 'accepts a DeveloperStudio manifest whose declared minimum matches the v0.3 target'

    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a missing identity receipt'

    Write-TestIdentity -Status 'in_progress' -CompleteStages $true
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a stale or incomplete build receipt'

    Write-TestIdentity -Status 'success' -CompleteStages $false
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a missing required stage result'

    Write-TestIdentity
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('b' * 40) } 'rejects receipt provenance for a different source revision'
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) -ExpectedBuildId 'f1d50e2a-8861-419b-8f9e-f22a1813d3c8' } 'rejects a receipt from an earlier build invocation'

    Write-TestIdentity -UseCurrentDeveloperStudio $true
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a newer unsigned DeveloperStudio package even when its receipt matches its bytes'

    Write-TestIdentity -UseHistoricalV021DeveloperStudio $true
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects the exact v0.2.1 media manifest because the current release selector has no compatible historical pin'

    Write-TestIdentity
    Remove-Item -LiteralPath (Join-Path $EspRoot 'Apps\DeveloperStudio\app.json')
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a missing signed-off DeveloperStudio manifest'

    Write-TestIdentity
    Remove-Item -LiteralPath (Join-Path $EspRoot 'Apps\DeveloperStudio\bin\amd64\developerstudio.elf')
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a missing signed-off DeveloperStudio ELF'

    $ramdiskPath = Join-Path $EspRoot 'ramdisk.img'
    Remove-Item -LiteralPath $ramdiskPath
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a missing required artifact even when an old receipt claims success'

    Write-TestIdentity
    $developerStudioPath = Join-Path $EspRoot 'Apps\DeveloperStudio\bin\amd64\developerstudio.elf'
    [IO.File]::AppendAllText($developerStudioPath, 'tampered')
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) } 'rejects a staged artifact whose hash changed after receipt creation'

    Write-TestIdentity
    Assert-Throws { Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) -ExpectedBuildId '2e3a5996-7f24-458d-a5d8-0398b6934f8b' } 'rejects the exact Phase 25 artifact pair for the Server v0.3 target despite a complete successful receipt'
    $validated = Assert-ReleaseBuildIdentity -IdentityPath $IdentityPath -EspRoot $EspRoot -ExpectedSourceCommit ('a' * 40) -ExpectedBuildId '2e3a5996-7f24-458d-a5d8-0398b6934f8b' -TargetServerVersion '0.5.0'
    Assert-True ($validated['buildStatus'] -eq 'success') 'accepts the exact Phase 25 identity for its declared Server 0.5.0 minimum'
} finally {
    if (Test-Path -LiteralPath $TestRoot -PathType Container) {
        Remove-Item -LiteralPath $TestRoot -Recurse -Force
    }
}

if ($Failures.Count -gt 0) {
    foreach ($failure in $Failures) { Write-Error "FAIL: $failure" }
    throw "Release build integrity tests failed: $($Failures.Count) of $Assertions assertions."
}
Write-Output "Release build integrity tests passed: $Assertions assertions."
