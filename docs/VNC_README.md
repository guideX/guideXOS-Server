# ? View guideXOS GUI from VM - README

## ? What This Is

A VNC server implementation that lets you **see the guideXOS compositor GUI from your VM or any remote client**.

---

## ? Quick Start

```bash
# 1. Start the server
./server

# 2. In server console:
gui.start          # Start compositor
vnc.start          # Start VNC server

# 3. Launch an app:
notepad

# 4. From VM, connect VNC client to:
vnc://localhost:5900
```

**Done!** You should see the GUI! ??

---

## ? What's Included

### Implementation Files
- `vnc_server.h` - VNC server interface
- `vnc_server.cpp` - Full RFB protocol implementation
- Modified `compositor.cpp` - Framebuffer capture
- Modified `server.cpp` - VNC commands

### Build & Documentation
- `build.bat` - Windows build script
- `QUICK_START_VNC.md` - 5-minute guide
- `VNC_SETUP_GUIDE.md` - Complete setup guide
- `VNC_IMPLEMENTATION_SUMMARY.md` - Technical details
- `GUI_FROM_VM_COMPLETE.md` - Full summary

---

## ? Features

? **Real-time GUI streaming** - See updates instantly
? **Multi-client support** - Multiple viewers can connect
? **Standard VNC protocol** - Works with any VNC client
? **Easy control** - Simple on/off commands
? **Cross-platform** - Windows and Linux compatible

---

## ? VNC Commands

```bash
vnc.start [port]   # Start VNC server (default port 5900)
vnc.stop           # Stop VNC server
vnc.status         # Check if running + client count
```

---

## ? Recommended VNC Clients

**Windows:**
- [TightVNC Viewer](https://www.tightvnc.com/)
- [RealVNC Viewer](https://www.realvnc.com/)

**Linux:**
```bash
sudo apt install tigervnc-viewer
vncviewer localhost:5900
```

**macOS:**
```bash
open vnc://localhost:5900
```

---

## ? VM Network Setup

### QEMU
```bash
-netdev user,id=net0,hostfwd=tcp::5900-:5900 \
-device e1000,netdev=net0
```

### VirtualBox
- Settings ? Network ? Port Forwarding
- Add: Host 5900 ? Guest 5900

---

## ? Build

```bash
# Windows
build.bat

# Linux/macOS (if cross-compiled)
make server
```

---

## ? Documentation

- **QUICK_START_VNC.md** - Get started in 5 minutes
- **VNC_SETUP_GUIDE.md** - Complete setup with troubleshooting
- **GUI_FROM_VM_COMPLETE.md** - Full implementation summary

---

## ? How It Works

```
Host: Compositor ? Framebuffer Capture ? VNC Server ? Network
VM:   Network ? VNC Client ? Display
```

1. Compositor renders GUI to Windows window
2. After each paint, framebuffer is captured
3. VNC server streams pixels via RFB protocol
4. VNC client receives and displays the GUI

---

## ? Testing

```bash
# Full test sequence:
./server           # Start server
gui.start          # Start compositor window
vnc.start          # Start VNC server
vnc.status         # Verify running
notepad            # Launch an app
calculator         # Launch another
# Connect VNC client: vnc://localhost:5900
vnc.stop           # When done
```

---

## ? Troubleshooting

**Can't start VNC?**
- Port 5900 in use: Try `vnc.start 5901`
- Check firewall settings

**Can't connect?**
- Verify: `vnc.status`
- Check VM port forwarding
- Try: `vnc://10.0.2.2:5900` from VM

**Black screen?**
- Make sure compositor is running: `gui.start`
- Launch an application: `notepad`

---

## ? Status

? **Implementation Complete**
? **Documentation Complete**
? **Ready to Build and Test**

---

## ? Next Steps

1. Build the server: `build.bat`
2. Start services: `gui.start`, `vnc.start`
3. Launch apps: `notepad`, `calculator`
4. Connect VNC client from VM
5. Enjoy your GUI! ??

---

**Questions? Check the full guides:**
- Quick start: **QUICK_START_VNC.md**
- Complete guide: **VNC_SETUP_GUIDE.md**
- Implementation: **VNC_IMPLEMENTATION_SUMMARY.md**

---

## ? License

Copyright (c) 2024 guideX

---

**Happy remote GUI development!** ??
