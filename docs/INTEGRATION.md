# Integration Guide: Bootloader ? Kernel ? Server

This guide explains how the three layers communicate and integrate.

## Data Flow

```
????????????????????
?   Bootloader     ?
?   (UEFI)         ?
????????????????????
        ?
        ? BootInfo* passed as parameter
        ?
????????????????????
?     Kernel       ?
?   (kernel.elf)   ?
????????????????????
        ?
        ? Launch via ELF loader (TODO)
        ? Framebuffer mapped to user space via syscall
        ?
????????????????????
? guideXOSServer   ?
?   (user mode)    ?
????????????????????
```

## 1. Bootloader ? Kernel

### Entry Point

**Bootloader** calls kernel entry point:
```cpp
// In guideXOSBootLoader/main.cpp
BootHandoffTrampoline(
    (void*)(UINTN)entryVirt,    // Kernel entry address
    (void*)v1BootInfo,           // BootInfo structure
    stackTop,                    // Stack pointer
    (void*)(UINTN)pt.Pml4Phys   // Page table
);
```

**Kernel** receives control:
```cpp
// In kernel/core/main.cpp
extern "C" void kernel_main(void* multiboot_info, uint32_t multiboot_magic)
```

### BootInfo Structure

Defined in `guideXOSBootLoader/guidexOSBootInfo.h`:

```cpp
namespace guideXOS {
    struct BootInfo {
        uint32_t Magic;              // 0x474D4F53 ("GXOS")
        uint16_t Version;            // 1
        uint16_t Size;               // sizeof(BootInfo)
        uint32_t HeaderChecksum;     // Checksum
        uint32_t Flags;              // Feature flags
        
        // Boot mode
        uint8_t BootMode;            // Uefi, Legacy, etc.
        
        // Memory map
        uint64_t MemoryMap;          // Physical address
        uint64_t MemoryMapEntryCount;
        uint64_t MemoryMapDescriptorSize;
        
        // Framebuffer
        uint64_t FramebufferBase;
        uint32_t FramebufferWidth;
        uint32_t FramebufferHeight;
        uint32_t FramebufferPitch;
        uint64_t FramebufferSize;
        uint8_t  FramebufferFormat;
        
        // ACPI
        uint64_t AcpiRsdp;
        
        // Ramdisk
        uint64_t RamdiskBase;
        uint64_t RamdiskSize;
        
        // ... other fields
    };
}
```

### What Kernel Does

```cpp
// Parse BootInfo
auto* mb_info = reinterpret_cast<kernel::multiboot::Info*>(multiboot_info);

// Initialize framebuffer from BootInfo
bool has_fb = kernel::framebuffer::init(multiboot_info);

// Use memory map from BootInfo for PMM initialization (TODO)
// Use ACPI RSDP from BootInfo (TODO)
// Access ramdisk from BootInfo to load init process (TODO)
```

## 2. Kernel ? guideXOSServer

### Current Status: NOT YET IMPLEMENTED

The kernel needs to:

1. **Load server ELF from ramdisk**
   ```cpp
   // TODO in kernel/core/process.cpp
   void launch_init_process() {
       // 1. Read /sbin/guideXOSServer from ramdisk
       // 2. Parse ELF headers
       // 3. Load segments to user memory
       // 4. Set up user page tables
       // 5. Create process structure
       // 6. Jump to user mode at ELF entry point
   }
   ```

2. **Provide syscalls for server to access hardware**
   ```cpp
   // Syscalls needed by guideXOSServer:
   // - sys_mmap_framebuffer() - Map framebuffer to user space
   // - sys_read() / sys_write() - File I/O
   // - sys_fork() / sys_exec() - Process management
   // - sys_ipc_send() / sys_ipc_recv() - IPC
   ```

### Server Initialization (Future)

Once kernel loads server:

```cpp
// In server.cpp (future)
int main(int argc, char** argv) {
    // Server runs in USER MODE
    // No direct hardware access
    // All hardware via syscalls
    
    // 1. Request framebuffer mapping from kernel
    void* fb = syscall_mmap_framebuffer();
    
    // 2. Initialize compositor with mapped framebuffer
    Lifecycle::bootstrap();
    
    // 3. Show desktop
    Lifecycle::ensureCompositor();
    
    // 4. Main event loop
    while (true) {
        // Handle events, render GUI
    }
}
```

## 3. Communication Interfaces

### Kernel Exports to Server

**System Calls (TODO):**

| Syscall | Purpose |
|---------|---------|
| `sys_mmap` | Map framebuffer to user space |
| `sys_read` | Read from files/devices |
| `sys_write` | Write to files/devices |
| `sys_fork` | Create new process |
| `sys_exec` | Execute program |
| `sys_wait` | Wait for child process |
| `sys_ipc_send` | Send IPC message |
| `sys_ipc_recv` | Receive IPC message |

**Syscall Interface (x86-64):**
```asm
; User mode calls kernel
mov rax, syscall_number
mov rdi, arg1
mov rsi, arg2
mov rdx, arg3
syscall  ; CPU mode switch to kernel
; rax = return value
```

### Server Exports to Applications

**IPC Protocol:**

Applications communicate with server via IPC bus:

```cpp
// In user application
ipc::Message msg;
msg.type = (uint32_t)gui::MsgType::MT_Create;
msg.data = "WindowTitle|640|480";
ipc::Bus::publish("gui.input", std::move(msg), false);

// Server receives and processes
ipc::Message response;
ipc::Bus::pop("gui.input", response, 100);
```

## 4. Resource Ownership

### Bootloader owns:
- ? Loading kernel ELF
- ? Collecting boot information
- ? Setting up initial page tables
- ? NOT server, NOT applications

### Kernel owns:
- ? Physical memory management
- ? Process scheduling
- ? Interrupt handling
- ? Device drivers (minimal)
- ? Framebuffer (hardware access)
- ? NOT desktop UI, NOT window management

### guideXOSServer owns:
- ? Compositor / window manager
- ? Desktop environment
- ? System services
- ? Application framework
- ? NOT hardware access (uses syscalls)

### Applications own:
- ? Their own UI windows
- ? Application logic
- ? NOT direct hardware access
- ? NOT other windows

## 5. Build Integration

### Current Build Process

```powershell
# build.ps1
# 1. Build kernel
cd kernel
make ARCH=amd64

# 2. Build UEFI bootloader
cd guideXOSBootLoader
build -a X64 -t VS2019 -p guideXOSBootLoader.dsc

# 3. Copy artifacts to ESP/
cp kernel/build/amd64/bin/kernel.elf ESP/kernel.elf
cp guideXOSBootLoader/Build/.../BOOTX64.EFI ESP/EFI/BOOT/BOOTX64.EFI
```

### Future Build Process

```powershell
# build.ps1 (future)
# 1. Build kernel
cd kernel
make ARCH=amd64

# 2. Build guideXOSServer as ELF
cd ..
g++ -o guideXOSServer.elf server.cpp compositor.cpp ... -static

# 3. Create ramdisk with server
mkdir ramdisk/sbin
cp guideXOSServer.elf ramdisk/sbin/guideXOSServer
./tools/mkramdisk ramdisk/ ramdisk.img

# 4. Build UEFI bootloader
cd guideXOSBootLoader
build -a X64 -t VS2019 -p guideXOSBootLoader.dsc

# 5. Copy to ESP
cp kernel/build/amd64/bin/kernel.elf ESP/kernel.elf
cp ramdisk.img ESP/ramdisk.img
cp guideXOSBootLoader/Build/.../BOOTX64.EFI ESP/EFI/BOOT/BOOTX64.EFI
```

## 6. Debugging Integration

### Bootloader Debug
- Serial output (COM1)
- Framebuffer markers (colored squares)
- QEMU `-d int,cpu_reset` flags

### Kernel Debug
- Serial output (if driver present)
- VGA text mode fallback
- Framebuffer messages

### Server Debug
- Kernel syscall tracing
- IPC bus logging
- Application event logs

## 7. Testing Integration

### Integration Test Sequence

```bash
# 1. Build everything
./build.ps1

# 2. Launch in QEMU
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M -serial stdio

# 3. Expected output:
# [Bootloader] guideXOS UEFI Bootloader
# [Bootloader] Kernel loaded at 0x...
# [Bootloader] Jumping to kernel...
# [Kernel] guideXOSServer Kernel v0.1
# [Kernel] Architecture: amd64 (64-bit)
# [Kernel] [ OK ] Interrupts disabled
# [Kernel] [ OK ] Architecture initialized
# [Kernel] [ OK ] Process subsystem initialized
# [Kernel] [TODO] Load and launch guideXOSServer as init process
# (Screen shows boot splash, then "waiting for init")

# 4. Future (when ELF loader works):
# [Server] guideXOSServer starting...
# [Server] Compositor pid=1
# (Desktop environment appears)
```

## 8. Migration Path

### Phase 1: Current State ?
- Bootloader loads kernel
- Kernel shows boot splash
- Kernel enters idle loop

### Phase 2: ELF Loader (In Progress) ??
- Kernel reads ramdisk
- Kernel parses ELF
- Kernel loads server to memory
- Kernel creates process structure

### Phase 3: User Mode ?
- Kernel sets up user page tables
- Kernel maps framebuffer to user space
- Kernel jumps to user mode
- Server runs as PID 1

### Phase 4: Syscalls ?
- Server requests framebuffer mapping
- Server uses syscalls for I/O
- Server launches applications
- Applications use IPC

### Phase 5: Full Integration ?
- Desktop appears on boot
- Applications run in user mode
- Full system services
- Multi-user support

## Summary

**Integration points:**

1. **Bootloader ? Kernel:** BootInfo structure passed as parameter
2. **Kernel ? Server:** ELF loading + syscall interface (TODO)
3. **Server ? Apps:** IPC bus protocol

**Missing pieces:**

- [ ] ELF loader in kernel
- [ ] Syscall interface
- [ ] User-mode execution
- [ ] Framebuffer mapping syscall
- [ ] Ramdisk filesystem driver

**Next steps:**

1. Implement ELF loader in kernel
2. Create ramdisk with server ELF
3. Implement basic syscall handler
4. Test user-mode execution
5. Map framebuffer to user space
6. Launch server as PID 1
