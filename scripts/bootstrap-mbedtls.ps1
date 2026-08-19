param(
    [switch]$Install,
    [string]$Destination = (Join-Path $PSScriptRoot '..\third_party\mbedtls'),
    [string]$StagingRoot = (Join-Path $PSScriptRoot '..\out\mbedtls-bootstrap'),
    [string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Destination = [System.IO.Path]::GetFullPath($Destination)
$StagingRoot = [System.IO.Path]::GetFullPath($StagingRoot)
$lockPath = Join-Path $RepoRoot 'guidexos\mbedtls.lock'
$profilePath = Join-Path $RepoRoot 'guidexos\mbedtls_profile.json'
$patchPath = Join-Path $RepoRoot 'patches\mbedtls\0001-guidexos-phase8f.patch'

if (-not (Test-Path -LiteralPath $lockPath)) {
    throw "Missing dependency lock file: $lockPath"
}
if (-not (Test-Path -LiteralPath $profilePath)) {
    throw "Missing dependency profile manifest: $profilePath"
}
if (-not (Test-Path -LiteralPath $patchPath)) {
    throw "Missing tracked Mbed TLS patch series: $patchPath"
}

$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json

if ($Destination -eq [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'third_party\mbedtls')) -and
    (Test-Path -LiteralPath $Destination) -and -not $Install) {
    & (Join-Path $PSScriptRoot 'verify-mbedtls-profile.ps1') -DependencyRoot $Destination -RepoRoot $RepoRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Existing Mbed TLS tree failed profile verification; refusing to modify it."
    }
    Write-Output "Mbed TLS profile already verified; no reconstruction was needed."
    exit 0
}

if ($Install -and (Test-Path -LiteralPath $Destination)) {
    throw "Refusing to overwrite existing dependency tree: $Destination. Preserve it and choose a new -Destination or move it manually after review."
}

$gitCommand = Get-Command git -ErrorAction SilentlyContinue
if ($null -eq $gitCommand) {
    throw "git is required to reconstruct the pinned dependency tree."
}

if (-not (Test-Path -LiteralPath $StagingRoot)) {
    New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null
}
$stage = Join-Path $StagingRoot ('mbedtls-' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $stage 'mbedtls'
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Write-Output ('MBEDTLS_BOOTSTRAP_STAGE=' + $stage)
Write-Output ('MBEDTLS_BOOTSTRAP_MBEDTLS_COMMIT=' + $lock.mbedtls.commit)
Write-Output ('MBEDTLS_BOOTSTRAP_TF_PSA_COMMIT=' + $lock.tfPsaCrypto.commit)

& git clone --filter=blob:none --no-checkout --recurse-submodules --branch $lock.mbedtls.tag $lock.mbedtls.repository $source
if ($LASTEXITCODE -ne 0) { throw "Mbed TLS clone failed." }
& git -C $source checkout --detach $lock.mbedtls.commit
if ($LASTEXITCODE -ne 0) { throw "Mbed TLS checkout failed for $($lock.mbedtls.commit)." }
& git -C $source submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { throw "Mbed TLS submodule initialization failed." }

$actualMbedTlsCommit = (& git -C $source rev-parse HEAD).Trim()
$actualTfCommit = (& git -C (Join-Path $source 'tf-psa-crypto') rev-parse HEAD).Trim()
if ($actualMbedTlsCommit -ne $lock.mbedtls.commit) {
    throw "Mbed TLS checkout is not pinned: expected $($lock.mbedtls.commit), got $actualMbedTlsCommit."
}
if ($actualTfCommit -ne $lock.tfPsaCrypto.commit) {
    throw "TF-PSA-Crypto checkout is not pinned: expected $($lock.tfPsaCrypto.commit), got $actualTfCommit."
}

& git -C $source apply --check --recount $patchPath
if ($LASTEXITCODE -ne 0) { throw "Mbed TLS guideXOS patch validation failed." }
& git -C $source apply --recount $patchPath
if ($LASTEXITCODE -ne 0) { throw "Mbed TLS guideXOS patch application failed." }

$pythonCommand = Get-Command $PythonPath -ErrorAction SilentlyContinue
if ($null -eq $pythonCommand -and -not (Test-Path -LiteralPath $PythonPath)) {
    throw "Python 3 is required to regenerate Mbed TLS generated files. Pass -PythonPath with the interpreter path."
}

Push-Location $source
try {
    & $PythonPath 'scripts\generate_config_checks.py'
    if ($LASTEXITCODE -ne 0) { throw "Mbed TLS configuration-check generation failed." }
    & $PythonPath 'tf-psa-crypto\scripts\generate_config_checks.py'
    if ($LASTEXITCODE -ne 0) { throw "TF-PSA-Crypto configuration-check generation failed." }
} finally {
    Pop-Location
}

& (Join-Path $PSScriptRoot 'verify-mbedtls-profile.ps1') -DependencyRoot $source -RepoRoot $RepoRoot
if ($LASTEXITCODE -ne 0) {
    throw "Reconstructed Mbed TLS tree failed the guideXOS profile verification."
}

if ($Install) {
    $destinationParent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $destinationParent)) {
        New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    }
    Move-Item -LiteralPath $source -Destination $Destination
    & (Join-Path $PSScriptRoot 'verify-mbedtls-profile.ps1') -DependencyRoot $Destination -RepoRoot $RepoRoot
    if ($LASTEXITCODE -ne 0) { throw "Installed Mbed TLS tree failed post-install verification." }
    Write-Output ('MBEDTLS_BOOTSTRAP_INSTALL=PASS')
} else {
    Write-Output ('MBEDTLS_BOOTSTRAP_RECONSTRUCTION=PASS')
    Write-Output ('MBEDTLS_BOOTSTRAP_INSTALL=SKIPPED')
    Write-Output ('To install into a missing default tree: scripts\bootstrap-mbedtls.ps1 -Install -PythonPath <python.exe>')
}
exit 0
