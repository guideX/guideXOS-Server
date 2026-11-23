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
    throw "The hosted BSP APIC test requires an x86_64 g++ target, found '$target'."
}
$sources = @(
    'tests/amd64_bsp_apic_test.cpp',
    'kernel/arch/amd64/bsp_apic.cpp',
    'kernel/arch/amd64/per_cpu.cpp',
    'kernel/arch/amd64/cpu_capabilities.cpp',
    'kernel/arch/amd64/boot_processor_inventory.cpp'
) | ForEach-Object { Join-Path $repoRoot $_ }
$testExecutable = Join-Path $outputPath 'amd64_bsp_apic_test.exe'

& $compiler.Source -std=c++14 -O2 -Wall -Wextra -Werror @sources -o $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 BSP APIC test compilation failed with exit code $LASTEXITCODE" }

& $testExecutable
if ($LASTEXITCODE -ne 0) { throw "AMD64 BSP APIC tests failed with exit code $LASTEXITCODE" }
