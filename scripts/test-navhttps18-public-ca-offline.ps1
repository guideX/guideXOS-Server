param(
    [string]$Python = "python",
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "tmp\navhttps18-test-build")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$pythonCommand = Get-Command $Python -ErrorAction Stop
$cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
$buildDirFull = [System.IO.Path]::GetFullPath($BuildDir)
$fixtureDir = Join-Path ([System.IO.Path]::GetTempPath()) ("navhttps18-fixtures-" + [guid]::NewGuid().ToString('N'))
$manifestPath = Join-Path $root 'tests\fixtures\navhttps18\manifest.json'
$chainPath = Join-Path $root 'tests\fixtures\navhttps18\letsencrypt-2026-05-29-chain.pem'
$rootPath = Join-Path $root 'tests\fixtures\navhttps18\isrg-root-x1.pem'
$sourceBundlePath = Join-Path $root 'assets\certs\candidates\mozilla-cacert-2026-11-05.pem'
$accountingPath = Join-Path $root 'assets\certs\candidates\mozilla-cacert-2026-11-05.accounting.json'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-Equal([string]$Name, $Actual, $Expected) {
    if ($Actual -ne $Expected) {
        throw "$Name expected '$Expected', got '$Actual'."
    }
}

$fixtureManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-Equal 'pinned public chain fixture hash' (Get-Sha256 $chainPath) ([string]$fixtureManifest.chain_sha256)
Assert-Equal 'pinned ISRG Root X1 fixture hash' (Get-Sha256 $rootPath) ([string]$fixtureManifest.trust_anchor_pem_sha256)
Assert-Equal 'pinned Mozilla source bundle hash' (Get-Sha256 $sourceBundlePath) ([string]$fixtureManifest.trust_anchor_source_bundle_sha256)

$accounting = Get-Content -LiteralPath $accountingPath -Raw | ConvertFrom-Json
Assert-Equal 'public root accounting includes no roots' ([int]$accounting.included_count) 0
Assert-Equal 'public root accounting excludes every source root' ([int]$accounting.excluded_count) ([int]$accounting.input_certificate_count)
Assert-Equal 'Mozilla input root count matches source' ([int]$accounting.input_certificate_count) 118

$mbedtlsRoot = Join-Path $root 'third_party\mbedtls'
$generatedSources = @(
    (Join-Path $mbedtlsRoot 'library\error.c'),
    (Join-Path $mbedtlsRoot 'library\ssl_debug_helpers_generated.c')
)
if (@($generatedSources | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -gt 0) {
    $perl = Get-Command perl.exe -ErrorAction SilentlyContinue
    if ($null -eq $perl) {
        $perlCandidates = @(
            (Join-Path $env:ProgramFiles 'Git\usr\bin\perl.exe'),
            'C:\msys64\usr\bin\perl.exe'
        )
        $perlPath = $perlCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
        if ($null -eq $perlPath) {
            throw 'Mbed TLS generated sources are absent and Perl is required by the pinned Mbed TLS generator.'
        }
        $env:PATH = (Split-Path -Parent $perlPath) + ';' + $env:PATH
    }
    Push-Location $mbedtlsRoot
    try {
        & $pythonCommand.Source scripts\make_generated_files.py
        if ($LASTEXITCODE -ne 0) { throw 'Pinned Mbed TLS generated-source step failed.' }
    } finally {
        Pop-Location
    }
}

try {
    & $cmakeCommand.Source -S (Join-Path $root 'tests\navhttps18') -B $buildDirFull -G Ninja `
        "-DGUIDEXOS_REPO_ROOT=$root" "-DMBEDTLS_ROOT=$mbedtlsRoot"
    if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS18 Mbed TLS test configuration failed.' }
    & $cmakeCommand.Source --build $buildDirFull --target navhttps18_mbedtls_verifier --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS18 Mbed TLS verifier build failed.' }

    New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
    & $pythonCommand.Source (Join-Path $root 'tests\navhttps18\generate_test_certificates.py') `
        --output-dir $fixtureDir --public-chain $chainPath
    if ($LASTEXITCODE -ne 0) { throw 'NAVHTTPS18 offline negative fixture generation failed.' }

    $generated = $fixtureDir
    $exe = Join-Path $buildDirFull 'navhttps18_mbedtls_verifier.exe'
    $wrongRoot = Join-Path $root 'scripts\fixtures\navigator-validated-root-ca-bundle.pem'
    $verifierArgs = @(
        $chainPath,
        $rootPath,
        $wrongRoot,
        (Join-Path $generated 'tampered-public-chain.pem'),
        (Join-Path $generated 'noncritical-nc-chain.pem'),
        (Join-Path $generated 'noncritical-nc-root.pem'),
        (Join-Path $generated 'unknown-critical-chain.pem'),
        (Join-Path $generated 'verifier-root.pem'),
        (Join-Path $generated 'critical-nc-chain.pem'),
        (Join-Path $generated 'critical-nc-root.pem'),
        (Join-Path $generated 'client-auth-chain.pem'),
        (Join-Path $generated 'pathlen-zero-chain.pem'),
        (Join-Path $generated 'pathlen-zero-root.pem'),
        $sourceBundlePath,
        (Join-Path $generated 'server-auth-policy-chain.pem'),
        (Join-Path $generated 'verifier-root.pem')
    )
    $output = @(& $exe @verifierArgs 2>&1)
    $exitCode = $LASTEXITCODE
    $output | ForEach-Object { Write-Output $_ }
    if ($exitCode -ne 0) { throw "NAVHTTPS18 Mbed TLS verifier returned $exitCode." }
    $caseCount = @($output | Where-Object { $_ -match '^MBEDTLS_CASE name=' }).Count
    Assert-Equal 'Mbed TLS offline verifier case count' $caseCount 15
    if (-not ($output -contains 'NAVHTTPS18_MBEDTLS_RESULT: PASS')) {
        throw 'NAVHTTPS18 Mbed TLS verifier summary marker is missing.'
    }
    Write-Output 'NAVHTTPS18_PUBLIC_CA_OFFLINE_RESULT: PASS'
    Write-Output 'Assertions: pinned public ACME chain, strict source-bundle parsing, unknown/excluded roots, date/hostname checks, invalid intermediate, critical extension rejection, noncritical Name Constraints behavior, serverAuth EKU, path length, policy-tree limitation, and empty trust store.'
}
finally {
    if (Test-Path -LiteralPath $fixtureDir -PathType Container) {
        Remove-Item -LiteralPath $fixtureDir -Recurse -Force
    }
}
