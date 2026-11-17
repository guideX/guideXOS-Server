param(
    [string]$Python = "python",
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "tmp\navhttps21-distrust-after-20261117")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$pythonCommand = Get-Command $Python -ErrorAction Stop
$cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
$buildDirFull = [System.IO.Path]::GetFullPath($BuildDir)
$ownerMarker = Join-Path $buildDirFull '.navhttps21-owned'
if (Test-Path -LiteralPath $buildDirFull) {
    if (-not (Test-Path -LiteralPath $ownerMarker -PathType Leaf)) {
        throw "Refusing to overwrite existing NAVHTTPS21 qualification artifacts: $buildDirFull"
    }
} else {
    New-Item -ItemType Directory -Path $buildDirFull -Force | Out-Null
    New-Item -ItemType File -Path $ownerMarker | Out-Null
}

$manifestPath = Join-Path $root 'tests\fixtures\navhttps18\manifest.json'
$fixture = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$chainPath = Join-Path $root 'tests\fixtures\navhttps18\letsencrypt-2026-05-29-chain.pem'
$rootPath = Join-Path $root 'tests\fixtures\navhttps18\isrg-root-x1.pem'
if ((Get-FileHash -LiteralPath $chainPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne [string]$fixture.chain_sha256 -or
    (Get-FileHash -LiteralPath $rootPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne [string]$fixture.trust_anchor_pem_sha256) {
    throw 'Pinned NAVHTTPS18 chain or test-only trust-anchor hash mismatch.'
}

$fixtureDir = Join-Path $buildDirFull 'fixtures'
New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
& $pythonCommand.Source (Join-Path $root 'tests\navhttps18\generate_test_certificates.py') `
    --output-dir $fixtureDir --public-chain $chainPath
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the pinned-chain tamper regression.' }
& $pythonCommand.Source (Join-Path $root 'tests\navhttps21\generate_alternate_anchor_fixtures.py') `
    --output-dir $fixtureDir
if ($LASTEXITCODE -ne 0) { throw 'Could not generate the alternate-anchor path corpus.' }
$alternate = Get-Content -LiteralPath (Join-Path $fixtureDir 'alternate-anchor-manifest.json') -Raw | ConvertFrom-Json
if (-not $alternate.fixture_only -or -not $alternate.intermediate_spki_equal -or
    $alternate.cross_signed_intermediate_der_equal -or
    $alternate.restricted_anchor_a_sha256 -eq $alternate.alternate_anchor_b_sha256) {
    throw 'Alternate-anchor fixtures do not preserve one issuer key under two distinct DER paths and anchors.'
}

$mbedtlsRoot = Join-Path $root 'third_party\mbedtls'
$buildDir = Join-Path $buildDirFull 'build'
& $cmakeCommand.Source -S (Join-Path $root 'tests\navhttps21') -B $buildDir -G Ninja `
    "-DGUIDEXOS_REPO_ROOT=$root" "-DMBEDTLS_ROOT=$mbedtlsRoot"
if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS21 Mbed TLS verifier configuration failed.' }
& $cmakeCommand.Source --build $buildDir --target navhttps21_distrust_after_verifier --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS21 Mbed TLS verifier build failed.' }

$exe = Join-Path $buildDir 'navhttps21_distrust_after_verifier.exe'
$expectedRoot = [string]$fixture.trust_anchor_der_sha256
$output = @(& $exe $chainPath $rootPath (Join-Path $fixtureDir 'tampered-public-chain.pem') $expectedRoot `
    (Join-Path $fixtureDir 'alternate-chain-a.pem') (Join-Path $fixtureDir 'alternate-root-a.pem') `
    (Join-Path $fixtureDir 'alternate-chain-b.pem') (Join-Path $fixtureDir 'alternate-root-b.pem') 2>&1)
$exitCode = $LASTEXITCODE
$output | ForEach-Object { Write-Output $_ }
if ($exitCode -ne 0) { throw "NAVHTTPS21 verifier returned $exitCode." }
$caseCount = @($output | Where-Object { $_ -match '^NAVHTTPS21_CASE name=' }).Count
if ($caseCount -ne 12 -or -not ($output -contains 'NAVHTTPS21_DISTRUST_AFTER_RESULT: PASS cases=12 failures=0')) {
    throw "NAVHTTPS21 distrust-after corpus did not pass all 12 cases (observed $caseCount)."
}
Write-Output "NAVHTTPS21_DISTRUST_AFTER_RESULT: PASS cases=$caseCount"
Write-Output "NAVHTTPS21_ARTIFACT_DIR=$buildDirFull"
