[CmdletBinding()]
param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $runId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([Guid]::NewGuid().ToString('N').Substring(0, 8))
    $OutputDirectory = Join-Path $repoRoot "out/aarch64-runsafe4a-host-$runId"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out')).TrimEnd('\') + '\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing RUNSAFE4-A output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite RUNSAFE4-A output: $OutputDirectory"
}
$null = New-Item -ItemType Directory -Path $OutputDirectory

Write-Host '[1/2] Exercising production Build identity and immutable artifact ownership...' -ForegroundColor Yellow
$buildIdentityOutput = Join-Path $OutputDirectory 'build-identity'
& (Join-Path $PSScriptRoot 'run-compiler-build-service-host-test.ps1') -OutputDirectory $buildIdentityOutput
if ($LASTEXITCODE -ne 0) { throw 'RUNSAFE4-A Build identity host controls failed.' }

Write-Host '[2/2] Exercising production ELF validation, mapping, trampoline, and scheduled lifecycle...' -ForegroundColor Yellow
$el0Output = Join-Path $OutputDirectory 'el0-loader'
& (Join-Path $PSScriptRoot 'test-aarch64-el0-host.ps1') -OutputDirectory $el0Output
if ($LASTEXITCODE -ne 0) { throw 'RUNSAFE4-A EL0 loader host controls failed.' }

$buildService = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel/core/compiler/compiler_build_service.cpp'))
$driver = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel/core/compiler/compiler_driver.cpp'))
$elfTests = [IO.File]::ReadAllText((Join-Path $repoRoot 'tests/aarch64_el0_isolation_host_tests.cpp'))
foreach ($required in @('start_for_owner', 'current_artifact', 'sourceSha256', 'artifactSha256',
        's_ownedArtifact.bytes', 'el0IntegerProfileValid')) {
    if (!$buildService.Contains($required)) { throw "RUNSAFE4-A identity source contract is missing: $required" }
}
if (!$driver.Contains('compile_for_target_from_bytes') -or
    !$elfTests.Contains('test_studio_elf_validation_negatives') -or
    !$elfTests.Contains('test_compiler_elf_task_preparation')) {
    throw 'RUNSAFE4-A captured-source, ELF-negative, or lifecycle host controls are missing.'
}

Write-Host 'RUNSAFE4A_HOST_PASS (Build identity + ELF loader; execution remains a hosted simulation)' -ForegroundColor Green
Write-Host "RUNSAFE4-A controls: $OutputDirectory" -ForegroundColor Cyan
