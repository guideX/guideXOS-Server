<#
.SYNOPSIS
    Runs the current-source DM12 FAT32 read-only USB mount proof for DM30.
.DESCRIPTION
    Clones a repository-owned 600 MiB DM9 FAT32 fixture, attaches the clone as
    read-only USB Mass Storage, and verifies the guest mounts it read-only,
    reads the proof directory/file, and unmounts cleanly. The source image is
    never attached to QEMU and no host physical disk is passed through.
#>
[CmdletBinding()]
param(
    [string]$SourceImage = "out\dm30-regression-dm9-ata\secondary-600-MiB.raw",
    [string]$KernelImage = "out\dm30-evidence\kernel-dm12-readonly-mount.elf",
    [string]$EspSource = "ESP",
    [string]$BootloaderImage = "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe",
    [string]$QemuExecutable = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OvmfCode = "OVMF.fd",
    [string]$QemuAccelerator = "whpx",
    [string]$WorkDir = "out\dm30-regression-dm12-usb-readonly-mount",
    [int]$TimeoutSeconds = 240
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $Root
$repoOut = [IO.Path]::GetFullPath((Join-Path $Root "out"))
$work = [IO.Path]::GetFullPath((Join-Path $Root $WorkDir))
$source = [IO.Path]::GetFullPath((Join-Path $Root $SourceImage))
if (-not $work.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "WorkDir must be below repository out/." }
if (-not $source.StartsWith($repoOut + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw "SourceImage must be below repository out/." }
if ((Get-Item -LiteralPath $source).Length -ne 629145600) {
    throw "DM12 proof requires its verified 600 MiB GPT/FAT32 image."
}
if (Test-Path -LiteralPath $work) {
    if ((Get-ChildItem -LiteralPath $work -Force | Measure-Object).Count -ne 0) {
        throw "WorkDir must be empty; existing evidence is preserved."
    }
} else { New-Item -ItemType Directory -Path $work -Force | Out-Null }

$kernelPath = [IO.Path]::GetFullPath((Join-Path $Root $KernelImage))
$bootloaderPath = [IO.Path]::GetFullPath((Join-Path $Root $BootloaderImage))
$espPath = [IO.Path]::GetFullPath((Join-Path $Root $EspSource))
$qemuPath = (Resolve-Path -LiteralPath $QemuExecutable).Path
$ovmfPath = (Resolve-Path -LiteralPath $OvmfCode).Path
foreach ($required in @($kernelPath,$bootloaderPath,(Join-Path $espPath "ramdisk.img"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing required proof input: $required" }
}

$image = Join-Path $work "usb-readonly-reference.raw"
$esp = Join-Path $work "esp"
$serial = Join-Path $work "qemu-serial.log"
$stdout = Join-Path $work "qemu-stdout.log"
$stderr = Join-Path $work "qemu-stderr.log"
$manifest = Join-Path $work "manifest.txt"
Copy-Item -LiteralPath $source -Destination $image
$hashBefore = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
New-Item -ItemType Directory -Path $esp -Force | Out-Null
Get-ChildItem -LiteralPath $espPath -Force | Copy-Item -Destination $esp -Recurse -Force
$bootDir = Join-Path $esp "EFI\BOOT"
New-Item -ItemType Directory -Path $bootDir -Force | Out-Null
Copy-Item -LiteralPath $bootloaderPath -Destination (Join-Path $bootDir "BOOTX64.EFI") -Force
Copy-Item -LiteralPath $kernelPath -Destination (Join-Path $esp "kernel.elf") -Force

$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start(); $monitorPort = ([Net.IPEndPoint]$listener.LocalEndpoint).Port; $listener.Stop()
$qemuArgs = @(
    "-accel",$QemuAccelerator,
    "-drive","if=pflash,format=raw,readonly=on,file=$ovmfPath",
    "-machine","pc,usb=off","-device","piix3-usb-uhci,id=uhci",
    "-drive","file=fat:rw:$esp,format=raw",
    "-drive","if=none,id=usbdata,file=$image,format=raw,readonly=on",
    "-device","usb-storage,id=usbdisk,bus=uhci.0,drive=usbdata,removable=on",
    "-netdev","user,id=net0","-device","e1000,netdev=net0",
    "-object","rng-builtin,id=rng0",
    "-device","virtio-rng-pci,rng=rng0,disable-modern=on,max-bytes=1024,period=1000",
    "-m","1024M","-vga","std","-display","none",
    "-serial","file:$serial",
    "-monitor","tcp:127.0.0.1:$monitorPort,server,nowait",
    "-rtc","base=utc,clock=host","-no-reboot"
)
$qemuVersion = (& $qemuPath --version | Select-Object -First 1)
$proc = $null
$result = "FAIL"
$failureMessage = $null
try {
    $proc = Start-Process -FilePath $qemuPath -ArgumentList $qemuArgs -WorkingDirectory $Root `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $success = "[DM12-QEMU-USB] proof=PASS read-only-mount=PASS file-read=PASS unmount=PASS"
    $failureMarker = "[DM12-QEMU-USB] proof=FAIL"
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serial) {
            $serialStream = [IO.File]::Open($serial, [IO.FileMode]::Open,
                [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
            try {
                $serialReader = [IO.StreamReader]::new($serialStream)
                $contents = $serialReader.ReadToEnd()
                $serialReader.Dispose()
            } finally { $serialStream.Dispose() }
            if ($contents.Contains($success)) { $result = "PASS"; break }
            if ($contents.Contains($failureMarker)) { throw "Guest DM12 USB proof reported failure." }
        }
        if ($proc.HasExited) { throw "QEMU exited before the read-only mount proof completed ($($proc.ExitCode))." }
        Start-Sleep -Seconds 2
    }
    if ($result -ne "PASS") { throw "Timed out waiting for DM12 read-only mount proof." }
} catch {
    $failureMessage = $_.Exception.Message
} finally {
    if ($proc) {
        $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($proc.Id)"
        if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and $row.CommandLine -like "*$serial*") {
            if (-not $proc.HasExited) {
                try {
                    $client = [Net.Sockets.TcpClient]::new()
                    $client.Connect("127.0.0.1",$monitorPort)
                    $writer = [IO.StreamWriter]::new($client.GetStream())
                    $writer.AutoFlush = $true; $writer.WriteLine("quit")
                    $writer.Dispose(); $client.Dispose()
                    [void]$proc.WaitForExit(5000)
                } catch { }
            }
            $row = Get-CimInstance Win32_Process -Filter "ProcessId=$($proc.Id)"
            if ($row -and $row.Name -eq "qemu-system-x86_64.exe" -and $row.CommandLine -like "*$serial*") {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                [void]$proc.WaitForExit(10000)
            }
        }
    }
    $hashAfter = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
    $evidence = @(
        "proof=DM30-QEMU-USB-READONLY-MOUNT",
        "timestampUtc=$([DateTime]::UtcNow.ToString('o'))",
        "qemu=$qemuVersion",
        "controller=PIIX3-UHCI USB Mass Storage",
        "image=$image",
        "imageAccess=read-only",
        "imageSha256Before=$hashBefore",
        "imageSha256After=$hashAfter",
        "physicalHostDisksPassedToQemu=none",
        "physicalHostUsbPassthrough=none",
        "serialLog=$serial",
        "result=$result"
    )
    if ($failureMessage) { $evidence += "failure=$($failureMessage -replace '[\r\n]+',' ')" }
    Set-Content -LiteralPath $manifest -Value $evidence -Encoding ascii
}
if ($result -ne "PASS" -or $hashBefore -ne $hashAfter) {
    throw "DM30 USB read-only mount proof failed or the cloned reference changed. See $manifest"
}
Write-Host "DM30 USB read-only mount proof passed. Evidence: $work"
