<#
.SYNOPSIS
    Runs the DM13 USB write and full Disk Manager lifecycle proof twice.
.DESCRIPTION
    The script attaches one repository-owned blank raw image as writable USB
    storage, runs the production write and Disk Manager services, stops QEMU,
    boots the same image again, and requires an exact VFS persistence check.
    Only the disposable raw image is writable storage; no host physical disk is
    passed to QEMU.
#>
[CmdletBinding()]
param(
    [string]$EspSource = "ESP",
    [string]$UsbImage = "out\dm13-qemu-usb-lifecycle.raw",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$QemuAccelerator = "whpx",
    [string]$OvmfCode = "OVMF.fd",
    [string]$WorkDir = "",
    [string]$KernelImage = "kernel\build\amd64\bin\kernel.elf",
    [string]$BootloaderImage = "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe",
    [switch]$TraceUsb,
    [switch]$StopAfterInitialize
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $WorkDir) { $WorkDir = "out\dm13-qemu-usb-lifecycle-$stamp" }
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
$UsbFull = [IO.Path]::GetFullPath((Join-Path $Root $UsbImage))
$EspFull = [IO.Path]::GetFullPath((Join-Path $Root $EspSource))
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below repository out/." }
if (-not $UsbFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "USB image must be below repository out/." }
if (-not (Test-Path -LiteralPath $UsbFull -PathType Leaf)) { throw "Create a fresh blank raw image first: $UsbFull" }
if ((Get-Item -LiteralPath $UsbFull).Length -lt 68MB) { throw "Lifecycle image must be at least 68 MiB for the supported FAT32 cluster range." }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir must be empty; existing evidence is preserved."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

$kernelPath = if ([IO.Path]::IsPathRooted($KernelImage)) { $KernelImage } else { Join-Path $Root $KernelImage }
$bootloaderPath = if ([IO.Path]::IsPathRooted($BootloaderImage)) { $BootloaderImage } else { Join-Path $Root $BootloaderImage }
$kernel = [IO.Path]::GetFullPath($kernelPath)
$bootloader = [IO.Path]::GetFullPath($bootloaderPath)
if (-not (Test-Path -LiteralPath $kernel) -or -not (Test-Path -LiteralPath $bootloader)) {
    throw "Build the UEFI loader and DM13 proof kernel first."
}
$espStage = Join-Path $WorkFull "esp"
New-Item -ItemType Directory -Path $espStage -Force | Out-Null
Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $espStage -Recurse -Force
$bootDir = Join-Path $espStage "EFI\BOOT"
New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
Copy-Item -LiteralPath $bootloader -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
Copy-Item -LiteralPath $kernel -Destination (Join-Path $espStage "kernel.elf") -Force

$events = Join-Path $WorkFull "usb-uhci.trace-events.txt"
@("usb_uhci_schedule_start","usb_uhci_packet_add","usb_uhci_packet_complete_success",
  "usb_uhci_td_complete","usb_uhci_packet_complete_error","usb_packet_state_fault",
  "usb_msd_cmd_submit","usb_msd_data_in","usb_msd_data_out","usb_msd_packet_async",
  "usb_msd_packet_complete","usb_msd_cmd_complete","usb_msd_send_status") |
    Set-Content -LiteralPath $events -Encoding ascii
$manifest = Join-Path $WorkFull "manifest.txt"
$verify = Join-Path $Root "scripts\verify-dm13-qemu-usb-image.py"
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw "Python was not found for independent raw-image verification." }
$blankCheck = & $python.Source $verify --check-blank $UsbFull 2>&1
if ($LASTEXITCODE -ne 0) { throw "Refusing to attach a nonblank USB image: $($blankCheck -join ' ')" }
$imageBefore = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
$imageBytes = (Get-Item -LiteralPath $UsbFull).Length
$qemuVersion = (& $QemuFull --version | Select-Object -First 1)
$qemuHash = (Get-FileHash -LiteralPath $QemuFull -Algorithm SHA256).Hash
$bootloaderHash = (Get-FileHash -LiteralPath (Join-Path $bootDir "BOOTX64.EFI") -Algorithm SHA256).Hash
$kernelHash = (Get-FileHash -LiteralPath (Join-Path $espStage "kernel.elf") -Algorithm SHA256).Hash
$kernelBytes = (Get-Item -LiteralPath (Join-Path $espStage "kernel.elf")).Length
$uefiHash = (Get-FileHash -LiteralPath $OvmfFull -Algorithm SHA256).Hash
$qemuAtStart = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'")
@("manifestSchema=DM19-TRANSPORT-1","proof=DM13-QEMU-USB-DISK-MANAGER-LIFECYCLE-AND-RESTART",
  "proofMode=$(if ($StopAfterInitialize) { 'production-initialize-prefix' } else { 'full-lifecycle-and-restart' })",
  "timestampUtc=$([DateTime]::UtcNow.ToString('o'))","qemu=$qemuVersion","qemuSha256=$qemuHash",
  "machine=pc,usb=off","cpu=QEMU-default (no -cpu argument)",
  "controller=PIIX3-UHCI","controllerArguments=-device piix3-usb-uhci,id=uhci",
  "accelerator=$QemuAccelerator",
  "storageImagePath=$UsbFull","storageImageBytes=$imageBytes",
  "storageImageSha256Before=$imageBefore","blankImageVerification=$($blankCheck -join '; ')",
  "storageImageAccess=writable-disposable-only","storageCacheMode=QEMU default (cache option omitted from argv)",
  "uefiImagePath=$OvmfFull","uefiSha256=$uefiHash",
  "bootloaderSha256=$bootloaderHash","kernelBytes=$kernelBytes","kernelSha256=$kernelHash",
  "timeoutUhciBulkFrames=1000","timeoutHarnessSeconds=3600","timeoutRestartSeconds=300",
  "timeoutKernelMainLoopSeconds=300","qemuTraceEnabled=$(if ($TraceUsb) { 'yes' } else { 'no' })",
  "qemuTraceEvents=$events","qemuTraceFiles=first-boot.uhci.trace.log,restart-boot.uhci.trace.log",
  "hostQemuProcessesBefore=$($qemuAtStart.Count)",
  "hostQemuPidsBefore=$(($qemuAtStart | ForEach-Object { $_.ProcessId }) -join ',')",
  "monitorProtocol=HMP TCP loopback; runner sends quit only after proof or in cleanup",
  "commonWriteCallback=enabled-and-exercised-by-production-lifecycle-services",
  "lifecycle=initialize,create-partition,format-fat32,mount,file-write,read,unmount,remount,mount-write-read-unmount-stress-10x",
  "physicalHostDisksPassedToQemu=none","bootloaderSha256=$bootloaderHash",
  "kernelSha256=$kernelHash") | Set-Content -LiteralPath $manifest -Encoding ascii

function Start-Dm13Qemu([string]$RunName) {
    $serial = Join-Path $WorkFull "$RunName.serial.log"
    $stdout = Join-Path $WorkFull "$RunName.stdout.log"
    $stderr = Join-Path $WorkFull "$RunName.stderr.log"
    $trace = Join-Path $WorkFull "$RunName.uhci.trace.log"
    $listener = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(); $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port; $listener.Stop()
    $arguments = @("-accel",$QemuAccelerator,
      "-drive","if=pflash,format=raw,readonly=on,file=$OvmfFull",
      "-machine","pc,usb=off","-device","piix3-usb-uhci,id=uhci",
      "-drive","file=fat:rw:$espStage,format=raw",
      "-drive","if=none,id=usbdata,file=$UsbFull,format=raw",
      "-device","usb-storage,id=usbdisk,bus=uhci.0,drive=usbdata,removable=on,serial=DM13WR01",
      "-netdev","user,id=net0","-device","e1000,netdev=net0",
      "-object","rng-builtin,id=rng0",
      "-device","virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
      "-m","1024M","-vga","std","-display","none","-serial","file:$serial")
    if ($TraceUsb) { $arguments += @("-trace","events=$events,file=$trace") }
    $arguments += @("-monitor","tcp:127.0.0.1:$port,server,nowait",
      "-rtc","base=utc,clock=host","-no-reboot")
    $process = Start-Process -FilePath $QemuFull -ArgumentList $arguments -WorkingDirectory $Root `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    $otherQemu = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" |
        Where-Object { $_.ProcessId -ne $process.Id })
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "qemu.$RunName.pid=$($process.Id)","qemu.$RunName.monitorPort=$port",
      "qemu.$RunName.serial=$serial","qemu.$RunName.commandLine=$($row.CommandLine)",
      "qemu.$RunName.trace=$trace",
      "qemu.$RunName.otherQemuProcessesAtStart=$($otherQemu.Count)",
      "qemu.$RunName.otherQemuPidsAtStart=$(($otherQemu | ForEach-Object { $_.ProcessId }) -join ',')")
    return [pscustomobject]@{ Process=$process; Port=$port; Serial=$serial; Trace=$trace }
}

function Stop-Dm13Qemu($Run) {
    if (-not $Run -or -not $Run.Process) { return }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($Run.Process.Id)"
    if (-not $row) { return }
    if ($row.Name -ne "qemu-system-x86_64.exe" -or $row.CommandLine -notlike "*$($Run.Serial)*") {
        throw "Refusing to stop a QEMU process not owned by this proof."
    }
    if (-not $Run.Process.HasExited) {
        try {
            $client=[System.Net.Sockets.TcpClient]::new(); $client.Connect("127.0.0.1",$Run.Port)
            $stream=$client.GetStream(); $quit=[Text.Encoding]::ASCII.GetBytes("quit`n")
            $stream.Write($quit,0,$quit.Length); $stream.Dispose(); $client.Dispose()
        } catch { }
        [void]$Run.Process.WaitForExit(7000)
    }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($Run.Process.Id)"
    if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and $row.CommandLine -like "*$($Run.Serial)*") {
        Stop-Process -Id $Run.Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Run.Process.WaitForExit(10000)
    }
}

function Wait-Dm13Marker($Run, [string]$Marker, [int]$TimeoutSeconds,
                         [bool]$RequireMainLoop = $true) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Run.Serial) {
            $log = Get-Content -LiteralPath $Run.Serial -Raw -ErrorAction SilentlyContinue
            if ($log -match '\[KERNEL-FAULT\]') { throw "QEMU kernel fault in $($Run.Serial)" }
            if ($log -match '\[DM13-QEMU-USB\] (?:proof=FAIL|initialize=FAIL|create-partition=FAIL|format-fat32=FAIL|gpt-unchanged=FAIL|vfs-gpt-unchanged=FAIL|lifecycle=FAIL|restart-persistence=FAIL|destructive-lifecycle=BLOCKED)') {
                throw "QEMU proof reported a failure or blocker; inspect $($Run.Serial)"
            }
            if ($log -and $log.Contains($Marker) -and
                (-not $RequireMainLoop -or
                 $log.Contains("[KERNEL] Entering main loop"))) { return }
        }
        if ($Run.Process.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for '$Marker'. Serial output: $($Run.Serial)"
}

$first = $null; $second = $null
try {
    $first = Start-Dm13Qemu "first-boot"
    if ($StopAfterInitialize) {
        Wait-Dm13Marker $first "[DM13-QEMU-USB] initialize=PASS verified=PASS" 3600 $false
        Stop-Dm13Qemu $first; $first = $null
        $prefixImageHash = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
        Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
          "firstBootSerial=$($WorkFull)\first-boot.serial.log",
          "initializationPrefix=PASS qemuTraceEnabled=$(if ($TraceUsb) { 'yes' } else { 'no' })",
          "imageSha256AfterInitialize=$prefixImageHash",
          "transportResult=PASS",
          "result=PASS tier=production-usb-initialize-prefix qemuTraceFromStartup=$(if ($TraceUsb) { 'yes' } else { 'no' }) physicalHostDisks=none")
        Write-Host "DM13 USB production initialize-prefix proof passed. Evidence: $WorkFull"
        return
    }
    Wait-Dm13Marker $first "[DM13-QEMU-USB] lifecycle=PASS" 3600
    Stop-Dm13Qemu $first; $first = $null

    $second = Start-Dm13Qemu "restart-boot"
    Wait-Dm13Marker $second "[DM13-QEMU-USB] restart-persistence=PASS" 300
    Stop-Dm13Qemu $second; $second = $null

    $verification = & $python.Source $verify $UsbFull 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Independent verifier failed: $($verification -join ' ')" }
    $hashAfter = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "firstBootSerial=$($WorkFull)\first-boot.serial.log",
      "restartBootSerial=$($WorkFull)\restart-boot.serial.log",
      "storageImageSha256After=$hashAfter","independentVerifier=$($verification -join '; ')",
      "transportResult=PASS",
      "result=PASS private-write=yes shared-write=yes durability=trusted lifecycle=yes cold-restart-persistence=yes independent-image-verification=yes host-physical-media=none")
    Write-Host "DM13 USB lifecycle and restart proof passed. Evidence: $WorkFull"
} catch {
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @("transportResult=FAIL","result=FAIL","failure=$($_.Exception.Message -replace '[\r\n]+',' ')")
    throw
} finally {
    if ($first) { Stop-Dm13Qemu $first }
    if ($second) { Stop-Dm13Qemu $second }
}
