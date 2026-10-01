<#
.SYNOPSIS
    Replays the exact archived DM18 kernel and bootloader in QEMU with USB tracing.
.DESCRIPTION
    Uses a newly-created blank 80 MiB raw image and the DM18 PIIX3-UHCI,
    WHPX, OVMF, and QEMU-default-cache profile. Stops after a CSW transport
    failure, DM13 lifecycle result, or a bounded observation timeout.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$KernelImage,
    [Parameter(Mandatory=$true)][string]$BootloaderImage,
    [string]$EvidenceRoot = "out\dm20-evidence",
    [string]$EspSource = "ESP",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$QemuAccelerator = "whpx",
    [string]$OvmfCode = "OVMF.fd",
    [int]$ObservationTimeoutSeconds = 900,
    [switch]$NoTrace
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$evidenceBase = if ([IO.Path]::IsPathRooted($EvidenceRoot)) {
    [IO.Path]::GetFullPath($EvidenceRoot)
} else { [IO.Path]::GetFullPath((Join-Path $Root $EvidenceRoot)) }
New-Item -ItemType Directory -Path $evidenceBase -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$WorkFull = Join-Path $evidenceBase "dm20-dm18-replay-$stamp"
New-Item -ItemType Directory -Path $WorkFull -ErrorAction Stop | Out-Null
$kernel = if ([IO.Path]::IsPathRooted($KernelImage)) { $KernelImage } else { Join-Path $Root $KernelImage }
$bootloader = if ([IO.Path]::IsPathRooted($BootloaderImage)) { $BootloaderImage } else { Join-Path $Root $BootloaderImage }
$kernel = (Resolve-Path -LiteralPath $kernel).Path
$bootloader = (Resolve-Path -LiteralPath $bootloader).Path
$EspFull = if ([IO.Path]::IsPathRooted($EspSource)) { $EspSource } else { Join-Path $Root $EspSource }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$UsbFull = Join-Path $WorkFull "dm18-pristine-blank.raw"
$espStage = Join-Path $WorkFull "esp"
$serial = Join-Path $WorkFull "first-boot.serial.log"
$trace = Join-Path $WorkFull "usb-uhci.trace.log"
$events = Join-Path $WorkFull "usb-uhci.trace-events.txt"
$manifest = Join-Path $WorkFull "manifest.txt"
$verify = Join-Path $Root "scripts\verify-dm13-qemu-usb-image.py"
$python = Get-Command python -ErrorAction Stop
$process = $null
$port = 0
$result = "FAIL"

function Stop-Dm20HistoricalQemu {
    if (-not $process -or $process.HasExited) { return }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if (-not $row -or $row.Name -ne "qemu-system-x86_64.exe" -or
        $row.CommandLine -notlike "*$serial*") { throw "Refusing to stop a QEMU process not owned by this replay." }
    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $client.Connect("127.0.0.1", $port)
        $stream = $client.GetStream()
        $quit = [Text.Encoding]::ASCII.GetBytes("quit`n")
        $stream.Write($quit, 0, $quit.Length)
        $stream.Dispose(); $client.Dispose()
    } catch { }
    [void]$process.WaitForExit(7000)
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and
        $row.CommandLine -like "*$serial*") {
        Stop-Process -Id $process.Id -Force
        [void]$process.WaitForExit(10000)
    }
}

try {
    if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) { throw "ESP runtime files are missing." }
    $stream = [IO.File]::Open($UsbFull, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.SetLength(80L * 1024L * 1024L) } finally { $stream.Dispose() }
    $blankCheck = & $python.Source $verify --check-blank $UsbFull 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Replay image is not blank: $($blankCheck -join ' ')" }

    New-Item -ItemType Directory -Path $espStage | Out-Null
    Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $espStage -Recurse -Force
    $bootDir = Join-Path $espStage "EFI\BOOT"
    New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
    Copy-Item -LiteralPath $bootloader -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $kernel -Destination (Join-Path $espStage "kernel.elf") -Force
    @("usb_uhci_schedule_start","usb_uhci_packet_add","usb_uhci_packet_complete_success",
      "usb_uhci_td_complete","usb_uhci_packet_complete_error","usb_packet_state_fault",
      "usb_msd_cmd_submit","usb_msd_data_in","usb_msd_data_out","usb_msd_packet_async",
      "usb_msd_packet_complete","usb_msd_cmd_complete","usb_msd_send_status") |
        Set-Content -LiteralPath $events -Encoding ascii

    $kernelHash = (Get-FileHash -LiteralPath (Join-Path $espStage "kernel.elf") -Algorithm SHA256).Hash
    $bootHash = (Get-FileHash -LiteralPath (Join-Path $bootDir "BOOTX64.EFI") -Algorithm SHA256).Hash
    $imageBefore = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    if ($kernelHash -ne "27F0C0ECD097404BC00C3E6FD9354C96D63F6A19CDB6313487ECB82B489E80FC") {
        throw "Historical kernel hash does not match the DM18 failure artifact: $kernelHash"
    }
    if ($bootHash -ne "1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107") {
        throw "Historical bootloader hash does not match the DM18 failure artifact: $bootHash"
    }
    if ($imageBefore -ne "33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF") {
        throw "Fresh raw image hash does not match the original DM18 blank image: $imageBefore"
    }
    $qemuVersion = (& $QemuFull --version | Select-Object -First 1)
    $qemuHash = (Get-FileHash -LiteralPath $QemuFull -Algorithm SHA256).Hash
    $ovmfHash = (Get-FileHash -LiteralPath $OvmfFull -Algorithm SHA256).Hash
    $listener = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(); $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port; $listener.Stop()
    @("manifestSchema=DM20-DM18-REPLAY-1","proof=DM20-REPLAY-ARCHIVED-DM18-USB-WRITABLE-FAILURE",
      "timestampUtc=$([DateTime]::UtcNow.ToString('o'))","qemu=$qemuVersion","qemuSha256=$qemuHash",
      "machine=pc,usb=off","controller=PIIX3-UHCI","accelerator=$QemuAccelerator",
      "storageImagePath=$UsbFull","storageImageBytes=83886080","storageImageSha256Before=$imageBefore",
      "storageImageBlankVerification=$($blankCheck -join '; ')","storageImageAccess=writable-disposable-only",
      "storageCacheMode=QEMU default (cache option omitted)","uefiPath=$OvmfFull","uefiSha256=$ovmfHash",
      "historicalKernelPath=$kernel","historicalKernelSha256=$kernelHash",
      "historicalBootloaderPath=$bootloader","historicalBootloaderSha256=$bootHash",
      "expectedKernelSha256=27F0C0ECD097404BC00C3E6FD9354C96D63F6A19CDB6313487ECB82B489E80FC",
      "expectedBootloaderSha256=1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107",
      "timeoutUhciBulkFrames=1000","observationTimeoutSeconds=$ObservationTimeoutSeconds",
      "qemuTraceEnabled=$(if ($NoTrace) { 'no' } else { 'yes' })",
      "qemuTraceEvents=$events","qemuTrace=$trace","guestSerial=$serial",
      "physicalHostDisksPassedToQemu=none") | Set-Content -LiteralPath $manifest -Encoding ascii

    $qemuArgs = @("-accel",$QemuAccelerator,
      "-drive","if=pflash,format=raw,readonly=on,file=$OvmfFull",
      "-machine","pc,usb=off","-device","piix3-usb-uhci,id=uhci",
      "-drive","file=fat:rw:$espStage,format=raw",
      "-drive","if=none,id=usbdata,file=$UsbFull,format=raw",
      "-device","usb-storage,id=usbdisk,bus=uhci.0,drive=usbdata,removable=on,serial=DM13WR01",
      "-netdev","user,id=net0","-device","e1000,netdev=net0",
      "-object","rng-builtin,id=rng0",
      "-device","virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
      "-m","1024M","-vga","std","-display","none","-serial","file:$serial")
    if (-not $NoTrace) { $qemuArgs += @("-trace","events=$events,file=$trace") }
    $qemuArgs += @("-monitor","tcp:127.0.0.1:$port,server,nowait",
      "-rtc","base=utc,clock=host","-no-reboot")
    $process = Start-Process -FilePath $QemuFull -ArgumentList $qemuArgs -WorkingDirectory $Root `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $WorkFull "qemu.stdout.log") `
        -RedirectStandardError (Join-Path $WorkFull "qemu.stderr.log")
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "qemuPid=$($process.Id)","qemuMonitorPort=$port","qemuCommandLine=$($row.CommandLine)")

    $deadline = [DateTime]::UtcNow.AddSeconds($ObservationTimeoutSeconds)
    $observed = ""
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serial) {
            $log = Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
            if ($log -match '\[KERNEL-FAULT\]') { $observed = "KERNEL_FAULT"; break }
            if ($log -match '\[USB-MSC\] bot-csw-failure') { $observed = "CSW_FAILURE"; break }
            if ($log -match '\[USB-UHCI\] bulk-transfer-failed.*endpoint=81.*requested=000D') { $observed = "CSW_BULK_TRANSFER_FAILURE"; break }
            if ($log -match '\[DM13-QEMU-USB\] lifecycle=PASS') { $observed = "LIFECYCLE_PASS"; break }
            if ($log -match '\[DM13-QEMU-USB\] proof=PASS') { $observed = "DM13_PROOF_PASS"; break }
            if ($log -match '\[DM13-QEMU-USB\].*=FAIL') { $observed = "DM13_PROOF_FAILURE"; break }
        }
        if ($process.HasExited) { $observed = "QEMU_EXITED"; break }
        Start-Sleep -Milliseconds 500
    }
    Stop-Dm20HistoricalQemu
    $process = $null
    $log = if (Test-Path -LiteralPath $serial) { Get-Content -LiteralPath $serial -Raw } else { "" }
    $cswLines = @($log -split "`r?`n" | Where-Object { $_ -match '\[USB-MSC\] bot-csw-failure' })
    $activeTdLines = @($log -split "`r?`n" | Where-Object { $_ -match 'status=03' -and $_ -match 'endpoint=81' -and $_ -match 'requested=000D' })
    $imageAfter = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    $result = if ($observed -eq "CSW_FAILURE" -or $observed -eq "CSW_BULK_TRANSFER_FAILURE") {
        "REPRODUCED_CSW_FAILURE"
    } elseif ($observed -eq "LIFECYCLE_PASS") { "NO_CSW_FAILURE_LIFECYCLE_PASS" }
    elseif ($observed -eq "DM13_PROOF_PASS") { "NO_CSW_FAILURE_DM13_PROOF_PASS" }
    else { "NO_CSW_FAILURE_OBSERVED" }
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "observation=$observed","cswFailureLineCount=$($cswLines.Count)",
      "activeEndpoint81CswTimeoutLikeLineCount=$($activeTdLines.Count)",
      "storageImageSha256After=$imageAfter","result=$result")
    Write-Host "$result. Evidence: $WorkFull"
} catch {
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "result=FAIL","failure=$($_.Exception.Message -replace '[\r\n]+',' ')")
    throw
} finally {
    if ($process) { Stop-Dm20HistoricalQemu }
}
