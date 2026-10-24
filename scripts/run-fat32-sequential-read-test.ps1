[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path ([IO.Path]::GetTempPath()) 'guidexos_fat32_sequential_read_test.exe')
)

$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $Compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $Compiler) { throw 'A hosted C++ compiler (g++) is required for the FAT32 cursor regression.' }

$Output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
$Arguments = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
    "-iquote$(Join-Path $Root 'kernel\core\include')",
    "-I$(Join-Path $Root 'kernel\arch\amd64\include')",
    (Join-Path $Root 'kernel\core\fs_fat.cpp'),
    (Join-Path $Root 'kernel\core\vfs.cpp'),
    (Join-Path $Root 'kernel\tests\fat32_sequential_read_test.cpp'),
    '-o', $Output
)

& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "FAT32 cursor regression compilation failed with exit code $LASTEXITCODE." }
& $Output
if ($LASTEXITCODE -ne 0) { throw "FAT32 cursor regression failed with exit code $LASTEXITCODE." }
Write-Host 'FAT32 sequential-read regression PASS.'
