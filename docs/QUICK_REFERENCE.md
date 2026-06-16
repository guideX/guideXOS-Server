# guideXOS Quick Reference

## Architecture at a Glance

```
Firmware ? Bootloader ? Kernel ? guideXOSServer ? Apps
```

## Layer Responsibilities

| Layer | Responsibilities | Forbidden |
|-------|-----------------|-----------|
| **Bootloader** | Load kernel, provide BootInfo | Load server, run apps |
| **Kernel** | Minimal services, process mgmt, syscalls | Desktop UI, user services |
| **guideXOSServer** | Compositor, desktop, system services | Direct hardware access, BootInfo |
| **Apps** | User applications | Kernel mode, other apps' memory |

## Key Files

| File | Purpose | Layer |
|------|---------|-------|
| `guideXOSBootLoader/main.cpp` | UEFI entry point | Bootloader |
| `guideXOSBootLoader/guidexOSBootInfo.h` | BootInfo structure | Contract |
| `kernel/core/main.cpp` | Kernel entry point | Kernel |
| `kernel/core/process.cpp` | Process management | Kernel |
| `server.cpp` | Server main entry | User mode |
| `compositor.cpp` | Window manager | User mode |

## Communication Flow

```
BootInfo (struct) ? Kernel receives at boot
Syscalls (interface) ? Kernel ? Server
IPC Bus (messages) ? Server ? Apps
```

## Current Status

| Component | Status | Next Step |
|-----------|--------|-----------|
| Bootloader | ? Working | - |
| Kernel | ? Boots, shows splash | Implement ELF loader |
| guideXOSServer | ? Code ready | Compile to ELF |
| User mode | ? Not implemented | Implement user-mode switch |
| Syscalls | ? Not implemented | Create syscall handler |

## Common Commands

```bash
# Build kernel only
cd kernel && make ARCH=x86

# Run in QEMU (if build.ps1 configured)
./build.ps1
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M

# Check architecture documentation
cat ARCHITECTURE.md
cat INTEGRATION.md
```

## Decision Tree: Where Does This Feature Go?

```
?? Needs hardware access directly? ??? YES ? Kernel (or syscall)
?
?? Boot-time only? ??? YES ? Bootloader or Kernel
?
?? Desktop/GUI related? ??? YES ? guideXOSServer
?
?? User application? ??? YES ? Separate app
?
?? System service? ??? Kernel or guideXOSServer (prefer user mode)
```

## Architecture Rules (Non-Negotiable)

1. **Bootloader boots kernel, NOT server**
2. **Kernel is boot-aware (receives BootInfo)**
3. **Server is boot-agnostic (no BootInfo access)**
4. **No layer collapsing**
5. **Desktop/GUI in user mode, NOT kernel**

## Missing Components (TODO)

- [ ] ELF loader in kernel
- [ ] Syscall interface
- [ ] User-mode execution
- [ ] Framebuffer mapping syscall
- [ ] Ramdisk filesystem driver
- [ ] Server compiled as ELF

## Anti-Patterns to Avoid

? Don't:
- Put GUI in kernel
- Make bootloader load server
- Give server access to BootInfo
- Bypass kernel to launch apps
- Put desktop code in kernel

? Do:
- Keep kernel minimal
- Use syscalls for hardware
- Put logic in user mode
- Create stubs for missing parts
- Document layer boundaries

## Getting Help

- **Architecture questions:** See `ARCHITECTURE.md`
- **Integration questions:** See `INTEGRATION.md`
- **Build issues:** Check component-specific docs
- **Boot issues:** Check bootloader serial output

## Quick Start for New Developers

1. Read `README.md`
2. Read `ARCHITECTURE.md`
3. Read `INTEGRATION.md`
4. Review decision tree above
5. Follow architecture rules

## Success Criteria

A feature is correctly implemented if:
- ? It's in the right layer
- ? It follows architecture rules
- ? It doesn't violate boundaries
- ? It's documented
- ? The system still boots

---

**Remember:** When in doubt, prefer user mode over kernel mode!
