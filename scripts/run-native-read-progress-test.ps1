[CmdletBinding()]
param(
    [string]$OutputPath = (Join-Path ([IO.Path]::GetTempPath()) 'guidexos_native_read_progress_test.exe')
)

$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $Compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $Compiler) { throw 'A hosted C++ compiler (g++) is required for the native read-progress regression.' }

$Output = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
$Arguments = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    "-iquote$(Join-Path $Root 'kernel\core\include')",
    (Join-Path $Root 'kernel\tests\native_read_progress_test.cpp'),
    '-o', $Output
)

& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Native read-progress regression compilation failed with exit code $LASTEXITCODE." }
& $Output
if ($LASTEXITCODE -ne 0) { throw "Native read-progress regression failed with exit code $LASTEXITCODE." }
Write-Host 'Native read-progress regression PASS.'
