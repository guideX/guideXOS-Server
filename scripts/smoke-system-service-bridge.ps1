[CmdletBinding()]
param(
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$RunRoot = Join-Path $Root 'out\validation\system-service-smoke'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDirectory = Join-Path $RunRoot $Stamp
New-Item -ItemType Directory -Force -Path $RunDirectory | Out-Null

function Find-Qemu {
    $command = Get-Command 'qemu-system-x86_64.exe' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        'C:\qemu\qemu-system-x86_64.exe',
        'D:\qemu\qemu-system-x86_64.exe')) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

function Get-FreeLoopbackPort {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

$Qemu = Find-Qemu
if (-not $Qemu) { throw 'qemu-system-x86_64.exe is unavailable.' }
foreach ($required in @(
    (Join-Path $Root 'OVMF.fd'),
    (Join-Path $Root 'ESP\EFI\BOOT\BOOTX64.EFI'),
    (Join-Path $Root 'kernel\build\amd64\bin\kernel.elf'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Runtime proof input is missing: $required" }
}

$Make = 'C:\mingw64\bin\mingw32-make.exe'
if (-not (Test-Path -LiteralPath $Make)) { throw 'C:\mingw64\bin\mingw32-make.exe is unavailable.' }
Push-Location (Join-Path $Root 'kernel')
try {
    & $Make ARCH=amd64 -j4
    if ($LASTEXITCODE -ne 0) { throw "AMD64 kernel build failed with exit code $LASTEXITCODE." }
} finally { Pop-Location }
Copy-Item -LiteralPath (Join-Path $Root 'kernel\build\amd64\bin\kernel.elf') `
    -Destination (Join-Path $Root 'ESP\kernel.elf') -Force

$Compiler = Get-Command 'g++.exe' -ErrorAction SilentlyContinue
if (-not $Compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $Compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (-not $Compiler) { throw 'g++.exe is unavailable.' }
$ClientPath = Join-Path $RunDirectory 'system_service_runtime_client.exe'
& $Compiler.Source -std=c++17 -Wall -Wextra -O2 -iquote . `
    tests\system_service_runtime_client.cpp -o $ClientPath -lws2_32
if ($LASTEXITCODE -ne 0) { throw "Runtime client build failed with exit code $LASTEXITCODE." }

$Port = Get-FreeLoopbackPort
$SerialLog = Join-Path $RunDirectory 'guest-com1.serial.log'
$StderrLog = Join-Path $RunDirectory 'qemu.stderr.log'
$QemuSerialPath = $SerialLog.Replace('\', '/')
$QemuStderrPath = $StderrLog.Replace('\', '/')
$QemuOvmfPath = (Join-Path $Root 'OVMF.fd').Replace('\', '/')
$QemuArguments = @(
    '-machine', 'pc,usb=off',
    '-drive', "if=pflash,format=raw,readonly=on,file=$QemuOvmfPath",
    '-drive', 'file=fat:rw:ESP,format=raw',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-m', '1024M',
    '-vga', 'std',
    '-display', 'none',
    '-monitor', 'none',
    '-serial', "file:$QemuSerialPath",
    '-serial', "tcp:127.0.0.1:$Port,server=on,wait=off",
    '-rtc', 'base=utc,clock=host',
    '-no-reboot',
    '-no-shutdown')

$QemuProcess = $null
try {
    $QemuProcess = Start-Process -FilePath $Qemu -ArgumentList $QemuArguments `
        -PassThru -WindowStyle Hidden -WorkingDirectory $Root `
        -RedirectStandardError $QemuStderrPath
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($QemuProcess.HasExited) {
            $stderr = if (Test-Path -LiteralPath $StderrLog) { Get-Content -Raw $StderrLog } else { '' }
            throw "QEMU exited before service initialization (exit=$($QemuProcess.ExitCode)). $stderr"
        }
        $serial = if (Test-Path -LiteralPath $SerialLog) { Get-Content -Raw $SerialLog -ErrorAction SilentlyContinue } else { '' }
        if ($serial -match '\[SYSBRIDGE\] initialized transport=uart-com2 protocol=1') { break }
        Start-Sleep -Milliseconds 250
    }
    $serial = if (Test-Path -LiteralPath $SerialLog) { Get-Content -Raw $SerialLog -ErrorAction SilentlyContinue } else { '' }
    if ($serial -notmatch '\[SYSBRIDGE\] initialized transport=uart-com2 protocol=1') {
        throw "The kernel service bridge did not initialize within $TimeoutSeconds seconds. Serial=$SerialLog"
    }
    if ($serial -notmatch '\[NIC\].*' -or $serial -notmatch '\[SettingsNetwork\] result=') {
        throw "The kernel boot did not emit NIC and network provider diagnostics. Serial=$SerialLog"
    }

    $clientOutput = @(& $ClientPath $Port 2>&1)
    $clientExit = $LASTEXITCODE
    if ($clientExit -ne 0) {
        throw "Production-path SystemServiceClient failed (exit=$clientExit): $($clientOutput -join [Environment]::NewLine)"
    }
    $clientText = $clientOutput -join [Environment]::NewLine
    $snapshots = [regex]::Matches($clientText,
        'SYSCLIENT snapshot received request=(?<request>\d+) generation=(?<generation>[0-9A-F]{16}) state=(?<state>[0-9A-F]{2}) adapters=(?<adapters>[0-9A-F]{8}) backend=(?<backend>[0-9A-F]{2}) name=(?<name>\S+) driver=(?<driver>.+?) link=(?<link>[0-9A-F]{2}) mode=(?<mode>[0-9A-F]{2}) dhcp=(?<dhcp>[0-9A-F]{2}) dnsSource=(?<dnsSource>[0-9A-F]{2}) ipv4=(?<ipv4>\S+) mask=(?<mask>\S+) gateway=(?<gateway>\S+) dns=(?<dns>\S+)')
    if ($snapshots.Count -ne 2) { throw "Runtime client did not receive two valid snapshots: $clientText" }

    $serial = Get-Content -Raw -LiteralPath $SerialLog
    $kernelSnapshots = [regex]::Matches($serial,
        '\[NETSNAP\] generation=(?<generation>[0-9A-F]{16}) adapters=(?<adapters>[0-9A-F]{8}) state=(?<state>[0-9A-F]{2}) name=(?<name>\S+) driver=(?<driver>.+?) link=(?<link>[0-9A-F]{2}) mode=(?<mode>[0-9A-F]{2}) dhcp=(?<dhcp>[0-9A-F]{2}) dnsSource=(?<dnsSource>[0-9A-F]{2}) ipv4=(?<ipv4>\S+) mask=(?<mask>\S+) gateway=(?<gateway>\S+) dns=(?<dns>\S+)')
    if ($kernelSnapshots.Count -ne 2) { throw "Kernel did not emit two complete snapshot markers. Serial=$SerialLog" }
    foreach ($snapshot in $snapshots) {
        $requestNumber = [int]$snapshot.Groups['request'].Value
        $requestId = $requestNumber.ToString('X8')
        $kernelSnapshot = $kernelSnapshots[$requestNumber - 1]
        foreach ($field in @('generation', 'state', 'adapters', 'name', 'driver',
                'link', 'mode', 'dhcp', 'dnsSource', 'ipv4', 'mask', 'gateway', 'dns')) {
            if ($snapshot.Groups[$field].Value -cne $kernelSnapshot.Groups[$field].Value) {
                throw "Client field '$field' does not match the kernel snapshot for request $requestNumber. Serial=$SerialLog"
            }
        }
        if ($serial -notmatch "\[SYSBRIDGE\] request received type=0001 id=$requestId" -or
            $serial -notmatch "\[SYSBRIDGE\] authorized service=network.read_snapshot" -or
            $serial -notmatch "\[SYSBRIDGE\] response sent id=$requestId status=0000 result=ok") {
            throw "Kernel request, authorization, or response markers are incomplete for request $requestNumber. Serial=$SerialLog"
        }
    }
    if ($serial -match 'UEFI.*(re-entry|returned)' -or $serial -match '\[KERNEL-PANIC\]') {
        throw "Kernel emitted a failure marker during the service proof. Serial=$SerialLog"
    }

    Write-Output '[SystemServiceBridgeRuntime] PASS'
    Write-Output "QEMU=$Qemu"
    Write-Output "COM2 loopback port=$Port"
    Write-Output "Client: $clientText"
    Write-Output "Serial evidence=$SerialLog"
    Write-Output "QEMU stderr=$StderrLog"
} finally {
    if ($QemuProcess -and -not $QemuProcess.HasExited) {
        Stop-Process -Id $QemuProcess.Id -Force -ErrorAction SilentlyContinue
        $QemuProcess.WaitForExit(5000) | Out-Null
    }
}
