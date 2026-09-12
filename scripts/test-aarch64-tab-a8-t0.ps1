[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$pythonCommand = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $pythonCommand) { throw 'Python 3 is required for Tab A8 host tests.' }
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$test = Join-Path $repoRoot 'tests\tab_a8_recon_tests.py'
& $pythonCommand.Source $test
if ($LASTEXITCODE -ne 0) { throw "Tab A8 host tests failed ($LASTEXITCODE)." }
$gxxCommand = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $gxxCommand) { throw 'g++ is required for the Tab A8 platform-map host test.' }
$hostTestSource = Join-Path $repoRoot 'tests\aarch64_tab_a8_platform_host_tests.cpp'
$hostTestOutput = Join-Path $repoRoot 'out\aarch64-tab-a8-t0\aarch64_tab_a8_platform_host_tests.exe'
New-Item -ItemType Directory -Path (Split-Path $hostTestOutput) -Force | Out-Null
& $gxxCommand.Source '-std=c++17' $hostTestSource '-o' $hostTestOutput
if ($LASTEXITCODE -ne 0) { throw "Tab A8 platform-map host test build failed ($LASTEXITCODE)." }
& $hostTestOutput
if ($LASTEXITCODE -ne 0) { throw "Tab A8 platform-map host test failed ($LASTEXITCODE)." }
Write-Host 'AARCH64 Tab A8 T0 host tests: PASS' -ForegroundColor Green
