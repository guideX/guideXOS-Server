#
# guideXOS Complete Build Script
#
# Builds: Bootloader + Kernel + Sets up ESP
# Usage: .\build-uefi.ps1 [-Clean] [-SkipKernel] [-RunQemu]
#
# Copyright (c) 2024 guideX
#

param(
    [switch]$Clean,
    [switch]$SkipKernel,
    [switch]$RunQemu,
    [string]$Arch = "amd64"
)

$ErrorActionPreference = "Stop"

Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  guideXOS Complete Build System" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""

$RootDir = $PSScriptRoot
$ESPDir = Join-Path $RootDir "ESP"
$KernelDir = Join-Path $RootDir "kernel"
$BootloaderDir = Join-Path $RootDir "guideXOSBootLoader"

# Step 1: Clean if requested
if ($Clean) {
    Write-Host "[1/6] Cleaning build artifacts..." -ForegroundColor Yellow
    
    if (Test-Path $ESPDir) {
        Remove-Item -Recurse -Force $ESPDir
        Write-Host "      Removed ESP/" -ForegroundColor Gray
    }
    
    if (Test-Path (Join-Path $BootloaderDir "guideXOS.1fedf2ad")) {
        Remove-Item -Recurse -Force (Join-Path $BootloaderDir "guideXOS.1fedf2ad")
        Write-Host "      Removed bootloader build/" -ForegroundColor Gray
    }
    
    if (Test-Path (Join-Path $BootloaderDir "x64")) {
        Remove-Item -Recurse -Force (Join-Path $BootloaderDir "x64")
        Write-Host "      Removed bootloader output/" -ForegroundColor Gray
    }
    
    if (Test-Path (Join-Path $KernelDir "build")) {
        Remove-Item -Recurse -Force (Join-Path $KernelDir "build")
        Write-Host "      Removed kernel build/" -ForegroundColor Gray
    }
    
    Write-Host "      Clean complete" -ForegroundColor Green
    Write-Host ""
}

# Step 2: Build UEFI Bootloader
Write-Host "[2/6] Building UEFI Bootloader..." -ForegroundColor Yellow

# Check if bootloader exists
if (!(Test-Path $BootloaderDir)) {
    Write-Host "      ERROR: Bootloader directory not found at: $BootloaderDir" -ForegroundColor Red
    exit 1
}

# Try to build bootloader with MSBuild
$BootloaderProject = Join-Path $BootloaderDir "guideXOSBootLoader.vcxproj"
if (Test-Path $BootloaderProject) {
    Write-Host "      Building with Visual Studio..." -ForegroundColor Cyan
    
    # Find MSBuild
    $MSBuild = $null
    $VSWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if (Test-Path $VSWhere) {
        $VSPath = & $VSWhere -latest -property installationPath
        $MSBuild = Join-Path $VSPath "MSBuild\Current\Bin\MSBuild.exe"
    }
    
    if (!$MSBuild -or !(Test-Path $MSBuild)) {
        Write-Host "      ERROR: MSBuild not found. Please install Visual Studio 2019 or later." -ForegroundColor Red
        exit 1
    }
    
    # Build bootloader
    & $MSBuild $BootloaderProject /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m /nologo /verbosity:minimal
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "      ERROR: Bootloader build failed" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "      Bootloader built successfully" -ForegroundColor Green
} else {
    Write-Host "      ERROR: Bootloader project not found at: $BootloaderProject" -ForegroundColor Red
    exit 1
}

Write-Host ""

# Step 3: Build Kernel
if (!$SkipKernel) {
    Write-Host "[3/6] Building Kernel ($Arch)..." -ForegroundColor Yellow

    Push-Location $KernelDir

    # Check if make is available
    # Try to find GNU make (avoid Embarcadero make or other incompatible makes)
    $Make = $null

    # Try mingw32-make first (MinGW default)
    if (Get-Command mingw32-make -ErrorAction SilentlyContinue) {
        $Make = "mingw32-make"
    }
    # Try gmake (GNU make)
    elseif (Get-Command gmake -ErrorAction SilentlyContinue) {
        $Make = "gmake"
    }
    # Try make (but verify it's GNU make)
    elseif (Get-Command make -ErrorAction SilentlyContinue) {
        try {
            $makeVersion = & make --version 2>&1 | Select-Object -First 1
            if ($makeVersion -match "GNU Make") {
                $Make = "make"
            }
        } catch {
            # Not GNU make
        }
    }

    if (!$Make) {
        Write-Host "      WARNING: GNU make not found. Kernel build skipped." -ForegroundColor Yellow
        Write-Host ""
        Write-Host "      To build the kernel, install MinGW-w64:" -ForegroundColor Cyan
        Write-Host "        1. Download: https://github.com/niXman/mingw-builds-binaries/releases" -ForegroundColor White
        Write-Host "        2. Get: x86_64-*-release-posix-seh-ucrt-*.7z" -ForegroundColor White
        Write-Host "        3. Extract to C:\mingw64" -ForegroundColor White
        Write-Host "        4. Add C:\mingw64\bin to PATH" -ForegroundColor White
        Write-Host ""
        Write-Host "      Or use WSL:" -ForegroundColor Cyan
        Write-Host "        wsl --install" -ForegroundColor White
        Write-Host "        wsl -e bash -c 'cd kernel && make ARCH=$Arch'" -ForegroundColor White
        Write-Host ""
        Write-Host "      Continuing without kernel (bootloader is ready)..." -ForegroundColor Gray
        Write-Host ""
        Pop-Location
        
        # Don't fail - bootloader is working
    } else {
        Write-Host "      Using: $Make" -ForegroundColor Cyan

        # Build kernel
        & $Make ARCH=$Arch

        if ($LASTEXITCODE -ne 0) {
            Write-Host "      ERROR: Kernel build failed" -ForegroundColor Red
            Pop-Location
            exit 1
        }

        Write-Host "      Kernel built successfully" -ForegroundColor Green
        Pop-Location
        Write-Host ""
    }
} else {
    Write-Host "[3/6] Kernel build skipped (-SkipKernel)" -ForegroundColor Gray
    Write-Host ""
}

# Step 4: Set up ESP directory
Write-Host "[4/6] Setting up ESP directory..." -ForegroundColor Yellow

# Create ESP structure
$ESPEfiBootDir = Join-Path $ESPDir "EFI\BOOT"
if (!(Test-Path $ESPEfiBootDir)) {
    New-Item -ItemType Directory -Path $ESPEfiBootDir -Force | Out-Null
}

# Copy bootloader
$BootloaderBin = Join-Path $BootloaderDir "x64\Release\guideXOSBootLoader.exe"
if (Test-Path $BootloaderBin) {
    $TargetBootloader = Join-Path $ESPEfiBootDir "BOOTX64.EFI"
    Copy-Item $BootloaderBin $TargetBootloader -Force
    Write-Host "      Copied: BOOTX64.EFI ($(((Get-Item $TargetBootloader).Length / 1KB).ToString('0.0')) KB)" -ForegroundColor Cyan
} else {
    Write-Host "      ERROR: Bootloader binary not found at: $BootloaderBin" -ForegroundColor Red
    exit 1
}

# Copy kernel if it exists
$KernelBin = Join-Path $KernelDir "build\$Arch\bin\kernel.elf"
if (Test-Path $KernelBin) {
    $TargetKernel = Join-Path $ESPDir "kernel.elf"
    Copy-Item $KernelBin $TargetKernel -Force
    Write-Host "      Copied: kernel.elf ($(((Get-Item $TargetKernel).Length / 1KB).ToString('0.0')) KB)" -ForegroundColor Cyan
} else {
    Write-Host "      WARNING: Kernel binary not found at: $KernelBin" -ForegroundColor Yellow
    Write-Host "      ESP will boot but needs a kernel to run" -ForegroundColor Gray
}

# Create empty ramdisk if it doesn't exist
$Ramdisk = Join-Path $ESPDir "ramdisk.img"
if (!(Test-Path $Ramdisk)) {
    # Create a minimal ramdisk (1MB of zeros for now)
    $null = New-Object byte[] 1048576
    [System.IO.File]::WriteAllBytes($Ramdisk, $null)
    Write-Host "      Created: ramdisk.img (1.0 MB, empty)" -ForegroundColor Cyan
}

Write-Host "      ESP directory ready" -ForegroundColor Green
Write-Host ""

# Step 5: Display ESP structure
Write-Host "[5/6] ESP Directory Structure:" -ForegroundColor Yellow
Write-Host ""

function Show-Tree {
    param($Path, $Indent = "")
    
    $items = Get-ChildItem $Path | Sort-Object { $_.PSIsContainer }, Name
    
    foreach ($item in $items) {
        if ($item.PSIsContainer) {
            Write-Host "$Indent├── $($item.Name)/" -ForegroundColor Blue
            Show-Tree $item.FullName "$Indent│   "
        } else {
            $size = if ($item.Length -lt 1KB) {
                "$($item.Length) bytes"
            } elseif ($item.Length -lt 1MB) {
                "$([math]::Round($item.Length / 1KB, 1)) KB"
            } else {
                "$([math]::Round($item.Length / 1MB, 1)) MB"
            }
            Write-Host "$Indent├── $($item.Name) ($size)" -ForegroundColor Gray
        }
    }
}

Write-Host "ESP/" -ForegroundColor Blue
Show-Tree $ESPDir ""
Write-Host ""

# Step 6: Check prerequisites for running
Write-Host "[6/6] Checking QEMU prerequisites..." -ForegroundColor Yellow

$AllReady = $true

# Check OVMF.fd
$OVMF = Join-Path $RootDir "OVMF.fd"
if (!(Test-Path $OVMF)) {
    Write-Host "      ⚠ OVMF.fd not found" -ForegroundColor Yellow
    Write-Host "        Download from: https://github.com/tianocore/edk2/releases" -ForegroundColor Gray
    Write-Host "        Or run: Invoke-WebRequest -Uri 'https://github.com/kraxel/edk2/raw/binaries/OVMF.fd' -OutFile 'OVMF.fd'" -ForegroundColor Gray
    $AllReady = $false
} else {
    Write-Host "      ✓ OVMF.fd found" -ForegroundColor Green
}

# Check QEMU
$Qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (!$Qemu) {
    Write-Host "      ⚠ QEMU not found in PATH" -ForegroundColor Yellow
    Write-Host "        Download from: https://www.qemu.org/download/#windows" -ForegroundColor Gray
    Write-Host "        Add to PATH: C:\Program Files\qemu" -ForegroundColor Gray
    $AllReady = $false
} else {
    Write-Host "      ✓ QEMU found: $($Qemu.Source)" -ForegroundColor Green
}

# Check kernel
if (!(Test-Path (Join-Path $ESPDir "kernel.elf"))) {
    Write-Host "      ⚠ kernel.elf not in ESP" -ForegroundColor Yellow
    Write-Host "        Install MinGW and rebuild to create kernel" -ForegroundColor Gray
    $AllReady = $false
} else {
    Write-Host "      ✓ kernel.elf in ESP" -ForegroundColor Green
}

Write-Host ""

# Summary
Write-Host "====================================" -ForegroundColor Green
Write-Host "  Build Complete!" -ForegroundColor Green
Write-Host "====================================" -ForegroundColor Green
Write-Host ""

if ($AllReady) {
    Write-Host "✓ All prerequisites met!" -ForegroundColor Green
    Write-Host ""
    if ($RunQemu) {
        Write-Host "Launching QEMU..." -ForegroundColor Cyan
        Write-Host "NOTE: Press Ctrl+Alt+G to grab/release the mouse in the QEMU window" -ForegroundColor Yellow
        Write-Host ""
        
        $QemuArgs = @(
            "-bios", $OVMF,
            "-drive", "file=fat:rw:ESP,format=raw",
            "-m", "1024M",
            "-vga", "std",
            "-serial", "stdio",
            "-no-reboot",
            "-d", "guest_errors"
        )
        
        & qemu-system-x86_64 $QemuArgs
    } else {
        Write-Host "To run in QEMU:" -ForegroundColor Cyan
        Write-Host "  .\build-uefi.ps1 -RunQemu" -ForegroundColor White
        Write-Host "  OR" -ForegroundColor Gray
        Write-Host "  .\run-qemu.bat" -ForegroundColor White
    }
} else {
    Write-Host "⚠ Some prerequisites missing" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Current status:" -ForegroundColor Cyan
    Write-Host "  Bootloader: ✓ Built successfully" -ForegroundColor Green
    Write-Host "  Kernel: " -NoNewline
    if (Test-Path (Join-Path $ESPDir "kernel.elf")) {
        Write-Host "✓ Available" -ForegroundColor Green
    } else {
        Write-Host "⚠ Not built (install MinGW)" -ForegroundColor Yellow
    }
    Write-Host "  OVMF: " -NoNewline
    if (Test-Path $OVMF) {
        Write-Host "✓ Available" -ForegroundColor Green
    } else {
        Write-Host "⚠ Download needed" -ForegroundColor Yellow
    }
    Write-Host "  QEMU: " -NoNewline
    if ($Qemu) {
        Write-Host "✓ Installed" -ForegroundColor Green
    } else {
        Write-Host "⚠ Install needed" -ForegroundColor Yellow
    }
    Write-Host ""
    Write-Host "See instructions above to complete setup" -ForegroundColor Gray
}

Write-Host ""
