$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$guest = Join-Path $root 'out/font12r-validation/guest'
$qemu = 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$serial = Join-Path $guest 'boot2.serial.log'
$stdout = Join-Path $guest 'boot2.stdout.log'
$stderr = Join-Path $guest 'boot2.stderr.log'
$args = @(
    '-machine', 'pc,accel=whpx',
    '-m', '1024M',
    '-smp', '2',
    '-vga', 'std',
    '-display', 'none',
    '-vnc', '127.0.0.1:50',
    '-drive', "if=pflash,format=raw,readonly=on,file=$($guest.Replace('\','/'))/OVMF-font12r-code.fd",
    '-drive', "file=$($guest.Replace('\','/'))/font12r-primary.img,format=raw,if=ide,index=0",
    '-serial', "file:$($serial.Replace('\','/'))",
    '-qmp', 'tcp:127.0.0.1:59348,server=on,wait=off',
    '-rtc', 'base=utc,clock=host',
    '-boot', 'order=c',
    '-net', 'none',
    '-no-reboot',
    '-monitor', 'none',
    '-name', 'FONT12R-acceptance-boot2'
)
$p = Start-Process -FilePath $qemu -ArgumentList $args -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
[pscustomobject]@{ pid=$p.Id; arguments=$args; serial=$serial; qmp='tcp:127.0.0.1:59348'; vnc='127.0.0.1:5950' } | ConvertTo-Json -Depth 4
