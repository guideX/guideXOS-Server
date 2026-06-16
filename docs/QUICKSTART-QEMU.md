# ?? Quick Start - Boot guideXOS in QEMU

## 1?? Prerequisites Check

**Windows:**
```cmd
i686-elf-gcc --version    # Cross-compiler for x86
nasm --version            # Assembler
qemu-system-i386 --version # Emulator
```

**Linux/Mac:**
```bash
i686-elf-gcc --version    # Cross-compiler for x86
nasm --version            # Assembler
qemu-system-i386 --version # Emulator
```

**Missing tools?** See [README-BOOT.md](README-BOOT.md) for installation links.

---

## 2?? Build the Kernel

**?? Windows (no GNU Make needed):**
```cmd
cd kernel
build-x86.bat
```

**?? Windows (with GNU Make):**
```cmd
cd kernel
make ARCH=x86
```

**?? Linux/Mac:**
```bash
cd kernel
make ARCH=x86
```

**?? Windows Note:** If you see `Command syntax error`, you're using the wrong `make`. Use `build-x86.bat` instead!

**Output location:** `build/x86/bin/kernel.elf`

---

## 3?? Run in QEMU

**Windows:**
```cmd
scripts\run-qemu-x86.bat
```

**Linux/Mac:**
```bash
chmod +x scripts/run-qemu-x86.sh
./scripts/run-qemu-x86.sh
```

---

## ? Expected Output

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

---

## ?? QEMU Controls

| Action | Key Combination |
|--------|----------------|
| Exit QEMU | `Ctrl+A` then `X` |
| Pause/Resume | `Ctrl+A` then `S` |
| Open Monitor | `Ctrl+A` then `C` |
| Windows Exit | `Ctrl+C` in terminal |

---

## ?? Quick Troubleshooting

### "Command syntax error" in make?
**You're using Embarcadero Make (wrong tool)!**
```cmd
# Use batch script instead
cd kernel
build-x86.bat
```

### Black Screen?
**Windows:**
```cmd
dir build\x86\bin\kernel.elf
```

**Linux/Mac:**
```bash
ls -lh build/x86/bin/kernel.elf
```

### Build Errors?
**Windows:**
```cmd
cd kernel
clean.bat
build-x86.bat
```

**Linux/Mac:**
```bash
make ARCH=x86 clean
make ARCH=x86
```

### Still issues?
See [README-BOOT.md](README-BOOT.md) for detailed troubleshooting.

---

## ?? What Just Happened?

1. ? **Bootloader (GRUB multiboot)** loaded your kernel at 1MB
2. ? **boot.asm** set up stack and called `kernel_main()`
3. ? **VGA driver** initialized text mode (80x25, color)
4. ? **Architecture layer** detected x86 and disabled interrupts
5. ? **Main loop** entered HLT state

---

## ?? Next Development Steps

1. **Implement GDT** - Set up proper segment descriptors
2. **Implement IDT** - Handle interrupts and exceptions  
3. **Timer (PIT)** - Get periodic interrupts working
4. **Keyboard** - Read PS/2 keyboard input
5. **Memory Manager** - PMM and VMM for paging
6. **Scheduler** - Multitasking support

---

**Full documentation:** [README-BOOT.md](README-BOOT.md)
