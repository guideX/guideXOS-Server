param(
    [string]$DependencyRoot = (Join-Path $PSScriptRoot '..\third_party\mbedtls'),
    [string]$RepoRoot = (Join-Path $PSScriptRoot '..'),
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$DependencyRoot = [System.IO.Path]::GetFullPath($DependencyRoot)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$profilePath = Join-Path $RepoRoot 'guidexos\mbedtls_profile.json'
$errors = [System.Collections.Generic.List[string]]::new()
$sha = [System.Security.Cryptography.SHA256]::Create()

function Add-ProfileError([string]$message) {
    $script:errors.Add($message)
}

function Get-NormalizedSha256([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes).Replace(
        ([char]13).ToString() + ([char]10).ToString(),
        ([char]10).ToString())
    return ([System.BitConverter]::ToString(
        $script:sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($text))
    )).Replace('-', '').ToLowerInvariant()
}

function Test-TextContains([string]$path, [string]$pattern, [string]$description) {
    if (-not (Test-Path -LiteralPath $path)) {
        Add-ProfileError ('missing ' + $description + ': ' + $path)
        return
    }
    if (-not ([System.Text.RegularExpressions.Regex]::IsMatch(
        [System.IO.File]::ReadAllText($path),
        $pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    ))) {
        Add-ProfileError("missing expected $description in $path")
    }
}

if (-not (Test-Path -LiteralPath $profilePath)) {
    throw "Missing tracked Mbed TLS profile manifest: $profilePath"
}
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json

if (-not (Test-Path -LiteralPath $DependencyRoot)) {
    throw "Mbed TLS dependency is missing: $DependencyRoot. Run scripts\bootstrap-mbedtls.ps1 -Install."
}

$versionHeader = Join-Path $DependencyRoot 'include\mbedtls\build_info.h'
$tfVersionHeader = Join-Path $DependencyRoot 'tf-psa-crypto\include\tf-psa-crypto\build_info.h'
Test-TextContains $versionHeader '#define\s+MBEDTLS_VERSION_MAJOR\s+4' 'Mbed TLS major version'
Test-TextContains $versionHeader '#define\s+MBEDTLS_VERSION_MINOR\s+1' 'Mbed TLS minor version'
Test-TextContains $versionHeader '#define\s+MBEDTLS_VERSION_PATCH\s+0' 'Mbed TLS patch version'
Test-TextContains $tfVersionHeader '#define\s+TF_PSA_CRYPTO_VERSION_MAJOR\s+1' 'TF-PSA-Crypto major version'
Test-TextContains $tfVersionHeader '#define\s+TF_PSA_CRYPTO_VERSION_MINOR\s+1' 'TF-PSA-Crypto minor version'
Test-TextContains $tfVersionHeader '#define\s+TF_PSA_CRYPTO_VERSION_PATCH\s+0' 'TF-PSA-Crypto patch version'

foreach ($property in $profile.sourceSha256.PSObject.Properties) {
    $path = Join-Path $DependencyRoot ($property.Name -replace '/', '\')
    if (-not (Test-Path -LiteralPath $path)) {
        Add-ProfileError("missing patched source: $path")
        continue
    }
    $actual = Get-NormalizedSha256 $path
    if ($actual -ne $property.Value.ToLowerInvariant()) {
        Add-ProfileError("patched source hash mismatch for $($property.Name): expected $($property.Value), got $actual")
    }
}

foreach ($property in $profile.overlaySha256.PSObject.Properties) {
    $path = Join-Path $RepoRoot ($property.Name -replace '/', '\')
    if (-not (Test-Path -LiteralPath $path)) {
        Add-ProfileError("missing tracked overlay: $path")
        continue
    }
    $actual = Get-NormalizedSha256 $path
    if ($actual -ne $property.Value.ToLowerInvariant()) {
        Add-ProfileError("tracked overlay hash mismatch for $($property.Name): expected $($property.Value), got $actual")
    }
}

$configPath = Join-Path $RepoRoot 'guidexos\crypto_config.h'
Test-TextContains $configPath '#define\s+PSA_WANT_ECC_SECP_R1_256\s+1' 'P-256 PSA profile'
Test-TextContains $configPath '#define\s+PSA_WANT_ECC_SECP_R1_384\s+1' 'P-384 PSA profile'
Test-TextContains $configPath '#define\s+MBEDTLS_ECP_NIST_OPTIM\s+1' 'optimized NIST reduction'
Test-TextContains $configPath '#define\s+MBEDTLS_PSA_P256M_DRIVER_ENABLED\s+1' 'P-256 accelerator'
Test-TextContains $configPath '#define\s+PSA_WANT_ALG_RSA_PKCS1V15_SIGN\s+1' 'RSA PKCS#1 v1.5'
Test-TextContains $configPath '#define\s+PSA_WANT_ALG_RSA_PSS\s+1' 'RSA-PSS'
if (Test-Path -LiteralPath $configPath) {
    if ([System.IO.File]::ReadAllText($configPath) -match 'PSA_WANT_ECC_FAMILY_X25519') {
        Add-ProfileError('X25519 must remain disabled in the current public profile')
    }
}

$patchPath = Join-Path $RepoRoot 'patches\mbedtls\0001-guidexos-phase8f.patch'
if (-not (Test-Path -LiteralPath $patchPath)) {
    Add-ProfileError("missing tracked patch series: $patchPath")
}

if (Test-Path -LiteralPath (Join-Path $DependencyRoot '.git')) {
    $actualMbedtlsCommit = (& git -C $DependencyRoot rev-parse HEAD).Trim()
    if ($actualMbedtlsCommit -ne $profile.mbedtls.commit) {
        Add-ProfileError("Mbed TLS commit mismatch: expected $($profile.mbedtls.commit), got $actualMbedtlsCommit")
    }
    $tfRoot = Join-Path $DependencyRoot 'tf-psa-crypto'
    if (Test-Path -LiteralPath (Join-Path $tfRoot '.git')) {
        $actualTfCommit = (& git -C $tfRoot rev-parse HEAD).Trim()
        if ($actualTfCommit -ne $profile.tfPsaCrypto.commit) {
            Add-ProfileError("TF-PSA-Crypto commit mismatch: expected $($profile.tfPsaCrypto.commit), got $actualTfCommit")
        }
    } else {
        Add-ProfileError("TF-PSA-Crypto submodule metadata is missing: $tfRoot")
    }
}

if ($errors.Count -ne 0) {
    if (-not $Quiet) {
        $errors | ForEach-Object { Write-Error $_ }
    }
    exit 1
}

if (-not $Quiet) {
    Write-Output ('MBEDTLS_PROFILE_VERIFY=PASS')
    Write-Output ('MBEDTLS_VERSION=' + $profile.mbedtls.version)
    Write-Output ('TF_PSA_CRYPTO_VERSION=' + $profile.tfPsaCrypto.version)
    Write-Output ('MBEDTLS_DEPENDENCY_ROOT=' + $DependencyRoot)
    Write-Output ('MBEDTLS_PATCH_COUNT=' + $profile.patchSeries.Count)
    Write-Output ('MBEDTLS_PROFILE_SHA_MODE=normalized-UTF8-LF')
}
exit 0
