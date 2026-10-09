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
        -I kernel/arch/amd64/include `
        -iquote kernel/core -iquote kernel/core/include -iquote kernel/arch/amd64/include `
        tests/storage_manager_test.cpp `
        kernel/core/usb_storage.cpp `
        kernel/core/block_device.cpp `
        kernel/core/partition_block_view.cpp `
        kernel/core/storage_manager.cpp `
        kernel/core/partition_table.cpp `
        kernel/core/gpt_repair.cpp `
        kernel/core/disk_initialization.cpp `
        kernel/core/partition_operations.cpp `
        kernel/core/fat32_formatter.cpp `
        kernel/core/fs_fat.cpp `
        kernel/core/fs_ext4.cpp `
        kernel/core/vfs.cpp `
        kernel/core/ramdisk.cpp `
        -o $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $testExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $widgetTestExe = Join-Path $outputDir 'widget_activation_test.exe'
    & $compiler.Source `
        -std=c++17 -O2 -Wall -Wextra `
        -iquote kernel/core/include `
        tests/widget_activation_test.cpp `
        -o $widgetTestExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $widgetTestExe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $python = Get-Command python -ErrorAction Stop
    & $python.Source (Join-Path $repoRoot 'tests/classify_dm20_usb_trace_test.py')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $python.Source (Join-Path $repoRoot 'tests/verify_dm29_gpt_test.py')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $python.Source (Join-Path $repoRoot 'tests/dm30_gpt_fixture_test.py')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Remove-Item -LiteralPath $testExe -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $outputDir 'widget_activation_test.exe') -Force -ErrorAction SilentlyContinue
}
