# How to Build and Run guideXOS

## ?? TL;DR

```powershell
# Build everything
.\build.ps1

# Run in QEMU
run-qemu.bat
```

Done! ??

---

## ?? What You Need

### Already Have ?
- Visual Studio 2022 (for bootloader)

### Need to Download ??

1. **OVMF.fd** (UEFI firmware)
```powershell
Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"
```

2. **MinGW** (for kernel - optional but recommended)
- Download: https://github.com/niXman/mingw-builds-binaries/releases
- Get the file: `x86_64-14.2.0-release-posix-seh-ucrt-rt_v12-rev0.7z`
- Extract to `C:\mingw64`
- Add `C:\mingw64\bin` to your PATH

3. **QEMU** (for testing - optional)
- Download: https://www.qemu.org/download/#windows
- Install and add `C:\Program Files\qemu` to PATH

---

## ?? Building

### Option 1: Build Everything (Recommended)

```powershell
.\build.ps1
```

This automatically:
- ? Builds the bootloader
- ? Builds the kernel (if MinGW installed)
- ? Creates ESP directory
- ? Copies BOOTX64.EFI
- ? Copies kernel.elf
- ? Tells you what's missing

### Option 2: Build Without Kernel

```powershell
.\build.ps1 -SkipKernel
```

Just builds the bootloader (if you don't have MinGW yet).

### Option 3: Clean Build

```powershell
.\build.ps1 -Clean
```

Removes all build artifacts first, then rebuilds.

---

## ?? Running

### Option 1: Use the Batch File (Easiest)

```batch
run-qemu.bat
```

Just double-click or run from command prompt.

### Option 2: Build and Run Together

```powershell
.\build.ps1 -RunQemu
```

Builds and immediately launches QEMU.

### Option 3: Manual QEMU

```batch
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M
```

---

## ?? What Gets Created

After running `.\build.ps1`, you'll have:

```
ESP/
??? EFI/
?   ??? BOOT/
?       ??? BOOTX64.EFI    ? Your bootloader (auto-copied)
??? kernel.elf             ? Your kernel (auto-copied, if built)
??? ramdisk.img            ? Empty ramdisk (auto-created)
```

---

## ?? What You'll See When Running

1. **TianoCore logo** (OVMF firmware starting up)
2. **"guideXOS UEFI Bootloader"** (your bootloader running)
3. **Boot splash** with animated progress bar
4. **"Waiting for init"** message (kernel stub)

---

## ? Troubleshooting

### "MSBuild not found"

You need Visual Studio 2019 or later with C++ support.

### "GNU make not found"

**Quick fix:** Install MinGW (see above)

**Or use WSL:**
```powershell
wsl --install
wsl -e bash -c "cd kernel && make ARCH=amd64"
```

### "OVMF.fd not found"

Run this:
```powershell
Invoke-WebRequest -Uri "https://github.com/kraxel/edk2/raw/binaries/OVMF.fd" -OutFile "OVMF.fd"
```

### "QEMU not found"

Install QEMU from https://www.qemu.org/download/#windows

Then add to PATH:
```powershell
$env:PATH = "$env:PATH;C:\Program Files\qemu"
```

---

## ? Pro Tips

### Rebuild After Changes

```powershell
# Make your changes
code kernel\core\main.cpp

# Quick rebuild and test
.\build.ps1 -RunQemu
```

### See Build Output

The build script shows you:
- ? What built successfully
- ?? What's missing
- ?? File sizes
- ?? ESP directory tree

### Check What You Have

```powershell
# Run build script to see status
.\build.ps1

# It will tell you:
# ? Bootloader: Built
# ? Kernel: Need MinGW
# ? OVMF: Need to download
# ? QEMU: Need to install
```

---

## ?? More Info

- **QUICK_START.md** - Detailed setup guide
- **BUILD_SUCCESS.md** - Build troubleshooting
- **UEFI_BOOT.md** - UEFI integration details

---

## ?? Current Status

| Component | Status |
|-----------|--------|
| Bootloader | ? Fully working |
| Kernel | ? Builds (needs MinGW) |
| Boot in QEMU | ? Working |
| Desktop | ? Coming soon |

---

**That's it! Now go build and run your OS! ??**

```powershell
.\build.ps1 -RunQemu
```
