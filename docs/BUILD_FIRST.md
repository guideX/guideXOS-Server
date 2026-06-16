# ? Quick Fix - Build First!

## The Message

```
Error: Kernel not found at kernel\build\x86\bin\kernel.elf
Please build the kernel first:
  cd kernel
  build-x86.bat
```

## What This Means

The kernel hasn't been built yet. You need to compile it first!

---

## ? Solution - Two Steps

### Step 1: Build
```bash
cd kernel
build-x86.bat
```

### Step 2: Run
```bash
cd ..
scripts\run-qemu-x86.bat
```

---

## Or Use The One-Liner

**From `guideXOSServer\scripts\` directory:**
```bash
cd ..\kernel && build-x86.bat && cd .. && scripts\run-qemu-x86.bat
```

**From `guideXOSServer\` (root) directory:**
```bash
cd kernel && build-x86.bat && cd .. && scripts\run-qemu-x86.bat
```

---

## Current Location Check

You're currently in: `guideXOSServer\scripts\`

To build and run from there:
```bash
cd ..\kernel
build-x86.bat
cd ..
scripts\run-qemu-x86.bat
```

---

## ?? The Right Way

Always run from the **root directory** (`guideXOSServer`):

```bash
# Go to root first
cd D:\devgitlab\guideXOS\guideXOSServer

# Then build and run
cd kernel && build-x86.bat && cd .. && scripts\run-qemu-x86.bat
```

---

**TL;DR: You forgot to build! Run `build-x86.bat` first!** ??
