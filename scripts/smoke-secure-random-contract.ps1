param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $Root

$outputDir = Join-Path $env:TEMP "guidex-navigator-secure-random-contract"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$exe = Join-Path $outputDir "secure_random_contract_test.exe"

$build = & g++ -std=c++14 -O2 -Wall -Wextra -I (Join-Path $Root "kernel\core\include") `
    (Join-Path $Root "tests\secure_random_contract_test.cpp") -o $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    $build | Write-Output
    throw "Secure-random contract test build failed."
}

& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Secure-random contract tests failed."
}

$provider = Get-Content -LiteralPath (Join-Path $Root "kernel\core\secure_random.cpp") -Raw
foreach ($forbidden in @('rdtsc', 'rdtscp', 'timestamp', 'deterministic', 'MAC address', 'uninitialized')) {
    if ($provider -match $forbidden) {
        throw "Secure-random provider contains a forbidden weak-entropy fallback token: $forbidden"
    }
}
foreach ($required in @(
        'kHardwareRetryLimit',
        'zero_random_bytes',
        'STATUS_CPU_RNG_EXHAUSTED',
        'provider_priority',
        's_rdseedRetryFailures',
        's_rdrandRetryFailures',
        's_providerFallbacks',
        'SecureRandomDiagnostics')) {
    if ($provider -notmatch $required) {
        throw "Secure-random provider contract is missing required fail-closed behavior: $required"
    }
}

$foundation = Get-Content -LiteralPath (Join-Path $Root "gxos_tls_foundation.cpp") -Raw
foreach ($required in @('mbedtls_psa_external_get_random', 'PSA_ERROR_INSUFFICIENT_ENTROPY', 'PSA_ERROR_HARDWARE_FAILURE')) {
    if ($foundation -notmatch $required) {
        throw "PSA external RNG failure mapping is missing: $required"
    }
}

foreach ($required in @(
        'GxosPsaRngDiagnostics',
        'GxosPsaRngLastResult',
        'gxos_psa_rng_record',
        'PSA_ERROR_INVALID_ARGUMENT',
        '*output_length = 0',
        'external RNG request success; secure source read confirmed')) {
    if ($foundation -notmatch [regex]::Escape($required)) {
        throw "PSA RNG diagnostics/propagation contract is missing: $required"
    }
}

if ($foundation -notmatch '(?s)if \(!output_length\).*?gxos_psa_rng_record\(gxos::GxosPsaRngLastResult::InvalidArgument\).*?return PSA_ERROR_INVALID_ARGUMENT') {
    throw "PSA invalid-argument propagation is not recorded before returning."
}
if ($foundation -notmatch '(?s)if \(gxos::gxos_random_quality\(\) != gxos::GxosRandomQuality::Secure\).*?GxosPsaRngLastResult::InsufficientEntropy.*?return PSA_ERROR_INSUFFICIENT_ENTROPY') {
    throw "PSA insufficient-entropy propagation is not fail-closed."
}
if ($foundation -notmatch '(?s)if \(!gxos::gxos_random_bytes\(output, output_size\)\).*?GxosPsaRngLastResult::HardwareFailure.*?return PSA_ERROR_HARDWARE_FAILURE') {
    throw "PSA hardware-failure propagation is not recorded."
}

Write-Output "Secure-random contract smoke PASS"
