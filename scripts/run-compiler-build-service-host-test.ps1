[CmdletBinding()]
param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $runId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([Guid]::NewGuid().ToString('N').Substring(0, 8))
    $OutputDirectory = Join-Path $repoRoot "out/compiler-build-service-host-$runId"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing host-test output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) { throw "Refusing to overwrite host-test output: $OutputDirectory" }
$null = New-Item -ItemType Directory -Path $OutputDirectory

$gxx = (Get-Command g++ -ErrorAction Stop).Source
$testBinary = Join-Path $OutputDirectory 'compiler-build-service-host-test.exe'
$sources = @(
    (Join-Path $repoRoot 'tests/compiler_build_service_host_test.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_build_service.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_driver.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_diagnostics.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_lexer.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_parser.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_target.cpp'),
    (Join-Path $repoRoot 'kernel/core/native_architecture.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/elf_writer.cpp'),
    (Join-Path $repoRoot 'kernel/arch/arm64/compiler_backend.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/compiler_backend.cpp')
)
& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$(Join-Path $repoRoot 'kernel')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'sdk/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    "-include$(Join-Path $repoRoot 'tests/compiler_build_service_host_serial.h')" `
    @sources -o $testBinary
if ($LASTEXITCODE -ne 0) { throw 'Compiler build-service host test build failed' }

Push-Location $OutputDirectory
try {
    $testOutput = & $testBinary 2>&1
    $testOutput | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Compiler build-service host test failed' }
    $digestLine = [string]($testOutput | Where-Object { $_ -match '^compiler_build_service_host_test: artifact_sha256=([0-9a-f]{64})$' } | Select-Object -Last 1)
    if (!$digestLine) { throw 'Host test did not report the build artifact SHA-256.' }
    $reportedDigest = [regex]::Match($digestLine, '([0-9a-f]{64})$').Groups[1].Value
    $actualDigest = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'compiled-arm64.elf') -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($reportedDigest -ne $actualDigest) { throw "Build service SHA-256 mismatch: reported=$reportedDigest actual=$actualDigest" }
    Write-Host "Compiler build-service artifact SHA-256: $actualDigest" -ForegroundColor Green
}
finally { Pop-Location }
