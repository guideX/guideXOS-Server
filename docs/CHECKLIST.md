# ? Pre-Flight Checklist - QEMU Boot

Use this checklist to verify everything is ready before testing.

---

## ?? Installation Check

### Required Software

- [ ] **i686-elf-gcc** installed and in PATH
  ```bash
  i686-elf-gcc --version
  ```
  Expected: Shows version (e.g., 13.2.0)
  
- [ ] **i686-elf-g++** installed and in PATH
  ```bash
  i686-elf-g++ --version
  ```
  Expected: Shows version
  
- [ ] **NASM** installed and in PATH
  ```bash
  nasm --version
  ```
  Expected: Shows version (e.g., 2.15.05)
  
- [ ] **QEMU** installed
  ```bash
  qemu-system-i386 --version
  ```
  Expected: Shows version
  
- [ ] **GNU Make** available
  ```bash
  make --version
  ```
  Expected: Shows GNU Make version

---

## ?? File Check

### New Files Created

- [ ] `kernel/core/include/kernel/vga.h` exists
- [ ] `kernel/core/vga.cpp` exists
- [ ] `kernel/core/main.cpp` updated with VGA calls
- [ ] `scripts/run-qemu-x86.sh` exists
- [ ] `scripts/run-qemu-x86.bat` exists
- [ ] `scripts/test-build.sh` exists
- [ ] `scripts/test-build.bat` exists
- [ ] `README-BOOT.md` exists
- [ ] `QUICKSTART-QEMU.md` exists
- [ ] `IMPLEMENTATION-SUMMARY.md` exists

### Existing Files (Verify No Changes Needed)

- [ ] `kernel/arch/x86/boot.asm` - should have multiboot header
- [ ] `kernel/arch/x86/linker.ld` - should load at 1MB
- [ ] `kernel/arch/x86/arch.cpp` - should have halt/interrupts
- [ ] `kernel/Makefile` - should use wildcards for .cpp files

---

## ?? Build Check

### Test Build

```bash
cd kernel
make ARCH=x86
```

- [ ] Build starts without errors
- [ ] See "Assembling kernel/arch/x86/boot.asm..."
- [ ] See "Compiling kernel/core/main.cpp..."
- [ ] See "Compiling kernel/core/vga.cpp..."
- [ ] See "Compiling kernel/core/arch.cpp..."
- [ ] See "Compiling kernel/arch/x86/arch.cpp..."
- [ ] See "Linking x86 kernel..."
- [ ] See "Built: build/x86/bin/kernel.elf"
- [ ] No error messages

### Verify Output

```bash
ls -lh build/x86/bin/kernel.elf
```

- [ ] File exists
- [ ] Size is reasonable (> 1KB, < 10MB)

```bash
file build/x86/bin/kernel.elf
```

- [ ] Shows "ELF 32-bit"
- [ ] Shows "Intel 80386"
- [ ] Shows "executable"

---

## ?? Launch Check

### Script Permissions (Linux/Mac Only)

```bash
chmod +x scripts/run-qemu-x86.sh
chmod +x scripts/test-build.sh
```

- [ ] Scripts are executable

### QEMU Launch

```bash
# Windows
scripts\run-qemu-x86.bat

# Linux/Mac
./scripts/run-qemu-x86.sh
```

- [ ] QEMU window opens
- [ ] VGA output visible
- [ ] Can see text on screen

---

## ?? Visual Verification

When QEMU boots, you should see:

- [ ] **First line**: "guideXOS Kernel v0.1" in **cyan/light blue**
- [ ] **Second line**: "Copyright (c) 2024 guideX"
- [ ] **Blank line**
- [ ] **Architecture line**: "Architecture: x86 (32-bit) (32-bit)"
- [ ] **Another blank line**
- [ ] **Status**: "[ OK ] Interrupts disabled" with **green [ OK ]**
- [ ] **Status**: "[ OK ] Architecture initialized" with **green [ OK ]**
- [ ] **Blank line**
- [ ] **TODO section** in **yellow**: "TODO: Initialize kernel subsystems"
- [ ] List of subsystems to implement
- [ ] **Blank line**
- [ ] **Info messages** with **blue [INFO]**: 
  - "Kernel initialization complete"
  - "Entering idle loop (interrupts disabled)"

---

## ?? Common Issues & Quick Fixes

### Issue: "i686-elf-gcc: command not found"

**Fix:**
```bash
# Windows PowerShell
$env:PATH += ";C:\path\to\i686-elf-tools\bin"

# Linux/Mac Bash
export PATH=$PATH:/path/to/i686-elf-tools/bin
```

### Issue: "nasm: command not found"

**Fix:** Download and install NASM from https://www.nasm.us/

### Issue: Build errors about missing headers

**Check:**
- [ ] `kernel/core/include/kernel/vga.h` exists
- [ ] `kernel/core/include/kernel/arch.h` exists
- [ ] `kernel/core/include/kernel/version.h` exists
- [ ] `kernel/arch/x86/include/arch/x86.h` exists

### Issue: QEMU shows black screen

**Debug:**
```bash
# Run with debug output
qemu-system-i386 -kernel build/x86/bin/kernel.elf -d int,cpu_reset -D qemu.log

# Check the log
cat qemu.log
```

### Issue: QEMU exits immediately

**Possible causes:**
- Triple fault (CPU exception)
- Invalid kernel format
- Multiboot header missing

**Check:**
```bash
objdump -x build/x86/bin/kernel.elf | grep multiboot
```
Should show multiboot section.

---

## ?? Test Sequence

Follow this exact sequence for first boot:

1. [ ] **Clean build**: `make ARCH=x86 clean`
2. [ ] **Build kernel**: `make ARCH=x86`
3. [ ] **Verify output**: `ls -lh build/x86/bin/kernel.elf`
4. [ ] **Launch QEMU**: `./scripts/run-qemu-x86.sh` (or .bat)
5. [ ] **Wait 2-3 seconds** for QEMU to initialize
6. [ ] **Look for VGA output** in QEMU window
7. [ ] **Verify boot messages** appear (see Visual Verification above)
8. [ ] **Kernel should idle** (screen stays static)
9. [ ] **Exit QEMU**: Press `Ctrl+A` then `X` (Linux/Mac) or `Ctrl+C` (Windows)

---

## ? Success Criteria

### Minimum Success

- [x] Build completes without errors
- [x] QEMU launches without crashing
- [x] Screen shows text (any text)
- [x] No immediate crash/reboot

### Expected Success

- [x] All Minimum Success criteria
- [x] Boot banner displays correctly
- [x] Colors are visible (cyan, green, yellow, blue)
- [x] Status messages show "[ OK ]"
- [x] Kernel enters idle loop (screen stops updating)

### Perfect Success

- [x] All Expected Success criteria
- [x] Text is properly aligned
- [x] No garbage characters
- [x] No screen corruption
- [x] Clean exit from QEMU

---

## ?? If All Else Fails

### Diagnostic Steps

1. **Save build log**:
   ```bash
   make ARCH=x86 clean
   make ARCH=x86 > build.log 2>&1
   ```

2. **Save QEMU log**:
   ```bash
   qemu-system-i386 -kernel build/x86/bin/kernel.elf -d int,cpu_reset -D qemu.log
   ```

3. **Check file structure**:
   ```bash
   find kernel/core -name "*.cpp" -o -name "*.h"
   ```

4. **Verify Makefile sees vga.cpp**:
   ```bash
   make ARCH=x86 info
   ```

### Get Help

Include in your report:
- [ ] Build log (`build.log`)
- [ ] QEMU log (`qemu.log`)
- [ ] Output of `make ARCH=x86 info`
- [ ] Output of `file build/x86/bin/kernel.elf`
- [ ] Screenshot of QEMU window (if it shows anything)

---

## ?? Ready to Go!

If all checkboxes above are marked, you're ready to boot!

**Run this command:**

```bash
# Windows
scripts\test-build.bat

# Linux/Mac
./scripts/test-build.sh
```

**Expected result:** Kernel builds and you see the boot messages in QEMU!

---

**Good luck! ??**
