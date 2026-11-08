$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $env:TEMP 'guidex-navhttps5r2-tcp-smoke'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$includes = @(
    '-I', (Join-Path $root 'kernel\core\include'),
    '-I', (Join-Path $root 'kernel\arch\amd64\include'),
    '-include', (Join-Path $root 'tests\network_serial_debug_stub.h')
)

Push-Location $root
try {
    $ipv4Exe = Join-Path $outDir 'ipv4_send_diagnostics_test.exe'
    & g++ -std=c++14 -O2 -Wall -Wextra @includes `
        (Join-Path $root 'tests\ipv4_send_diagnostics_test.cpp') `
        (Join-Path $root 'kernel\core\ipv4.cpp') `
        (Join-Path $root 'kernel\core\ethernet.cpp') -o $ipv4Exe
    if ($LASTEXITCODE -ne 0) { throw 'IPv4 send diagnostics test build failed.' }
    & $ipv4Exe
    if ($LASTEXITCODE -ne 0) { throw 'IPv4 send diagnostics tests failed.' }
    Write-Output 'IPv4 attribution cases PASS: configuration, packet validation, route, neighbor, NIC submission'

    $tcpExe = Join-Path $outDir 'tcp_connect_cleanup_test.exe'
    & g++ -std=c++14 -O2 -Wall -Wextra @includes `
        (Join-Path $root 'tests\tcp_connect_cleanup_test.cpp') `
        (Join-Path $root 'kernel\core\tcp.cpp') `
        (Join-Path $root 'kernel\core\ethernet.cpp') -o $tcpExe
    if ($LASTEXITCODE -ne 0) { throw 'TCP connect cleanup test build failed.' }
    & $tcpExe
    if ($LASTEXITCODE -ne 0) { throw 'TCP connect cleanup tests failed.' }
    Write-Output 'TCP connection and cleanup cases PASS: failed SYN, pending SYN close, socket bounds'
} finally {
    Pop-Location
}

Write-Output 'Focused TCP/IPv4 attribution smoke PASS'
