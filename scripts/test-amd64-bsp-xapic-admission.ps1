param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$compiler = Get-Command g++ -ErrorAction Stop
$target = & $compiler.Source -dumpmachine
if ($LASTEXITCODE -ne 0 -or $target -notmatch '^x86_64-') {
    throw "The hosted xAPIC admission test requires an x86_64 g++ target, found '$target'."
}

$sources = @(
    'tests/amd64_bsp_xapic_admission_test.cpp',
    'kernel/arch/amd64/bsp_xapic_admission.cpp',
    'kernel/arch/amd64/bsp_apic.cpp',
    'kernel/arch/amd64/per_cpu.cpp',
    'kernel/arch/amd64/boot_processor_inventory.cpp'
) | ForEach-Object { Join-Path $repoRoot $_ }
$includePath = Join-Path $repoRoot 'kernel/core/include'
$archIncludePath = Join-Path $repoRoot 'kernel/arch/amd64/include'
$testExecutable = Join-Path $outputPath 'amd64_bsp_xapic_admission_test.exe'

& $compiler.Source -std=c++14 -O2 -Wall -Wextra -Werror -idirafter $includePath -I $archIncludePath `
    -DGXOS_ENABLE_BSP_XAPIC_ADMISSION_DIAGNOSTIC @sources -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 xAPIC admission test compilation failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 xAPIC admission tests failed with exit code $LASTEXITCODE" }
