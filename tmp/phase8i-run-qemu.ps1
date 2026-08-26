$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$python = 'C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$server = Join-Path $root 'scripts\navigator_kernel_http_server.py'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$ovmf = 'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
$serialLog = Join-Path $root 'logs\phase8i-stage2-example.serial.log'
$httpLog = Join-Path $root 'logs\phase8i-stage2-http.log'
$httpErrLog = Join-Path $root 'logs\phase8i-stage2-http.err.log'
$httpsLog = Join-Path $root 'logs\phase8i-stage2-https.log'
$httpsErrLog = Join-Path $root 'logs\phase8i-stage2-https.err.log'
Remove-Item -LiteralPath $serialLog -Force -ErrorAction SilentlyContinue

$httpArgs = @(
    "`"$server`"", '--port', '8080', '--host', '0.0.0.0', '--root', "`"$root`"",
    '--http-port', '8080', '--https-port', '8443', '--policy-host', 'dev.guidexos.test',
    '--policy-wrong-host', 'wrong.guidexos.test', '--public-pilot-host', 'public-pilot.guidexos.test'
)
$httpsArgs = @(
    "`"$server`"", '--port', '8443', '--host', '0.0.0.0', '--root', "`"$root`"",
    '--http-port', '8080', '--https-port', '8443', '--policy-host', 'dev.guidexos.test',
    '--policy-wrong-host', 'wrong.guidexos.test',
    '--local-tls-cert', "`"$(Join-Path $root 'scripts\fixtures\navigator-smoke-guidexos.test.crt')`"",
    '--local-tls-key', "`"$(Join-Path $root 'scripts\fixtures\navigator-smoke-guidexos.test.key')`"",
    '--policy-tls-cert', "`"$(Join-Path $root 'scripts\fixtures\navigator-policy-dev.guidexos.test.crt')`"",
    '--policy-tls-key', "`"$(Join-Path $root 'scripts\fixtures\navigator-policy-dev.guidexos.test.key')`"",
    '--public-pilot-host', 'public-pilot.guidexos.test',
    '--public-pilot-tls-cert', "`"$(Join-Path $root 'scripts\fixtures\navigator-public-pilot.guidexos.test.crt')`"",
    '--public-pilot-tls-key', "`"$(Join-Path $root 'scripts\fixtures\navigator-public-pilot.guidexos.test.key')`""
)
$httpProc = Start-Process -FilePath $python -ArgumentList $httpArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpLog -RedirectStandardError $httpErrLog
$httpsProc = Start-Process -FilePath $python -ArgumentList $httpsArgs -PassThru -WindowStyle Hidden -RedirectStandardOutput $httpsLog -RedirectStandardError $httpsErrLog
Start-Sleep -Milliseconds 800
if ($httpProc.HasExited -or $httpsProc.HasExited) {
    throw "Navigator smoke server exited early. HTTP=$($httpProc.HasExited) HTTPS=$($httpsProc.HasExited)"
}

$args = @(
    '-machine', 'pc',
    '-drive', "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
    '-drive', "file=fat:rw:`"$(Join-Path $root 'ESP')`",format=raw,if=ide,index=0",
    '-m', '512M', '-vga', 'std', '-display', 'none',
    '-serial', "file:`"$serialLog`"", '-no-reboot', '-rtc', 'base=utc,clock=host',
    '-netdev', 'user,id=net0', '-device', 'e1000,netdev=net0',
    '-object', 'rng-builtin,id=rng0',
    '-device', 'virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000'
)
$qemuProc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
Write-Output "QEMU_PID=$($qemuProc.Id) HTTP_PID=$($httpProc.Id) HTTPS_PID=$($httpsProc.Id)"
try {
    $deadline = (Get-Date).AddSeconds(120)
    while (-not $qemuProc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
    }
} finally {
    foreach ($proc in @($qemuProc, $httpProc, $httpsProc)) {
        if ($proc -and -not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
Write-Output "QEMU_EXITED=$($qemuProc.HasExited) SERIAL_EXISTS=$(Test-Path -LiteralPath $serialLog)"
