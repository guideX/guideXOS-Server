<#
.SYNOPSIS
    Boots guideXOS with a read-only QEMU USB Mass Storage device and proves a real FAT32 read.

.DESCRIPTION
    Stages a private ESP copy and the current AMD64 kernel/UEFI loader, then
    attaches one repository-owned raw image behind QEMU's PIIX3 UHCI controller.
    The USB backing image is opened read-only. No host physical disk is passed
    to QEMU. The run succeeds only after guideXOS logs shared USB MSC registration
    mounts the existing FAT32 partition read-only, reads a deterministic file,
    unmounts cleanly, and reaches its main loop. All output is preserved under out/.
#>
[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$UsbImage = "out\dm10-qemu-repeatability-final-340\attempt-01\secondary-600m.raw",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $WorkDir) { $WorkDir = "out\dm12-qemu-uhci-proof-$timestamp" }
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
$UsbFull = [IO.Path]::GetFullPath((Join-Path $Root $UsbImage))
$EspFull = [IO.Path]::GetFullPath((Join-Path $Root $EspSource))
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path

if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "WorkDir must be a new directory below $repoOut"
}
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir already contains files. Choose a new directory to preserve evidence."
    }
} else {
    New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null
}
if (-not $UsbFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "UsbImage must be a repository-owned file below $repoOut; physical disks are not accepted."
}
if (-not (Test-Path -LiteralPath $UsbFull -PathType Leaf)) { throw "USB image not found: $UsbFull" }
if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) {
    throw "EspSource must contain the guideXOS runtime files, including ramdisk.img."
}

$EspPath = Join-Path $WorkFull "esp"
$SerialPath = Join-Path $WorkFull "usb-probe.serial.log"
$StderrPath = Join-Path $WorkFull "usb-probe.stderr.log"
$StdoutPath = Join-Path $WorkFull "usb-probe.stdout.log"
$TracePath = Join-Path $WorkFull "usb-uhci.trace.log"
$TraceEventsPath = Join-Path $WorkFull "usb-uhci.trace-events.txt"
$ManifestPath = Join-Path $WorkFull "manifest.txt"
$QemuProcess = $null
$MonitorPort = 0
$Passed = $false

function Stop-UsbProbeQemu([System.Diagnostics.Process]$Process, [int]$Port,
                           [string]$ExpectedSerialPath) {
    if (-not $Process) { return }
    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if (-not $current) { return }
    if ($current.Name -ne "qemu-system-x86_64.exe" -or
        $current.CommandLine -notlike "*$ExpectedSerialPath*") {
        throw "Refusing to stop a QEMU process whose identity does not match this probe."
    }
    if (-not $Process.HasExited) {
        try {
            $client = [System.Net.Sockets.TcpClient]::new()
            $client.Connect("127.0.0.1", $Port)
            $stream = $client.GetStream()
            $quit = [Text.Encoding]::ASCII.GetBytes("quit`n")
            $stream.Write($quit, 0, $quit.Length)
            $stream.Dispose()
            $client.Dispose()
        } catch { }
        [void]$Process.WaitForExit(5000)
    }
    $current = Get-CimInstance Win32_Process -Filter "ProcessId=$($Process.Id)"
    if ($current -and $current.Name -eq "qemu-system-x86_64.exe" -and
        $current.CommandLine -like "*$ExpectedSerialPath*") {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Process.WaitForExit(10000)
    }
}

try {
    $bootloaderSource = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
    $kernelSource = Join-Path $Root "kernel\build\amd64\bin\kernel.elf"
    if (-not (Test-Path -LiteralPath $bootloaderSource) -or
        -not (Test-Path -LiteralPath $kernelSource)) {
        throw "Built UEFI bootloader or AMD64 kernel is missing. Run the DM11 builds first."
    }

    New-Item -ItemType Directory -Path $EspPath -Force | Out-Null
    Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $EspPath -Recurse -Force
    $bootPath = Join-Path $EspPath "EFI\BOOT"
    New-Item -ItemType Directory -Path $bootPath -Force | Out-Null
    Copy-Item -LiteralPath $bootloaderSource -Destination (Join-Path $bootPath "BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $kernelSource -Destination (Join-Path $EspPath "kernel.elf") -Force

    $usbHashBefore = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    $bootHash = (Get-FileHash -LiteralPath (Join-Path $bootPath "BOOTX64.EFI") -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath (Join-Path $EspPath "kernel.elf") -Algorithm SHA256).Hash
    $Monitor = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $Monitor.Start()
    $MonitorPort = ([Net.IPEndPoint]$Monitor.LocalEndpoint).Port
    $Monitor.Stop()
    @(
        "usb_uhci_schedule_start",
        "usb_uhci_packet_add",
        "usb_uhci_packet_complete_success",
        "usb_uhci_td_complete",
        "usb_uhci_packet_complete_error",
        "usb_packet_state_fault",
        "usb_msd_cmd_submit",
        "usb_msd_data_in",
        "usb_msd_packet_async",
        "usb_msd_packet_complete",
        "usb_msd_cmd_complete",
        "usb_msd_send_status"
    ) | Set-Content -LiteralPath $TraceEventsPath -Encoding ascii

    $arguments = @(
        "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfFull",
        "-machine", "pc,usb=off",
        "-device", "piix3-usb-uhci,id=uhci",
        "-drive", "file=fat:rw:$EspPath,format=raw",
        "-drive", "if=none,id=usbdata,file=$UsbFull,format=raw,readonly=on",
        "-device", "usb-storage,id=usbdisk,bus=uhci.0,drive=usbdata,removable=on,serial=DM12USB01",
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-object", "rng-builtin,id=rng0",
        "-device", "virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", "file:$SerialPath",
        "-trace", "events=$TraceEventsPath,file=$TracePath",
        "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait",
        "-rtc", "base=utc,clock=host", "-no-reboot"
    )
    $commandLine = '"{0}" {1}' -f $QemuFull, ($arguments -join ' ')
    @(
        "proof=DM12-QEMU-USB-UHCI-READ-ONLY-FAT32",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "qemu=$((& $QemuFull --version | Select-Object -First 1))",
        "controller=PIIX3-UHCI",
        "usbDevice=QEMU-usb-storage-BOT",
        "usbImage=$UsbFull",
        "usbImageBytes=$((Get-Item -LiteralPath $UsbFull).Length)",
        "usbImageSha256Before=$usbHashBefore",
        "usbImageAccess=read-only",
        "physicalHostDisksPassedToQemu=none",
        "bootloaderSha256=$bootHash",
        "kernelSha256=$kernelHash",
        "qemuCommandLine=$commandLine"
    ) | Set-Content -LiteralPath $ManifestPath -Encoding ascii

    $QemuProcess = Start-Process -FilePath $QemuFull -ArgumentList $arguments `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath
    $deadline = [DateTime]::UtcNow.AddSeconds(180)
    $Booted = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $SerialPath) {
            $serial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
            if ($serial -and $serial -match '\[KERNEL-FAULT\]') { throw "QEMU kernel fault; inspect $SerialPath" }
            if ($serial -and $serial.Contains("[KERNEL] Entering main loop")) {
                $Booted = $true
                $Passed = ($serial -match '\[USB-MSC\] registered') -and
                    ($serial -match '\[DM12-QEMU-USB\] proof=PASS read-only-mount=PASS file-read=PASS unmount=PASS')
                break
            }
        }
        if ($QemuProcess.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $Passed) {
        $serial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
        $usbHashAfter = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
        Add-Content -LiteralPath $ManifestPath -Encoding ascii -Value "usbImageSha256After=$usbHashAfter"
        if ($usbHashBefore -ne $usbHashAfter) { throw "Read-only USB backing image hash changed." }
        $lastLines = if ($serial) { (($serial -split "`r?`n") | Select-Object -Last 30) -join ' | ' } else { "(serial log is empty)" }
        $stage = if (-not $Booted) { "boot did not reach the main loop" } elseif ($serial -notmatch '\[USB-MSC\] registered') { "boot completed without USB MSC shared-block registration" } else { "USB FAT32 read-only mount proof did not pass" }
        throw "QEMU $stage. Serial: $SerialPath. Last lines: $lastLines"
    }
    Stop-UsbProbeQemu $QemuProcess $MonitorPort $SerialPath
    $QemuProcess = $null
    $usbHashAfter = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    if ($usbHashBefore -ne $usbHashAfter) { throw "Read-only USB backing image hash changed." }
    Add-Content -LiteralPath $ManifestPath -Encoding ascii -Value @(
        "serialLog=$SerialPath",
        "uhciTrace=$TracePath",
        "usbImageSha256After=$usbHashAfter",
        "result=PASS shared-block-registration=yes read-only-fat32-mount=yes deterministic-file-read=yes clean-unmount=yes full-boot=yes usb-image-unchanged=yes"
    )
    Write-Host "DM12 QEMU UHCI read-only USB proof passed. Evidence: $WorkFull"
} catch {
    Add-Content -LiteralPath $ManifestPath -Encoding ascii -Value @(
        "result=FAIL",
        "failure=$($_.Exception.Message -replace '[\r\n]+', ' ')"
    ) -ErrorAction SilentlyContinue
    throw
} finally {
    if ($QemuProcess) { Stop-UsbProbeQemu $QemuProcess $MonitorPort $SerialPath }
}
