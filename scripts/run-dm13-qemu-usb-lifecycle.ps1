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
@("usb_uhci_schedule_start","usb_uhci_qh_load","usb_uhci_td_load",
  "usb_uhci_td_queue","usb_uhci_td_nextqh","usb_uhci_td_async",
  "usb_uhci_packet_add","usb_uhci_packet_link_async",
  "usb_uhci_packet_complete_success","usb_uhci_packet_complete_shortxfer",
  "usb_uhci_packet_complete_stall","usb_uhci_packet_complete_babble",
  "usb_uhci_packet_complete_error","usb_uhci_packet_cancel",
  "usb_packet_state_change","usb_packet_state_fault","usb_uhci_td_complete",
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
@("manifestSchema=DM21-TRANSPORT-1","proof=DM13-QEMU-USB-DISK-MANAGER-LIFECYCLE-AND-RESTART",
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
  "timeoutKernelMainLoopSeconds=300","qemuTraceEnabled=yes",
  "qemuTraceEvents=$events","qemuTraceFiles=first-boot.uhci.trace.log,restart-boot.uhci.trace.log",
  "hostQemuProcessesBefore=$($qemuAtStart.Count)",
  "hostQemuPidsBefore=$(($qemuAtStart | ForEach-Object { $_.ProcessId }) -join ',')",
  "controlProtocol=QMP TCP loopback; qmp transcript saved per boot; quit after proof or in cleanup",
  "commonWriteCallback=enabled-and-exercised-by-production-lifecycle-services",
  "lifecycle=initialize,create-partition,format-fat32,mount,file-write,read,unmount,remount,mount-write-read-unmount-stress-10x",
  "physicalHostDisksPassedToQemu=none","bootloaderSha256=$bootloaderHash",
  "kernelSha256=$kernelHash") | Set-Content -LiteralPath $manifest -Encoding ascii

function Start-Dm13Qemu([string]$RunName) {
    $serial = Join-Path $WorkFull "$RunName.serial.log"
    $stdout = Join-Path $WorkFull "$RunName.stdout.log"
    $stderr = Join-Path $WorkFull "$RunName.stderr.log"
    $trace = Join-Path $WorkFull "$RunName.uhci.trace.log"
    $qmpLog = Join-Path $WorkFull "$RunName.qmp.log"
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
    $arguments += @("-trace","events=$events,file=$trace",
      "-qmp","tcp:127.0.0.1:$port,server=on,wait=off",
      "-rtc","base=utc,clock=host","-no-reboot")
    $process = Start-Process -FilePath $QemuFull -ArgumentList $arguments -WorkingDirectory $Root `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    $otherQemu = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" |
        Where-Object { $_.ProcessId -ne $process.Id })
    $client = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not $client -and [DateTime]::UtcNow -lt $deadline) {
        $attempt = [System.Net.Sockets.TcpClient]::new()
        try { $attempt.Connect("127.0.0.1", $port); $client = $attempt }
        catch { $attempt.Dispose(); Start-Sleep -Milliseconds 100 }
    }
    if (-not $client -or -not $client.Connected) { throw "QEMU QMP did not open on port $port." }
    $stream = $client.GetStream(); $stream.ReadTimeout = 10000
    $encoding = [Text.UTF8Encoding]::new($false)
    $reader = [IO.StreamReader]::new($stream, $encoding, $false, 1024, $true)
    $writer = [IO.StreamWriter]::new($stream, $encoding, 1024, $true)
    $writer.AutoFlush = $true
    $run = [pscustomobject]@{ Name=$RunName; Process=$process; Port=$port;
        Serial=$serial; Stdout=$stdout; Stderr=$stderr; Trace=$trace;
        QmpLog=$qmpLog; QmpClient=$client; QmpReader=$reader; QmpWriter=$writer }
    $greeting = $reader.ReadLine()
    Add-Content -LiteralPath $qmpLog -Encoding utf8 -Value "RX $greeting"
    [void](Invoke-Dm13Qmp $run 'qmp_capabilities' 'caps')
    [void](Invoke-Dm13Qmp $run 'query-status' 'status')
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "qemu.$RunName.pid=$($process.Id)","qemu.$RunName.qmpPort=$port",
      "qemu.$RunName.serial=$serial","qemu.$RunName.commandLine=$($row.CommandLine)",
      "qemu.$RunName.trace=$trace","qemu.$RunName.qmpTranscript=$qmpLog",
      "qemu.$RunName.otherQemuProcessesAtStart=$($otherQemu.Count)",
      "qemu.$RunName.otherQemuPidsAtStart=$(($otherQemu | ForEach-Object { $_.ProcessId }) -join ',')")
    return $run
}

function Invoke-Dm13Qmp($Run, [string]$Command, [string]$Id) {
    if (-not $Run.QmpWriter -or -not $Run.QmpReader) { return }
    $request = '{"execute":"' + $Command + '","id":"' + $Id + '"}'
    Add-Content -LiteralPath $Run.QmpLog -Encoding utf8 -Value "TX $request"
    $Run.QmpWriter.WriteLine($request)
    while ($true) {
        $line = $Run.QmpReader.ReadLine()
        if ($null -eq $line) { throw "QMP closed while waiting for '$Command'." }
        Add-Content -LiteralPath $Run.QmpLog -Encoding utf8 -Value "RX $line"
        if ($line -match ('"id"\s*:\s*"' + [regex]::Escape($Id) + '"')) { return $line }
    }
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
            [void](Invoke-Dm13Qmp $Run 'quit' 'quit')
        } catch { }
        [void]$Run.Process.WaitForExit(7000)
    }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($Run.Process.Id)"
    if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and $row.CommandLine -like "*$($Run.Serial)*") {
        Stop-Process -Id $Run.Process.Id -Force -ErrorAction SilentlyContinue
        [void]$Run.Process.WaitForExit(10000)
    }
    if ($Run.QmpReader) { $Run.QmpReader.Dispose() }
    if ($Run.QmpWriter) { $Run.QmpWriter.Dispose() }
    if ($Run.QmpClient) { $Run.QmpClient.Dispose() }
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
        if ($Run.Process.HasExited) {
            $exitCode = $Run.Process.ExitCode
            throw "QEMU exited before '$Marker' with exit code $exitCode. Serial output: $($Run.Serial); stderr: $($Run.Stderr)"
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Timed out waiting for '$Marker'. Serial output: $($Run.Serial)"
}

$first = $null; $second = $null
$allRuns = [System.Collections.Generic.List[object]]::new()
$proofFailed = $false
try {
    $first = Start-Dm13Qemu "first-boot"
    $allRuns.Add($first)
    if ($StopAfterInitialize) {
        Wait-Dm13Marker $first "[DM13-QEMU-USB] initialize=PASS verified=PASS" 3600 $false
        Stop-Dm13Qemu $first; $first = $null
        $prefixImageHash = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
        Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
          "firstBootSerial=$($WorkFull)\first-boot.serial.log",
          "initializationPrefix=PASS qemuTraceEnabled=yes",
          "imageSha256AfterInitialize=$prefixImageHash",
          "transportResult=PASS",
          "result=PASS tier=production-usb-initialize-prefix qemuTraceFromStartup=yes physicalHostDisks=none")
        Write-Host "DM13 USB production initialize-prefix proof passed. Evidence: $WorkFull"
        return
    }
    Wait-Dm13Marker $first "[DM13-QEMU-USB] lifecycle=PASS" 3600
    Stop-Dm13Qemu $first; $first = $null
    $firstBootImageHash = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash

    $second = Start-Dm13Qemu "restart-boot"
    $allRuns.Add($second)
    Wait-Dm13Marker $second "[DM13-QEMU-USB] restart-persistence=PASS" 300
    Stop-Dm13Qemu $second; $second = $null

    $verification = & $python.Source $verify $UsbFull 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Independent verifier failed: $($verification -join ' ')" }
    $hashAfter = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "firstBootSerial=$($WorkFull)\first-boot.serial.log",
      "restartBootSerial=$($WorkFull)\restart-boot.serial.log",
      "imageSha256AfterFirstBoot=$firstBootImageHash",
      "storageImageSha256After=$hashAfter","finalImageHash=$hashAfter",
      "independentVerifier=$($verification -join '; ')",
      "transportResult=PASS",
      "result=PASS private-write=yes shared-write=yes durability=trusted lifecycle=yes cold-restart-persistence=yes independent-image-verification=yes host-physical-media=none")
    Write-Host "DM13 USB lifecycle and restart proof passed. Evidence: $WorkFull"
} catch {
    $proofFailed = $true
    $failureImageHash = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
    $failureRows = [System.Collections.Generic.List[string]]::new()
    $failureRows.Add("transportResult=FAIL")
    $failureRows.Add("result=FAIL")
    $failureRows.Add("imageSha256AtFailure=$failureImageHash")
    foreach ($run in $allRuns) {
        if ($run.Process.HasExited) {
            $failureRows.Add("qemu.$($run.Name).exitCode=$($run.Process.ExitCode)")
        } else {
            $failureRows.Add("qemu.$($run.Name).stillRunning=yes")
        }
    }
    $failureRows.Add("failure=$($_.Exception.Message -replace '[\r\n]+',' ')")
    Add-Content -LiteralPath $manifest -Encoding ascii -Value $failureRows
    throw
} finally {
    if ($first) { Stop-Dm13Qemu $first }
    if ($second) { Stop-Dm13Qemu $second }
    if ($proofFailed -and (Test-Path -LiteralPath $UsbFull)) {
        try {
            $finalFailureImageHash = (Get-FileHash -LiteralPath $UsbFull -Algorithm SHA256).Hash
            Add-Content -LiteralPath $manifest -Encoding ascii -Value "storageImageSha256AfterQemuStop=$finalFailureImageHash"
        } catch { }
    }
    if ($proofFailed) {
        foreach ($run in $allRuns) {
            if ((Test-Path -LiteralPath $run.Serial) -and
                (Test-Path -LiteralPath $run.Trace)) {
                $classification = [IO.Path]::ChangeExtension($run.Serial,
                    ".trace-correlation.json")
                $classifierLog = Join-Path $WorkFull `
                    "$([IO.Path]::GetFileNameWithoutExtension($run.Serial)).classifier.log"
                & $python.Source (Join-Path $Root "scripts\classify-dm20-usb-trace.py") `
                    --serial $run.Serial --trace $run.Trace --output $classification `
                    *> $classifierLog
                $classifierExitCode = $LASTEXITCODE
                Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
                    "failureTraceCorrelation=$classification",
                    "failureTraceCorrelationExitCode=$classifierExitCode")
            }
        }
    }
}
