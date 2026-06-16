# ? VNC Server Setup Guide

## Overview

The guideXOS Server now includes a **VNC (Virtual Network Computing) server** that allows you to view and interact with the GUI from a VM or remote client.

---

## ? Quick Start

### 1. Start the guideXOS Server

```bash
# Build and run the server
./server
```

### 2. Start the Compositor

```bash
gui.start
```

The compositor window will open on your host machine.

### 3. Start the VNC Server

```bash
vnc.start
```

**Output:**
```
VNC server started on port 5900
Connect from VM with: vnc://localhost:5900
```

### 4. Connect from VM

From your VM or remote client, use any VNC viewer to connect:

**Built-in VNC Clients:**
- **Windows**: Use TightVNC, RealVNC, or UltraVNC
- **Linux**: `vncviewer localhost:5900` or use Remmina
- **macOS**: Built-in Screen Sharing app: `vnc://localhost:5900`

**From QEMU/VM:**
If your VM has network access to the host, connect to the host's IP address:
```
vnc://192.168.1.100:5900
```

---

## ? VNC Commands

### Start VNC Server

```bash
vnc.start [port]
```

**Examples:**
```bash
vnc.start          # Start on default port 5900
vnc.start 5901     # Start on custom port 5901
```

### Stop VNC Server

```bash
vnc.stop
```

### Check VNC Status

```bash
vnc.status
```

**Output:**
```
VNC server is running
Connected clients: 1
```

---

## ? Connection Information

### Default Settings

- **Protocol**: RFB (Remote Framebuffer) 3.8
- **Port**: 5900 (configurable)
- **Authentication**: None (can be added with password)
- **Resolution**: 1024x768 (32-bit RGBA)
- **Encoding**: Raw (uncompressed)

### Network Setup

**Same Machine:**
```
vnc://localhost:5900
```

**Local Network:**
```
vnc://192.168.1.100:5900
```

**From VM with NAT:**
Configure port forwarding in your VM settings:
- Host: 5900 ? Guest: 5900

---

## ? VNC Clients

### Recommended Clients

**Windows:**
- [TightVNC Viewer](https://www.tightvnc.com/) - Free, lightweight
- [RealVNC Viewer](https://www.realvnc.com/) - Feature-rich
- [UltraVNC](https://uvnc.com/) - Open source

**Linux:**
```bash
# Install TigerVNC
sudo apt install tigervnc-viewer

# Connect
vncviewer localhost:5900
```

**macOS:**
Built-in Screen Sharing app:
```bash
open vnc://localhost:5900
```

### Browser-Based (Advanced)

For web access, you can use **noVNC** (requires web server setup):
```bash
git clone https://github.com/novnc/noVNC
cd noVNC
./utils/novnc_proxy --vnc localhost:5900
```

Then open: `http://localhost:6080/vnc.html`

---

## ? Testing the Connection

### Test Workflow

1. **Start Server:**
   ```bash
   ./server
   ```

2. **Start Compositor:**
   ```bash
   gui.start
   ```

3. **Start VNC:**
   ```bash
   vnc.start
   ```

4. **Launch a Window:**
   ```bash
   notepad
   ```

5. **Connect with VNC Client:**
   ```
   vnc://localhost:5900
   ```

You should see the compositor window with Notepad open!

---

## ? Firewall Configuration

### Windows Firewall

If you're connecting from another machine, you may need to allow the port:

```powershell
# PowerShell (as Administrator)
New-NetFirewallRule -DisplayName "guideXOS VNC" -Direction Inbound -LocalPort 5900 -Protocol TCP -Action Allow
```

### Linux Firewall

```bash
# UFW
sudo ufw allow 5900/tcp

# firewalld
sudo firewall-cmd --add-port=5900/tcp --permanent
sudo firewall-cmd --reload
```

---

## ? VM Setup Examples

### QEMU with Network

When running your kernel in QEMU, you can access the VNC server on the host:

```bash
# QEMU command with network
qemu-system-i386 \
  -kernel build/x86/bin/kernel.elf \
  -m 128M \
  -netdev user,id=net0,hostfwd=tcp::5900-:5900 \
  -device e1000,netdev=net0
```

Then from inside QEMU (if networking is configured):
```
vnc://10.0.2.2:5900
```

### VirtualBox

1. **Network Settings:**
   - Go to VM Settings ? Network
   - Adapter 1: NAT
   - Port Forwarding: Host Port 5900 ? Guest Port 5900

2. **From VM:**
   ```
   vnc://10.0.2.2:5900
   ```

### VMware

Network is usually bridged by default, so just use the host IP:
```
vnc://192.168.1.100:5900
```

---

## ? Features

### Current Features

- ? **Real-time framebuffer streaming** - See GUI updates instantly
- ? **32-bit color depth** - Full RGBA color support
- ? **Low latency** - Direct memory access to compositor framebuffer
- ? **Multiple clients** - Support for multiple simultaneous viewers
- ? **Standard protocol** - Compatible with all VNC clients

### Planned Features

- ? **Keyboard input forwarding** - Type in VNC client
- ? **Mouse input forwarding** - Click and interact remotely
- ? **Clipboard sharing** - Copy/paste between host and VM
- ? **Password authentication** - Secure access
- ? **Compression** - Reduce bandwidth usage
- ? **TLS encryption** - Secure communication

---

## ? Troubleshooting

### "VNC server already running"

The server is already started. Use `vnc.stop` first, then `vnc.start` again.

### "Failed to start VNC server"

**Common causes:**
- Port 5900 is already in use by another application
- Firewall blocking the port
- Permission issues

**Solutions:**
```bash
# Try a different port
vnc.start 5901

# Check if port is in use (Windows)
netstat -an | findstr 5900

# Check if port is in use (Linux)
netstat -tulpn | grep 5900
```

### "Cannot connect from VNC client"

**Check:**
1. VNC server is running: `vnc.status`
2. Firewall allows port 5900
3. Using correct IP address
4. VM network is configured correctly

### "Black screen in VNC"

**Causes:**
- Compositor not started
- No windows open

**Solution:**
```bash
gui.start          # Make sure compositor is running
notepad            # Launch an application
```

---

## ? Performance Tips

### Optimize for Speed

1. **Use Raw encoding** - Already enabled by default
2. **Disable wallpaper** - Reduces bandwidth:
   ```bash
   desktop.wallpaper ""
   ```

3. **Close unused windows** - Less data to transmit

### Network Optimization

- **Local network**: Use wired connection for best performance
- **Reduce resolution**: If bandwidth is limited (future feature)
- **Use compression**: Enable in VNC client settings

---

## ? Example Session

```bash
# Terminal 1: Start server and services
./server
gui.start
vnc.start

# Terminal 2: Launch applications
notepad
calculator
files

# From VNC client: vnc://localhost:5900
# You should see all three windows!
```

---

## ? Security Considerations

### Current Setup

- ? **No authentication** - Anyone who can connect to the port can view
- ? **No encryption** - Data sent in plain text
- ? **Local only recommended** - Use on trusted networks only

### Future Enhancements

Will add:
- Password authentication
- TLS encryption
- IP whitelist
- Session logging

For now, **only use on trusted local networks** or **use SSH tunneling**:

```bash
# SSH tunnel for secure remote access
ssh -L 5900:localhost:5900 user@remote-host
```

Then connect to `vnc://localhost:5900` on your local machine.

---

## ? Next Steps

Now that you have VNC working:

1. ? **Set up your VM** with network access to host
2. ? **Configure port forwarding** in VM settings
3. ? **Connect with VNC client** from the VM
4. ? **Test applications** - Launch notepad, calculator, etc.
5. ? **Verify interactivity** - Check that GUI updates in real-time

---

## ? Success!

You can now view the guideXOS GUI from your VM! ??

**The compositor window on the host and the VNC view should show the same content.**

Happy remote development! ??
