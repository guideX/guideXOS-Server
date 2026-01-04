# guideXOS

A modern, layered operating system with UEFI boot support.

## Architecture

guideXOS follows a **strictly layered architecture**:

```
Firmware ? Bootloader ? Kernel ? guideXOSServer ? User Apps
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed documentation.

## Quick Start

### Prerequisites

- **For bootloader (UEFI):**
  - Visual Studio 2019+ with EDK2
  - OVMF UEFI firmware
  
- **For kernel (C++):**
  - GCC/Clang cross-compiler
  - NASM assembler
  - Make

- **For server (user-mode):**
  - C++17 compiler
  - Standard library support

### Building

#### Build kernel only:
```bash
make x86          # Build 32-bit x86 kernel
make amd64        # Build 64-bit x86-64 kernel
```

#### Build and run in QEMU:
```bash
make qemu         # Build x86 kernel and launch QEMU
```

#### Build complete UEFI system:
```powershell
# Windows (PowerShell)
.\build.ps1
```

### Running

#### QEMU (UEFI boot):
```bash
qemu-system-x86_64 \
  -bios OVMF.fd \
  -drive file=fat:rw:ESP,format=raw \
  -m 1024M \
  -serial stdio
```

## Project Structure

```
guideXOSServer/
??? guideXOSBootLoader/      # UEFI bootloader (loads kernel)
??? kernel/                  # Kernel (minimal, microkernel-style)
?   ??? core/                # Core kernel code
?   ??? arch/                # Architecture-specific code
??? server.cpp               # guideXOSServer (user-mode init)
??? compositor.cpp           # Window manager
??? desktop_service.cpp      # Desktop environment
```

## Key Features

- ? UEFI boot support
- ? Multi-architecture kernel (x86, amd64, arm, ia64, sparc)
- ? Framebuffer graphics
- ? Boot splash screen
- ? Process management (basic)
- ?? User-mode execution (TODO)
- ?? Desktop environment (implemented in server, needs kernel ELF loader)

## Development Status

### Working Components

- **Bootloader:** UEFI bootloader successfully loads kernel
- **Kernel:** Receives BootInfo, initializes subsystems, shows boot splash
- **guideXOSServer:** Full-featured desktop environment (runs standalone for testing)

### In Progress

- **ELF Loader:** Kernel needs to load guideXOSServer from ramdisk
- **Syscalls:** System call interface between kernel and user mode
- **User Mode:** Transition from kernel to user mode

### Roadmap

1. ? UEFI bootloader
2. ? Kernel boot and initialization
3. ?? Process management (stub created)
4. ? ELF loader in kernel
5. ? User-mode execution
6. ? Launch guideXOSServer as init process
7. ? Desktop environment running in user mode

## Architecture Rules

**Non-negotiable:**

1. **Bootloader boots kernel, NOT guideXOSServer**
2. **Kernel is the only boot-aware component**
3. **guideXOSServer must remain boot-agnostic**
4. **No collapsing layers**
5. **Desktop/GUI belongs in user mode, NOT kernel**

See [ARCHITECTURE.md](ARCHITECTURE.md) for details.

## Contributing

When adding features, always ask:

1. Which layer does this belong in?
2. Can this be done in user mode?
3. Does this need boot information?

**Anti-patterns to avoid:**
- ? GUI rendering in kernel (except minimal boot splash)
- ? Bootloader loading guideXOSServer directly
- ? User applications in kernel
- ? Making guideXOSServer boot-aware

## Testing

### Unit Testing
```bash
make test
```

### VM Testing
```bash
# QEMU (recommended)
make qemu

# VMware
# Configure VM to boot from ESP/ directory
```

## License

Copyright (c) 2024 guideX

## Support

For architecture questions, see [ARCHITECTURE.md](ARCHITECTURE.md).

For build issues, check the respective component's documentation:
- Bootloader: `guideXOSBootLoader/README.md`
- Kernel: `kernel/README.md`
- Server: `SERVER.md`
