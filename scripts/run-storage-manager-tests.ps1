$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$compiler = Get-Command g++ -ErrorAction Stop
$outputDir = Join-Path $repoRoot 'out'
$testExe = Join-Path $outputDir 'storage_manager_test.exe'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

try {
    & $compiler.Source `
        -std=c++17 -O2 -Wall -Wextra `
        -DKERNEL_STORAGE_TEST `
        -iquote kernel/core -iquote kernel/core/include -iquote kernel/arch/amd64/include `
        tests/storage_manager_test.cpp `
        kernel/core/block_device.cpp `
        kernel/core/storage_manager.cpp `
        kernel/core/partition_table.cpp `
        kernel/core/disk_initialization.cpp `
        kernel/core/partition_operations.cpp `
        kernel/core/ramdisk.cpp `
        -o $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Remove-Item -LiteralPath $testExe -Force -ErrorAction SilentlyContinue
}
