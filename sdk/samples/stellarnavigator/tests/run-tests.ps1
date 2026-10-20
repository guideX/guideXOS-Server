[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$SourceDir = Split-Path -Parent $PSScriptRoot
$Dataset = Join-Path $Root 'Apps\StellarNavigator\Astronomy.Ephemeris\Data\ephemeris_de440s_chebyshev.sn4bin'
$Compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $Compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $Compiler) { throw 'A hosted C++ compiler (g++) is required for the native core tests.' }
if (-not (Test-Path -LiteralPath $Dataset -PathType Leaf)) { throw "DE440s dataset is missing: $Dataset" }

$BuildDir = Join-Path $env:TEMP ('stellar-navigator-native-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Executable = Join-Path $BuildDir 'ephemeris_core_test.exe'
$Arguments = @(
    '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
    (Join-Path $SourceDir 'ephemeris_core.cpp'),
    (Join-Path $PSScriptRoot 'ephemeris_core_test.cpp'),
    '-o', $Executable
)

& $Compiler.Source @Arguments
if ($LASTEXITCODE -ne 0) { throw "Native ephemeris test compilation failed with exit code $LASTEXITCODE." }
& $Executable $Dataset
if ($LASTEXITCODE -ne 0) { throw "Native ephemeris tests failed with exit code $LASTEXITCODE." }
