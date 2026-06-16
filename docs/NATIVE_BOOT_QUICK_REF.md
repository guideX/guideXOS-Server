# ?? Native Boot - Quick Reference

## ? 3 Steps to Boot

```bash
# 1. Build
cd kernel && build-x86.bat

# 2. Run
scripts\run-qemu-x86.bat

# 3. Watch it boot!
```

---

## ?? What You'll See

### 1. Boot Splash (2 seconds)
- Dark gradient background
- "guideXOS" title area
- Progress bar animation

### 2. Desktop
- Teal gradient background
- Taskbar at bottom
- Start button
- Welcome window

---

## ?? Command Reference

| Task | Command |
|------|---------|
| **Build** | `cd kernel && build-x86.bat` |
| **Run (Native)** | `scripts\run-qemu-x86.bat` |
| **Run (VNC)** | `scripts\run-qemu-x86-vnc.bat` |
| **Clean** | `cd kernel && make clean` |
| **Rebuild** | `cd kernel && make clean && make ARCH=x86` |

---

## ?? Visual Layout

```
???????????????????????????????????????
?  [Teal Gradient Background]         ?
?                                     ?
?    ?? Welcome ???????????           ?
?    ?  guideXOS           ?           ?
?    ?  Version 0.1        ?           ?
?    ???????????????????????           ?
?                                     ?
???????????????????????????????????????
? [Start] [Taskbar]                   ?
???????????????????????????????????????
```

---

## ? Features

- ? Real kernel boot (not simulation!)
- ? Framebuffer graphics (1024x768x32)
- ? Boot splash screen
- ? Desktop environment
- ? VNC optional (for remote viewing)
- ? Text mode fallback (if no graphics)

---

## ?? VNC (Optional)

**To enable remote viewing:**
```bash
scripts\run-qemu-x86-vnc.bat
```

**Then connect from another computer:**
```bash
vncviewer server-ip:5900
```

---

## ?? Troubleshooting

| Problem | Solution |
|---------|----------|
| **Build failed** | Check that all kernel files exist |
| **Black screen** | Framebuffer init failed, check QEMU output |
| **Text mode only** | Normal fallback, graphics not available |
| **QEMU not found** | Install QEMU or update path in script |

---

## ?? Full Documentation

- **NATIVE_BOOT_GUIDE.md** - Complete guide
- **NATIVE_BOOT_COMPLETE.md** - Implementation summary
- **VNC_REMOTE_BOOT_GUIDE.md** - VNC optional feature

---

## ?? Like C# guideXOS

This boots **exactly like** the C# version:
- Same Multiboot initialization
- Same framebuffer access
- Same boot splash
- Same desktop gradient
- Same architecture

**But in C++!** ??

---

**Happy OS development!** ?
