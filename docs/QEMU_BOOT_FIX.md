# ?? QEMU "Booting from ROM..." Fix

## Problem

QEMU shows "Booting from ROM..." and doesn't boot the kernel.

---

## ? Quick Fixes (Try These First)

### Fix #1: Rebuild the Kernel

The kernel might be corrupted or built incorrectly.

```bash
cd kernel
build-x86.bat
cd ..
scripts\run-qemu-x86.bat
```

---

### Fix #2: Add VGA Flag

Updated the script to include `-vga std` flag. Try running:

```bash
scripts\run-qemu-x86.bat
```

---

### Fix #3: Verify Multiboot Header

The kernel needs a valid Multiboot header. Check that `kernel/arch/x86/boot.asm` has:

```asm
section .multiboot
align 4
    dd 0x1BADB002              ; Magic number
    dd 0x00000007              ; Flags
    dd -(0x1BADB002 + 0x00000007)    ; Checksum
```

---

### Fix #4: Try Alternative QEMU Command

Run QEMU manually with verbose output:

```bash
cd D:\devgitlab\guideXOS\guideXOSServer

"C:\Program Files\qemu\qemu-system-i386.exe" ^
    -kernel kernel\build\x86\bin\kernel.elf ^
    -m 128M ^
    -vga std ^
    -serial stdio ^
    -display sdl
```

---

## ?? Diagnostic Steps

### Step 1: Check Kernel File

```bash
dir kernel\build\x86\bin\kernel.elf
```

**Expected:** File exists, size ~20-50 KB

**If missing:** Build the kernel first:
```bash
cd kernel
build-x86.bat
```

---

### Step 2: Verify Kernel Format

The kernel should be an ELF file. Check the build output from `build-x86.bat`:

```
[7/7] Linking kernel...
[OK] kernel.elf created
```

**If you see errors during linking:** The kernel is not valid.

---

### Step 3: Run Diagnostic Script

```bash
scripts\diagnose-qemu.bat
```

This will check:
- Kernel file exists
- Kernel size is reasonable
- QEMU is installed
- Show the exact command being run

---

## ?? Common Causes

### Cause #1: Visual Studio Built the Kernel Wrong

**Problem:** If you built using Visual Studio (F7), it might produce a Windows PE/COFF executable instead of an ELF file.

**Solution:** Always use the kernel build script:
```bash
cd kernel
build-x86.bat
```

**How to tell:** Check the build output directory:
- ? Correct: `kernel\build\x86\bin\kernel.elf` (28KB, built by build-x86.bat)
- ? Wrong: `x64\Debug\guideXOSServer.exe` (Windows executable)

---

### Cause #2: Multiboot Header Corrupted

**Problem:** The bootloader (GRUB, built into QEMU) can't find the Multiboot magic number.

**Solution:** Verify `kernel/arch/x86/boot.asm` is correct (see Fix #3 above).

---

### Cause #3: Wrong QEMU Architecture

**Problem:** Using `qemu-system-x86_64` instead of `qemu-system-i386`.

**Solution:** The script uses the correct one (`qemu-system-i386`), but if running manually, use:
```bash
qemu-system-i386 -kernel kernel.elf
```

**NOT:**
```bash
qemu-system-x86_64 -kernel kernel.elf  # Wrong!
```

---

### Cause #4: Kernel Not Properly Linked

**Problem:** The linker didn't include the boot section first.

**Check:** Open `kernel/arch/x86/linker.ld` and verify it has:

```ld
SECTIONS
{
    . = 1M;
    
    .boot :
    {
        *(.multiboot)
        *(.boot)
    }
    
    .text :
    {
        *(.text)
    }
    
    /* ... rest of sections ... */
}
```

The `.multiboot` and `.boot` sections **must** be first!

---

## ?? Recommended Approach

### Clean Build

```bash
# 1. Clean everything
cd kernel
clean.bat  # or: rmdir /s /q build

# 2. Rebuild
build-x86.bat

# 3. Verify build succeeded
dir build\x86\bin\kernel.elf

# 4. Run
cd ..
scripts\run-qemu-x86.bat
```

**Expected Result:** QEMU boots and shows either:
- Boot splash (dark gradient, progress bar)
- Desktop (teal gradient, taskbar)
- OR text mode (colored text output)

---

## ?? What Should Happen

### Successful Boot Sequence:

```
QEMU Starts
  ?
Looks for bootable media
  ?
Finds kernel via -kernel flag
  ?
Loads kernel into memory
  ?
Checks for Multiboot header
  ?
Transfers control to kernel_main()
  ?
Kernel displays:
  - Graphics mode: Boot splash ? Desktop
  - Text mode: Colored console output
```

### Failed Boot:

```
QEMU Starts
  ?
Looks for bootable media
  ?
-kernel flag not working or file invalid
  ?
Falls back to ROM boot
  ?
Shows: "Booting from ROM..."
  ?
Hangs (no bootable ROM)
```

---

## ?? Alternative: Create Bootable ISO

If `-kernel` flag doesn't work, create a proper ISO:

```bash
# Install GRUB (one-time setup)
# Download: https://ftp.gnu.org/gnu/grub/

# Create ISO with GRUB
cd kernel
mkdir -p isofiles/boot/grub

# Copy kernel
copy build\x86\bin\kernel.elf isofiles\boot\kernel.elf

# Create grub.cfg
echo "menuentry "guideXOS" {" > isofiles\boot\grub\grub.cfg
echo "    multiboot /boot/kernel.elf" >> isofiles\boot\grub\grub.cfg
echo "}" >> isofiles\boot\grub\grub.cfg

# Create ISO (requires grub-mkrescue)
grub-mkrescue -o guideXOS.iso isofiles/

# Boot from ISO
cd ..
"C:\Program Files\qemu\qemu-system-i386.exe" -cdrom kernel\guideXOS.iso -m 128M
```

---

## ?? Most Likely Solution

Based on the error, the most likely cause is that the kernel needs to be rebuilt:

```bash
cd kernel
build-x86.bat
cd ..
scripts\run-qemu-x86.bat
```

If you see:
- ? Boot splash or desktop = SUCCESS!
- ? Colored text output = SUCCESS (text mode fallback)
- ? "Booting from ROM..." = Kernel not loading

---

## ?? Debug Output

Add this to see what QEMU is doing:

```bash
"C:\Program Files\qemu\qemu-system-i386.exe" ^
    -kernel kernel\build\x86\bin\kernel.elf ^
    -m 128M ^
    -d int,cpu_reset,guest_errors ^
    -D qemu-debug.log
```

Then check `qemu-debug.log` for errors.

---

## ? Success Indicators

### You'll know it's working when you see:

**Graphics Mode:**
- Black screen briefly
- Dark gradient background
- Cyan-bordered box (title)
- Progress bar animation
- Then teal gradient desktop

**Text Mode:**
```
guideXOS Kernel v0.1
Copyright (c) 2024 guideX

Architecture: x86 (32-bit)

[ OK ] Interrupts disabled
[ OK ] Architecture initialized
```

---

## ?? If Nothing Works

1. **Check the exact QEMU command** being used
2. **Verify the kernel file** isn't corrupted (try rebuilding)
3. **Check QEMU version** (`qemu-system-i386 --version`)
4. **Try a simpler kernel** - create a minimal "Hello World" kernel to test QEMU

---

**Most Common Fix:** Rebuild the kernel with `build-x86.bat` and try again! ??
