# guideXOS

A modern, layered operating system with UEFI boot support and multi-architecture kernel design.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Supported Architectures](#supported-architectures)
- [Key Features](#key-features)
- [Prerequisites](#prerequisites)
- [Building](#building)
- [Testing and Running](#testing-and-running)
- [Project Structure](#project-structure)
- [Development Status](#development-status)
- [Contributing](#contributing)
- [License](#license)

## Overview

guideXOS is a research/experimental operating system project featuring a strictly layered architecture, UEFI boot support, and a portable multi-architecture kernel written in C++14. The project includes a bootloader, kernel, and user-mode server/desktop component.

## Architecture

guideXOS follows a **strictly layered architecture**:

```
Firmware -> Bootloader -> Kernel -> guideXOSServer -> User Apps
```

Each layer has a single responsibility:
- **Firmware (UEFI):** Hardware initialization and boot services
- **Bootloader:** Loads the kernel and provides boot information
- **Kernel:** Hardware abstraction, memory management, process management
- **guideXOSServer:** User-mode services, desktop environment, compositor
- **User Apps:** Applications running in user space

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed architectural documentation.

## Supported Architectures

The guideXOS kernel is designed to be portable across multiple CPU architectures:

| Architecture | Status | Bitness | Notes |
|-------------|--------|---------|-------|
| **x86** | ? Stable | 32-bit | Primary development platform |
| **AMD64** (x86-64) | ? Stable | 64-bit | Fully supported with UEFI |
| **ARM** (ARMv7-A) | ?? Experimental | 32-bit | Basic support |
| **ARM64** (ARMv8-A) | ?? Experimental | 64-bit | In development |
| **RISC-V 64** | ?? Experimental | 64-bit | RV64IMA ISA |
| **SPARC** (v8) | ?? Experimental | 32-bit | Basic support |
| **SPARC64** (v9) | ?? Experimental | 64-bit | UltraSPARC |
| **IA-64** (Itanium) | ?? Experimental | 64-bit | Legacy platform |
| **LoongArch64** | ?? In Development | 64-bit | Loongson platform |
| **MIPS64** | ?? In Development | 64-bit | Big-endian MIPS |
| **PowerPC64** | ?? In Development | 64-bit | IBM POWER |

### Hardware Support

- **Graphics:** Framebuffer (UEFI GOP, VGA, VESA)
- **Storage:** ATA/IDE, AHCI (in development)
- **Input:** PS/2 keyboard, basic USB (planned)
- **Virtual Machines:** QEMU (primary), VMware, VirtualBox

## Key Features

- ? **UEFI boot support** via EDK2-based bootloader
- ? **Multi-architecture kernel** with clean abstraction layer
- ? **Framebuffer graphics** with boot splash screen
- ? **Desktop environment** (compositor, window manager)
- ? **Disk management** utilities (FAT, EXT2/3/4)
- ? **Control panel** and system services
- ?? **Process management** (basic implementation)
- ?? **User-mode execution** (kernel ELF loader needed)
- ?? **Syscall interface** (design in progress)

## Prerequisites

### For Building the Bootloader (UEFI)

- **Windows:**
  - Visual Studio 2019 or newer
  - EDK2 (TianoCore) build environment
  - OVMF firmware for testing

- **Linux:**
  - EDK2 build tools
  - GCC/Clang toolchain
  - NASM assembler

### For Building the Kernel (C++)

You need cross-compilation toolchains for your target architecture:

- **x86 (32-bit):**
  - `i686-elf-gcc`, `i686-elf-g++`
  - NASM assembler
  - GNU Make

- **AMD64 (x86-64):**
  - `x86_64-elf-gcc`, `x86_64-elf-g++` (or MinGW-w64 on Windows)
  - NASM assembler
  - GNU Make

- **ARM (32-bit):**
  - `arm-none-eabi-gcc`, `arm-none-eabi-g++`
  - GNU Make

- **ARM64 (64-bit):**
  - `aarch64-none-elf-gcc`, `aarch64-none-elf-g++`
  - GNU Make

- **RISC-V 64:**
  - `riscv64-unknown-elf-gcc` or `riscv64-linux-gnu-gcc`
  - Install: `sudo apt install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu`

- **SPARC (32-bit):**
  - `sparc-elf-gcc`, `sparc-elf-g++`

- **SPARC64 (64-bit):**
  - `sparc64-linux-gnu-gcc`, `sparc64-linux-gnu-g++`
  - Install: `sudo apt install gcc-sparc64-linux-gnu g++-sparc64-linux-gnu`

- **IA-64 (Itanium):**
  - `ia64-linux-gnu-gcc`, `ia64-linux-gnu-g++`
  - Install: `sudo apt install gcc-ia64-linux-gnu g++-ia64-linux-gnu`

### For Building the Server (User-Mode)

- C++17 (or newer) compiler
- Standard C++ library
- QEMU for VM testing

## Building

### Quick Build (x86/AMD64)

From the repository root:

```bash
# Build 32-bit x86 kernel
make x86

# Build 64-bit AMD64 kernel
make amd64

# Build and run in QEMU
make qemu

# Run tests
make test

# Clean build files
make clean
```

### Building for Specific Architectures

#### x86 (32-bit) - Windows

```batch
cd kernel
call build-x86.bat
```

Output: `build/x86/bin/kernel.elf`

#### AMD64 (x86-64) - Windows

```powershell
.\build.ps1
```

or

```bash
cd kernel
make ARCH=amd64
```

Output: `build/amd64/bin/kernel.elf`

#### RISC-V 64 - Linux

```bash
./scripts/build-riscv64.sh
```

Output: `build/riscv64/bin/kernel.elf`

#### SPARC64 - Linux

```bash
./scripts/build-sparcv9.sh
```

Output: `build/sparc64/bin/kernel.elf`

#### IA-64 (Itanium) - Linux

```bash
./scripts/build-ia64.sh
```

Output: `build/ia64/bin/kernel.elf`

#### ARM - Manual Build

```bash
cd kernel
make ARCH=arm
```

Output: `build/arm/bin/kernel.elf`

### Building the Complete UEFI System

For a full UEFI bootable system (bootloader + kernel):

**Windows (PowerShell):**
```powershell
.\build-uefi.ps1
```

**Linux:**
```bash
./scripts/build-uefi.sh
```

This creates a bootable ESP directory structure with UEFI bootloader and kernel.

## Testing and Running

### QEMU Testing

#### x86 (32-bit)

```bash
qemu-system-i386 \
  -kernel build/x86/bin/kernel.elf \
  -m 128M \
  -serial stdio
```

#### AMD64 (x86-64) with UEFI

```bash
qemu-system-x86_64 \
  -bios OVMF.fd \
  -drive file=fat:rw:ESP,format=raw \
  -m 1024M \
  -serial stdio
```

#### RISC-V 64

```bash
qemu-system-riscv64 \
  -machine virt \
  -bios default \
  -kernel build/riscv64/bin/kernel.elf \
  -m 128M \
  -nographic
```

**Or use the convenience script:**
```bash
./scripts/build-and-run-riscv64_in_linux.sh
```

#### SPARC v8 (32-bit)

```bash
qemu-system-sparc \
  -M SS-5 \
  -kernel build/sparc/bin/kernel.elf \
  -m 128M \
  -nographic
```

**Or use the convenience script:**
```bash
./scripts/build-and-run-sparcv8_in_linux.sh
```

#### SPARC v9 (64-bit)

```bash
qemu-system-sparc64 \
  -M sun4u \
  -kernel build/sparc64/bin/kernel.elf \
  -m 256M \
  -nographic
```

**Or use the convenience script:**
```bash
./scripts/build-and-run-sparcv9_in_linux.sh
```

#### ARM (32-bit)

```bash
qemu-system-arm \
  -M versatilepb \
  -kernel build/arm/bin/kernel.elf \
  -m 128M \
  -serial stdio
```

### VMware / VirtualBox Testing

1. Create a new VM with UEFI firmware support
2. Attach the `ESP` directory as a FAT32 virtual disk
3. Boot the VM
4. The UEFI bootloader will load the kernel

### Physical Hardware Testing

?? **Warning:** Testing on physical hardware may result in data loss or hardware damage. Use caution.

1. Format a USB drive as FAT32
2. Copy the contents of the `ESP` directory to the USB drive
3. Ensure your computer supports UEFI boot
4. Boot from the USB drive
5. Select the guideXOS bootloader from the UEFI boot menu

## Project Structure

```
guideXOS.SERVER/
??? guideXOSBootLoader/          # UEFI bootloader (EDK2-based)
?   ??? boot.c                   # Boot entry point
?   ??? bootinfo.h               # Boot information structures
?   ??? ...
??? kernel/                      # Multi-architecture kernel
?   ??? core/                    # Portable kernel code
?   ?   ??? main.cpp             # Kernel entry point
?   ?   ??? arch.cpp             # Architecture abstraction
?   ?   ??? desktop.cpp          # Framebuffer/desktop init
?   ?   ??? shell.cpp            # Kernel shell
?   ?   ??? include/             # Kernel headers
?   ??? arch/                    # Architecture-specific code
?   ?   ??? x86/                 # 32-bit x86
?   ?   ??? amd64/               # 64-bit x86-64
?   ?   ??? arm/                 # 32-bit ARM
?   ?   ??? arm64/               # 64-bit ARM
?   ?   ??? riscv64/             # RISC-V 64
?   ?   ??? sparc/               # SPARC v8
?   ?   ??? sparc64/             # SPARC v9
?   ?   ??? ia64/                # Itanium
?   ?   ??? loongarch64/         # LoongArch 64
?   ?   ??? mips64/              # MIPS 64
?   ?   ??? ppc64/               # PowerPC 64
?   ??? Makefile                 # Main kernel build system
?   ??? README.md                # Kernel documentation
??? compositor.cpp               # Window manager/compositor
??? desktop_service.cpp          # Desktop environment service
??? disk_manager.cpp             # Disk management UI
??? control_panel.cpp            # Control panel
??? icons.cpp                    # Icon rendering
??? focus_indicator.cpp          # Window focus indicators
??? scripts/                     # Build and test scripts
?   ??? build-riscv64.sh         # RISC-V 64 build script
?   ??? build-sparcv9.sh         # SPARC v9 build script
?   ??? build-ia64.sh            # IA-64 build script
?   ??? ...
??? Makefile                     # Top-level build system
??? build.ps1                    # Windows build script
??? build-uefi.ps1               # UEFI system build script
??? README.md                    # This file
??? SERVER.md                    # Server component docs
??? ARCHITECTURE.md              # Architecture documentation
```

## Development Status

### Working Components

- ? **Bootloader:** UEFI bootloader successfully loads kernel and provides boot information
- ? **Kernel (x86/AMD64):** Boots, initializes subsystems, displays boot splash
- ? **Framebuffer:** Graphics output working on x86/AMD64
- ? **guideXOSServer:** Desktop environment and compositor (runs standalone for testing)
- ? **Disk Manager:** FAT and EXT2/3/4 filesystem support
- ? **Control Panel:** System configuration UI

### In Progress

- ?? **ELF Loader:** Kernel needs to load guideXOSServer from ramdisk/initramfs
- ?? **Syscall Interface:** System call ABI between kernel and user mode
- ?? **User Mode Execution:** Transition from kernel to user mode (Ring 3)
- ?? **Multi-architecture Testing:** Validating experimental architectures

### Roadmap

1. ? UEFI bootloader
2. ? Kernel boot and initialization (x86/AMD64)
3. ?? Process management (stub created, needs expansion)
4. ?? ELF loader in kernel
5. ?? User-mode execution and ring transitions
6. ?? Launch guideXOSServer as init process
7. ?? Desktop environment running in user mode
8. ?? Device driver framework
9. ?? Network stack
10. ?? Multi-tasking and scheduling

## Architecture Rules

**Non-negotiable design principles:**

1. **Bootloader boots kernel, NOT guideXOSServer**
2. **Kernel is the only boot-aware component**
3. **guideXOSServer must remain boot-agnostic**
4. **No collapsing layers**
5. **Desktop/GUI belongs in user mode, NOT kernel** (except minimal boot splash)

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed rationale and guidelines.

## Contributing

When adding features, always ask:

1. Which layer does this belong in?
2. Can this be done in user mode?
3. Does this need boot information?

**Anti-patterns to avoid:**

- ? Implementing GUI rendering in kernel (except minimal boot splash)
- ? Having bootloader load guideXOSServer directly
- ? Placing user applications in kernel
- ? Making guideXOSServer boot-aware

### Code Style

- Use C++14 standard (kernel code)
- Use C++17+ for user-mode server code
- Follow existing naming conventions
- Keep architecture-specific code isolated in `kernel/arch/<arch>/`
- Write portable code in `kernel/core/`

## Testing

### Unit Testing

```bash
make test
```

### Build System Testing

```bash
./scripts/test-build.sh
```

### VM Testing (QEMU)

```bash
# Quick test (x86)
make qemu

# Specific architecture
qemu-system-<arch> -kernel build/<arch>/bin/kernel.elf -m 128M -serial stdio
```

### Cross-Architecture Testing

Test on multiple architectures to ensure portability:

```bash
# Test all stable architectures
make ARCH=x86 && make ARCH=amd64

# Test experimental architectures (Linux)
./scripts/build-riscv64.sh
./scripts/build-sparcv9.sh
./scripts/build-ia64.sh
```

## Debugging

### QEMU with GDB

```bash
# Terminal 1: Start QEMU with GDB server
qemu-system-x86_64 -kernel build/amd64/bin/kernel.elf -s -S

# Terminal 2: Connect GDB
gdb build/amd64/bin/kernel.elf
(gdb) target remote localhost:1234
(gdb) continue
```

### Serial Console Output

Add `-serial stdio` to QEMU commands to see kernel debug output.

## License

Copyright (c) 2024 guideX

## Support

For detailed documentation:
- **Architecture/Design:** [ARCHITECTURE.md](ARCHITECTURE.md)
- **Bootloader:** `guideXOSBootLoader/README.md`
- **Kernel:** [kernel/README.md](kernel/README.md)
- **Server:** [SERVER.md](SERVER.md)
- **Disk Manager:** [DISKMANAGER_IMPLEMENTATION.md](DISKMANAGER_IMPLEMENTATION.md)
- **Control Panel:** [CONTROL_PANEL_INTEGRATION.md](CONTROL_PANEL_INTEGRATION.md)

For build issues, include:
- Your OS and toolchain versions
- Target architecture
- Complete build output
- Exact commands you ran

## Repositories

- **GitHub (Server):** https://github.com/guideX/guideXOS-Server
- **GitLab (Server):** https://gitlab.com/guideX/guidexos-server
- **GitHub (Legacy):** https://github.com/guideX/guideXOS-Legacy
