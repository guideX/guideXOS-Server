# Quick Start Guide

## Building guideXOS

### One-Command Build

```powershell
.\build.ps1
```

This will:
1. ? Build the UEFI bootloader
2. ? Build the kernel (if MinGW installed)
3. ? Set up ESP directory
4. ? Copy all files to correct locations
5. ? Check prerequisites

### Build Options

```powershell
# Clean build (remove all build artifacts first)
.\build.ps1 -Clean

# Skip kernel build (bootloader only)
.\build.ps1 -SkipKernel

# Build and run immediately
.\build.ps1 -RunQemu

# Combine options
.\build.ps1 -Clean -RunQemu
```

## Running guideXOS

### Simple Run

```batch
run-qemu.bat
```

OR

```powershell
.\build.ps1 -RunQemu
```

### What You'll See

1. **TianoCore logo** (OVMF UEFI firmware)
2. **Bootloader messages** (guideXOS UEFI Bootloader)
3. **Boot splash** (animated progress bar)
4. **Kernel output** (if kernel present)

## Prerequisites

### Required

- **Visual Studio 2019+** (for bootloader)
  - Download: https://visualstudio.microsoft.com/

### Optional (for kernel)

- **MinGW-w64** (for kernel compilation)
  - Download: https://github.com/niXman/mingw-builds-binaries/releases
  - Get: `x86_64-*-release-posix-seh-ucrt-*.7z`
  - Extract to `C:\mingw64`
  - Add `C:\mingw64\bin` to PATH

### For Running

- **QEMU** (for testing)
  - Download: https://www.qemu.org/download/#windows
  - Add `C:\Program Files\qemu` to PATH

- **OVMF.fd** (UEFI firmware)
  - Download: https://github.com/tianocore/edk2/releases
  - Or run: `Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"`

## Quick Install Guide

### Install MinGW (for kernel building)

```powershell
# Download MinGW
Invoke-WebRequest -Uri "https://github.com/niXman/mingw-builds-binaries/releases/download/14.2.0-rt_v12-rev0/x86_64-14.2.0-release-posix-seh-ucrt-rt_v12-rev0.7z" -OutFile "mingw64.7z"

# Extract (requires 7-Zip)
7z x mingw64.7z -oC:\

# Add to PATH (for current session)
$env:PATH = "C:\mingw64\bin;$env:PATH"

# Add to PATH (permanent - run as admin)
[Environment]::SetEnvironmentVariable("Path", "$env:PATH;C:\mingw64\bin", "Machine")
```

### Install QEMU

```powershell
# Download QEMU installer
Invoke-WebRequest -Uri "https://qemu.weilnetz.de/w64/2024/qemu-w64-setup-20241217.exe" -OutFile "qemu-setup.exe"

# Run installer
.\qemu-setup.exe

# Add to PATH (after installation)
$env:PATH = "$env:PATH;C:\Program Files\qemu"
```

### Download OVMF

```powershell
# Download UEFI firmware
Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"
```

## Typical Workflow

### First Time Setup

```powershell
# 1. Install prerequisites (see above)

# 2. Download OVMF
Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"

# 3. Build everything
.\build.ps1

# 4. Run
.\run-qemu.bat
```

### Development Workflow

```powershell
# Make changes to code
code kernel\core\main.cpp

# Rebuild and test
.\build.ps1 -RunQemu
```

### Clean Rebuild

```powershell
# Clean everything and rebuild
.\build.ps1 -Clean -RunQemu
```

## Troubleshooting

### "MSBuild not found"

Install Visual Studio 2019 or later with C++ support.

### "GNU make not found"

Install MinGW-w64 (see above) or use WSL:

```powershell
# Install WSL
wsl --install

# Build kernel in WSL
wsl -e bash -c "cd kernel && make ARCH=amd64"
```

### "OVMF.fd not found"

```powershell
Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"
```

### "QEMU not found"

Install QEMU and add to PATH:

```powershell
$env:PATH = "$env:PATH;C:\Program Files\qemu"
```

Or use the full path:

```batch
"C:\Program Files\qemu\qemu-system-x86_64.exe" -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M
```

### "Build succeeded but no kernel in ESP"

This means MinGW is not installed. The bootloader is ready, but you need MinGW to build the kernel.

## File Structure

After building:

```
guideXOSServer/
??? build.ps1                    # Build script (use this)
??? run-qemu.bat                 # Run script (use this)
??? OVMF.fd                      # UEFI firmware (download once)
??? ESP/
?   ??? EFI/
?   ?   ??? BOOT/
?   ?       ??? BOOTX64.EFI      # Bootloader (auto-generated)
?   ??? kernel.elf               # Kernel (auto-generated if MinGW installed)
?   ??? ramdisk.img              # Empty for now
??? guideXOSBootLoader/
?   ??? x64/Release/
?       ??? guideXOSBootLoader.exe  # Bootloader source
??? kernel/
    ??? build/amd64/bin/
        ??? kernel.elf           # Kernel source (if built)
```

## Quick Commands

```powershell
# Build everything
.\build.ps1

# Build and run
.\build.ps1 -RunQemu

# Clean build
.\build.ps1 -Clean

# Just run (if already built)
.\run-qemu.bat

# Build bootloader only
.\build.ps1 -SkipKernel
```

## Expected Output

### Build Output

```
====================================
  guideXOS Complete Build System
====================================

[2/6] Building UEFI Bootloader...
      Building with Visual Studio...
      Bootloader built successfully

[3/6] Building Kernel (amd64)...
      Using: mingw32-make
      Kernel built successfully

[4/6] Setting up ESP directory...
      Copied: BOOTX64.EFI (67.5 KB)
      Copied: kernel.elf (123.4 KB)
      Created: ramdisk.img (1.0 MB, empty)
      ESP directory ready

[5/6] ESP Directory Structure:
...

====================================
  Build Complete!
====================================

? All prerequisites met!
```

### QEMU Output

```
====================================
  guideXOS UEFI Boot
====================================

Launching QEMU with UEFI firmware...

[You'll see TianoCore logo]
[Then bootloader messages]
[Then boot splash]
[Then kernel output]
```

## Tips

- **First build takes longer** - subsequent builds are faster
- **Use `-Clean` if builds fail** - removes old artifacts
- **QEMU output goes to console** - watch for kernel messages
- **Press Ctrl+C to exit QEMU** - or close the window

## Next Steps

After successfully building and running:

1. **Modify the kernel** - Make changes to `kernel/core/main.cpp`
2. **Rebuild and test** - Run `.\build.ps1 -RunQemu`
3. **See your changes** - Watch the output in QEMU

Happy developing! ??
