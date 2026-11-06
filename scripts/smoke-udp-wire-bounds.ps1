$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $env:TEMP 'guidex-navhttps3-udp-smoke'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Push-Location $root
try {
    $commonIncludes = @(
        '-I', (Join-Path $root 'kernel\core\include'),
        '-I', (Join-Path $root 'kernel\arch\amd64\include'),
        '-include', (Join-Path $root 'tests\dns_serial_debug_stub.h')
    )
    $udpExe = Join-Path $outDir 'udp_wire_bounds_test.exe'
    & g++ -std=c++14 -O2 -Wall -Wextra @commonIncludes `
        (Join-Path $root 'tests\udp_wire_bounds_test.cpp') `
        (Join-Path $root 'kernel\core\udp.cpp') -o $udpExe
    if ($LASTEXITCODE -ne 0) { throw 'UDP wire test build failed.' }
    & $udpExe
    if ($LASTEXITCODE -ne 0) { throw 'UDP wire tests failed.' }
    Write-Output 'UDP checksum cases PASS: 4'

    $socketExe = Join-Path $outDir 'socket_datagram_bounds_test.exe'
    & g++ -std=c++14 -O2 -Wall -Wextra @commonIncludes `
        (Join-Path $root 'tests\socket_datagram_bounds_test.cpp') `
        (Join-Path $root 'kernel\core\socket.cpp') -o $socketExe
    if ($LASTEXITCODE -ne 0) { throw 'Socket datagram test build failed.' }
    & $socketExe
    if ($LASTEXITCODE -ne 0) { throw 'Socket datagram tests failed.' }
    Write-Output 'Socket datagram-bound cases PASS: 2'
} finally {
    Pop-Location
}

Write-Output 'UDP/socket wire-bound smoke PASS (6 cases)'
