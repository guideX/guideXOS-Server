# guideXOS UEFI Boot Integration

This document describes how to build and run guideXOS with UEFI boot support.

## Overview

guideXOS now supports **dual boot modes**:

1. **UEFI Boot** (Recommended) - via `guideXOSBootLoader`
2. **Legacy BIOS Boot** (Fallback) - via Multiboot

The kernel automatically detects which mode it was booted in and adapts accordingly.

## Boot Flow

### UEFI Boot Flow
```
UEFI Firmware (OVMF)
    ?
ESP/EFI/BOOT/BOOTX64.EFI (guideXOSBootLoader)
    ? (Creates BootInfo structure)
ESP/kernel.elf (guideXOS Kernel)
    ? (Detects BootInfo, initializes from it)
Graphics Mode Boot Splash
    ? (TODO: Load from ramdisk)
guideXOSServer (User-mode init process)
```

### Legacy BIOS Boot Flow
```
BIOS/SeaBIOS
    ? (Multiboot)
kernel.elf (guideXOS Kernel)
    ? (Detects Multiboot, initializes from it)
Graphics/Text Mode
    ? (TODO: Load from ramdisk)
guideXOSServer (User-mode init process)
```

## Prerequisites

### Windows

1. **Visual Studio 2019 or later** - For building UEFI bootloader
2. **MinGW-w64 or WSL** - For building kernel
3. **QEMU** - For testing
   - Download from: https://www.qemu.org/download/
4. **OVMF.fd** - UEFI firmware for QEMU
   - Download from: https://github.com/tianocore/edk2/releases
   - Place in repository root

### Linux

1. **gcc/g++** - For building kernel
2. **make** - Build system
3. **QEMU** - For testing
   ```bash
   sudo apt-get install qemu-system-x86
   ```
4. **OVMF** - UEFI firmware
   ```bash
   sudo apt-get install ovmf
   cp /usr/share/ovmf/OVMF.fd .
   ```

## Building

### Option 1: PowerShell Build Script (Windows)

Build everything (bootloader + kernel) and set up ESP:

```powershell
# Full build
.\build-uefi.ps1

# Clean build
.\build-uefi.ps1 -Clean

# Build for specific architecture
.\build-uefi.ps1 -Arch amd64

# Build and run in QEMU
.\build-uefi.ps1 -RunQemu
```

This will:
1. Build the UEFI bootloader (`guideXOSBootLoader`)
2. Build the kernel
3. Create the ESP directory structure
4. Copy files to ESP/

### Option 2: Manual Build

#### Step 1: Build UEFI Bootloader

```powershell
# Using Visual Studio
cd guideXOSBootLoader
msbuild guideXOSBootLoader.vcxproj /p:Configuration=Release /p:Platform=x64
```

#### Step 2: Build Kernel

```bash
cd kernel
make ARCH=amd64  # For UEFI (64-bit)
# or
make ARCH=x86    # For legacy BIOS (32-bit)
```

#### Step 3: Set Up ESP Directory

```powershell
# Create ESP structure
mkdir ESP\EFI\BOOT

# Copy bootloader
copy guideXOSBootLoader\x64\Release\guideXOSBootLoader.efi ESP\EFI\BOOT\BOOTX64.EFI

# Copy kernel
copy kernel\build\amd64\bin\kernel.elf ESP\kernel.elf

# Create empty ramdisk (optional)
fsutil file createnew ESP\ramdisk.img 1048576
```

## Running

### UEFI Boot (Recommended)

#### Option 1: Using Build Script

```powershell
.\build-uefi.ps1 -RunQemu
```

#### Option 2: Using Run Script

**Windows:**
```powershell
powershell .\run-uefi.sh
```

**Linux:**
```bash
./run-uefi.sh
```

#### Option 3: Manual QEMU Command

```bash
qemu-system-x86_64 \
    -bios OVMF.fd \
    -drive file=fat:rw:ESP,format=raw \
    -m 1024M \
    -serial stdio
```

### Legacy BIOS Boot (Fallback)

```bash
# Using script
./scripts/run-qemu-x86.sh

# Or manually
qemu-system-i386 \
    -kernel kernel/build/x86/bin/kernel.elf \
    -m 128M \
    -serial stdio
```

### Switching Between Modes

The run script supports both modes:

```bash
# Legacy BIOS boot
./scripts/run-qemu-x86.sh

# UEFI boot
./scripts/run-qemu-x86.sh uefi
```

## ESP Directory Structure

After building, you should have:

```
ESP/
??? EFI/
?   ??? BOOT/
?       ??? BOOTX64.EFI       # UEFI bootloader
??? kernel.elf                 # Kernel binary
??? ramdisk.img                # Initial ramdisk (optional)
```

## BootInfo Structure

The UEFI bootloader provides a firmware-neutral `BootInfo` structure to the kernel:

```cpp
namespace guideXOS {
    struct BootInfo {
        uint32_t Magic;                // 0x49425847 ('GXBI')
        uint16_t Version;              // 1
        uint32_t Flags;                // Feature flags
        
        // Memory map
        uint64_t MemoryMap;
        uint64_t MemoryMapEntryCount;
        uint64_t MemoryMapDescriptorSize;
        
        // Framebuffer
        uint64_t FramebufferBase;
        uint32_t FramebufferWidth;
        uint32_t FramebufferHeight;
        uint32_t FramebufferPitch;
        FramebufferFormat FramebufferFormat;
        
        // ACPI
        uint64_t AcpiRsdp;
        
        // Ramdisk
        uint64_t RamdiskBase;
        uint64_t RamdiskSize;
    };
}
```

## Kernel Boot Detection

The kernel automatically detects the boot mode:

```cpp
extern "C" void kernel_main(void* boot_environment, uint32_t boot_magic)
{
    bool is_multiboot = (boot_magic == 0x2BADB002);
    bool is_bootinfo = false;
    
    if (!is_multiboot) {
        auto* bootinfo = static_cast<guideXOS::BootInfo*>(boot_environment);
        if (bootinfo->Magic == GUIDEXOS_BOOTINFO_MAGIC) {
            is_bootinfo = true;
        }
    }
    
    // Initialize framebuffer based on boot mode
    if (is_bootinfo) {
        framebuffer::init_from_bootinfo(bootinfo);
    } else {
        framebuffer::init(boot_environment);
    }
}
```

## Expected Boot Sequence

### UEFI Boot

1. **OVMF displays** TianoCore logo
2. **Bootloader executes**
   - Loads kernel.elf
   - Shows "guideXOS UEFI Bootloader"
   - Prepares BootInfo
   - Exits boot services
3. **Kernel boots**
   - Detects BootInfo
   - Shows boot splash with progress bar
   - Shows "waiting for init" message (stub)
   - Enters idle loop

### Legacy BIOS Boot

1. **SeaBIOS/QEMU** loads kernel directly
2. **Kernel boots**
   - Detects Multiboot
   - Shows boot splash (if framebuffer available)
   - Or shows text mode output
   - Enters idle loop

## Current Limitations

1. **ELF Loader** - Not yet implemented
   - Server launch shows stub message
   - TODO: Implement in kernel

2. **User Mode** - Not yet implemented
   - Server would run in kernel mode (unsafe)
   - TODO: Implement ring 3 switch

3. **Syscalls** - Not yet implemented
   - Server can't access hardware
   - TODO: Implement syscall interface

4. **Ramdisk** - Basic support only
   - Loaded but not parsed
   - TODO: Implement tar/initramfs parser

## Troubleshooting

### "OVMF.fd not found"

Download OVMF:
- Windows: https://github.com/tianocore/edk2/releases
- Linux: `sudo apt-get install ovmf && cp /usr/share/ovmf/OVMF.fd .`

### "Bootloader build failed"

Ensure Visual Studio 2019 or later is installed with:
- C++ Desktop Development workload
- Windows SDK

### "Kernel not found"

Build the kernel first:
```bash
cd kernel
make ARCH=amd64
```

### "Triple fault / CPU reset"

Enable QEMU debugging:
```bash
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M -d int,cpu_reset -no-reboot
```

### "Black screen / No output"

1. Check serial console output (`-serial stdio`)
2. Verify framebuffer was initialized
3. Try legacy BIOS boot to compare

### "Bootloader doesn't find kernel.elf"

Verify ESP structure:
```powershell
tree /F ESP
```

Should show `kernel.elf` in ESP root.

## Development Workflow

Typical development cycle:

```powershell
# 1. Make changes to kernel
code kernel/core/main.cpp

# 2. Build everything
.\build-uefi.ps1

# 3. Test in QEMU
.\build-uefi.ps1 -RunQemu

# 4. Debug if needed
# Check serial output for kernel messages
```

## Next Steps

1. **Implement ELF Loader** - Load guideXOSServer from ramdisk
2. **Implement User Mode** - Switch to ring 3 for server
3. **Implement Syscalls** - Hardware access interface
4. **Package Server** - Build server.cpp as ELF, add to ramdisk

## References

- UEFI Specification: https://uefi.org/specifications
- Multiboot Specification: https://www.gnu.org/software/grub/manual/multiboot/
- QEMU Documentation: https://www.qemu.org/docs/
- EDK2 (OVMF): https://github.com/tianocore/edk2

## License

Copyright (c) 2024 guideX
