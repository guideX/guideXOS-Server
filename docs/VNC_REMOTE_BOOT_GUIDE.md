# ?? VNC Remote Boot Guide

## Overview

Your guideXOS kernel now supports **VNC remote viewing**! This allows you to:
- Boot the kernel on one computer (Server)
- View and control it from another computer (Client)
- Perfect for remote development, demos, and testing

---

## ?? How It Works

```
???????????????????????????????????????
?      Computer A (Server)            ?
?                                     ?
?  QEMU + VNC Server (Port 5900)     ?
?         ?                           ?
?  guideXOS Kernel                    ?
?         ?                           ?
?  Framebuffer (1024x768x32)         ?
???????????????????????????????????????
                  ? Network
                  ? VNC Protocol
                  ?
???????????????????????????????????????
?      Computer B (Client)            ?
?                                     ?
?  VNC Viewer                         ?
?  (TightVNC/RealVNC/built-in)      ?
?         ?                           ?
?  See guideXOS GUI in real-time     ?
???????????????????????????????????????
```

---

## ?? Quick Start

### On Server Computer (Where Kernel Runs)

**1. Build the kernel:**
```bash
cd kernel
build-x86.bat    # Windows
# or
make ARCH=x86    # Linux/Mac
```

**2. Launch with VNC:**
```bash
# Windows
scripts\run-qemu-x86-vnc.bat

# Linux/Mac
chmod +x scripts/run-qemu-x86-vnc.sh
./scripts/run-qemu-x86-vnc.sh
```

**You'll see:**
```
Starting guideXOS Kernel with VNC support...

VNC Server will be available on:
  - localhost:5900 (from this computer)
  - YOUR-PC:5900 (from network)
  - 192.168.1.100:5900 (from other computers)

Connect with any VNC client:
  vncviewer localhost:5900
```

### On Client Computer (Where You View)

**Option 1: Built-in VNC Viewers**

**Windows:**
- Download [TightVNC Viewer](https://www.tightvnc.com/download.php)
- Or [RealVNC Viewer](https://www.realvnc.com/en/connect/download/viewer/)
- Connect to: `192.168.1.100:5900` (use server's IP)

**Linux:**
```bash
# Install VNC viewer
sudo apt install tigervnc-viewer

# Connect
vncviewer 192.168.1.100:5900
```

**macOS:**
```bash
# Use built-in VNC client
open vnc://192.168.1.100:5900
```

**Option 2: Web Browser (noVNC)**

If you have noVNC installed on the server:
```
http://192.168.1.100:6080/vnc.html?host=192.168.1.100&port=5900
```

---

## ?? Configuration

### Change VNC Port

Default is 5900. To use a different port:

**Edit script:**
```bash
# Change this line:
-vnc :0        # Port 5900

# To:
-vnc :1        # Port 5901
-vnc :2        # Port 5902
# etc.
```

### Add VNC Password

For security, add password protection:

**Create password file:**
```bash
# Linux/Mac
echo "your_password" > vnc.passwd
chmod 600 vnc.passwd
```

**Use in QEMU:**
```bash
qemu-system-i386 \
    -kernel kernel.elf \
    -m 128M \
    -vnc :0,password \
    -serial stdio
```

QEMU will prompt for password when client connects.

### Resolution

Current: 1024x768x32

To change, edit `kernel/arch/x86/boot.asm`:
```asm
dd 1920                    ; width (change to 1920 for HD)
dd 1080                    ; height (change to 1080 for HD)
dd 32                      ; depth (keep 32-bit color)
```

Then rebuild kernel.

---

## ?? Network Setup

### Same Network (Easy)

If both computers are on the same network:
1. Server: Run `scripts/run-qemu-x86-vnc.bat`
2. Note your IP address (shown in script output)
3. Client: Connect to that IP on port 5900

### Different Networks (Port Forwarding)

If server is behind router:

**1. Forward port on router:**
- Port: 5900
- Protocol: TCP
- Forward to: Server's local IP

**2. Connect from client:**
```
vnc://your-public-ip:5900
```

Get public IP: https://whatismyipaddress.com/

### VPN/SSH Tunnel (Most Secure)

**Option 1: VPN**
- Use VPN software (OpenVPN, WireGuard)
- Both computers on same VPN network
- Connect via VPN IP

**Option 2: SSH Tunnel**
```bash
# On client:
ssh -L 5900:localhost:5900 user@server-ip

# Then connect to:
vncviewer localhost:5900
```

---

## ?? What You'll See

When you connect via VNC, you'll see:

```
???????????????????????????????????????????
? guideXOS Kernel - VNC Remote View      ?
???????????????????????????????????????????
?                                         ?
?  ?? Red Rectangle    (Test Pattern)    ?
?  ?? Green Rectangle  (Graphics Working) ?
?  ?? Blue Rectangle   (Framebuffer OK)   ?
?                                         ?
?  White Border (Around screen)           ?
?                                         ?
?  + VGA Text Output (Console)            ?
?                                         ?
?  guideXOS Kernel v0.1                   ?
?  Architecture: x86 (32-bit)             ?
?  [ OK ] Framebuffer initialized         ?
?                                         ?
???????????????????????????????????????????
```

---

## ?? Troubleshooting

### "Connection refused" on client

**Causes:**
- VNC server not started
- Wrong IP address
- Firewall blocking port 5900

**Solutions:**
1. Check server is running: Script should show "VNC Server available"
2. Verify IP: Use `ipconfig` (Windows) or `ifconfig` (Linux)
3. Check firewall:

**Windows:**
```powershell
# Allow port 5900
New-NetFirewallRule -DisplayName "QEMU VNC" -Direction Inbound -LocalPort 5900 -Protocol TCP -Action Allow
```

**Linux:**
```bash
# UFW
sudo ufw allow 5900/tcp

# firewalld
sudo firewall-cmd --add-port=5900/tcp --permanent
sudo firewall-cmd --reload
```

### Black screen in VNC

**Causes:**
- Kernel not built with framebuffer support
- QEMU framebuffer not initialized

**Solutions:**
1. Rebuild kernel with new boot.asm
2. Check QEMU output for errors
3. Try VGA mode first (text output should work)

### Slow/Laggy VNC

**Causes:**
- Network latency
- High resolution
- Uncompressed VNC encoding

**Solutions:**
1. Use local network (not internet)
2. Reduce resolution (edit boot.asm)
3. Use TightVNC (supports compression)
4. Try `vncviewer -compresslevel 9 server:5900`

### Can't connect from another computer

**Checklist:**
- [ ] Server shows correct IP in output
- [ ] Both computers on same network
- [ ] Firewall allows port 5900
- [ ] Using correct IP:PORT format
- [ ] VNC client compatible with RFB protocol

---

## ?? Performance

### Typical Latency

| Connection | Latency | Quality |
|------------|---------|---------|
| Same machine | <1ms | Perfect |
| Local network | 1-5ms | Excellent |
| Same building | 5-20ms | Good |
| Internet | 50-200ms | Acceptable |

### Bandwidth Usage

- **Idle**: ~1 KB/s (mostly unchanged pixels)
- **Active**: ~10-50 KB/s (normal GUI updates)
- **Heavy**: ~1-5 MB/s (full screen redraws)

---

## ?? Security Considerations

### Current Setup (Development)

- ? No authentication
- ? No encryption
- ? Anyone on network can connect

### Production Recommendations

1. **Add password** (see Configuration section)
2. **Use SSH tunnel** for encryption
3. **Firewall rules** to restrict access
4. **VPN** for remote access

---

## ?? Use Cases

### 1. Remote Development
- Code on laptop
- Test on desktop
- View via VNC

### 2. Demonstrations
- Present to team
- Everyone views same screen
- No projector needed

### 3. Testing
- Run on server
- Monitor from dev machine
- Automate tests

### 4. Debugging
- Kernel crash?
- View from another computer
- Serial output still available

---

## ?? Advanced: Multiple Clients

QEMU VNC supports multiple viewers simultaneously!

**Server:**
```bash
scripts/run-qemu-x86-vnc.bat
```

**Clients (all at once):**
```bash
# Computer 1
vncviewer 192.168.1.100:5900

# Computer 2  
vncviewer 192.168.1.100:5900

# Computer 3
vncviewer 192.168.1.100:5900

# All see the same kernel!
```

Perfect for:
- Teaching/training
- Code reviews
- Team debugging

---

## ?? Summary

### What You Have Now

? Kernel with framebuffer support
? QEMU configured for VNC
? Launch scripts for easy start
? Network-accessible display
? Multi-client support

### What You Can Do

- Boot kernel on one computer
- View from any other computer
- Share screen with team
- Remote debugging
- Demos without hardware

### Next Steps

1. Build: `cd kernel && build-x86.bat`
2. Start: `scripts\run-qemu-x86-vnc.bat`
3. Connect: `vncviewer server-ip:5900`
4. Develop!

---

## ?? You're Done!

Your kernel is now VNC-enabled and accessible from anywhere on your network!

**Test it:**
1. Start kernel on one computer
2. Grab your laptop
3. Connect via VNC
4. See your kernel running remotely!

**Happy remote development!** ??
