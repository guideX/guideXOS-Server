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
    if ($identity['buildStatus'] -ne 'success') { throw "Build identity status is '$($identity['buildStatus'])', not success." }
    if ($identity['sourceCommit'] -ne $ExpectedSourceCommit) { throw "Build identity source commit $($identity['sourceCommit']) does not match $ExpectedSourceCommit." }
    if ($identity['architecture'] -ne $Architecture) { throw "Build identity architecture $($identity['architecture']) does not match $Architecture." }
    if ($ExpectedBuildId -and $identity['buildId'] -ne $ExpectedBuildId) { throw "Build identity ID $($identity['buildId']) does not match this build invocation $ExpectedBuildId." }
    try { [void][Guid]::Parse($identity['buildId']) } catch { throw "Invalid build identity ID: $($identity['buildId'])" }
    try { [void][DateTime]::Parse($identity['startedAtUtc']).ToUniversalTime() } catch { throw "Invalid build identity timestamp: $($identity['startedAtUtc'])" }

    foreach ($stage in @('bootloader', 'kernel', 'symbolAudit', 'ramdisk', 'pacman', 'developerStudio', 'appPackages')) {
        $key = "stage.$stage"
        if (-not $identity.ContainsKey($key) -or $identity[$key] -ne 'passed') {
            $result = if ($identity.ContainsKey($key)) { $identity[$key] } else { 'missing' }
            throw "Required production stage '$stage' is '$result'."
        }
    }

    $requiredArtifacts = @(
        'bootloader', 'kernel', 'ramdisk', 'kernelSymbolAudit',
        'pacman.manifest', 'pacman.elf', 'developerStudio.manifest', 'developerStudio.elf'
    )
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
    $developerStudioManifest = Join-Path $resolvedEspRoot ($identity['artifact.developerStudio.manifest.path'].Replace('/', '\'))
    [void](Assert-DeveloperStudioMinimumServerVersion -ManifestPath $developerStudioManifest -TargetServerVersion $TargetServerVersion)
    return $identity
}

Export-ModuleMember -Function Get-DeveloperStudioSignedOffIdentity, Assert-DeveloperStudioMinimumServerVersion, Assert-ReleaseCommandCompleted, Read-ReleaseBuildIdentity, Assert-ReleaseBuildIdentity
