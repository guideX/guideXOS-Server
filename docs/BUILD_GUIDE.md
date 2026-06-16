# guideXOS Build Guide

This document explains the build system and scripts for guideXOS.

## Quick Reference

| Script | Purpose | When to Use |
|--------|---------|-------------|
| **`build.ps1`** | Complete build (Bootloader + Kernel + ESP setup) | **Main script - use this for full builds** |
| **`build-uefi.ps1`** | Same as build.ps1 (alias) | Alternative name for build.ps1 |
| **`build.bat`** | Build guideXOSServer.exe only (Windows CLI app) | Testing server locally without QEMU |
| **`kernel\build-x86.bat`** | Build 32-bit x86 kernel only | Legacy 32-bit builds |
| **`run-qemu.bat`** | Launch OS in QEMU | After building, to test |

---

## Recommended Workflow

### 1. Full Build (Most Common)

```powershell
cd D:\devgitlab\guideXOS\guideXOS.SERVER
powershell -ExecutionPolicy Bypass -File build.ps1
```

This will:
1. Build the UEFI Bootloader (`guideXOSBootLoader.vcxproj`)
2. Build the Kernel (requires MinGW or cross-compiler)
3. Set up the ESP folder with bootloader + kernel
4. Ready to run with QEMU

### 2. Clean Build (Fresh Start)

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
```

### 3. Build and Run Immediately

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -RunQemu
```

### 4. Skip Kernel (Bootloader Only)

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -SkipKernel
```

### 5. Run in QEMU (After Building)

```cmd
run-qemu.bat
```

---

## Build Requirements

| Component | Tool Required | Installation |
|-----------|--------------|--------------|
| Bootloader | Visual Studio 2022 | Install with C++ Desktop workload |
| Kernel | MinGW-w64 (64-bit) | See below |
| Server (.exe) | Visual Studio 2022 | Already installed |

### Installing MinGW for Kernel Builds

1. Download from: https://github.com/niXman/mingw-builds-binaries/releases
2. Get: `x86_64-*-release-posix-seh-ucrt-*.7z` (64-bit version, NOT i686)
3. Extract to `C:\mingw64`
4. Add `C:\mingw64\bin` to your system PATH

To verify installation:
```cmd
g++ --version
mingw32-make --version
```

---

## Project Structure

```
guideXOS.SERVER/
??? guideXOSBootLoader/       ? UEFI bootloader (.efi file)
?   ??? *.vcxproj             ? Built with Visual Studio
?
??? kernel/                   ? Bare-metal kernel
?   ??? core/                 ? Main kernel code (main.cpp, desktop.cpp, etc.)
?   ?   ??? include/          ? Kernel headers
?   ??? arch/                 ? Architecture-specific code
?   ?   ??? amd64/            ? 64-bit x86-64
?   ?   ??? x86/              ? 32-bit x86
?   ?   ??? arm/              ? ARM 32-bit
?   ?   ??? ia64/             ? Intel Itanium
?   ?   ??? sparc/            ? SPARC
?   ??? Makefile              ? Built with MinGW/GCC
?
??? ESP/                      ? EFI System Partition (created by build)
?   ??? EFI/BOOT/BOOTX64.EFI  ? Bootloader binary
?   ??? kernel.elf            ? Kernel binary
?
??? *.cpp/*.h                 ? guideXOSServer (Windows testing app)
?   ??? guideXOSServer.vcxproj ? Built with Visual Studio
?
??? build.ps1                 ? Main build script
??? run-qemu.bat              ? QEMU launcher
??? OVMF.fd                   ? UEFI firmware for QEMU
```

---

## Typical Development Cycle

1. **Make code changes** to kernel files (e.g., `kernel/core/desktop.cpp`)

2. **Rebuild**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File build.ps1
   ```

3. **Test in QEMU**:
   ```cmd
   run-qemu.bat
   ```

4. **Repeat**

---

## Building Individual Components

### Bootloader Only (Visual Studio)

```cmd
cd guideXOSBootLoader
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" guideXOSBootLoader.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Kernel Only (MinGW)

```cmd
cd kernel
mingw32-make ARCH=amd64
```

Or use the batch file:
```cmd
kernel\build-x86.bat
```

### Server EXE Only (Visual Studio)

```cmd
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" guideXOSServer.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Or with GCC:
```cmd
build.bat
```

---

## Troubleshooting

| Error | Solution |
|-------|----------|
| "MinGW not found" | Install MinGW-w64 64-bit and add `C:\mingw64\bin` to PATH |
| "MSBuild not found" | Install Visual Studio with C++ Desktop Development workload |
| "ESP directory not found" | Run `build.ps1` first to create it |
| "kernel.elf not found" | MinGW may not be installed; kernel build was skipped |
| "OVMF.fd not found" | Download UEFI firmware or let build.ps1 handle it |
| NuGet errors with vcxproj | Delete `obj/*.json`, `obj/*.props`, `obj/*.targets` files |

### Common Build Fixes

**Fix 1: Clean stale NuGet artifacts**
```powershell
Remove-Item obj\*.json, obj\*.props, obj\*.targets, obj\*.cache -Force -ErrorAction SilentlyContinue
```

**Fix 2: Full clean and rebuild**
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
```

---

## Architecture Support

The kernel supports multiple architectures:

| Architecture | Directory | Status |
|--------------|-----------|--------|
| AMD64 (x86-64) | `kernel/arch/amd64/` | Primary, fully supported |
| x86 (32-bit) | `kernel/arch/x86/` | Supported |
| ARM | `kernel/arch/arm/` | Experimental |
| IA64 (Itanium) | `kernel/arch/ia64/` | Experimental |
| SPARC | `kernel/arch/sparc/` | Experimental |

To build for a specific architecture:
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Arch amd64
```

---

## QEMU Options

The `run-qemu.bat` script launches QEMU with these settings:
- UEFI firmware (OVMF.fd)
- 1024MB RAM
- Serial output to console
- FAT filesystem from ESP/ directory

To modify QEMU settings, edit `run-qemu.bat`.

---

## Visual Studio Projects

| Project | File | Purpose |
|---------|------|---------|
| guideXOSServer | `guideXOSServer.vcxproj` | Windows testing application |
| guideXOSKernel | `guideXOSKernel.vcxproj` | Kernel (VS build, experimental) |
| guideXOSBootLoader | `guideXOSBootLoader/guideXOSBootLoader.vcxproj` | UEFI bootloader |

To open in Visual Studio:
```cmd
start guideXOSServer.sln
```

---

## Summary: Most Common Commands

```powershell
# Full build
powershell -ExecutionPolicy Bypass -File build.ps1

# Clean build  
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean

# Build and run
powershell -ExecutionPolicy Bypass -File build.ps1 -RunQemu

# Just run (after building)
run-qemu.bat
```

---

*Document created: March 2026*
*Copyright (c) 2024-2026 guideX*
