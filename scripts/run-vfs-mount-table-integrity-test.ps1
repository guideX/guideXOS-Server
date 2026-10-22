[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path ([IO.Path]::GetTempPath()) 'guidexos_vfs_mount_table_integrity_test.exe')
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root

$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $Compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $Compiler) { throw 'g++ was not found.' }

$Output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
$Arguments = @(
    '-std=c++14', '-Wall', '-Wextra', '-O2',
    '-ffunction-sections', '-fdata-sections',
    '-iquote', 'kernel/core/include', '-Ikernel/arch/amd64/include',
    'tests/vfs_mount_table_integrity_test.cpp', 'kernel/core/vfs.cpp',
    '-Wl,--gc-sections', '-o', $Output
)
Write-Host ("Running: {0} {1}" -f $Compiler.Source, ($Arguments -join ' '))
& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Kernel VFS mount-table test build failed with exit code $LASTEXITCODE." }
& $Output
if ($LASTEXITCODE -ne 0) { throw "Kernel VFS mount-table test failed with exit code $LASTEXITCODE." }
Write-Host 'Kernel VFS mount-table test PASS.'
