# Network Setup Guide for guideXOS

## Problem Fixed

**Error**: "send failed error code 04" when trying to ping
**Root Cause**: QEMU was not configured to emulate a network card
**Solution**: Added network device configuration to all test scripts

## What Changed

All QEMU launch scripts now include network support:

```sh
-netdev user,id=net0        # User-mode networking (NAT)
-device e1000,netdev=net0   # Intel E1000 NIC (supported by kernel)
```

### Updated Scripts

1. ? **`run-qemu.bat`** - Windows UEFI boot (now with network)
2. ? **`run-uefi.sh`** - Linux UEFI boot (now with network)  
3. ? **`scripts/run-qemu-fs-test.ps1`** - Filesystem tests (now with network)
4. ? **`scripts/run-qemu-fs-test.sh`** - Filesystem tests Linux (now with network)

## Network Configuration

When you boot guideXOS with the updated scripts, the network is automatically configured:

- **Your IP**: `10.0.2.15`
- **Subnet**: `255.255.255.0` (10.0.2.0/24)
- **Gateway**: `10.0.2.2` (QEMU host)
- **DNS**: `10.0.2.3` (QEMU DNS proxy)

This is QEMU's standard "user networking" mode - it provides NAT without requiring root/admin privileges.

## Testing Network

### 1. Check Configuration
```sh
ifconfig
```

You should see:
- Interface: `eth0`
- IP: `10.0.2.15`
- MAC address
- TX/RX packet counts

### 2. Test Local Connectivity
```sh
ping 10.0.2.2
```

This pings the QEMU gateway. **This should work immediately.**

### 3. Test External Connectivity (if routing works)
```sh
ping 8.8.8.8
```

This pings Google's DNS server through NAT.

### 4. Test DNS (future)
```sh
ping google.com
```

This requires DNS resolution to work.

## What Won't Work

Some IP addresses are **unreachable** from guideXOS:

? **`192.168.0.1`** - Your host's local network (different subnet)
? **`10.0.2.15`** - Pinging yourself (loopback not fully implemented)
? **Host IP** - QEMU user networking is isolated

## Why This is Good

? **No admin/root required** - User networking mode needs no special privileges
? **Safe** - Isolated from host network
? **Portable** - Works the same on Windows, Linux, macOS
? **Simple** - No bridge/tap configuration needed

## Architecture Notes

### x86 vs amd64/UEFI

There's **no limitation** on features by architecture:

| Feature | x86 (32-bit) | amd64 (64-bit) | UEFI Boot |
|---------|--------------|----------------|-----------|
| **Networking** | ? Yes | ? Yes | ? Yes |
| **File Systems** | ? Yes | ? Yes | ? Yes |
| **Graphics** | ? Yes | ? Yes | ? Yes |

**UEFI** is just a **boot method** - it works with both 32-bit and 64-bit systems.

The scripts were simply focused on different test scenarios:
- `run-qemu.bat` ? Desktop/graphics testing (now also has network)
- `run-qemu-fs-test.*` ? Filesystem testing (now also has network)

### What's Actually Different

- **x86 (32-bit)**: Uses legacy BIOS boot, simpler
- **amd64 (64-bit)**: Uses UEFI boot, more modern
- **Boot method** doesn't affect kernel features!

## Filesystem + Network Together

The filesystem test scripts (`run-qemu-fs-test.*`) now support **both** filesystems **and** networking:

```powershell
# Windows
.\scripts\run-qemu-fs-test.ps1

# Linux
./scripts/run-qemu-fs-test.sh
```

You can now:
- Mount test disks
- Access network
- Test both subsystems together!

## Advanced Network Options

If you need more advanced networking (not included by default):

### Port Forwarding
```sh
-netdev user,id=net0,hostfwd=tcp::8080-:80
```
Forwards host port 8080 to guideXOS port 80.

### Custom IP Range
```sh
-netdev user,id=net0,net=192.168.100.0/24,dhcpstart=192.168.100.10
```
Changes the default IP range.

### TAP Networking (requires root)
```sh
-netdev tap,id=net0,ifname=tap0,script=no,downscript=no
-device e1000,netdev=net0
```
Bridges with host network (advanced, requires setup).

## Troubleshooting

### "send failed error code 04"
? **Fixed!** This meant the NIC wasn't configured in QEMU.

### "network interface not active"
Check serial output during boot for:
```
[NIC] Device detected but MMIO not mapped
```

This means:
- NIC found by PCI scan
- But MMIO region not accessible
- **Solution**: Use UEFI boot (bootloader maps MMIO)

### "ping: invalid IP address"
Syntax error in ping command. Use:
```sh
ping 10.0.2.2
```

### No reply from ping
1. Check NIC is active: `ifconfig`
2. Check network polling is working (code was fixed)
3. Try gateway first: `ping 10.0.2.2`

## Next Steps

Now that networking works, you can:

1. ? Test ping to gateway (`10.0.2.2`)
2. ? Test ping to external IPs (`8.8.8.8`)
3. ?? Implement DNS resolution
4. ?? Implement TCP/UDP sockets
5. ?? Build network applications

## Credits

Network polling fix: Added `ipv4::poll_network()` to receive packets
Script updates: Added `-netdev` and `-device` arguments to all QEMU scripts

---

**Last Updated**: 2025-01-20
**guideXOS Version**: Server Edition
