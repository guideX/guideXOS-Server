param(
    [string]$Python = "python",
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "tmp\navhttps23-mbedtls-offline")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$pythonCommand = Get-Command $Python -ErrorAction Stop
$cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
$gitCommand = Get-Command git.exe -ErrorAction Stop
$fixtureDir = Join-Path $root 'tests\fixtures\navhttps23'
$fixtureManifestPath = Join-Path $fixtureDir 'manifest.json'
$buildDirFull = [System.IO.Path]::GetFullPath($BuildDir)

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-Equal([string]$Name, $Actual, $Expected) {
    if ($Actual -ne $Expected) {
        throw "$Name expected '$Expected', got '$Actual'."
    }
}

$fixtureManifest = Get-Content -LiteralPath $fixtureManifestPath -Raw | ConvertFrom-Json
Assert-Equal 'fixture manifest schema' ([string]$fixtureManifest.schema_version) 'guidexos.navhttps23.digicert-chain-fixtures.v1'
Assert-Equal 'fixture root identity' ([string]$fixtureManifest.source_record.root_sha256) '018e13f0772532cf809bd1b17281867283fc48c6e13be9c69812854a490c1b05'
foreach ($entry in $fixtureManifest.fixtures.PSObject.Properties) {
    $path = Join-Path $fixtureDir $entry.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Pinned NAVHTTPS23 certificate fixture is missing: $($entry.Name)"
    }
    Assert-Equal "fixture hash $($entry.Name)" (Get-Sha256 $path) ([string]$entry.Value.sha256)
}

$mbedtlsRoot = Join-Path $root 'third_party\mbedtls'
$mbedtlsRevision = (& $gitCommand.Source -C $mbedtlsRoot rev-parse HEAD).Trim()
Assert-Equal 'pinned Mbed TLS source revision' $mbedtlsRevision '0fe989b6b514192783c469039edd325fd0989806'
$generatedSources = @(
    (Join-Path $mbedtlsRoot 'library\error.c'),
    (Join-Path $mbedtlsRoot 'library\ssl_debug_helpers_generated.c')
)
if (@($generatedSources | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -gt 0) {
    $perlCandidates = @(
        (Join-Path $env:ProgramFiles 'Git\usr\bin\perl.exe'),
        'C:\msys64\usr\bin\perl.exe'
    )
    $perlPath = $perlCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $perlPath) { throw 'Mbed TLS generated sources are absent and Perl is required.' }
    $env:PATH = (Split-Path -Parent $perlPath) + ';' + $env:PATH
    Push-Location $mbedtlsRoot
    try {
        & $pythonCommand.Source scripts\make_generated_files.py
        if ($LASTEXITCODE -ne 0) { throw 'Pinned Mbed TLS generated-source step failed.' }
    } finally {
        Pop-Location
    }
}

$timeJson = & $pythonCommand.Source -c "import json; from datetime import datetime,timezone,timedelta; from pathlib import Path; from cryptography import x509; c=x509.load_pem_x509_certificate(Path(r'$fixtureDir\valid_leaf.pem').read_bytes()); valid=int(datetime(2026,11,19,12,0,0,tzinfo=timezone.utc).timestamp()); future=int((c.not_valid_before_utc-timedelta(seconds=1)).timestamp()); print(json.dumps({'valid':valid,'future':future}))"
if ($LASTEXITCODE -ne 0) { throw 'Could not derive fixed offline verification times.' }
$times = ($timeJson -join "`n") | ConvertFrom-Json
$exe = Join-Path $buildDirFull 'navhttps23_mbedtls_verifier.exe'
$wrongRoot = Join-Path $root 'tests\fixtures\navhttps18\isrg-root-x1.pem'
$verifierArgs = @(
    (Join-Path $fixtureDir 'valid_g5_chain.pem'),
    (Join-Path $fixtureDir 'expired_g5_chain.pem'),
    (Join-Path $fixtureDir 'alternate_global_g3_chain.pem'),
    (Join-Path $fixtureDir 'alternate_candidate_only_chain.pem'),
    (Join-Path $fixtureDir 'g5_root.pem'),
    (Join-Path $fixtureDir 'global_root_g3.pem'),
    $wrongRoot,
    (Join-Path $fixtureDir 'tampered_leaf_chain.pem'),
    (Join-Path $fixtureDir 'tampered_issuing_ca_chain.pem'),
    [string]$times.valid,
    [string]$times.future
)

& $cmakeCommand.Source -S (Join-Path $root 'tests\navhttps23') -B $buildDirFull -G Ninja `
    "-DGUIDEXOS_REPO_ROOT=$root" "-DMBEDTLS_ROOT=$mbedtlsRoot"
if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS23 Mbed TLS verifier configuration failed.' }
& $cmakeCommand.Source --build $buildDirFull --target navhttps23_mbedtls_verifier --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS23 Mbed TLS verifier build failed.' }
$output = @(& $exe @verifierArgs 2>&1)
$exitCode = $LASTEXITCODE
$output | ForEach-Object { Write-Output $_ }
if ($exitCode -ne 0) { throw "NAVHTTPS23 Mbed TLS verifier returned $exitCode." }
$caseLines = @($output | Where-Object { $_ -match '^MBEDTLS_CASE name=' })
Assert-Equal 'DigiCert offline verifier case count' $caseLines.Count 11
if (-not ($output -contains 'NAVHTTPS23_MBEDTLS_RESULT: PASS')) {
    throw 'NAVHTTPS23 Mbed TLS verifier summary marker is missing.'
}

$cases = foreach ($line in $caseLines) {
    if ($line -match '^MBEDTLS_CASE name=([^ ]+) result=([^ ]+)') {
        [ordered]@{ name = $Matches[1]; result = $Matches[2]; verifier_output = $line }
    }
}
$report = [ordered]@{
    schema_version = 'guidexos.navhttps23.mbedtls-qualification.v1'
    assessment_date_utc = [string]$fixtureManifest.capture_date_utc
    verifier = 'Mbed TLS 4.1.0; active guideXOS config; mbedtls_x509_crt_verify_with_profile'
    mbedtls_source_revision = $mbedtlsRevision
    test_input_sha256 = [ordered]@{
        'mbedtls-x509-verifier-source' = Get-Sha256 (Join-Path $mbedtlsRoot 'library\x509_crt.c')
        'guidexos-mbedtls-config' = Get-Sha256 (Join-Path $root 'guidexos\mbedtls_config.h')
        'guidexos-crypto-config' = Get-Sha256 (Join-Path $root 'guidexos\crypto_config.h')
        'navhttps23-verifier-harness' = Get-Sha256 (Join-Path $root 'tests\navhttps23\navhttps23_mbedtls_verifier.c')
        'navhttps23-cmake' = Get-Sha256 (Join-Path $root 'tests\navhttps23\CMakeLists.txt')
        'navhttps23-offline-runner' = Get-Sha256 $PSCommandPath
        'navhttps23-fixture-preparer' = Get-Sha256 (Join-Path $root 'tests\navhttps23\prepare_fixtures.py')
    }
    candidate_root_sha256 = [string]$fixtureManifest.source_record.root_sha256
    trust_bundle_scope = 'single selected root for candidate-anchor cases'
    fixture_manifest_sha256 = Get-Sha256 $fixtureManifestPath
    valid_verification_time_unix = [int64]$times.valid
    future_verification_time_unix = [int64]$times.future
    expected_case_count = 11
    passed_case_count = $cases.Count
    cases = @($cases)
    adjacent_regression_coverage = [ordered]@{
        name_constraints = 'NAVHTTPS19 Mbed TLS corpus; 40 cases'
        unsupported_constrained_name_forms = 'NAVHTTPS19 Mbed TLS corpus; unsupported applicable forms fail closed'
        client_only_wrong_purpose = 'NAVHTTPS18 Mbed TLS verifier; serverAuth EKU negative case'
        manifest_activation_and_time_scope = 'NAVHTTPS21 trust-profile and HTTPS qualification contract suites'
        plaintext_downgrade = 'HTTPS qualification contract and hosted local-fixture smoke'
    }
    limitations = @(
        'The captured public demo leaf has both serverAuth and clientAuth EKUs; it cannot serve as a client-only negative fixture.',
        'The real G5 demo chain has no applicable Name Constraints extension, so constraint violations are covered by the shared NAVHTTPS19 corpus.',
        'The demo leaf includes the EV policy OID, but this candidate qualification is ordinary TLS only and asserts no EV treatment.'
    )
    result = 'offline-qualified-for-scoped-ordinary-tls-path-tests'
}
$reportPath = Join-Path $fixtureDir 'mbedtls-qualification.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output "NAVHTTPS23_PUBLIC_CA_OFFLINE_RESULT: PASS"
Write-Output "Evidence manifest: $reportPath"
