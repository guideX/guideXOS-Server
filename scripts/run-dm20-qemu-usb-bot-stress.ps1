<#
.SYNOPSIS
    Runs the DM20 production-path USB BOT CSW stress proof in QEMU.
.DESCRIPTION
    Builds a fresh kernel unless -SkipBuild is selected, creates an 80 MiB
    disposable blank raw image, records startup USB traces, runs the guest
    stress marker, then requires byte-for-byte raw-image restoration. Evidence
    and the raw image are written to a unique folder below EvidenceRoot.
#>
[CmdletBinding()]
param(
    [string]$EvidenceRoot = "out\dm20-evidence",
    [string]$EspSource = "ESP",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$QemuAccelerator = "whpx",
    [string]$OvmfCode = "OVMF.fd",
    [string]$KernelImage = "",
    [string]$BootloaderImage = "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe",
    [int]$TimeoutFrames = 1000,
    [int]$LayoutPadBytes = 0,
    [int]$LayoutBufferOffsetBytes = 0,
    [int]$LayoutDescriptorPadBytes = 0,
    [string]$UsbSerial = "DM13WR01",
    [int]$CommandCount = 1000,
    [int]$ProofTimeoutSeconds = 1800,
    [switch]$SkipBuild,
    [switch]$NoTrace
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
if ($TimeoutFrames -lt 1 -or $TimeoutFrames -gt 65535) { throw "TimeoutFrames must be between 1 and 65535." }
if ($LayoutPadBytes -lt 0 -or $LayoutPadBytes -gt 1048576) { throw "LayoutPadBytes must be between 0 and 1048576." }
if (($LayoutPadBytes % 4096) -ne 0) { throw "LayoutPadBytes must be zero or a multiple of 4096." }
if ($LayoutBufferOffsetBytes -lt 0 -or $LayoutBufferOffsetBytes -gt 65535) { throw "LayoutBufferOffsetBytes must be between 0 and 65535." }
if ($LayoutDescriptorPadBytes -lt 0 -or $LayoutDescriptorPadBytes -gt 4080 -or (($LayoutDescriptorPadBytes % 16) -ne 0)) { throw "LayoutDescriptorPadBytes must be a 16-byte multiple between 0 and 4080." }
if ($CommandCount -lt 1 -or $CommandCount -gt 100000) { throw "CommandCount must be between 1 and 100000." }

$evidenceBase = if ([IO.Path]::IsPathRooted($EvidenceRoot)) {
    [IO.Path]::GetFullPath($EvidenceRoot)
} else { [IO.Path]::GetFullPath((Join-Path $Root $EvidenceRoot)) }
New-Item -ItemType Directory -Path $evidenceBase -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runName = "dm20-t$TimeoutFrames-p$LayoutPadBytes-c$CommandCount-$stamp"
$WorkFull = Join-Path $evidenceBase $runName
New-Item -ItemType Directory -Path $WorkFull -ErrorAction Stop | Out-Null
$UsbFull = Join-Path $WorkFull "blank-usb.raw"
$EspFull = if ([IO.Path]::IsPathRooted($EspSource)) { $EspSource } else { Join-Path $Root $EspSource }
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
$verify = Join-Path $Root "scripts\verify-dm13-qemu-usb-image.py"
$python = Get-Command python -ErrorAction Stop
$qemuMonitor = $null
$qemuProcess = $null
$manifest = Join-Path $WorkFull "manifest.txt"
$serial = Join-Path $WorkFull "guest.serial.log"
$trace = Join-Path $WorkFull "usb-uhci.trace.log"
$events = Join-Path $WorkFull "usb-uhci.trace-events.txt"

try {
    if (-not (Test-Path -LiteralPath (Join-Path $EspFull "ramdisk.img"))) {
        throw "ESP runtime files are missing: $EspFull"
    }

    $kernel = ""
    $buildLog = "not-built"
    $buildFlags = ""
    if ($SkipBuild) {
        if (-not $KernelImage) { throw "-KernelImage is required with -SkipBuild." }
        $kernel = if ([IO.Path]::IsPathRooted($KernelImage)) { $KernelImage } else { Join-Path $Root $KernelImage }
        $kernel = (Resolve-Path -LiteralPath $kernel).Path
    } else {
        $make = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
        if (-not $make) { $make = Get-Command make.exe -ErrorAction SilentlyContinue }
        if (-not $make) { throw "MinGW make was not found." }
        $buildDir = "build/dm20-$stamp"
        $buildFlags = "-DGXOS_DM20_USB_BOT_STRESS_PROOF -DGXOS_DM20_USB_DIAGNOSTICS -DGXOS_DM20_UHCI_BULK_TIMEOUT_FRAMES=$TimeoutFrames -DGXOS_DM20_LAYOUT_PAD_BYTES=$LayoutPadBytes -DGXOS_DM20_LAYOUT_BUFFER_OFFSET_BYTES=$LayoutBufferOffsetBytes -DGXOS_DM20_LAYOUT_DESCRIPTOR_PAD_BYTES=$LayoutDescriptorPadBytes -DGXOS_DM20_BOT_STRESS_COMMANDS=$CommandCount"
        $buildLog = Join-Path $WorkFull "kernel-build.log"
        & $make.Source -C (Join-Path $Root "kernel") ARCH=amd64 "BUILD_DIR=$buildDir" `
            "EXTRA_CFLAGS=$buildFlags" NVME_WRITE_PROVEN=0 NVME_FLUSH_PROVEN=0 -j1 `
            *> $buildLog
        if ($LASTEXITCODE -ne 0) {
            Get-Content -LiteralPath $buildLog -Tail 80
            throw "DM20 diagnostic kernel build failed; see $buildLog."
        }
        $kernel = Join-Path $Root "kernel\$buildDir\bin\kernel.elf"
    }

    $bootloaderPath = if ([IO.Path]::IsPathRooted($BootloaderImage)) {
        $BootloaderImage
    } else { Join-Path $Root $BootloaderImage }
    $bootloaderPath = (Resolve-Path -LiteralPath $bootloaderPath).Path
    if (-not (Test-Path -LiteralPath $kernel -PathType Leaf)) { throw "Kernel image is missing: $kernel" }

    $stream = [IO.File]::Open($UsbFull, [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.SetLength(80L * 1024L * 1024L) } finally { $stream.Dispose() }
    $blankCheck = & $python.Source $verify --check-blank $UsbFull 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Fresh raw image did not pass blank check: $($blankCheck -join ' ')" }

    $espStage = Join-Path $WorkFull "esp"
    New-Item -ItemType Directory -Path $espStage | Out-Null
    Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $espStage -Recurse -Force
    $bootDir = Join-Path $espStage "EFI\BOOT"
    New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
    Copy-Item -LiteralPath $bootloaderPath -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $kernel -Destination (Join-Path $espStage "kernel.elf") -Force

    @("usb_uhci_schedule_start","usb_uhci_packet_add","usb_uhci_packet_complete_success",
      "usb_uhci_td_complete","usb_uhci_packet_complete_error","usb_packet_state_fault",
      "usb_msd_cmd_submit","usb_msd_data_in","usb_msd_data_out","usb_msd_packet_async",
      "usb_msd_packet_complete","usb_msd_cmd_complete","usb_msd_send_status") |
        Set-Content -LiteralPath $events -Encoding ascii

    $imageBefore = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    $kernelStage = Join-Path $espStage "kernel.elf"
    $bootStage = Join-Path $bootDir "BOOTX64.EFI"
    $qemuVersion = (& $QemuFull --version | Select-Object -First 1)
    $qemuHash = (Get-FileHash -LiteralPath $QemuFull -Algorithm SHA256).Hash
    $kernelHash = (Get-FileHash -LiteralPath $kernelStage -Algorithm SHA256).Hash
    $bootHash = (Get-FileHash -LiteralPath $bootStage -Algorithm SHA256).Hash
    $ovmfHash = (Get-FileHash -LiteralPath $OvmfFull -Algorithm SHA256).Hash
    $head = (& git rev-parse HEAD).Trim()
    $dirty = (& git status --porcelain --untracked-files=no | Out-String).Trim()
    $monitorListener = [System.Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $monitorListener.Start()
    $port = ([Net.IPEndPoint]$monitorListener.LocalEndpoint).Port
    $monitorListener.Stop()
    @("manifestSchema=DM20-USB-BOT-1","proof=DM20-QEMU-PRODUCTION-UHCI-BOT-CSW-STRESS",
      "timestampUtc=$([DateTime]::UtcNow.ToString('o'))","sourceHead=$head",
      "trackedTreeDirty=$(if ($dirty) { 'yes' } else { 'no' })",
      "qemu=$qemuVersion","qemuSha256=$qemuHash","machine=pc,usb=off",
      "controller=PIIX3-UHCI","controllerArguments=-device piix3-usb-uhci,id=uhci",
      "accelerator=$QemuAccelerator","storageImagePath=$UsbFull","storageImageBytes=83886080",
      "storageImageSha256Before=$imageBefore","storageImageBlankVerification=$($blankCheck -join '; ')",
      "storageImageAccess=writable-disposable-only","storageCacheMode=QEMU default (cache option omitted)",
      "uefiImagePath=$OvmfFull","uefiSha256=$ovmfHash","bootloaderSha256=$bootHash",
      "kernelPath=$kernelStage","kernelBytes=$((Get-Item -LiteralPath $kernelStage).Length)",
      "kernelSha256=$kernelHash","kernelBuildLog=$buildLog","kernelBuildFlags=$buildFlags",
      "timeoutUhciBulkFrames=$TimeoutFrames","timeoutHarnessSeconds=$ProofTimeoutSeconds",
      "layoutPadBytes=$LayoutPadBytes","stressCommandCount=$CommandCount",
      "layoutBufferOffsetBytes=$LayoutBufferOffsetBytes","usbSerial=$UsbSerial",
      "layoutDescriptorPadBytes=$LayoutDescriptorPadBytes",
      "proofSequences=A,B,C,D,E;repeats=10;large-transfer=128-sectors",
      "qemuTraceEnabled=$(if ($NoTrace) { 'no' } else { 'yes' })","qemuTraceEvents=$events",
      "monitorProtocol=HMP TCP loopback; runner sends quit only after proof or cleanup",
      "hostPhysicalDisksPassedToQemu=none") | Set-Content -LiteralPath $manifest -Encoding ascii

    $arguments = @("-accel",$QemuAccelerator,
      "-drive","if=pflash,format=raw,readonly=on,file=$OvmfFull",
      "-machine","pc,usb=off","-device","piix3-usb-uhci,id=uhci",
      "-drive","file=fat:rw:$espStage,format=raw",
      "-drive","if=none,id=usbdata,file=$UsbFull,format=raw",
      "-device","usb-storage,id=usbdisk,bus=uhci.0,drive=usbdata,removable=on,serial=$UsbSerial",
      "-netdev","user,id=net0","-device","e1000,netdev=net0",
      "-object","rng-builtin,id=rng0",
      "-device","virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
      "-m","1024M","-vga","std","-display","none","-serial","file:$serial")
    if (-not $NoTrace) { $arguments += @("-trace","events=$events,file=$trace") }
    $arguments += @("-monitor","tcp:127.0.0.1:$port,server,nowait",
      "-rtc","base=utc,clock=host","-no-reboot")
    $stdout = Join-Path $WorkFull "qemu.stdout.log"
    $stderr = Join-Path $WorkFull "qemu.stderr.log"
    $qemuProcess = Start-Process -FilePath $QemuFull -ArgumentList $arguments `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $procRow = Get-CimInstance Win32_Process -Filter "ProcessId=$($qemuProcess.Id)"
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "qemuPid=$($qemuProcess.Id)","qemuMonitorPort=$port",
      "qemuCommandLine=$($procRow.CommandLine)","guestSerial=$serial","qemuTrace=$trace")

    $deadline = [DateTime]::UtcNow.AddSeconds($ProofTimeoutSeconds)
    $passed = $false
    $reportedCommandCount = $null
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serial) {
            $log = Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
            if ($log -match '\[KERNEL-FAULT\]') { throw "QEMU guest kernel fault: $serial" }
            if ($log -match '\[DM20-QEMU-USB-BOT\] stress=FAIL') { throw "DM20 guest proof failed: $serial" }
            if ($log -match '\[DM20-QEMU-USB-BOT\] stress=PASS commands=([0-9A-Fa-f]{8})') {
                $reportedCommandCount = [Convert]::ToUInt32($Matches[1], 16)
                if ($reportedCommandCount -ne $CommandCount) {
                    throw "The kernel proved $reportedCommandCount stress commands, but this run requested $CommandCount. Use a kernel built with the matching GXOS_DM20_BOT_STRESS_COMMANDS value."
                }
                $passed = $true
                break
            }
        }
        if ($qemuProcess.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $passed) { throw "Timed out or QEMU exited before the DM20 stress PASS marker. Serial: $serial" }

    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($qemuProcess.Id)"
    if (-not $row -or $row.Name -ne "qemu-system-x86_64.exe" -or
        $row.CommandLine -notlike "*$serial*") { throw "Refusing to stop a QEMU process not owned by this run." }
    if (-not $qemuProcess.HasExited) {
        try {
            $qemuMonitor = [System.Net.Sockets.TcpClient]::new()
            $qemuMonitor.Connect("127.0.0.1", $port)
            $stream = $qemuMonitor.GetStream()
            $quit = [Text.Encoding]::ASCII.GetBytes("quit`n")
            $stream.Write($quit, 0, $quit.Length)
            $stream.Dispose(); $qemuMonitor.Dispose(); $qemuMonitor = $null
        } catch { }
        [void]$qemuProcess.WaitForExit(7000)
    }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($qemuProcess.Id)"
    if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and $row.CommandLine -like "*$serial*") {
        Stop-Process -Id $qemuProcess.Id -Force
        [void]$qemuProcess.WaitForExit(10000)
    }
    $qemuProcess = $null

    $imageAfter = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    if ($imageAfter -ne $imageBefore) { throw "USB image SHA-256 changed after restore: before=$imageBefore after=$imageAfter" }
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "storageImageSha256After=$imageAfter","imageRestoredByteForByte=yes",
      "proofReportedCommandCount=$reportedCommandCount",
      "guestPassMarker=[DM20-QEMU-USB-BOT] stress=PASS commands=$('{0:X8}' -f $reportedCommandCount)","result=PASS")
    Write-Host "DM20 BOT stress passed. Evidence: $WorkFull"
} catch {
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "result=FAIL","failure=$($_.Exception.Message -replace '[\r\n]+',' ')")
    throw
} finally {
    if ($qemuMonitor) { $qemuMonitor.Dispose() }
    if ($qemuProcess -and -not $qemuProcess.HasExited) {
        $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($qemuProcess.Id)"
        if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and
            $row.CommandLine -like "*$serial*") {
            try {
                $client = [System.Net.Sockets.TcpClient]::new()
                $client.Connect("127.0.0.1", $port)
                $stream = $client.GetStream()
                $quit = [Text.Encoding]::ASCII.GetBytes("quit`n")
                $stream.Write($quit, 0, $quit.Length)
                $stream.Dispose(); $client.Dispose()
            } catch { }
            [void]$qemuProcess.WaitForExit(5000)
            $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($qemuProcess.Id)"
            if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and
                $row.CommandLine -like "*$serial*") {
                Stop-Process -Id $qemuProcess.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
