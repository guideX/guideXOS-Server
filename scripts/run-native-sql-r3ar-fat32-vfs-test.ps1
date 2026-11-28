[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$CandidateRamdisk,
    [Parameter(Mandatory = $true)][string]$SourceFixture,
    [Parameter(Mandatory = $true)][string]$TrustedManifest,
    [string]$SqlRoot = 'D:\dev\guideXOS_SqlDatabase',
    [string]$OutputPath = 'out\ndb1r4p11r3ar\native-sql-r3ar-fat32-vfs-test.exe'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $Compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $Compiler) { throw 'g++ was not found.' }

$SqlRoot = [IO.Path]::GetFullPath($SqlRoot)
$CandidateRamdisk = [IO.Path]::GetFullPath($CandidateRamdisk)
$SourceFixture = [IO.Path]::GetFullPath($SourceFixture)
$TrustedManifest = [IO.Path]::GetFullPath($TrustedManifest)
$Output = [IO.Path]::GetFullPath($OutputPath)
foreach ($path in @($CandidateRamdisk, $SourceFixture, $TrustedManifest)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required R3AR hosted VFS input is missing: $path"
    }
}
if (Test-Path -LiteralPath $Output) {
    throw "Refusing to overwrite existing R3AR VFS test executable: $Output"
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$Arguments = @(
    '-std=c++14', '-Wall', '-Wextra', '-O2', '-DGXOS_HOSTED_TEST',
    '-DGX_NATIVE_SQL_READ_QUALIFICATION=1', '-DGX_NATIVE_SQL_READ_DIAGNOSTIC=1',
    '-iquote', 'kernel/core/include', '-I', 'kernel/arch/amd64/include',
    '-I', (Join-Path $SqlRoot 'database'),
    '-I', (Join-Path $SqlRoot 'database\native'),
    '-I', (Join-Path $SqlRoot 'database\portable'),
    'tests/native_sql_r3ar_fat32_vfs_test.cpp',
    'kernel/core/block_device.cpp', 'kernel/core/fs_fat.cpp', 'kernel/core/fs_ext4.cpp',
    'kernel/core/native_database_file.cpp', 'kernel/core/native_sql_read_diagnostic.cpp',
    'kernel/core/ramdisk.cpp', 'kernel/core/vfs.cpp',
    (Join-Path $SqlRoot 'database\native\gx_native_sql_index_page_provider.cpp'),
    (Join-Path $SqlRoot 'database\native\gx_native_sql_read_service.cpp'),
    (Join-Path $SqlRoot 'database\database_checksum.cpp'),
    (Join-Path $SqlRoot 'database\database_format.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_page_codec.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_allocator.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_array.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_page_visit_set.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_boolean_key.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_integer_key.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_text_key.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_physical_key.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_leaf_entry.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_leaf_traversal.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_internal_node.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_tree_descent.cpp'),
    (Join-Path $SqlRoot 'database\portable\gx_index_lookup.cpp'),
    '-o', $Output
)

Write-Host ("Building focused R3AR production-VFS test: {0} {1}" -f $Compiler.Source, ($Arguments -join ' '))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "R3AR production-VFS test build failed with exit code $LASTEXITCODE." }

& $Output $CandidateRamdisk $SourceFixture $TrustedManifest
if ($LASTEXITCODE -ne 0) { throw "R3AR production-VFS test failed with exit code $LASTEXITCODE." }
Write-Host 'R3AR standards-conforming FAT32 production-VFS test PASS.'
