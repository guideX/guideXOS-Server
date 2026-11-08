[CmdletBinding()]
param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $runId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([Guid]::NewGuid().ToString('N').Substring(0, 8))
    $OutputDirectory = Join-Path $repoRoot "out/compiler-host-regressions-$runId"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing host-regression output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) { throw "Refusing to overwrite host-regression output: $OutputDirectory" }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$gxx = (Get-Command g++ -ErrorAction Stop).Source

$common = @(
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_diagnostics.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_lexer.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_parser.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/compiler_target.cpp'),
    (Join-Path $repoRoot 'kernel/core/native_architecture.cpp'),
    (Join-Path $repoRoot 'kernel/core/compiler/elf_writer.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/compiler_backend.cpp'),
    (Join-Path $repoRoot 'kernel/arch/amd64/arch.cpp')
)
$arm64Binary = Join-Path $OutputDirectory 'compiler-arm64-host-test.exe'
$arm64Sources = @((Join-Path $repoRoot 'tests/compiler_arm64_host_test.cpp'),
    (Join-Path $repoRoot 'kernel/arch/arm64/compiler_backend.cpp')) + $common
& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    @arm64Sources -o $arm64Binary
if ($LASTEXITCODE -ne 0) { throw 'ARM64 compiler hosted regression build failed' }
& $arm64Binary
if ($LASTEXITCODE -ne 0) { throw 'ARM64 compiler hosted regression failed' }

$bootstrapBinary = Join-Path $OutputDirectory 'compiler-bootstrap-host-test.exe'
$bootstrapSources = @((Join-Path $repoRoot 'tests/compiler_bootstrap_host_test.cpp')) + $common
& $gxx -std=c++14 -Wall -Wextra -O2 -DGXOS_BARE_METAL `
    "-I$repoRoot" "-I$(Join-Path $repoRoot 'kernel')" `
    "-iquote$(Join-Path $repoRoot 'kernel/core/include')" `
    "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')" `
    @bootstrapSources -o $bootstrapBinary
if ($LASTEXITCODE -ne 0) { throw 'Compiler bootstrap hosted regression build failed' }
& $bootstrapBinary
if ($LASTEXITCODE -ne 0) { throw 'Compiler bootstrap hosted regression failed' }
