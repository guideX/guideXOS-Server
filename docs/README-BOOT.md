# ?? Booting guideXOS in QEMU

This guide will help you boot the guideXOS C++ kernel in QEMU.

## Prerequisites

### Required Tools

1. **Cross-Compiler Toolchain**
   - `i686-elf-gcc` and `i686-elf-g++` for x86
   - `x86_64-elf-gcc` and `x86_64-elf-g++` for AMD64
   - Download from: https://github.com/lordmilko/i686-elf-tools/releases

2. **NASM Assembler**
   - Version 2.14 or later
   - Download from: https://www.nasm.us/

3. **GNU Make**
   - Windows: Install via MinGW or Cygwin
   - Linux/Mac: Usually pre-installed

4. **QEMU**
   - Windows: https://www.qemu.org/download/#windows
   - Linux: `sudo apt install qemu-system-x86`
   - Mac: `brew install qemu`

### Verify Installation

```bash
# Check cross-compiler
i686-elf-gcc --version

# Check NASM
nasm --version

# Check QEMU
qemu-system-i386 --version
```

---

## Building the Kernel

### Step 1: Navigate to kernel directory

```bash
cd D:/devgitlab/guideXOS/guideXOSServer/kernel
```

### Step 2: Build for x86 (32-bit)

```bash
make ARCH=x86
```

**Expected output:**
```
Architecture: x86
Compiler: i686-elf-g++
Assembler: nasm
...
Assembling kernel/arch/x86/boot.asm...
Compiling kernel/core/main.cpp...
Compiling kernel/core/vga.cpp...
Compiling kernel/core/arch.cpp...
Compiling kernel/arch/x86/arch.cpp...
Linking x86 kernel...
Built: build/x86/bin/kernel.elf
```

### Step 3: Verify the build

```bash
file build/x86/bin/kernel.elf
```

Should show:
```
kernel.elf: ELF 32-bit LSB executable, Intel 80386, version 1 (SYSV), statically linked, not stripped
```

---

## Running in QEMU

### Option 1: Using the launch script (Recommended)

**Windows:**
```cmd
scripts\run-qemu-x86.bat
```

**Linux/Mac:**
```bash
chmod +x scripts/run-qemu-x86.sh
./scripts/run-qemu-x86.sh
```

### Option 2: Manual QEMU command

```bash
qemu-system-i386 \
    -kernel build/x86/bin/kernel.elf \
    -m 128M \
    -serial stdio \
    -display gtk \
    -no-reboot \
    -no-shutdown
```

### QEMU Controls

- **Exit QEMU**: Press `Ctrl+A` then `X` (or `Ctrl+C` in Windows)
- **Pause/Resume**: Press `Ctrl+A` then `S`
- **QEMU Monitor**: Press `Ctrl+A` then `C`

---

## Expected Output

When the kernel boots successfully, you should see:

```
guideXOS Kernel v0.1
Copyright (c) 2024 guideX

Architecture: x86 (32-bit) (32-bit)

[ OK ] Interrupts disabled
[ OK ] Architecture initialized

TODO: Initialize kernel subsystems
  - GDT, IDT (x86/amd64)
  - Memory manager (PMM, VMM)
  - Scheduler
  - Drivers

[INFO] Kernel initialization complete
[INFO] Entering idle loop (interrupts disabled)
```

The kernel will then enter an infinite loop with `hlt` instructions.

---

## Troubleshooting

### Build Errors

#### "i686-elf-g++: command not found"
**Solution:** Install the cross-compiler toolchain and add it to your PATH.

```bash
# Windows (PowerShell)
$env:PATH += ";C:\path\to\i686-elf-tools\bin"

# Linux/Mac
export PATH=$PATH:/path/to/i686-elf-tools/bin
```

#### "nasm: command not found"
**Solution:** Install NASM and add it to your PATH.

#### Multiple definition errors
**Solution:** Clean and rebuild:
```bash
make ARCH=x86 clean
make ARCH=x86
```

### QEMU Errors

#### "QEMU not found"
**Solution:** Install QEMU and ensure it's in your PATH.

Windows default path: `C:\Program Files\qemu\qemu-system-i386.exe`

#### "Could not open kernel file"
**Solution:** Make sure you built the kernel first:
```bash
make ARCH=x86
```

#### Black screen with no output
**Possible causes:**
1. Kernel crashed before VGA init
2. Bootloader issue
3. Multiboot header problem

**Debug steps:**
```bash
# Enable QEMU debug output
qemu-system-i386 -kernel build/x86/bin/kernel.elf -d int,cpu_reset

# Check multiboot header
objdump -x build/x86/bin/kernel.elf | grep multiboot
```

#### Kernel immediately exits
**Possible causes:**
1. Triple fault (CPU reset due to unhandled exception)
2. Stack corruption
3. Invalid instruction

**Debug steps:**
```bash
# Run with CPU state dump on reset
qemu-system-i386 -kernel build/x86/bin/kernel.elf -d int,cpu_reset -D qemu.log

# Check the log file
cat qemu.log
```

---

## Advanced Testing

### Debug Mode

Enable debug symbols and disable optimization:

```bash
make ARCH=x86 CFLAGS="-g -O0"
```

### GDB Debugging

**Terminal 1 - Start QEMU with GDB stub:**
```bash
qemu-system-i386 -kernel build/x86/bin/kernel.elf -s -S
```

**Terminal 2 - Connect GDB:**
```bash
i686-elf-gdb build/x86/bin/kernel.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

### Memory Dump

```bash
# In QEMU monitor (Ctrl+A then C)
info registers
info mem
x/20i $eip   # Disassemble 20 instructions at current position
```

---

## Next Steps

Once the kernel boots successfully:

1. ? **Implement GDT** (Global Descriptor Table)
2. ? **Implement IDT** (Interrupt Descriptor Table)
3. ? **Enable interrupts** (after IDT is set up)
4. ? **Implement timer interrupt** (PIT or APIC)
5. ? **Implement keyboard driver**
6. ? **Implement memory manager**
7. ? **Implement basic syscalls**

---

## Other Architectures

### AMD64 (x86-64)

```bash
make ARCH=amd64
qemu-system-x86_64 -kernel build/amd64/bin/kernel.elf
```

### ARM

```bash
make ARCH=arm
qemu-system-arm -M vexpress-a9 -kernel build/arm/bin/kernel.elf -serial stdio
```

---

## Resources

- [OSDev Wiki](https://wiki.osdev.org/)
- [QEMU Documentation](https://www.qemu.org/docs/master/)
- [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/)
- [Intel x86 Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

---

## Build System Reference

### Makefile Targets

```bash
make ARCH=x86 all      # Build kernel (default)
make ARCH=x86 clean    # Clean build artifacts
make ARCH=x86 info     # Show build configuration
```

### Architecture Options

- `ARCH=x86` - 32-bit x86 (i686)
- `ARCH=amd64` - 64-bit x86 (x86-64)
- `ARCH=arm` - 32-bit ARM (ARMv7-A)
- `ARCH=sparc` - 32-bit SPARC (v8)
- `ARCH=ia64` - 64-bit Itanium

### Environment Variables

```bash
# Override compiler
CXX=clang++ make ARCH=x86

# Add extra flags
CFLAGS="-DDEBUG -g" make ARCH=x86

# Verbose build
V=1 make ARCH=x86
```

---

**Happy Hacking! ??**
