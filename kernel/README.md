# guideXOS Kernel

A portable, architecture-layered kernel supporting multiple CPU architectures.

## Supported Architectures

- **AMD64** (x86-64) - 64-bit x86 architecture
- **x86** (i386) - 32-bit x86 architecture
- **ARM** (ARMv7-A) - 32-bit ARM architecture
- **SPARC** (v8) - 32-bit SPARC architecture
- **Itanium** (IA-64) - 64-bit Intel Itanium architecture

## Directory Structure

```
kernel/
??? arch/               # Architecture-specific code
?   ??? amd64/         # AMD64 (x86-64) support
?   ?   ??? boot.asm   # Boot entry point
?   ?   ??? arch.cpp   # Architecture implementation
?   ?   ??? linker.ld  # Linker script
?   ?   ??? Makefile.arch
?   ?   ??? include/
?   ??? x86/           # x86 (32-bit) support
?   ??? arm/           # ARM support
?   ??? ia64/          # Itanium support
?   ??? sparc/         # SPARC support
??? core/              # Portable kernel code
?   ??? main.cpp       # Kernel entry point
?   ??? arch.cpp       # Architecture abstraction
?   ??? include/
?       ??? kernel/
?           ??? version.h
?           ??? arch.h
??? Makefile           # Main build system
??? README.md          # This file
```

## Building

### Prerequisites

You need cross-compilation toolchains for your target architecture:

- **AMD64**: `x86_64-elf-gcc`, `x86_64-elf-g++`, `nasm`
- **x86**: `i686-elf-gcc`, `i686-elf-g++`, `nasm`
- **ARM**: `arm-none-eabi-gcc`, `arm-none-eabi-g++`
- **SPARC**: `sparc-elf-gcc`, `sparc-elf-g++`
- **IA-64**: `ia64-elf-gcc`, `ia64-elf-g++`

### Build Commands

```bash
# Build for AMD64 (x86-64)
make ARCH=amd64

# Build for x86 (32-bit)
make ARCH=x86

# Build for ARM
make ARCH=arm

# Build for SPARC
make ARCH=sparc

# Build for Itanium
make ARCH=ia64

# Show build configuration
make ARCH=amd64 info

# Clean build artifacts
make ARCH=amd64 clean
```

### Output

Built kernels are located in:
```
build/<arch>/bin/kernel.elf
```

## Architecture Abstraction

The kernel uses a clean architecture abstraction layer that allows portable code to work across all architectures:

```cpp
#include <kernel/arch.h>

// These functions work on all architectures:
kernel::arch::halt();              // Halt CPU
kernel::arch::enable_interrupts(); // Enable interrupts
kernel::arch::disable_interrupts(); // Disable interrupts
kernel::arch::init();              // Initialize arch-specific features
kernel::arch::get_arch_name();     // Get architecture name
kernel::arch::get_arch_bits();     // Get architecture bitness (32/64)
```

## Boot Process

Each architecture has its own boot sequence:

### AMD64/x86
1. Boot from bootloader (GRUB, etc.)
2. Disable interrupts
3. Set up segment registers
4. Set up stack
5. Call `kernel_main()`

### ARM
1. Boot from firmware
2. CPU 0 initialization (other cores halt)
3. Clear BSS section
4. Set up stack
5. Call `kernel_main()`

### SPARC
1. Boot from OpenBoot
2. Disable interrupts
3. Set up stack and frame pointer
4. Clear BSS section
5. Call `kernel_main()`

### Itanium
1. Boot from EFI
2. Disable interrupts
3. Set up global and stack pointers
4. Clear BSS section
5. Call `kernel_main()`

## Development

### Adding New Features

1. Add portable code to `kernel/core/`
2. Add architecture-specific code to `kernel/arch/<arch>/`
3. Update headers in `kernel/core/include/kernel/` or `kernel/arch/<arch>/include/arch/`

### Architecture-Specific Operations

Each architecture provides its own operations:

#### x86/AMD64
- Port I/O: `inb()`, `outb()`, `inw()`, `outw()`, `inl()`, `outl()`
- MSR: `read_msr()`, `write_msr()`
- Control registers: `read_cr0()`, `write_cr0()`, etc.

#### ARM
- CP15 operations: `read_sctlr()`, `write_sctlr()`, etc.
- Cache operations: `invalidate_icache()`, `flush_dcache()`, etc.
- TLB operations: `invalidate_tlb()`

#### SPARC
- ASI operations: `read_asi()`, `write_asi()`
- Register windows: `flush_windows()`
- Processor state: `read_psr()`, `write_psr()`

#### Itanium
- Application registers: `read_ar()`, `write_ar()`
- Control registers: `read_cr()`, `write_cr()`
- RSE operations: `flush_rse()`, `read_bsp()`

## Testing

### QEMU Testing

```bash
# Test AMD64 kernel
qemu-system-x86_64 -kernel build/amd64/bin/kernel.elf

# Test x86 kernel
qemu-system-i386 -kernel build/x86/bin/kernel.elf

# Test ARM kernel
qemu-system-arm -M vexpress-a9 -kernel build/arm/bin/kernel.elf

# Test SPARC kernel
qemu-system-sparc -kernel build/sparc/bin/kernel.elf
```

## Roadmap

### Phase 1: Foundation (Current)
- [x] Boot loaders for all architectures
- [x] Architecture abstraction layer
- [x] Build system
- [ ] Early console output

### Phase 2: Memory Management
- [ ] Physical memory manager (PMM)
- [ ] Virtual memory manager (VMM)
- [ ] Paging support for all architectures

### Phase 3: Interrupt Handling
- [ ] IDT/IVT setup (x86/AMD64)
- [ ] Interrupt vectors (ARM, SPARC, IA-64)
- [ ] Timer interrupts
- [ ] Exception handling

### Phase 4: Process Management
- [ ] Process/thread structures
- [ ] Context switching
- [ ] Scheduler
- [ ] System calls

### Phase 5: Device Drivers
- [ ] Device abstraction
- [ ] Serial console
- [ ] Keyboard/mouse
- [ ] Storage drivers

## License

Copyright (c) 2024 guideX

## Contributing

Contributions are welcome! Please ensure your code:
- Compiles with C++14
- Works across all supported architectures
- Follows the existing code style
- Includes appropriate comments
