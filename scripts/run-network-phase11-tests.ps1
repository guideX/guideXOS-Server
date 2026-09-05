param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Get-Command g++.exe -ErrorAction Stop
$testDir = Join-Path ([System.IO.Path]::GetTempPath()) 'guidex-phase11-network-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null

$diagnosticsExe = Join-Path $testDir 'network_diagnostics_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/amd64/include') `
    (Join-Path $root 'tests/network_diagnostics_test.cpp') `
    (Join-Path $root 'kernel/core/ethernet.cpp') -o $diagnosticsExe
if ($LASTEXITCODE -ne 0) { throw 'network_diagnostics_test compile failed' }
& $diagnosticsExe
if ($LASTEXITCODE -ne 0) { throw 'network_diagnostics_test failed' }

$wireExe = Join-Path $testDir 'dhcp_wire_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    '-U__x86_64__' '-U__x86_64' '-U__amd64' '-U__amd64__' '-D__aarch64__' `
    '-ffunction-sections' '-fdata-sections' `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/arm64/include') `
    (Join-Path $root 'tests/dhcp_wire_test.cpp') `
    (Join-Path $root 'kernel/core/dhcp.cpp') `
    (Join-Path $root 'kernel/core/udp.cpp') `
    (Join-Path $root 'kernel/core/ipv4.cpp') `
    (Join-Path $root 'kernel/core/ethernet.cpp') `
    '-Wl,--gc-sections' -o $wireExe
if ($LASTEXITCODE -ne 0) { throw 'dhcp_wire_test compile failed' }
& $wireExe
if ($LASTEXITCODE -ne 0) { throw 'dhcp_wire_test failed' }

$stateExe = Join-Path $testDir 'dhcp_state_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    '-U__x86_64__' '-U__x86_64' '-U__amd64' '-U__amd64__' '-D__aarch64__' `
    '-ffunction-sections' '-fdata-sections' `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/arm64/include') `
    (Join-Path $root 'tests/dhcp_state_test.cpp') `
    (Join-Path $root 'kernel/core/dhcp.cpp') `
    (Join-Path $root 'kernel/core/udp.cpp') `
    (Join-Path $root 'kernel/core/ipv4.cpp') `
    (Join-Path $root 'kernel/core/ethernet.cpp') `
    '-Wl,--gc-sections' -o $stateExe
if ($LASTEXITCODE -ne 0) { throw 'dhcp_state_test compile failed' }
& $stateExe
if ($LASTEXITCODE -ne 0) { throw 'dhcp_state_test failed' }

$configurationExe = Join-Path $testDir 'network_configuration_test.exe'
& $compiler.Source -std=c++14 -O2 -Wall -Wextra `
    '-U__x86_64__' '-U__x86_64' '-U__amd64' '-U__amd64__' '-D__aarch64__' `
    -I (Join-Path $root 'kernel/core/include') `
    -I (Join-Path $root 'kernel/arch/arm64/include') `
    (Join-Path $root 'tests/network_configuration_test.cpp') `
    (Join-Path $root 'kernel/core/ipv4.cpp') `
    (Join-Path $root 'kernel/core/ethernet.cpp') -o $configurationExe
if ($LASTEXITCODE -ne 0) { throw 'network_configuration_test compile failed' }
& $configurationExe
if ($LASTEXITCODE -ne 0) { throw 'network_configuration_test failed' }

Write-Host 'Phase 11 network tests PASS.' -ForegroundColor Green
