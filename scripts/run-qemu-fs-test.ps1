#
# run-qemu-fs-test.ps1
# Launches QEMU with test disk images attached for filesystem testing
#
# Usage: .\run-qemu-fs-test.ps1 [options]
#
# Options:
#   -Fat32Only    Only attach FAT32 disk
#   -Ext4Only     Only attach ext4 disk
#   -Debug        Enable QEMU debug output
#   -WaitGdb      Wait for GDB connection on port 1234
#   -Memory       Set memory size (default: 256M)
#
# Copyright (c) 2025 guideXOS Server
#

param(
    [switch]$Fat32Only,
    [switch]$Ext4Only,
    [switch]$Debug,
    [switch]$WaitGdb,
    [string]$Memory = "256M"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$DiskDir = Join-Path $ProjectDir "disks"
$KernelDir = Join-Path $ProjectDir "kernel\build"

# Determine which disks to use
$UseFat32 = -not $Ext4Only
$UseExt4 = -not $Fat32Only

function Find-Kernel {
    $candidates = @(
        (Join-Path $KernelDir "amd64\bin\kernel.elf"),
        (Join-Path $KernelDir "x86\bin\kernel.elf"),
        (Join-Path $ProjectDir "build\amd64\bin\kernel.elf"),
        (Join-Path $ProjectDir "build\x86\bin\kernel.elf"),
        (Join-Path $ProjectDir "kernel.elf")
    )
    
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    
    return $null
}

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

function Build-QemuCommand {
    param([string]$QemuPath, [string]$KernelPath)
    
    $args = @()
    
    # Basic options
    $args += "-m", $Memory
    $args += "-serial", "stdio"
    $args += "-no-reboot"
    $args += "-no-shutdown"
    
    # Kernel
    if ($KernelPath -and (Test-Path $KernelPath)) {
        $args += "-kernel", $KernelPath
    }
    
    # Disk images
    $driveIndex = 0
    
    if ($UseFat32) {
        $fat32Path = Join-Path $DiskDir "test-fat32.img"
        if (Test-Path $fat32Path) {
            $args += "-drive", "file=$fat32Path,format=raw,if=ide,index=$driveIndex"
            $driveIndex++
        }
    }
    
    if ($UseExt4) {
        $ext4Path = Join-Path $DiskDir "test-ext4.img"
        if (Test-Path $ext4Path) {
            $args += "-drive", "file=$ext4Path,format=raw,if=ide,index=$driveIndex"
            $driveIndex++
        }
    }
    
    # Debug options
    if ($Debug) {
        $args += "-d", "int,cpu_reset"
        $args += "-D", "qemu-debug.log"
    }
    
    # GDB support
    if ($WaitGdb) {
        $args += "-s", "-S"
        Write-Host "Waiting for GDB connection on localhost:1234..." -ForegroundColor Yellow
    }
    
    return $args
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
    
    # Check disk images
    if (-not (Test-DiskImages)) {
        exit 1
    }
    
    # Find kernel
    $kernel = Find-Kernel
    if (-not $kernel) {
        Write-Host "WARNING: Kernel not found. QEMU will start without a kernel." -ForegroundColor Yellow
        Write-Host "Build the kernel first: make amd64"
        Write-Host ""
        Write-Host "Continuing anyway (for disk inspection)..."
    } else {
        Write-Host "Using kernel: $kernel" -ForegroundColor Green
    }
    
    # Show disk info
    Write-Host ""
    Write-Host "Attached disks:"
    if ($UseFat32) {
        Write-Host "  IDE0: $DiskDir\test-fat32.img (FAT32)"
    }
    if ($UseExt4) {
        Write-Host "  IDE1: $DiskDir\test-ext4.img (ext4)"
    }
    Write-Host ""
    
    # Build QEMU command
    $qemuArgs = Build-QemuCommand -QemuPath $qemu -KernelPath $kernel
    
    Write-Host "Running: $qemu $($qemuArgs -join ' ')"
    Write-Host ""
    Write-Host "=============================================="
    Write-Host "Press Ctrl+A, X to exit QEMU"
    Write-Host "=============================================="
    Write-Host ""
    
    # Run QEMU
    & $qemu $qemuArgs
}

Main
