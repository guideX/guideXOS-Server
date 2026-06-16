# Quick Reference: UEFI Boot

## Build and Run (One Command)

**Windows:**
```powershell
.\build-uefi.ps1 -RunQemu
```

**Linux/WSL:**
```bash
# Build kernel
cd kernel && make ARCH=amd64

# Run
./run-uefi.sh
```

## Build Only

**Windows:**
```powershell
# Full build
.\build-uefi.ps1

# Clean build
.\build-uefi.ps1 -Clean
```

**Linux:**
```bash
# Build kernel
cd kernel
make ARCH=amd64

# Set up ESP manually (after building bootloader)
mkdir -p ESP/EFI/BOOT
cp guideXOSBootLoader/x64/Release/guideXOSBootLoader.efi ESP/EFI/BOOT/BOOTX64.EFI
cp kernel/build/amd64/bin/kernel.elf ESP/kernel.elf
```

## Run Only

**UEFI Boot:**
```bash
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M
```

**Legacy Boot:**
```bash
qemu-system-i386 -kernel kernel/build/x86/bin/kernel.elf -m 128M
```

**Using Scripts:**
```bash
# Legacy
./scripts/run-qemu-x86.sh

# UEFI
./scripts/run-qemu-x86.sh uefi
```

## Prerequisites

### Download OVMF.fd

**Windows:**
1. Go to: https://github.com/tianocore/edk2/releases
2. Download OVMF.fd
3. Place in repository root

**Linux:**
```bash
sudo apt-get install ovmf
cp /usr/share/ovmf/OVMF.fd .
```

## Expected Directory Structure

```
guideXOSServer/
??? ESP/
?   ??? EFI/
?   ?   ??? BOOT/
?   ?       ??? BOOTX64.EFI  (bootloader)
?   ??? kernel.elf            (kernel)
?   ??? ramdisk.img          (optional)
??? OVMF.fd                   (UEFI firmware)
??? guideXOSBootLoader/       (bootloader source)
??? kernel/                   (kernel source)
??? build-uefi.ps1           (build script)
```

## Boot Modes

| Mode | Command | Arch | Bootloader |
|------|---------|------|------------|
| UEFI | `./run-uefi.sh` | x86-64 | guideXOSBootLoader |
| Legacy | `./scripts/run-qemu-x86.sh` | x86 | Multiboot |

## Troubleshooting

| Issue | Solution |
|-------|----------|
| OVMF.fd not found | Download from edk2 releases |
| ESP not found | Run `build-uefi.ps1` first |
| Kernel not found | Run `make ARCH=amd64` |
| Triple fault | Add `-d int,cpu_reset` to QEMU |
| Black screen | Check serial output (`-serial stdio`) |

## Quick Debug

```bash
# Full debug output
qemu-system-x86_64 \
    -bios OVMF.fd \
    -drive file=fat:rw:ESP,format=raw \
    -m 1024M \
    -serial stdio \
    -d int,cpu_reset \
    -no-reboot
```

## Files to Check

- **Bootloader output**: `guideXOSBootLoader/x64/Release/guideXOSBootLoader.efi`
- **Kernel output**: `kernel/build/amd64/bin/kernel.elf`
- **ESP bootloader**: `ESP/EFI/BOOT/BOOTX64.EFI`
- **ESP kernel**: `ESP/kernel.elf`

## Common Commands

```bash
# List ESP contents
tree ESP  # Windows
ls -R ESP  # Linux

# Check kernel size
ls -lh kernel/build/amd64/bin/kernel.elf

# Check bootloader size
ls -lh ESP/EFI/BOOT/BOOTX64.EFI

# Rebuild everything
make clean && ./build-uefi.ps1

# Test both modes
./scripts/run-qemu-x86.sh        # Legacy
./scripts/run-qemu-x86.sh uefi   # UEFI
```

## What You Should See

### UEFI Boot
1. TianoCore logo (OVMF)
2. "guideXOS UEFI Bootloader"
3. Boot splash with progress bar
4. "Waiting for init" message

### Legacy Boot
1. Immediate kernel start
2. Boot splash (if framebuffer)
3. Or text mode output
4. "Waiting for init" or text info

## Next Steps

1. ? UEFI boot working
2. ?? Implement ELF loader
3. ?? Implement user mode
4. ?? Launch guideXOSServer
