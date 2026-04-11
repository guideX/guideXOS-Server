#
# run-qemu-fs-test.ps1
# Launches QEMU with test disk images attached for filesystem testing
#
# This script boots guideXOS via UEFI (same as run-qemu.bat) but also
# attaches the test FAT32 and ext4 disk images for filesystem testing.
#
# Usage: .\run-qemu-fs-test.ps1 [options]
#
# Options:
#   -Fat32Only    Only attach FAT32 disk
#   -Ext4Only     Only attach ext4 disk
#   -Debug        Enable QEMU debug output
#   -WaitGdb      Wait for GDB connection on port 1234
#   -Memory       Set memory size (default: 1024M)
#
# Copyright (c) 2025 guideXOS Server
#

param(
    [switch]$Fat32Only,
    [switch]$Ext4Only,
    [switch]$Debug,
    [switch]$WaitGdb,
    [string]$Memory = "1024M"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$DiskDir = Join-Path $ProjectDir "disks"
$EspDir = Join-Path $ProjectDir "ESP"

# Determine which disks to use
$UseFat32 = -not $Ext4Only
$UseExt4 = -not $Fat32Only

function Find-Qemu {
    # Check if qemu is in PATH
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) {
        return $qemu.Source
    }
    
    # Check common installation locations
    $locations = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )
    
    foreach ($loc in $locations) {
        if (Test-Path $loc) {
            return $loc
        }
    }
    
    return $null
}

function Find-OvmfFirmware {
    # Returns hashtable with Code, Vars, and SplitMode
    $result = @{ Code = $null; Vars = $null; SplitMode = $false }
    
    # Try local combined OVMF.fd first (simplest)
    $localOvmf = Join-Path $ProjectDir "OVMF.fd"
    if (Test-Path $localOvmf) {
        $result.Code = $localOvmf
        return $result
    }
    
    # Try QEMU's built-in split images
    $qemuShareCode = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
    $qemuShareVars = "C:\Program Files\qemu\share\edk2-x86_64-vars.fd"
    
    if (Test-Path $qemuShareCode) {
        $result.Code = $qemuShareCode
        $result.SplitMode = $true
        
        # Check for local vars copy or create one
        $localVars = Join-Path $ProjectDir "OVMF_VARS.fd"
        if (-not (Test-Path $localVars)) {
            if (Test-Path $qemuShareVars) {
                Copy-Item $qemuShareVars $localVars
            } else {
                # Create empty 128KB vars file
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

# Main
function Main {
    Write-Host "=============================================="
    Write-Host "guideXOS Filesystem Test - QEMU Launcher"
    Write-Host "=============================================="
    Write-Host ""
    
    # Find QEMU
    $qemu = Find-Qemu
    if (-not $qemu) {
        Write-Host "ERROR: QEMU not found" -ForegroundColor Red
        Write-Host "Install QEMU from: https://www.qemu.org/download/#windows"
        Write-Host "Or add QEMU to your PATH environment variable."
        exit 1
    }
    Write-Host "Using QEMU: $qemu" -ForegroundColor Green
    
    # Find UEFI firmware
    $ovmf = Find-OvmfFirmware
    if (-not $ovmf.Code) {
        Write-Host "ERROR: UEFI firmware (OVMF) not found" -ForegroundColor Red
        Write-Host "Download OVMF.fd from: https://github.com/tianocore/edk2/releases"
        Write-Host "Or place OVMF.fd in: $ProjectDir"
        exit 1
    }
    if ($ovmf.SplitMode) {
        Write-Host "Using UEFI: Split pflash (code + vars)" -ForegroundColor Green
    } else {
        Write-Host "Using UEFI: $($ovmf.Code)" -ForegroundColor Green
    }
    
    # Check ESP directory
    if (-not (Test-EspDirectory)) {
        exit 1
    }
    Write-Host "Using ESP: $EspDir" -ForegroundColor Green
    
    # Check disk images
    if (-not (Test-DiskImages)) {
        exit 1
    }
    
    # Show disk info
    Write-Host ""
    Write-Host "Attached test disks:" -ForegroundColor Cyan
    if ($UseFat32) {
        $fat32Path = Join-Path $DiskDir "test-fat32.img"
        Write-Host "  IDE index 2 (secondary master): $fat32Path (FAT32)"
        Write-Host "    -> Will appear as block device 1 (ata1m) after ram0"
    }
    if ($UseExt4) {
        $ext4Path = Join-Path $DiskDir "test-ext4.img"
        Write-Host "  IDE index 3 (secondary slave): $ext4Path (ext4)"
        Write-Host "    -> Will appear as block device 2 (ata1s)"
    }
    Write-Host ""
    
    # Build QEMU arguments
    $qemuArgs = @()
    
    # Machine type - Q35 for UEFI support
    # We'll add an explicit PIIX IDE controller for the test disks
    $qemuArgs += "-machine", "q35"
    
    # UEFI firmware
    if ($ovmf.SplitMode) {
        $qemuArgs += "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$($ovmf.Code)"
        $qemuArgs += "-drive", "if=pflash,format=raw,unit=1,file=$($ovmf.Vars)"
    } else {
        $qemuArgs += "-drive", "if=pflash,format=raw,readonly=on,file=$($ovmf.Code)"
    }
    
    # ESP as FAT drive (bootloader + kernel)
    $qemuArgs += "-drive", "file=fat:rw:$EspDir,format=raw"
    
    # Add an explicit ISA IDE controller for test disks
    # Q35 doesn't have legacy IDE by default, so we add a piix3-ide on the ISA bus
    $qemuArgs += "-device", "piix3-ide,id=ide"
    
    # Test disk images attached as IDE drives (compatible with legacy ATA driver)
    # Note: AHCI support is not yet implemented in the kernel, so we use IDE mode
    if ($UseFat32) {
        $fat32Path = Join-Path $DiskDir "test-fat32.img"
        # Attach to the IDE controller we just added
        $qemuArgs += "-drive", "file=$fat32Path,format=raw,if=none,id=fat32disk"
        $qemuArgs += "-device", "ide-hd,drive=fat32disk,bus=ide.0"
    }
    
    if ($UseExt4) {
        $ext4Path = Join-Path $DiskDir "test-ext4.img"
        $qemuArgs += "-drive", "file=$ext4Path,format=raw,if=none,id=ext4disk"
        $qemuArgs += "-device", "ide-hd,drive=ext4disk,bus=ide.1"
    }
    
    # Memory and display
    $qemuArgs += "-m", $Memory
    $qemuArgs += "-vga", "std"
    $qemuArgs += "-display", "gtk"
    $qemuArgs += "-vnc", ":0"
    $qemuArgs += "-serial", "stdio"
    $qemuArgs += "-no-reboot"
    
    # Debug options
    if ($Debug) {
        $qemuArgs += "-d", "int,cpu_reset"
        $qemuArgs += "-D", "qemu-debug.log"
        Write-Host "Debug output will be written to qemu-debug.log" -ForegroundColor Yellow
    }
    
    # GDB support
    if ($WaitGdb) {
        $qemuArgs += "-s", "-S"
        Write-Host "Waiting for GDB connection on localhost:1234..." -ForegroundColor Yellow
    }
    
    Write-Host "=============================================="
    Write-Host "Filesystem Testing Quick Reference:" -ForegroundColor Cyan
    Write-Host "  The test disks are attached as IDE drives."
    Write-Host "  In the shell, use these commands:" -ForegroundColor White
    Write-Host "    vfstest        - Run filesystem diagnostics"
    Write-Host "    vfsmount / 1   - Mount FAT32 disk (device 1) at /"
    Write-Host "    vfsls          - List files in mounted filesystem"
    Write-Host "    vfscat /test.txt - Read a file"
    Write-Host ""
    Write-Host "  Mouse: Ctrl+Alt+G to grab/release"
    Write-Host "  Exit:  Close window or Ctrl+C in terminal"
    Write-Host "=============================================="
    Write-Host ""
    
    
    # Change to project directory for relative paths
    Push-Location $ProjectDir
    
    try {
        # Run QEMU
        & $qemu $qemuArgs
    } finally {
        Pop-Location
    }
}

Main
