<#
.SYNOPSIS
    Proves private BOT/SCSI writes and the enabled shared USB write callback.
.DESCRIPTION
    QEMU receives only the repository-owned raw image in writable mode. The
    shared BlockDevice write callback is enabled. The kernel exercises
    WRITE, SYNCHRONIZE CACHE, shared block read-back, neighboring-sector guards,
    and exact restoration. Host SHA-256 equality is required after shutdown.
#>
[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$UsbImage = "out\dm13-qemu-usb-private.raw",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$WorkDir = "",
    [switch]$UsbReadOnly
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $WorkDir) { $WorkDir = "out\dm13-qemu-usb-write-$timestamp" }
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
$UsbFull = [IO.Path]::GetFullPath((Join-Path $Root $UsbImage))
$EspFull = [IO.Path]::GetFullPath((Join-Path $Root $EspSource))
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below repository out/." }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir must be empty; evidence is never overwritten."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }
if (-not $UsbFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "USB image must be a repository-owned file under out/." }
if (-not (Test-Path -LiteralPath $UsbFull -PathType Leaf)) { throw "Create a fresh disposable raw image first: $UsbFull" }
if ((Get-Item -LiteralPath $UsbFull).Length -lt 64MB) { throw "USB proof image must be at least 64 MiB." }
if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) { throw "ESP runtime files are missing." }
$kernel = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
$bootloader = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
if (-not (Test-Path -LiteralPath $kernel) -or -not (Test-Path -LiteralPath $bootloader)) { throw "Build the UEFI loader and DM13 proof kernel first." }
$espStage = Join-Path $WorkFull "esp"
$serial = Join-Path $WorkFull "usb-write.serial.log"
$stderr = Join-Path $WorkFull "usb-write.stderr.log"
$stdout = Join-Path $WorkFull "usb-write.stdout.log"
$trace = Join-Path $WorkFull "usb-uhci.trace.log"
$events = Join-Path $WorkFull "usb-uhci.trace-events.txt"
$manifest = Join-Path $WorkFull "manifest.txt"
$proc = $null; $port = 0; $passed = $false
function Stop-Dm13Qemu([System.Diagnostics.Process]$Process,[int]$Port,[string]$ExpectedSerial) {
    if (-not $Process) { return }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if (-not $row) { return }
    if ($row.Name -ne "qemu-system-x86_64.exe" -or $row.CommandLine -notlike "*$ExpectedSerial*") {
        throw "Refusing to stop a QEMU process not owned by this proof."
    }
    if (-not $Process.HasExited) {
        try {
            $client=[System.Net.Sockets.TcpClient]::new(); $client.Connect("127.0.0.1",$Port)
            $stream=$client.GetStream(); $quit=[Text.Encoding]::ASCII.GetBytes("quit`n")
            $stream.Write($quit,0,$quit.Length); $stream.Dispose(); $client.Dispose()
        } catch { }
        [void]$Process.WaitForExit(5000)
    }
    $row=Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and $row.CommandLine -like "*$ExpectedSerial*") {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Process.WaitForExit(10000)
    }
}
try {
    New-Item -ItemType Directory -Path $espStage -Force | Out-Null
    Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $espStage -Recurse -Force
    $bootDir=Join-Path $espStage "EFI\BOOT"; New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
    Copy-Item -LiteralPath $bootloader -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $kernel -Destination (Join-Path $espStage "kernel.elf") -Force
    $hashBefore=(Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    $monitor=[System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0); $monitor.Start()
    $port=([Net.IPEndPoint]$monitor.LocalEndpoint).Port; $monitor.Stop()
    @("usb_uhci_schedule_start","usb_uhci_packet_add","usb_uhci_packet_complete_success",
      "usb_uhci_td_complete","usb_uhci_packet_complete_error","usb_packet_state_fault",
      "usb_msd_cmd_submit","usb_msd_data_out","usb_msd_packet_async",
      "usb_msd_packet_complete","usb_msd_cmd_complete","usb_msd_send_status") |
        Set-Content -LiteralPath $events -Encoding ascii
    $usbDrive = if ($UsbReadOnly) { "file=$UsbFull,format=raw,readonly=on" } else { "file=$UsbFull,format=raw" }
    $args=@("-drive","if=pflash,format=raw,readonly=on,file=$OvmfFull",
      "-machine","pc,usb=off","-device","piix3-usb-uhci,id=uhci",
      "-drive","file=fat:rw:$espStage,format=raw",
      "-drive","if=none,id=usbdata,$usbDrive",
      "-device","usb-storage,id=usbdisk,bus=uhci.0,drive=usbdata,removable=on,serial=DM13WR01",
      "-netdev","user,id=net0","-device","e1000,netdev=net0",
      "-object","rng-builtin,id=rng0","-device","virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
      "-m","1024M","-vga","std","-display","none","-serial","file:$serial",
      "-trace","events=$events,file=$trace","-monitor","tcp:127.0.0.1:$port,server,nowait",
      "-rtc","base=utc,clock=host","-no-reboot")
    @("proof=DM13-QEMU-USB-PRIVATE-WRITE-RESTORE","timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
      "qemu=$((& $QemuFull --version | Select-Object -First 1))","controller=PIIX3-UHCI",
      "usbImage=$UsbFull","usbImageBytes=$((Get-Item -LiteralPath $UsbFull).Length)",
      "usbImageSha256Before=$hashBefore","usbImageAccess=$(if ($UsbReadOnly) { 'read-only-disposable-only' } else { 'writable-disposable-only' })",
      "targetLba=32768",
      "targetCounts=$(if ($UsbReadOnly) { 'single-sector-read-only-check' } else { '1x100,2,7,128' })",
      "targetCanaries=$(if ($UsbReadOnly) { 'read-only-sector' } else { 'LBA-1 and LBA+count' })",
      "physicalHostDisksPassedToQemu=none","sharedWriteCallbackExpected=$(if ($UsbReadOnly) { 'no' } else { 'yes' })") |
        Set-Content -LiteralPath $manifest -Encoding ascii
    $proc=Start-Process -FilePath $QemuFull -ArgumentList $args -WorkingDirectory $Root -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $deadline=[DateTime]::UtcNow.AddSeconds(300); $booted=$false
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serial) {
            $log=Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
            if ($log -and $log -match '\[KERNEL-FAULT\]') { throw "QEMU kernel fault: $serial" }
            if ($log -and $log.Contains("[KERNEL] Entering main loop")) {
                $booted=$true
                if ($UsbReadOnly) {
                    $passed=$log -match '\[DM13-QEMU-USB\] write-protect=PASS reads=PASS private-write-blocked=PASS common-write-blocked=PASS'
                } else {
                    $passed=($log -match '\[DM13-QEMU-USB\] proof=PASS private-write=PASS sync-cache=PASS readback=PASS adjacent-canaries=PASS restored=PASS') -and
                        ($log -match '\[DM13-QEMU-USB\] common-write-callback-readback-restore=PASS')
                }
                break
            }
        }
        if ($proc.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $passed) {
        $log=Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
        $tail=if ($log) { (($log -split "`r?`n") | Select-Object -Last 28) -join ' | ' } else { '(serial empty)' }
        throw "DM13 USB write proof failed (booted=$booted). $tail"
    }
    Stop-Dm13Qemu $proc $port $serial; $proc=$null
    $hashAfter=(Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    if ($hashBefore -ne $hashAfter) { throw "Host image hash changed after restore: $hashBefore => $hashAfter" }
    $resultSummary = if ($UsbReadOnly) { "result=PASS qemu-write-protection=yes reads=yes writes-blocked=yes host-image-unchanged=yes physical-media=none" } else { "result=PASS private-production-write=yes sync-cache=yes exact-readback=yes canaries=yes host-image-restored=yes physical-media=none" }
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @("serialLog=$serial","uhciTrace=$trace",
      "usbImageSha256After=$hashAfter",$resultSummary)
    Write-Host "DM13 private USB write proof passed. Evidence: $WorkFull"
} catch {
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @("result=FAIL","failure=$($_.Exception.Message -replace '[\r\n]+',' ')") -ErrorAction SilentlyContinue
    throw
} finally { if ($proc) { Stop-Dm13Qemu $proc $port $serial } }
