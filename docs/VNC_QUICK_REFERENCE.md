# ?? VNC Remote Boot - Quick Reference

## ? Quick Start (3 Steps)

### 1?? Build
```bash
cd kernel && build-x86.bat
```

### 2?? Launch
```bash
scripts\run-qemu-x86-vnc.bat
```

### 3?? Connect
```bash
vncviewer [server-ip]:5900
```

**Done!** See your kernel from anywhere! ??

---

## ?? Command Reference

| Task | Windows | Linux/Mac |
|------|---------|-----------|
| **Build kernel** | `cd kernel && build-x86.bat` | `cd kernel && make ARCH=x86` |
| **Launch VNC** | `scripts\run-qemu-x86-vnc.bat` | `./scripts/run-qemu-x86-vnc.sh` |
| **Get IP** | `ipconfig` | `ifconfig` or `ip addr` |
| **Connect** | TightVNC ? server:5900 | `vncviewer server:5900` |

---

## ?? Connection Info

**Default Port:** 5900

**From Same Computer:**
```
vnc://localhost:5900
```

**From Local Network:**
```
vnc://192.168.1.100:5900
```

**From Internet (with port forward):**
```
vnc://your-public-ip:5900
```

---

## ?? What You'll See

```
????????????????????????????????
? guideXOS Kernel - Remote     ?
????????????????????????????????
?                              ?
?  ?? Red Test    Pattern      ?
?  ?? Green Test  Working!     ?
?  ?? Blue Test   Graphics OK  ?
?                              ?
?  White Border (All sides)    ?
?                              ?
?  Console Text:               ?
?  guideXOS Kernel v0.1        ?
?  Architecture: x86 (32-bit)  ?
?  [ OK ] Framebuffer init     ?
?                              ?
????????????????????????????????
```

---

## ?? Customization

### Change Resolution

Edit `kernel/arch/x86/boot.asm`:
```asm
dd 1920    ; width (change this)
dd 1080    ; height (change this)
dd 32      ; depth (keep 32-bit)
```

### Change Port

Edit launch script:
```bash
-vnc :0    # Port 5900 (default)
-vnc :1    # Port 5901
-vnc :2    # Port 5902
```

### Add Password

```bash
qemu-system-i386 -kernel kernel.elf -m 128M -vnc :0,password
```

---

## ?? Quick Fixes

| Problem | Solution |
|---------|----------|
| **Connection refused** | Check firewall, verify IP |
| **Black screen** | Rebuild kernel, check boot.asm |
| **Slow VNC** | Use local network, try compression |
| **Can't find kernel** | Build first: `cd kernel && build-x86.bat` |

---

## ?? Pro Tips

**Multiple Viewers:**
Everyone can connect at once!
```bash
# Computer 1, 2, 3... all connect:
vncviewer server:5900
```

**Firewall (Windows):**
```powershell
New-NetFirewallRule -DisplayName "QEMU VNC" -Direction Inbound -LocalPort 5900 -Protocol TCP -Action Allow
```

**Firewall (Linux):**
```bash
sudo ufw allow 5900/tcp
```

**SSH Tunnel (Secure):**
```bash
ssh -L 5900:localhost:5900 user@server
vncviewer localhost:5900
```

---

## ?? Full Documentation

- **VNC_REMOTE_BOOT_GUIDE.md** - Complete guide
- **VNC_IMPLEMENTATION_COMPLETE.md** - What was done
- **VNC_REMOTE_BOOT_PLAN.md** - Architecture details

---

## ? Success Checklist

- [ ] Kernel builds: `build/x86/bin/kernel.elf` exists
- [ ] Script runs: Shows "VNC Server available on..."
- [ ] Client connects: No "connection refused" error
- [ ] Test pattern visible: Red/Green/Blue boxes
- [ ] Text visible: "guideXOS Kernel v0.1"
- [ ] Border visible: White frame around screen

---

## ?? Use Cases

**Development:** Code on laptop, run on desktop
**Demos:** Present to remote team
**Teaching:** Students view instructor's kernel
**Testing:** Automated remote monitoring
**Debugging:** View crashes from another machine

---

**Need Help?** Check **VNC_REMOTE_BOOT_GUIDE.md** for detailed instructions!

**Happy remote kernel development!** ??
