<#
.SYNOPSIS
    Runs the DM14 real-QEMU USB removal and replacement proof.
.DESCRIPTION
    QMP device_del/device_add commands hot-unplug and reinsert repository-owned
    disposable raw images on the same PIIX3 UHCI root-port bus. The script never
    passes through a host USB device or physical disk.
#>
[CmdletBinding()]
param(
    [string]$SourceImage = "out\dm13-qemu-usb-repeat-07-68m.raw",
    [string]$EspSource = "ESP",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$QemuAccelerator = "whpx",
    [string]$OvmfCode = "OVMF.fd",
    [string]$KernelPath = "kernel\build\amd64-dm14-proof\bin\kernel.elf",
    [string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $WorkDir) { $WorkDir = "out\dm14-qemu-hotplug-$stamp" }
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
$WorkFull = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
$SourceFull = [IO.Path]::GetFullPath((Join-Path $Root $SourceImage))
$EspFull = [IO.Path]::GetFullPath((Join-Path $Root $EspSource))
$QemuFull = (Resolve-Path -LiteralPath $QemuExecutable).Path
$OvmfFull = (Resolve-Path -LiteralPath $OvmfCode).Path
if (-not $WorkFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below repository out/." }
if (-not $SourceFull.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "SourceImage must be below repository out/." }
if (-not (Test-Path -LiteralPath $SourceFull -PathType Leaf)) { throw "Missing trusted DM13 source image: $SourceFull" }
if ((Get-Item -LiteralPath $SourceFull).Length -lt 68MB) { throw "Source image is too small for this FAT32 QEMU fixture." }
if (Test-Path -LiteralPath $WorkFull) {
    if ((Get-ChildItem -LiteralPath $WorkFull -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir must be empty; existing evidence is preserved."
    }
} else { New-Item -ItemType Directory -Path $WorkFull -Force | Out-Null }

$imageA = Join-Path $WorkFull "dm14-media-A.raw"
$imageB = Join-Path $WorkFull "dm14-media-B.raw"
Copy-Item -LiteralPath $SourceFull -Destination $imageA
Copy-Item -LiteralPath $SourceFull -Destination $imageB
$needle = [Text.Encoding]::ASCII.GetBytes("guideXOS DM13 USB lifecycle proof 001`r`n")
$replacement = [Text.Encoding]::ASCII.GetBytes("guideXOS DM14 USB revisionB proof 002`r`n")
if ($needle.Length -ne $replacement.Length) { throw "A/B file payloads must remain the same size." }
$bytesB = [IO.File]::ReadAllBytes($imageB)
$payloadText = [Text.Encoding]::ASCII.GetString($needle)
$payloadBytesText = [Text.Encoding]::GetEncoding(28591).GetString($bytesB)
$payloadOffset = $payloadBytesText.IndexOf($payloadText, [StringComparison]::Ordinal)
$secondPayloadOffset = if ($payloadOffset -ge 0) {
    $payloadBytesText.IndexOf($payloadText, $payloadOffset + 1,
        [StringComparison]::Ordinal)
} else { -1 }
if ($payloadOffset -lt 0 -or $secondPayloadOffset -ge 0) {
    throw "Expected one unique DM13 payload in B; first=$payloadOffset second=$secondPayloadOffset."
}
[Array]::Copy($replacement, 0, $bytesB, $payloadOffset, $replacement.Length)
[IO.File]::WriteAllBytes($imageB, $bytesB)
$payloadBytesText = $null
$payloadText = $null
$null = $bytesB
[GC]::Collect()

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw "Python is required to independently verify the repository-owned source image." }
$verify = Join-Path $Root "scripts\verify-dm13-qemu-usb-image.py"
$sourceCheck = & $python.Source $verify $SourceFull 2>&1
if ($LASTEXITCODE -ne 0) { throw "DM13 source fixture verification failed: $($sourceCheck -join ' ')" }
$hashAStart = (Get-FileHash -LiteralPath $imageA -Algorithm SHA256).Hash
$hashBStart = (Get-FileHash -LiteralPath $imageB -Algorithm SHA256).Hash
$lbaOffset = [long]90000 * 512
$sectorBeforeA = [IO.File]::OpenRead($imageA)
$sectorBeforeB = [IO.File]::OpenRead($imageB)
$sectorBufferA = [byte[]]::new(512); $sectorBufferB = [byte[]]::new(512)
[void]$sectorBeforeA.Seek($lbaOffset, [IO.SeekOrigin]::Begin)
[void]$sectorBeforeB.Seek($lbaOffset, [IO.SeekOrigin]::Begin)
[void]$sectorBeforeA.Read($sectorBufferA,0,512); [void]$sectorBeforeB.Read($sectorBufferB,0,512)
$sectorBeforeA.Dispose(); $sectorBeforeB.Dispose()
function Get-ByteArraySha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace("-","") }
    finally { $sha.Dispose() }
}
$sectorHashAStart = Get-ByteArraySha256 $sectorBufferA
$sectorHashBStart = Get-ByteArraySha256 $sectorBufferB

$kernel = [IO.Path]::GetFullPath((Join-Path $Root $KernelPath))
$bootloader = Join-Path $Root "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
if (-not (Test-Path -LiteralPath $kernel) -or -not (Test-Path -LiteralPath $bootloader)) {
    throw "Build the DM14 proof kernel and UEFI x64 Release bootloader first."
}
$espStage = Join-Path $WorkFull "esp"
New-Item -ItemType Directory -Path $espStage -Force | Out-Null
Get-ChildItem -LiteralPath $EspFull -Force | Copy-Item -Destination $espStage -Recurse -Force
$bootDir = Join-Path $espStage "EFI\BOOT"
New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
Copy-Item -LiteralPath $bootloader -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
Copy-Item -LiteralPath $kernel -Destination (Join-Path $espStage "kernel.elf") -Force

$serial = Join-Path $WorkFull "qemu-serial.log"
$stdout = Join-Path $WorkFull "qemu-stdout.log"
$stderr = Join-Path $WorkFull "qemu-stderr.log"
$qmpLog = Join-Path $WorkFull "qmp-transcript.jsonl"
$trace = Join-Path $WorkFull "usb-uhci.trace.log"
$events = Join-Path $WorkFull "usb-uhci.trace-events.txt"
$traceEvents = @("usb_uhci_schedule_start","usb_uhci_qh_load","usb_uhci_td_load",
  "usb_uhci_td_queue","usb_uhci_td_nextqh","usb_uhci_td_async",
  "usb_uhci_packet_add","usb_uhci_packet_link_async",
  "usb_uhci_packet_complete_success","usb_uhci_packet_complete_shortxfer",
  "usb_uhci_packet_complete_stall","usb_uhci_packet_complete_babble",
  "usb_uhci_packet_complete_error","usb_uhci_packet_cancel",
  "usb_packet_state_change","usb_packet_state_fault","usb_uhci_td_complete",
  "usb_msd_cmd_submit","usb_msd_data_in","usb_msd_data_out",
  "usb_msd_packet_async","usb_msd_packet_complete","usb_msd_cmd_complete",
  "usb_msd_send_status")
$traceEvents | Set-Content -LiteralPath $events -Encoding ascii
$manifest = Join-Path $WorkFull "manifest.txt"
$qemuVersion = (& $QemuFull --version | Select-Object -First 1)
$qemuHash = (Get-FileHash -LiteralPath $QemuFull -Algorithm SHA256).Hash
$ovmfHash = (Get-FileHash -LiteralPath $OvmfFull -Algorithm SHA256).Hash
$kernelHash = (Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash
$kernelBytes = (Get-Item -LiteralPath $kernel).Length
$bootloaderHash = (Get-FileHash -LiteralPath $bootloader -Algorithm SHA256).Hash
$qemuAtStart = @(Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'")
@("proof=DM14-QEMU-USB-HOTPLUG",
  "manifestSchema=DM21-TRANSPORT-1",
  "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
  "qemu=$qemuVersion", "qemuSha256=$qemuHash",
  "machine=pc,usb=off", "cpu=QEMU-default (no -cpu argument)",
  "controller=PIIX3-UHCI", "controllerArguments=-device piix3-usb-uhci,id=uhci",
  "accelerator=$QemuAccelerator", "rootPortBus=uhci.0",
  "deviceId=usbdisk", "sourceImage=$SourceFull",
  "sourceImageSha256=$((Get-FileHash -LiteralPath $SourceFull -Algorithm SHA256).Hash)",
  "mediaA=$imageA", "mediaASha256Before=$hashAStart",
  "mediaB=$imageB", "mediaBSha256Before=$hashBStart",
  "mediaBytes=$((Get-Item -LiteralPath $imageA).Length)",
  "storageImagePath=A:$imageA;B:$imageB",
  "storageImageBytes=$((Get-Item -LiteralPath $imageA).Length)",
  "storageImageSha256Before=A:$hashAStart;B:$hashBStart",
  "storageImageAccess=writable-disposable-only",
  "storageCacheMode=writeback (explicit on initial and replacement backends)",
  "uefiImagePath=$OvmfFull", "uefiSha256=$ovmfHash",
  "kernelBytes=$kernelBytes", "kernelSha256=$kernelHash",
  "bootloaderSha256=$bootloaderHash",
  "timeoutSerialMarkerSeconds=180", "timeoutQmpConnectSeconds=30",
  "timeoutQmpCommandSeconds=15", "timeoutDeviceDeletedSeconds=15",
  "hostQemuProcessesBefore=$($qemuAtStart.Count)",
  "hostQemuPidsBefore=$(($qemuAtStart | ForEach-Object { $_.ProcessId }) -join ',')",
  "sameCapacity=yes", "sameSectorSize=512", "sameDefaultVidPid=yes",
  "usbSerialDescriptor=not-specified-by-qemu-command-line",
  "mediaBPayloadOffset=$payloadOffset", "mediaBPayload=guideXOS DM14 USB revisionB proof 002\\r\\n",
  "mediaATestLba90000Sha256Before=$sectorHashAStart",
  "mediaBTestLba90000Sha256Before=$sectorHashBStart",
  "physicalHostUsbPassthrough=none", "physicalHostDiskPassthrough=none",
  "qemuTraceEnabled=yes", "qemuTraceEvents=$events", "qemuTrace=$trace",
  "evidenceDirectory=$WorkFull") | Set-Content -LiteralPath $manifest -Encoding ascii

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start(); $qmpPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port; $listener.Stop()
$arguments = @("-accel",$QemuAccelerator,
  "-drive","if=pflash,format=raw,readonly=on,file=$OvmfFull",
  "-machine","pc,usb=off","-device","piix3-usb-uhci,id=uhci",
  "-drive","file=fat:rw:$espStage,format=raw",
  "-drive","if=none,id=usbdataA,file=$imageA,format=raw,cache=writeback",
  "-device","usb-storage,id=usbdisk,bus=uhci.0,port=1,drive=usbdataA,removable=on",
  "-netdev","user,id=net0","-device","e1000,netdev=net0",
  "-object","rng-builtin,id=rng0",
  "-device","virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
  "-m","1024M","-vga","std","-display","none","-serial","file:$serial",
  "-trace","events=$events,file=$trace",
  "-qmp","tcp:127.0.0.1:$qmpPort,server=on,wait=off",
  "-rtc","base=utc,clock=host","-no-reboot")
$process = Start-Process -FilePath $QemuFull -ArgumentList $arguments -WorkingDirectory $Root `
    -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$processRow = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
    "qemuPid=$($process.Id)", "qmpPort=$qmpPort", "serialLog=$serial",
    "qmpLog=$qmpLog", "qemuCommandLine=$($processRow.CommandLine)",
    "qemu.otherProcessesAtStart=$(($qemuAtStart | Where-Object { $_.ProcessId -ne $process.Id }).Count)",
    "qemu.otherPidsAtStart=$(($qemuAtStart | Where-Object { $_.ProcessId -ne $process.Id } | ForEach-Object { $_.ProcessId }) -join ',')")

$script:QmpId = 0
$script:Run = $null
$hotplugFailed = $false
function Add-QmpEvidence([string]$Line) {
    Add-Content -LiteralPath $qmpLog -Encoding utf8 -Value $Line
}
function Read-QmpMessage($Run, [int]$TimeoutMilliseconds = 500) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $Run.Reader.ReadLine()
            if ($null -ne $line) { Add-QmpEvidence $line; return $line }
            throw "QMP socket closed."
        } catch [IO.IOException] {
            if ($Run.Process.HasExited) { throw "QEMU exited while reading QMP." }
        }
    }
    return $null
}
function Send-Qmp([string]$Execute, [hashtable]$Arguments = @{}) {
    $script:QmpId++
    $id = "dm14-$($script:QmpId)"
    $command = [ordered]@{ execute=$Execute; id=$id }
    if ($Arguments.Count) { $command.arguments=$Arguments }
    $json = $command | ConvertTo-Json -Compress -Depth 12
    Add-QmpEvidence $json
    $script:Run.Writer.WriteLine($json)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $line = Read-QmpMessage $script:Run 1000
        if ($null -eq $line) { continue }
        $message = $line | ConvertFrom-Json
        if ($message.event) { $script:Run.Events.Enqueue($message); continue }
        if ($message.id -eq $id) {
            if ($message.error) { throw "QMP $Execute failed: $($message.error | ConvertTo-Json -Compress)" }
            return $message.return
        }
    }
    throw "Timed out waiting for QMP command $Execute."
}
function Wait-QmpDeviceDeleted([string]$DeviceId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        while ($script:Run.Events.Count -gt 0) {
            $event = $script:Run.Events.Dequeue()
            if ($event.event -eq "DEVICE_DELETED" -and
                $event.data.device -eq $DeviceId) { return $event }
        }
        $line = Read-QmpMessage $script:Run 1000
        if ($null -eq $line) { continue }
        $message = $line | ConvertFrom-Json
        if ($message.event) {
            $script:Run.Events.Enqueue($message)
        } elseif ($message.error) {
            throw "QMP event read failed: $($message.error | ConvertTo-Json -Compress)"
        }
    }
    throw "Timed out waiting for DEVICE_DELETED for $DeviceId."
}
function Connect-Qmp {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $client = [Net.Sockets.TcpClient]::new()
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $client.Connect("127.0.0.1",$qmpPort); break }
        catch { if ($process.HasExited) { throw "QEMU exited before opening QMP." }; Start-Sleep -Milliseconds 100 }
    }
    if (-not $client.Connected) { throw "QMP did not listen before the bounded deadline." }
    $stream = $client.GetStream(); $stream.ReadTimeout = 1000
    $reader = [IO.StreamReader]::new($stream,[Text.Encoding]::ASCII,$false,4096,$true)
    $writer = [IO.StreamWriter]::new($stream,[Text.Encoding]::ASCII,4096,$true)
    $writer.AutoFlush = $true
    $script:Run = [pscustomobject]@{ Process=$process; Client=$client; Stream=$stream;
        Reader=$reader; Writer=$writer; Events=[Collections.Generic.Queue[object]]::new() }
    $greeting = Read-QmpMessage $script:Run 5000
    if (-not $greeting -or -not ($greeting | ConvertFrom-Json).QMP) { throw "Invalid QMP greeting." }
    [void](Send-Qmp "qmp_capabilities")
}
function Record-UsbTopology([string]$Stage) {
    $usb = Send-Qmp "human-monitor-command" @{ "command-line"="info usb" }
    $pci = Send-Qmp "query-pci"
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
        "topology.$Stage.infoUsb=$($usb -replace '[\r\n]+','; ')",
        "topology.$Stage.queryPci=$($pci | ConvertTo-Json -Compress -Depth 12)")
}
function Remove-Usb([string]$Stage) {
    [void](Send-Qmp "device_del" @{ id="usbdisk" })
    $event = Wait-QmpDeviceDeleted "usbdisk"
    Add-Content -LiteralPath $manifest -Encoding ascii -Value "qmp.$Stage.deviceDeleted=$($event | ConvertTo-Json -Compress -Depth 8)"
}
function Add-Usb([string]$Backend, [string]$Stage) {
    $imagePath = if ($Backend -eq "usbdataA") { $imageA } else { $imageB }
    $driveId = "dm14-$Stage"
    $driveLine = "drive_add 0 if=none,id=$driveId,file=$imagePath,format=raw,cache=writeback"
    $driveResult = Send-Qmp "human-monitor-command" @{ "command-line"=$driveLine }
    Add-Content -LiteralPath $manifest -Encoding ascii -Value "qmp.$Stage.driveAdd=$($driveResult -replace '[\r\n]+','; ')"
    if ($driveResult -match "Error:") { throw "QMP drive_add failed: $driveResult" }
    [void](Send-Qmp "device_add" @{ driver="usb-storage"; id="usbdisk";
        bus="uhci.0"; port="1"; drive=$driveId; removable=$true })
    Record-UsbTopology $Stage
}
function Wait-SerialMarker([string]$Marker, [int]$Occurrence, [int]$TimeoutSeconds = 180) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serial) {
            $text = Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
            if ($null -eq $text) { $text = "" }
            if ($text -match '\[KERNEL-FAULT\]' -or
                $text -match '\[DM14-QEMU-USB\] [^\r\n]*=FAIL') {
                throw "The guest reported a fault or failed DM14 check. Inspect $serial"
            }
            $count = [regex]::Matches($text,[regex]::Escape($Marker)).Count
            if ($count -ge $Occurrence) { return }
        }
        if ($process.HasExited) { throw "QEMU exited before '$Marker'." }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for serial marker '$Marker' occurrence $Occurrence."
}
function Run-RemovalAndReinsert([string]$Marker, [int]$Occurrence,
                                [string]$Backend, [string]$Stage) {
    Wait-SerialMarker $Marker $Occurrence
    Remove-Usb $Stage
    if ($Backend) { Add-Usb $Backend $Stage }
}
function Stop-Qemu {
    if (-not $process -or $process.HasExited) { return }
    $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if (-not $row -or $row.Name -ne "qemu-system-x86_64.exe" -or
        $row.CommandLine -notlike "*$serial*") {
        throw "Refusing to stop a QEMU process not owned by this proof."
    }
    try { [void](Send-Qmp "quit") } catch { }
    [void]$process.WaitForExit(10000)
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        [void]$process.WaitForExit(10000)
    }
}

try {
    Connect-Qmp
    Record-UsbTopology "initial"
    Run-RemovalAndReinsert "[DM14-QEMU] wait=idle-mounted-no-handles" 1 "usbdataA" "idle-mounted-unplug"
    Run-RemovalAndReinsert "[DM14-QEMU] wait=idle-open-handles" 1 "usbdataA" "same-media-reinsert"
    Run-RemovalAndReinsert "[DM14-QEMU] wait=replace-same-media-with-b" 1 "usbdataB" "different-media-replacement"
    Run-RemovalAndReinsert "[DM14-QEMU] wait=data-out-active" 1 "usbdataA" "vfs-write-interruption"
    Run-RemovalAndReinsert "[DM14-QEMU] wait=data-out-active" 2 "usbdataB" "multi-td-data-out-interruption"
    Run-RemovalAndReinsert "[DM14-QEMU] wait=sync-cache-before-csw" 1 $null "sync-cache-interruption"
    Wait-SerialMarker "[DM14-QEMU-USB] proof=END" 1 180
    Stop-Qemu
    $hashAEnd = (Get-FileHash -LiteralPath $imageA -Algorithm SHA256).Hash
    $hashBEnd = (Get-FileHash -LiteralPath $imageB -Algorithm SHA256).Hash
    $sectorAfterA = [IO.File]::OpenRead($imageA); $sectorAfterB = [IO.File]::OpenRead($imageB)
    $sectorBufferA = [byte[]]::new(512); $sectorBufferB = [byte[]]::new(512)
    [void]$sectorAfterA.Seek($lbaOffset,[IO.SeekOrigin]::Begin)
    [void]$sectorAfterB.Seek($lbaOffset,[IO.SeekOrigin]::Begin)
    [void]$sectorAfterA.Read($sectorBufferA,0,512); [void]$sectorAfterB.Read($sectorBufferB,0,512)
    $sectorAfterA.Dispose(); $sectorAfterB.Dispose()
    $sectorHashAEnd = Get-ByteArraySha256 $sectorBufferA
    $sectorHashBEnd = Get-ByteArraySha256 $sectorBufferB
    $serialText = Get-Content -LiteralPath $serial -Raw
    $failure = [regex]::Matches($serialText,'\[DM14-QEMU-USB\] [^\r\n]*=FAIL')
    $inspection = Join-Path $WorkFull "raw-image-inspection.txt"
    @("mediaA.sha256.before=$hashAStart", "mediaA.sha256.after=$hashAEnd",
      "mediaB.sha256.before=$hashBStart", "mediaB.sha256.after=$hashBEnd",
      "mediaA.lba90000.sha256.before=$sectorHashAStart",
      "mediaA.lba90000.sha256.after=$sectorHashAEnd",
      "mediaB.lba90000.sha256.before=$sectorHashBStart",
      "mediaB.lba90000.sha256.after=$sectorHashBEnd",
      "mediaA.lba90000.changed=$($sectorHashAStart -ne $sectorHashAEnd)",
      "mediaB.lba90000.changed=$($sectorHashBStart -ne $sectorHashBEnd)",
      "vfs-interruption-result=see-guest-marker-and-final-raw-image",
      "filesystem-consistency-required-after-interruption=no") |
        Set-Content -LiteralPath $inspection -Encoding ascii
    $verifyAStdout = Join-Path $WorkFull "dm13-verifier-A.stdout.log"
    $verifyAStderr = Join-Path $WorkFull "dm13-verifier-A.stderr.log"
    $verifyBStdout = Join-Path $WorkFull "dm13-verifier-B.stdout.log"
    $verifyBStderr = Join-Path $WorkFull "dm13-verifier-B.stderr.log"
    $verifyAProcess = Start-Process -FilePath $python.Source `
        -ArgumentList @($verify,$imageA) -WorkingDirectory $Root `
        -WindowStyle Hidden -PassThru -Wait `
        -RedirectStandardOutput $verifyAStdout -RedirectStandardError $verifyAStderr
    $verifyAExit = $verifyAProcess.ExitCode
    $verifyA = @((Get-Content -LiteralPath $verifyAStdout -ErrorAction SilentlyContinue) +
        (Get-Content -LiteralPath $verifyAStderr -ErrorAction SilentlyContinue))
    $verifyBProcess = Start-Process -FilePath $python.Source `
        -ArgumentList @($verify,$imageB) -WorkingDirectory $Root `
        -WindowStyle Hidden -PassThru -Wait `
        -RedirectStandardOutput $verifyBStdout -RedirectStandardError $verifyBStderr
    $verifyBExit = $verifyBProcess.ExitCode
    $verifyB = @((Get-Content -LiteralPath $verifyBStdout -ErrorAction SilentlyContinue) +
        (Get-Content -LiteralPath $verifyBStderr -ErrorAction SilentlyContinue))
    Add-Content -LiteralPath $inspection -Encoding ascii -Value @(
      "mediaA.dm13VerifierExit=$verifyAExit", "mediaA.dm13Verifier=$($verifyA -join '; ')",
      "mediaB.dm13VerifierExit=$verifyBExit", "mediaB.dm13Verifier=$($verifyB -join '; ')")
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
      "mediaASha256After=$hashAEnd", "mediaBSha256After=$hashBEnd",
      "rawInspection=$inspection", "dm14FailureMarkerCount=$($failure.Count)",
      "transportResult=PASS",
      "result=PASS-QEMU-SEQUENCE-AND-PROOF-COMPLETED")
    Write-Host "DM14 QEMU hotplug sequence completed. Evidence: $WorkFull"
} catch {
    $hotplugFailed = $true
    Stop-Qemu
    $hashAAtFailure = (Get-FileHash -LiteralPath $imageA -Algorithm SHA256).Hash
    $hashBAtFailure = (Get-FileHash -LiteralPath $imageB -Algorithm SHA256).Hash
    Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
        "transportResult=FAIL", "result=FAIL",
        "mediaASha256AtFailure=$hashAAtFailure",
        "mediaBSha256AtFailure=$hashBAtFailure",
        "failure=$($_.Exception.Message -replace '[\r\n]+',' ')")
    if (Test-Path -LiteralPath $serial) {
        Get-Content -LiteralPath $serial -Tail 80 | Set-Content -LiteralPath (Join-Path $WorkFull "failure-tail.txt") -Encoding utf8
    }
    throw
} finally {
    Stop-Qemu
    if ($script:Run) {
        try { $script:Run.Reader.Dispose(); $script:Run.Writer.Dispose(); $script:Run.Client.Dispose() } catch { }
    }
    if ($hotplugFailed -and (Test-Path -LiteralPath $manifest)) {
        try {
            $hashAAfterStop = (Get-FileHash -LiteralPath $imageA -Algorithm SHA256).Hash
            $hashBAfterStop = (Get-FileHash -LiteralPath $imageB -Algorithm SHA256).Hash
            Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
                "mediaASha256AfterQemuStop=$hashAAfterStop",
                "mediaBSha256AfterQemuStop=$hashBAfterStop")
            if ((Test-Path -LiteralPath $serial) -and (Test-Path -LiteralPath $trace)) {
                $correlation = Join-Path $WorkFull "failure-trace-correlation.json"
                $classifierLog = Join-Path $WorkFull "failure-trace-classifier.log"
                & $python.Source (Join-Path $Root "scripts\classify-dm20-usb-trace.py") `
                    --serial $serial --trace $trace --output $correlation *> $classifierLog
                Add-Content -LiteralPath $manifest -Encoding ascii -Value @(
                    "failureTraceCorrelation=$correlation",
                    "failureTraceCorrelationExitCode=$LASTEXITCODE")
            }
        } catch { }
    }
}
