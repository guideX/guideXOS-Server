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
if ($LASTEXITCODE -ne 0 -or $target -notmatch '^x86_64-.*mingw') {
    throw "The hosted BSP APIC_BASE probe test requires AMD64 MinGW, found '$target'."
}

$fixupObject = Join-Path $outputPath 'bsp_apic_base_fixup_test.o'
& nasm -f win64 -DGXOS_APIC_BASE_HOSTED_TEST (Join-Path $repoRoot 'kernel/arch/amd64/bsp_apic_base_fixup.asm') -o $fixupObject
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE fixup assembly failed with exit code $LASTEXITCODE" }

$sources = @(
    'tests/amd64_bsp_apic_base_probe_test.cpp',
    'kernel/arch/amd64/bsp_apic_base_probe.cpp',
    'kernel/arch/amd64/bsp_xapic_admission.cpp',
    'kernel/arch/amd64/bsp_apic.cpp',
    'kernel/arch/amd64/per_cpu.cpp',
    'kernel/arch/amd64/boot_processor_inventory.cpp',
    'kernel/arch/amd64/cpu_capabilities.cpp'
) | ForEach-Object { Join-Path $repoRoot $_ }
$testExecutable = Join-Path $outputPath 'amd64_bsp_apic_base_probe_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra -Werror -pthread `
    -idirafter (Join-Path $repoRoot 'kernel/core/include') -I (Join-Path $repoRoot 'kernel/arch/amd64/include') `
    @sources $fixupObject -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE probe test compilation failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "APIC_BASE probe tests failed with exit code $LASTEXITCODE" }
