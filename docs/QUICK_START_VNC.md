# ? QUICK START - View GUI from VM

## ? In 3 Steps!

### 1?? Start the Server

```bash
./server
```

Then in the server console:

```bash
gui.start
vnc.start
```

### 2?? Launch an App

```bash
notepad
```

### 3?? Connect from VM

**From any VNC client:**
```
vnc://localhost:5900
```

**Done!** You should see the GUI! ??

---

## ? Recommended VNC Clients

**Windows:**
- [TightVNC](https://www.tightvnc.com/) - Download and run the viewer

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
Add to your QEMU command:
```bash
-netdev user,id=net0,hostfwd=tcp::5900-:5900 \
-device e1000,netdev=net0
```

### VirtualBox
1. VM Settings ? Network ? Port Forwarding
2. Add: Host Port 5900 ? Guest Port 5900

### VMware
Usually works out of the box with default bridged networking.

---

## ? Full Command Sequence

```bash
# Terminal 1: Start server
./server

# In server console:
gui.start          # Start compositor window
vnc.start          # Start VNC server (port 5900)

# Launch some apps to see:
notepad
calculator
files

# From VM or remote machine:
# Open VNC client and connect to: localhost:5900
```

---

## ? Verify It's Working

You should see:
- ? Compositor window on your host screen
- ? VNC server reports "running" with `vnc.status`
- ? VNC client can connect
- ? Same GUI appears in VNC viewer
- ? Any windows you open appear in both places

---

## ? Troubleshooting

**Can't connect?**
```bash
vnc.status          # Make sure server is running
```

**Port already in use?**
```bash
vnc.start 5901      # Try different port
```

**Black screen?**
```bash
gui.start           # Make sure compositor is running
notepad             # Launch an application
```

---

## ? Next: Full Guide

For detailed setup, see: **VNC_SETUP_GUIDE.md**

---

**Happy remote GUI development!** ??
