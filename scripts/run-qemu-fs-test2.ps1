# run-qemu-fs-test.ps1
# Launches QEMU with test disk images attached for filesystem testing

param(
    [switch]$Fat32Only,
    [switch]$Ext4Only,
    [switch]$Debug,
    [switch]$WaitGdb,
    [switch]$NoUsbTablet,
    [string]$Memory = "1024M"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$DiskDir = Join-Path $ProjectDir "disks"
$EspDir = Join-Path $ProjectDir "ESP"

$UseFat32 = -not $Ext4Only
$UseExt4 = -not $Fat32Only

function Find-Qemu {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) { return $qemu.Source }

    $locations = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )

    foreach ($loc in $locations) {
        if (Test-Path $loc) { return $loc }
    }

    return $null
}

function Find-OvmfFirmware {
    $result = @{ Code = $null; Vars = $null; SplitMode = $false }

    $localOvmf = Join-Path $ProjectDir "OVMF.fd"
    if (Test-Path $localOvmf) {
        $result.Code = $localOvmf
        return $result
    }

    $qemuShareCode = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    $qemuShareVars = "C:\Program Files\qemu\share\edk2-x86_64-vars.fd"

    if (Test-Path $qemuShareCode) {
        $result.Code = $qemuShareCode
        $result.SplitMode = $true

        $localVars = Join-Path $ProjectDir "OVMF_VARS.fd"
        if (-not (Test-Path $localVars)) {
            if (Test-Path $qemuShareVars) {
                Copy-Item $qemuShareVars $localVars
            } else {
                $bytes = New-Object byte[] 131072
                [System.IO.File]::WriteAllBytes($localVars, $bytes)
            }
        }

        $result.Vars = $localVars
        return $result
    }

    return $result
}

function Test-DiskImages {
    $missing = $false

    if ($UseFat32 -and -not (Test-Path (Join-Path $DiskDir "test-fat32.img"))) {
        Write-Host "ERROR: FAT32 test disk not found: $DiskDir\test-fat32.img" -ForegroundColor Red
        Write-Host "Run: .\scripts\create-test-disks.ps1"
        $missing = $true
    }

    if ($UseExt4 -and -not (Test-Path (Join-Path $DiskDir "test-ext4.img"))) {
        Write-Host "ERROR: ext4 test disk not found: $DiskDir\test-ext4.img" -ForegroundColor Red
        Write-Host "Run: .\scripts\create-test-disks.ps1"
        $missing = $true
    }

    return -not $missing
}

function Test-EspDirectory {
    if (-not (Test-Path $EspDir)) {
        Write-Host "ERROR: ESP directory not found: $EspDir" -ForegroundColor Red
        Write-Host "Run: .\build.ps1 to build the kernel and bootloader"
        return $false
    }

    $kernelPath = Join-Path $EspDir "kernel.elf"
    if (-not (Test-Path $kernelPath)) {
        Write-Host "WARNING: kernel.elf not found in ESP" -ForegroundColor Yellow
        Write-Host "The bootloader will run but may not have a kernel to boot."
    }

    return $true
}

function Main {
    Write-Host "=============================================="
    Write-Host "guideXOS Filesystem Test - QEMU Launcher"
    Write-Host "=============================================="
    Write-Host ""

    $qemu = Find-Qemu
    if (-not $qemu) {
        Write-Host "ERROR: QEMU not found" -ForegroundColor Red
        Write-Host "Install QEMU or add QEMU to your PATH."
        exit 1
    }

    Write-Host "Using QEMU: $qemu" -ForegroundColor Green

    $ovmf = Find-OvmfFirmware
    if (-not $ovmf.Code) {
        Write-Host "ERROR: UEFI firmware / OVMF not found" -ForegroundColor Red
        Write-Host "Place OVMF.fd in: $ProjectDir"
        exit 1
    }

    if ($ovmf.SplitMode) {
        Write-Host "Using UEFI: Split pflash code + vars" -ForegroundColor Green
    } else {
        Write-Host "Using UEFI: $($ovmf.Code)" -ForegroundColor Green
    }

    if (-not (Test-EspDirectory)) {
        exit 1
    }

    Write-Host "Using ESP: $EspDir" -ForegroundColor Green

    if (-not (Test-DiskImages)) {
        exit 1
    }

    Write-Host ""
    Write-Host "Attached test disks:" -ForegroundColor Cyan

    if ($UseFat32) {
        $fat32Path = Join-Path $DiskDir "test-fat32.img"
        Write-Host "  IDE primary slave index 1: $fat32Path FAT32"
    }

    if ($UseExt4) {
        $ext4Path = Join-Path $DiskDir "test-ext4.img"
        Write-Host "  IDE secondary master index 2: $ext4Path ext4"
    }

    Write-Host ""

    $qemuArgs = @()

    # Machine type - i440FX has native PIIX IDE, friendlier for ATA PIO testing
    $qemuArgs += "-machine", "pc"

    # UEFI firmware
    if ($ovmf.SplitMode) {
        $qemuArgs += "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$($ovmf.Code)"
        $qemuArgs += "-drive", "if=pflash,format=raw,unit=1,file=$($ovmf.Vars)"
    } else {
        $qemuArgs += "-drive", "if=pflash,format=raw,readonly=on,file=$($ovmf.Code)"
    }

    # ESP as FAT drive - primary master
    $qemuArgs += "-drive", "file=fat:rw:$EspDir,format=raw,if=ide,index=0"

    # Test disk images
    if ($UseFat32) {
        $fat32Path = Join-Path $DiskDir "test-fat32.img"
        $qemuArgs += "-drive", "file=$fat32Path,format=raw,if=ide,index=1,media=disk"
    }

    if ($UseExt4) {
        $ext4Path = Join-Path $DiskDir "test-ext4.img"
        $qemuArgs += "-drive", "file=$ext4Path,format=raw,if=ide,index=2,media=disk"
    }

    # Memory and display
    $qemuArgs += "-m", $Memory
    $qemuArgs += "-vga", "std"
    $qemuArgs += "-display", "gtk"

    # Mouse input fix:
    # usb-tablet gives QEMU absolute mouse positioning and usually prevents edge escape weirdness.
    # If this causes trouble, run: .\run-qemu-fs-test.ps1 -NoUsbTablet
    if (-not $NoUsbTablet) {
        $qemuArgs += "-usb"
        $qemuArgs += "-device", "usb-tablet"
        Write-Host "Mouse mode: USB tablet absolute pointer" -ForegroundColor Green
    } else {
        Write-Host "Mouse mode: default PS/2 relative pointer" -ForegroundColor Yellow
    }

    # VNC disabled by default because GTK + VNC can make input testing confusing.
    # Uncomment only if you actually need VNC.
    # $qemuArgs += "-vnc", ":0"

    $qemuArgs += "-serial", "stdio"
    $qemuArgs += "-no-reboot"

    # Network support
    $qemuArgs += "-netdev", "user,id=net0"
    $qemuArgs += "-device", "e1000,netdev=net0"

    if ($Debug) {
        $qemuArgs += "-d", "int,cpu_reset"
        $qemuArgs += "-D", "qemu-debug.log"
        Write-Host "Debug output will be written to qemu-debug.log" -ForegroundColor Yellow
    }

    if ($WaitGdb) {
        $qemuArgs += "-s", "-S"
        Write-Host "Waiting for GDB connection on localhost:1234..." -ForegroundColor Yellow
    }

    Write-Host "=============================================="
    Write-Host "Filesystem + Network Testing Quick Reference:" -ForegroundColor Cyan
    Write-Host "  Filesystem commands:"
    Write-Host "    vfstest"
    Write-Host "    vfsmount / 1"
    Write-Host "    vfsls"
    Write-Host "    vfscat /test.txt"
    Write-Host ""
    Write-Host "  Network commands:"
    Write-Host "    ifconfig"
    Write-Host "    ping 10.0.2.2"
    Write-Host "    ping 8.8.8.8"
    Write-Host ""
    Write-Host "  Mouse:"
    Write-Host "    Ctrl+Alt+G to grab/release if needed"
    Write-Host "    If mouse acts worse, run with -NoUsbTablet"
    Write-Host ""
    Write-Host "  Exit:"
    Write-Host "    Close QEMU window or Ctrl+C in terminal"
    Write-Host "=============================================="
    Write-Host ""

    Push-Location $ProjectDir

    try {
        & $qemu $qemuArgs
    } finally {
        Pop-Location
    }
}

Main