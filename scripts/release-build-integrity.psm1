Set-StrictMode -Version Latest

function Get-DeveloperStudioSignedOffIdentity {
    return [PSCustomObject]@{
        ManifestBytes = 1112
        ManifestSha256 = 'D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B'
        ElfBytes = 1015064
        ElfSha256 = '5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7'
        Provenance = 'DeveloperStudio Phase 25 release-candidate record; sign-off bb66e11c38dffae21514384f73b1920b53ac64fe; paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02'
    }
}

function Get-ReleaseProfileContract {
    param(
        [ValidateSet('Development', 'ServerV03PacManOnly')]
        [string]$Profile = 'Development'
    )

    switch ($Profile) {
        'Development' {
            return [PSCustomObject]@{
                Name = 'Development'
                PackageNames = @('PacMan', 'DeveloperStudio')
                RequiresDeveloperStudio = $true
                OutputDirectoryName = 'dist'
            }
        }
        'ServerV03PacManOnly' {
            return [PSCustomObject]@{
                Name = 'ServerV03PacManOnly'
                PackageNames = @('PacMan')
                RequiresDeveloperStudio = $false
                OutputDirectoryName = 'artifacts'
            }
        }
    }
}

function Assert-ReleaseCommandCompleted {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [AllowEmptyString()][string]$Output = ''
    )

    if ($ExitCode -ne 0) {
        throw "$Command failed with process exit code ${ExitCode}."
    }
    if ($Output -match '(?im)\bERROR\s*:') {
        $errorLines = @($Output -split "`r?`n" | Where-Object { $_ -match '(?im)\bERROR\s*:' })
        throw "$Command returned exit code 0 but reported a stage error: $($errorLines -join ' | ')"
    }
}

function Assert-DeveloperStudioMinimumServerVersion {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [string]$TargetServerVersion = '0.3.0'
    )

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "DeveloperStudio manifest is missing for runtime compatibility check: $ManifestPath"
    }

    try {
        $manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "DeveloperStudio manifest is invalid JSON: $ManifestPath. $($_.Exception.Message)"
    }
    if ($manifest.id -ne 'com.guidexos.developerstudio') {
        throw "Unexpected application ID in DeveloperStudio manifest: $($manifest.id)"
    }
    if ([string]::IsNullOrWhiteSpace([string]$manifest.minGuideXOSVersion)) {
        throw 'DeveloperStudio manifest is missing minGuideXOSVersion.'
    }

    try {
        $minimumVersion = [Version]::Parse([string]$manifest.minGuideXOSVersion)
    } catch {
        throw "DeveloperStudio manifest has an invalid minGuideXOSVersion: $($manifest.minGuideXOSVersion)"
    }
    try {
        $targetVersion = [Version]::Parse($TargetServerVersion)
    } catch {
        throw "Invalid target Server version for DeveloperStudio compatibility check: $TargetServerVersion"
    }

    if ($minimumVersion -gt $targetVersion) {
        throw "DeveloperStudio requires guideXOS Server $minimumVersion or later; release target $targetVersion is unsupported."
    }

    return [PSCustomObject]@{
        MinimumServerVersion = $minimumVersion.ToString()
        TargetServerVersion = $targetVersion.ToString()
    }
}

function Get-ReleaseNativeAppContract {
    param([Parameter(Mandatory = $true)][string]$PackageName)

    switch ($PackageName) {
        'PacMan' {
            return [PSCustomObject]@{
                Id = 'com.guidexos.pacman'
                Elf = 'bin/amd64/pacman.elf'
            }
        }
        'DeveloperStudio' {
            return [PSCustomObject]@{
                Id = 'com.guidexos.developerstudio'
                Elf = 'bin/amd64/developerstudio.elf'
            }
        }
        default { throw "Native app package is not in the Server v0.3 package selection: $PackageName" }
    }
}

function Assert-ReleaseNativeAppManifest {
    param(
        [Parameter(Mandatory = $true)][string]$ManifestPath,
        [Parameter(Mandatory = $true)][string]$PackageName,
        [ValidateSet('amd64')][string]$Architecture = 'amd64',
        [string]$TargetServerVersion = '0.3.0'
    )

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "Native app manifest is missing: $ManifestPath"
    }
    try {
        $manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "Native app manifest is invalid JSON: $ManifestPath. $($_.Exception.Message)"
    }

    $contract = Get-ReleaseNativeAppContract -PackageName $PackageName
    if ($manifest.id -ne $contract.Id -or $manifest.kind -ne 'NativeElf') {
        throw "Native app manifest does not match the selected $PackageName v0.3 package contract."
    }
    try {
        $minimumVersion = [Version]::Parse([string]$manifest.minGuideXOSVersion)
        $targetVersion = [Version]::Parse($TargetServerVersion)
    } catch {
        throw "Native app manifest has an invalid minimum Server version or target version: $ManifestPath"
    }
    if ($minimumVersion -gt $targetVersion) {
        throw "$PackageName requires guideXOS Server $minimumVersion or later; release target $targetVersion is unsupported."
    }

    $supportedArchitectures = @($manifest.supportedArchitectures | ForEach-Object { ([string]$_).ToLowerInvariant() })
    if ($supportedArchitectures.Count -ne 1 -or $supportedArchitectures[0] -ne $Architecture) {
        throw "$PackageName must declare only the supported release architecture '$Architecture'."
    }
    $entries = @($manifest.entries)
    if ($entries.Count -ne 1 -or $entries[0].architecture -ne $Architecture -or
        $entries[0].path -ne $contract.Elf -or $entries[0].entryPoint -ne 'gx_main' -or
        $entries[0].abi -ne 'guidexos-c-abi-v1' -or $entries[0].runtime -ne 'native-elf') {
        throw "$PackageName manifest does not declare exactly one compatible $Architecture NativeElf entry."
    }

    return [PSCustomObject]@{
        PackageName = $PackageName
        MinimumServerVersion = $minimumVersion.ToString()
        TargetServerVersion = $targetVersion.ToString()
        Architecture = $Architecture
        EntryPath = $contract.Elf
    }
}

function Assert-ReleaseNativeAppShipSet {
    param(
        [Parameter(Mandatory = $true)][string]$EspRoot,
        [string[]]$RequiredPackageNames = @('PacMan', 'DeveloperStudio'),
        [ValidateSet('amd64')][string]$Architecture = 'amd64',
        [string]$TargetServerVersion = '0.3.0'
    )

    if (-not (Test-Path -LiteralPath $EspRoot -PathType Container)) {
        throw "ESP root is missing: $EspRoot"
    }
    $appsRoot = Join-Path $EspRoot 'Apps'
    if (-not (Test-Path -LiteralPath $appsRoot -PathType Container)) {
        throw "ESP Native app root is missing: $appsRoot"
    }

    $packageDirectories = @(Get-ChildItem -LiteralPath $appsRoot -Directory -Force)
    $selectedNames = @($packageDirectories | ForEach-Object { $_.Name })
    foreach ($name in $selectedNames) {
        [void](Get-ReleaseNativeAppContract -PackageName $name)
    }
    foreach ($requiredName in $RequiredPackageNames) {
        if ($selectedNames -notcontains $requiredName) {
            throw "Required Native app package is missing from the ESP ship-set: $requiredName"
        }
    }
    $unselectedNames = @($selectedNames | Where-Object { $RequiredPackageNames -notcontains $_ })
    if ($unselectedNames.Count -gt 0) {
        throw "Native app package is present but excluded from the selected ship-set: $($unselectedNames -join ', ')"
    }
    foreach ($packageDirectory in $packageDirectories) {
        $contract = Get-ReleaseNativeAppContract -PackageName $packageDirectory.Name
        $manifestPath = Join-Path $packageDirectory.FullName 'app.json'
        [void](Assert-ReleaseNativeAppManifest -ManifestPath $manifestPath `
            -PackageName $packageDirectory.Name -Architecture $Architecture `
            -TargetServerVersion $TargetServerVersion)
        $elfPath = Join-Path $packageDirectory.FullName ($contract.Elf -replace '/', '\')
        if (-not (Test-Path -LiteralPath $elfPath -PathType Leaf)) {
            throw "Selected Native app package is missing its AMD64 ELF: $($packageDirectory.Name)"
        }
        $bytes = [IO.File]::ReadAllBytes($elfPath)
        if ($bytes.Length -lt 20 -or $bytes[0] -ne 0x7f -or $bytes[1] -ne 0x45 -or
            $bytes[2] -ne 0x4c -or $bytes[3] -ne 0x46 -or $bytes[4] -ne 2 -or
            $bytes[5] -ne 1 -or $bytes[18] -ne 0x3e -or $bytes[19] -ne 0) {
            throw "Selected Native app ELF is not a little-endian AMD64 ELF64 image: $($packageDirectory.Name)"
        }
    }

    return [PSCustomObject]@{ PackageNames = $selectedNames; Architecture = $Architecture }
}

function Read-ReleaseBuildIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Release build identity is missing: $Path"
    }

    $identity = @{}
    foreach ($line in [IO.File]::ReadAllLines((Resolve-Path -LiteralPath $Path).Path)) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { throw "Malformed release build identity line: $line" }
        $key = $line.Substring(0, $separator).Trim()
        $value = $line.Substring($separator + 1).Trim()
        if ($identity.ContainsKey($key)) { throw "Duplicate release build identity key: $key" }
        $identity[$key] = $value
    }
    return $identity
}

function Assert-ReleaseBuildIdentity {
    param(
        [Parameter(Mandatory = $true)][string]$IdentityPath,
        [Parameter(Mandatory = $true)][string]$EspRoot,
        [Parameter(Mandatory = $true)][string]$ExpectedSourceCommit,
        [string]$ExpectedBuildId,
        [ValidateSet('Development', 'ServerV03PacManOnly')]
        [string]$ExpectedReleaseProfile = 'Development',
        [ValidateSet('amd64')][string]$Architecture = 'amd64',
        [string]$TargetServerVersion = '0.3.0'
    )

    $identity = Read-ReleaseBuildIdentity -Path $IdentityPath
    foreach ($key in @('schemaVersion', 'buildId', 'buildStatus', 'sourceCommit', 'architecture', 'startedAtUtc')) {
        if (-not $identity.ContainsKey($key) -or [string]::IsNullOrWhiteSpace($identity[$key])) {
            throw "Release build identity is missing required field '$key'."
        }
    }
    if ($identity['schemaVersion'] -ne '2') { throw "Unsupported release build identity schema: $($identity['schemaVersion'])" }
    $profile = if ($identity.ContainsKey('releaseProfile')) { $identity['releaseProfile'] } else { 'Development' }
    if ($profile -ne $ExpectedReleaseProfile) {
        throw "Build identity release profile '$profile' does not match selected profile '$ExpectedReleaseProfile'."
    }
    $profileContract = Get-ReleaseProfileContract -Profile $ExpectedReleaseProfile
    if ($identity['buildStatus'] -ne 'success') { throw "Build identity status is '$($identity['buildStatus'])', not success." }
    if ($identity['sourceCommit'] -ne $ExpectedSourceCommit) { throw "Build identity source commit $($identity['sourceCommit']) does not match $ExpectedSourceCommit." }
    if ($identity['architecture'] -ne $Architecture) { throw "Build identity architecture $($identity['architecture']) does not match $Architecture." }
    if ($ExpectedBuildId -and $identity['buildId'] -ne $ExpectedBuildId) { throw "Build identity ID $($identity['buildId']) does not match this build invocation $ExpectedBuildId." }
    try { [void][Guid]::Parse($identity['buildId']) } catch { throw "Invalid build identity ID: $($identity['buildId'])" }
    try { [void][DateTime]::Parse($identity['startedAtUtc']).ToUniversalTime() } catch { throw "Invalid build identity timestamp: $($identity['startedAtUtc'])" }

    $requiredStages = @('bootloader', 'kernel', 'symbolAudit', 'ramdisk', 'pacman', 'appPackages')
    if ($profileContract.RequiresDeveloperStudio) { $requiredStages += 'developerStudio' }
    foreach ($stage in $requiredStages) {
        $key = "stage.$stage"
        if (-not $identity.ContainsKey($key) -or $identity[$key] -ne 'passed') {
            $result = if ($identity.ContainsKey($key)) { $identity[$key] } else { 'missing' }
            throw "Required production stage '$stage' is '$result'."
        }
    }

    $requiredArtifacts = @('bootloader', 'kernel', 'ramdisk', 'kernelSymbolAudit', 'pacman.manifest', 'pacman.elf')
    $expectedArtifactPaths = @{
        bootloader = 'EFI/BOOT/BOOTX64.EFI'
        kernel = 'kernel.elf'
        ramdisk = 'ramdisk.img'
        kernelSymbolAudit = 'kernel-symbol-audit.txt'
        'pacman.manifest' = 'Apps/PacMan/app.json'
        'pacman.elf' = 'Apps/PacMan/bin/amd64/pacman.elf'
    }
    if ($profileContract.RequiresDeveloperStudio) {
        $requiredArtifacts += @('developerStudio.manifest', 'developerStudio.elf')
        $expectedArtifactPaths['developerStudio.manifest'] = 'Apps/DeveloperStudio/app.json'
        $expectedArtifactPaths['developerStudio.elf'] = 'Apps/DeveloperStudio/bin/amd64/developerstudio.elf'
    } elseif (@($identity.Keys | Where-Object { $_ -like 'artifact.developerStudio.*' }).Count -gt 0) {
        throw 'PacMan-only build identity unexpectedly records DeveloperStudio artifacts.'
    }
    $resolvedEspRoot = (Resolve-Path -LiteralPath $EspRoot).Path
    foreach ($artifact in $requiredArtifacts) {
        $prefix = "artifact.$artifact"
        foreach ($suffix in @('path', 'bytes', 'sha256')) {
            $key = "$prefix.$suffix"
            if (-not $identity.ContainsKey($key) -or [string]::IsNullOrWhiteSpace($identity[$key])) {
                throw "Build identity is missing artifact field '$key'."
            }
        }
        $relativePath = $identity["$prefix.path"].Replace('/', '\')
        if ([IO.Path]::IsPathRooted($relativePath) -or $relativePath -match '(^|\\)\.\.(\\|$)') {
            throw "Build identity artifact path escapes ESP: $relativePath"
        }
        if (-not [string]::Equals($relativePath.Replace('\', '/'), $expectedArtifactPaths[$artifact], [StringComparison]::OrdinalIgnoreCase)) {
            throw "Build identity artifact '$artifact' has unexpected path '$relativePath'; expected '$($expectedArtifactPaths[$artifact])'."
        }
        $artifactPath = Join-Path $resolvedEspRoot $relativePath
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) { throw "Required staged artifact is missing: $relativePath" }
        $file = Get-Item -LiteralPath $artifactPath
        if ([int64]$file.Length -ne [int64]$identity["$prefix.bytes"]) { throw "Staged artifact size does not match build identity: $relativePath" }
        $hash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $identity["$prefix.sha256"].ToLowerInvariant()) { throw "Staged artifact SHA-256 does not match build identity: $relativePath" }
        $developerStudioPin = Get-DeveloperStudioSignedOffIdentity
        if ($artifact -eq 'developerStudio.manifest' -and
            ([int64]$file.Length -ne $developerStudioPin.ManifestBytes -or $hash -ne $developerStudioPin.ManifestSha256.ToLowerInvariant())) {
            throw 'DeveloperStudio manifest is not the exact Phase 25 signed-off package manifest.'
        }
        if ($artifact -eq 'developerStudio.elf' -and
            ([int64]$file.Length -ne $developerStudioPin.ElfBytes -or $hash -ne $developerStudioPin.ElfSha256.ToLowerInvariant())) {
            throw 'DeveloperStudio ELF is not the exact Phase 25 signed-off package binary.'
        }
    }
    if ($profileContract.RequiresDeveloperStudio) {
        $developerStudioManifest = Join-Path $resolvedEspRoot ($identity['artifact.developerStudio.manifest.path'].Replace('/', '\'))
        [void](Assert-DeveloperStudioMinimumServerVersion -ManifestPath $developerStudioManifest -TargetServerVersion $TargetServerVersion)
    } elseif (-not $identity.ContainsKey('stage.developerStudio') -or $identity['stage.developerStudio'] -ne 'not_selected') {
        throw "DeveloperStudio stage must be 'not_selected' in the PacMan-only profile."
    } else {
        [void](Assert-ReleaseNativeAppShipSet -EspRoot $resolvedEspRoot `
            -RequiredPackageNames $profileContract.PackageNames -Architecture $Architecture `
            -TargetServerVersion $TargetServerVersion)
    }
    return $identity
}

Export-ModuleMember -Function Get-DeveloperStudioSignedOffIdentity, Get-ReleaseProfileContract, Assert-DeveloperStudioMinimumServerVersion, Assert-ReleaseNativeAppManifest, Assert-ReleaseNativeAppShipSet, Assert-ReleaseCommandCompleted, Read-ReleaseBuildIdentity, Assert-ReleaseBuildIdentity
