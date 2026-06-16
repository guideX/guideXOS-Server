# TROUBLESHOOTING: "Ping Failed Error Code 04"

## Quick Fix Steps

### 1. **Clean Build**
The changes we made need to be compiled into the kernel:

```powershell
# Clean everything first
Remove-Item -Recurse -Force ESP\kernel.elf -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force kernel\build\amd64 -ErrorAction SilentlyContinue

# Rebuild everything
.\build-uefi.ps1
```

### 2. **Verify QEMU Launch Script**
Make sure you're using the UPDATED `run-qemu.bat` that includes:
```
-netdev user,id=net0
-device e1000,netdev=net0
```

### 3. **Check Serial Output**
When you launch QEMU, watch the console output for:

```
=== PCI Enumeration ===
Found 1 network controller(s)
  [00:03.0] Vendor=8086 Device=100E Class=02/00 IRQ=11
    BAR0: Phys=... Size=... (32-bit or 64-bit)
    ** Supported Intel E1000 NIC **

*** Using NIC at [00:03.0] ***
    Vendor: 8086  Device: 100E
    MMIO Phys: ...  Size: ...
    PCI bus mastering enabled
```

If you **DON'T** see this, QEMU isn't providing the network device!

### 4. **Run Diagnostic Command**
Once booted in guideXOS:

```sh
nicinfo
```

Expected output if working:
```
=== NIC Diagnostic Information ===

Device Name: eth0
Vendor ID: 0x8086
Device ID: 0x100E
...
--- MMIO Status ---
MMIO Mapped: YES
MMIO Virtual Address: 0x...
MMIO Physical Address: 0x...

--- Operation Status ---
Active: YES
Link State: UP
```

## Common Issues

### Issue 1: NIC Not Found by Bootloader

**Symptom**: Bootloader says "Found 0 network controllers"

**Cause**: QEMU not configured with network device

**Fix**:
1. Check you're running `run-qemu.bat` (not an old script)
2. Verify QEMU command line includes `-netdev` and `-device` args
3. Try manually running QEMU with explicit network args

**Manual Test**:
```cmd
qemu-system-x86_64.exe ^
  -bios OVMF.fd ^
  -drive file=fat:rw:ESP,format=raw ^
  -netdev user,id=net0 ^
  -device e1000,netdev=net0 ^
  -m 1024M ^
  -serial stdio
```

### Issue 2: MMIO Not Mapped

**Symptom**: `nicinfo` shows "MMIO Mapped: NO"

**Cause**: Bootloader found NIC but didn't map its MMIO region

**Debug**:
- Check bootloader serial output for "Mapping NIC MMIO" message
- If missing, bootloader PCI enumeration failed
- If present but still not mapped, page table issue

**Fix**: Rebuild bootloader and kernel with latest code

### Issue 3: Network Device Wrong Type

**Symptom**: Bootloader finds network device but wrong vendor/device ID

**Cause**: QEMU using different NIC model

**Fix**: Explicitly specify E1000 in QEMU:
```
-device e1000,netdev=net0
```

NOT:
- `-device e1000e` (E1000E variant, different registers)
- `-device rtl8139` (Realtek, not supported)
- `-device virtio-net` (VirtIO, different driver needed)

### Issue 4: Stale Kernel Binary

**Symptom**: Code changes don't take effect

**Cause**: Old `kernel.elf` still in ESP directory

**Fix**:
```powershell
# Force clean
Remove-Item ESP\kernel.elf -Force
.\build-uefi.ps1
```

## Verification Checklist

Before running, verify:

- [ ] `run-qemu.bat` contains `-netdev user,id=net0`
- [ ] `run-qemu.bat` contains `-device e1000,netdev=net0`
- [ ] Kernel has been rebuilt with latest changes
- [ ] `ESP\kernel.elf` exists and is recent (check file timestamp)
- [ ] Bootloader has been rebuilt

## Test Sequence

1. **Clean build**:
   ```powershell
   .\build-uefi.ps1
   ```

2. **Launch QEMU**:
   ```powershell
   .\run-qemu.bat
   ```

3. **Watch serial output** for:
   - "PCI Enumeration"
   - "Found X network controllers"
   - "Using NIC at [XX:XX.X]"
   - "Mapping NIC MMIO"
   - "[NIC] E1000 initialization complete"

4. **In guideXOS shell**:
   ```sh
   nicinfo      # Check NIC status
   ifconfig     # Check IP config
   ping 10.0.2.2
   ```

## Expected Results

### Bootloader Output (Serial):
```
=== PCI Enumeration ===
Found 1 network controller(s)
  [00:03.0] Vendor=8086 Device=100E Class=02/00 IRQ=11
    BAR0: Phys=0xFEBC0000 Size=20000 (32-bit)
    ** Supported Intel E1000 NIC **

*** Using NIC at [00:03.0] ***
    Vendor: 8086  Device: 100E
    MMIO Phys: 00000000FEBC0000  Size: 20000
    PCI bus mastering enabled

...

Mapping NIC MMIO: FEBC0000 size 20000
NIC MMIO mapped: Phys=00000000FEBC0000 Virt=00000000FEBC0000
```

### Kernel Output (Serial):
```
[KERNEL] Initializing NIC driver...
[KERNEL] NIC info found in BootInfo, using mapped MMIO
[NIC] Initializing from BootInfo:
[NIC]   Vendor: 8086 Device: 100E
[NIC]   MMIO Phys: 00000000FEBC0000
[NIC]   MMIO Virt: 00000000FEBC0000
[NIC] Initializing E1000 hardware...
[NIC] MAC: 52:54:00:12:34:56
[NIC] Link: UP
[NIC] E1000 initialization complete!
```

### nicinfo Output (guideXOS Shell):
```
=== NIC Diagnostic Information ===

Device Name: eth0
Vendor ID: 0x8086
Device ID: 0x100E
PCI Location: 00:03.0
IRQ Line: 11
MAC Address: 52:54:00:12:34:56

--- MMIO Status ---
MMIO Mapped: YES
MMIO Virtual Address: 0xFEBC0000
MMIO Physical Address: 0xFEBC0000

--- Operation Status ---
Active: YES
Link State: UP

--- Statistics ---
TX Frames: 0   TX Bytes: 0
RX Frames: 0   RX Bytes: 0
TX Errors: 0   RX Errors: 0
Interrupts: 0
```

### Ping Output:
```
PING 10.0.2.2 56 data bytes
64 bytes from 10.0.2.2: icmp_seq=1 ttl=255 time=2 ms
64 bytes from 10.0.2.2: icmp_seq=2 ttl=255 time=1 ms
64 bytes from 10.0.2.2: icmp_seq=3 ttl=255 time=1 ms
64 bytes from 10.0.2.2: icmp_seq=4 ttl=255 time=1 ms

--- 10.0.2.2 ping statistics ---
4 packets tx, 4 rx, 0% loss
```

## If Still Failing

If after clean build and using correct script you still see "error code 04":

1. **Capture full serial output** from bootloader to error
2. **Run `nicinfo`** and paste output
3. **Check which script you ran**: `run-qemu.bat` or something else?
4. **Verify QEMU version**: `qemu-system-x86_64 --version`

## Alternative: Filesystem Test Script

The filesystem test script **also has network** now:

```powershell
.\scripts\run-qemu-fs-test.ps1
```

This has both filesystem testing AND network support.

---

**Last Updated**: 2025-01-20
**Error Code 04**: `ICMP_ERR_TX_FAILED` = NIC transmission failure
