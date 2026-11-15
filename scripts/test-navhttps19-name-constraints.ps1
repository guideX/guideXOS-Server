param(
    [string]$Python = "python",
    [string]$BuildDir = (Join-Path ([System.IO.Path]::GetTempPath()) ("navhttps19-tests-" + [guid]::NewGuid().ToString('N')))
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildDirFull = [System.IO.Path]::GetFullPath($BuildDir)
$pythonCommand = Get-Command $Python -ErrorAction Stop
$cmakeCommand = Get-Command cmake.exe -ErrorAction Stop

if (Test-Path -LiteralPath $buildDirFull) {
    throw "Refusing to overwrite existing NAVHTTPS19 test artifacts: $buildDirFull"
}
New-Item -ItemType Directory -Path $buildDirFull -Force | Out-Null

$nav18Build = Join-Path $buildDirFull 'navhttps18'
& (Join-Path $root 'scripts\test-navhttps18-public-ca-offline.ps1') `
    -Python $pythonCommand.Source -BuildDir $nav18Build
if ($LASTEXITCODE -ne 0) {
    throw 'NAVHTTPS18 offline verifier regression failed.'
}

$fixtureDir = Join-Path $buildDirFull 'fixtures'
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
$nav18Chain = Join-Path $fixtureDir 'noncritical-nc-chain.pem'
$nav18Root = Join-Path $fixtureDir 'noncritical-nc-root.pem'
& $pythonCommand.Source (Join-Path $root 'tests\navhttps18\generate_test_certificates.py') `
    --output-dir $fixtureDir `
    --public-chain (Join-Path $root 'tests\fixtures\navhttps18\letsencrypt-2026-05-29-chain.pem')
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to generate the unchanged NAVHTTPS18 exploit fixture.'
}
if (-not (Test-Path -LiteralPath $nav18Chain -PathType Leaf) -or
    -not (Test-Path -LiteralPath $nav18Root -PathType Leaf)) {
    throw 'The NAVHTTPS18 noncritical Name Constraints exploit chain was not generated.'
}

& $pythonCommand.Source (Join-Path $root 'tests\navhttps19\generate_name_constraints_corpus.py') `
    --output-dir $fixtureDir
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to generate the NAVHTTPS19 certificate corpus.'
}
$manifestPath = Join-Path $fixtureDir 'cases.tsv'
Add-Content -LiteralPath $manifestPath `
    -Value 'navhttps18-original-noncritical|noncritical-nc-chain.pem|noncritical-nc-root.pem|www.outside.example|C' `
    -Encoding ascii

$nav19Build = Join-Path $buildDirFull 'navhttps19'
& $cmakeCommand.Source -S (Join-Path $root 'tests\navhttps19') -B $nav19Build -G Ninja `
    "-DGUIDEXOS_REPO_ROOT=$root" "-DMBEDTLS_ROOT=$(Join-Path $root 'third_party\mbedtls')"
if ($LASTEXITCODE -ne 0) {
    throw 'NAVHTTPS19 verifier configuration failed.'
}
& $cmakeCommand.Source --build $nav19Build --target navhttps19_name_constraints_verifier --parallel 6
if ($LASTEXITCODE -ne 0) {
    throw 'NAVHTTPS19 verifier build failed.'
}

$verifier = Join-Path $nav19Build 'navhttps19_name_constraints_verifier.exe'
$output = @(& $verifier $fixtureDir $manifestPath 2>&1)
$exitCode = $LASTEXITCODE
$output | ForEach-Object { Write-Output $_ }
if ($exitCode -ne 0) {
    throw "NAVHTTPS19 verifier returned $exitCode."
}
$manifestCaseCount = @(Get-Content -LiteralPath $manifestPath | Where-Object { $_.Trim().Length -gt 0 }).Count
$summary = $output | Where-Object { $_ -match '^NAVHTTPS19_MBEDTLS_SUMMARY ' } | Select-Object -Last 1
if ($null -eq $summary -or $summary -notmatch "total=$manifestCaseCount\b") {
    throw "NAVHTTPS19 verifier case count did not match the manifest ($manifestCaseCount)."
}
if (-not ($output -contains 'NAVHTTPS19_MBEDTLS_RESULT: PASS')) {
    throw 'NAVHTTPS19 verifier summary marker is missing.'
}
Write-Output 'NAVHTTPS19_NAME_CONSTRAINTS_RESULT: PASS'
Write-Output "NAVHTTPS19_ARTIFACT_DIR=$buildDirFull"
