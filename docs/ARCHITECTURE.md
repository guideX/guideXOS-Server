# guideXOS Architecture

## Layer Separation (Non-Negotiable)

guideXOS follows a strictly layered architecture. **Do not collapse layers or bypass the kernel.**

```
???????????????????????????????????????
?         Firmware (UEFI/BIOS)        ?
???????????????????????????????????????
                  ?
???????????????????????????????????????
?    guideXOSBootLoader (UEFI only)   ?
?  - Loads kernel.elf                 ?
?  - Provides BootInfo to kernel      ?
?  - Exits boot services              ?
?  - Jumps to kernel entry            ?
???????????????????????????????????????
                  ?
???????????????????????????????????????
?         Kernel (kernel.elf)         ?
?  - Minimal, microkernel-style       ?
?  - Boot-aware (receives BootInfo)   ?
?  - Memory management                ?
?  - Process scheduling               ?
?  - System calls                     ?
?  - Device drivers (minimal)         ?
?  - NO GUI rendering (minimal boot   ?
?    splash only)                     ?
?  - Launches init process            ?
???????????????????????????????????????
                  ?
???????????????????????????????????????
?   guideXOSServer (User Mode Init)   ?
?  - First user process               ?
?  - Compositor / Window Manager      ?
?  - Desktop Environment              ?
?  - System Services                  ?
?  - Application Framework            ?
?  - IPC Bus                          ?
?  - GUI Protocol                     ?
???????????????????????????????????????
                  ?
???????????????????????????????????????
?       User Applications             ?
?  - Notepad, Calculator, etc.        ?
?  - Runs in user mode                ?
?  - Communicates via IPC             ?
???????????????????????????????????????
```

## Component Responsibilities

### 1. Bootloader (`guideXOSBootLoader/`)

**Purpose:** Load the kernel and hand off control

**Responsibilities:**
- Parse UEFI environment
- Load `kernel.elf` from ESP
- Parse ELF, load segments to memory
- Set up initial page tables
- Collect boot information (memory map, framebuffer, ACPI, ramdisk)
- Create `BootInfo` structure
- Exit UEFI boot services
- Jump to kernel entry point with `BootInfo*` parameter

**Technologies:**
- UEFI/EDK2
- C++ (no CRT, no STL)
- MS x64 ABI
- Visual Studio / MSVC

**Key Files:**
- `guideXOSBootLoader/main.cpp` - UEFI entry point
- `guideXOSBootLoader/elf.cpp` - ELF loader
- `guideXOSBootLoader/guidexOSBootInfo.h` - BootInfo structure

**Forbidden:**
- User-mode services
- GUI rendering (minimal debug framebuffer OK)
- Loading guideXOSServer directly

### 2. Kernel (`kernel/`)

**Purpose:** Minimal microkernel providing core services

**Responsibilities:**
- Receive and parse `BootInfo` from bootloader
- Initialize memory management (PMM, VMM)
- Set up GDT, IDT, interrupts
- Implement system calls
- Process/thread scheduling
- Minimal device drivers (timer, keyboard, mouse)
- Load init process (`guideXOSServer`) from ramdisk
- Execute init process in user mode
- Handle syscalls from user processes

**Technologies:**
- C++14 (freestanding)
- GCC/Clang for cross-compilation
- Multi-architecture support (x86, amd64, arm, etc.)

**Key Files:**
- `kernel/core/main.cpp` - Kernel entry point (receives `BootInfo*`)
- `kernel/core/process.cpp` - Process management
- `kernel/arch/*/` - Architecture-specific code

**Rules:**
- **Boot-aware:** Must accept and use `BootInfo` from bootloader
- **Minimal GUI:** Only simple boot splash allowed, NO desktop rendering
- **No user services:** Desktop, compositor, apps belong in user mode

### 3. guideXOSServer (`server.cpp`, `compositor.cpp`, etc.)

**Purpose:** User-mode system server providing desktop environment

**Responsibilities:**
- Compositor / window manager
- Desktop environment (taskbar, start menu, wallpaper)
- System services (console, file manager)
- Application framework
- IPC bus for inter-process communication
- GUI protocol for applications

**Technologies:**
- C++ with STL
- Native Linux/Windows development (for now)
- Eventually: compiled to ELF, loaded by kernel

**Key Files:**
- `server.cpp` - Main entry point, REPL interface
- `compositor.cpp` - Window composition
- `desktop_service.cpp` - Desktop management
- `console_service.cpp` - Console/terminal service
- Applications: `notepad.cpp`, `calculator.cpp`, etc.

**Rules:**
- **Boot-agnostic:** Never accesses bootloader or BootInfo directly
- **User-mode only:** No kernel privileges
- **IPC communication:** All inter-process communication via IPC bus

## Boot Sequence

1. **Firmware** starts, loads bootloader from ESP
2. **Bootloader** (`guideXOSBootLoader/main.cpp`):
   - Loads `kernel.elf` 
   - Prepares `BootInfo` structure
   - Exits boot services
   - Jumps to kernel entry
3. **Kernel** (`kernel/core/main.cpp`):
   - Receives `BootInfo*` parameter
   - Shows minimal boot splash (if framebuffer available)
   - Initializes subsystems (memory, interrupts, processes)
   - Loads `guideXOSServer` from ramdisk (TODO: implement ELF loader)
   - Launches `guideXOSServer` as PID 1 in user mode
   - Enters idle loop, handling syscalls
4. **guideXOSServer** (`server.cpp`):
   - Initializes compositor
   - Shows desktop environment
   - Starts system services
   - Enters event loop
   - Launches user applications on demand

## Current Status

? **Working:**
- UEFI bootloader loads kernel
- Kernel receives BootInfo
- Boot splash displays
- Layer separation enforced in code

?? **In Progress:**
- Kernel process management (stub created)
- ELF loader in kernel (TODO)
- User-mode execution (TODO)
- System call interface (TODO)

? **TODO:**
- Load `guideXOSServer` as init process
- Kernel-to-userspace transition
- Proper syscall handler
- Compile `guideXOSServer` to ELF
- Package server in ramdisk

## Development Guidelines

### When adding features, ask:

1. **"Which layer does this belong in?"**
   - Boot-time only? ? Bootloader
   - Core OS service? ? Kernel
   - Desktop/GUI? ? guideXOSServer
   - User application? ? Separate app

2. **"Can this be done in user mode?"**
   - If yes ? Put it in guideXOSServer or user app
   - If no (requires privileges) ? Kernel only

3. **"Does this need boot information?"**
   - Yes ? Kernel (receives BootInfo)
   - No ? guideXOSServer or apps

### Anti-patterns to avoid:

? **DO NOT:**
- Put GUI rendering in kernel (except minimal boot splash)
- Make bootloader load guideXOSServer directly
- Bypass kernel when launching user processes
- Put user applications in kernel
- Make guideXOSServer boot-aware

? **DO:**
- Keep kernel minimal
- Put complex logic in user mode
- Use IPC for communication
- Respect layer boundaries
- Create stubs when components are missing

## Missing Components & Stubs

When a component is missing, **create a minimal stub** rather than collapsing layers.

**Example:** Kernel can't load guideXOSServer yet?
- ? Create stub that logs "TODO: Load init process"
- ? Don't put server code in kernel

**Example:** Server needs framebuffer access?
- ? Create syscall for framebuffer mapping
- ? Don't make server run in kernel mode

## File Organization

```
guideXOSServer/                    # Repository root
??? guideXOSBootLoader/            # UEFI bootloader
?   ??? main.cpp                   # UEFI entry (efi_main)
?   ??? elf.cpp                    # ELF loader
?   ??? guidexOSBootInfo.h         # BootInfo structure
??? kernel/                        # Kernel
?   ??? core/
?   ?   ??? main.cpp               # Kernel entry (kernel_main)
?   ?   ??? process.cpp            # Process management
?   ?   ??? include/kernel/        # Kernel headers
?   ??? arch/                      # Architecture-specific code
?       ??? x86/
?       ??? amd64/
?       ??? arm/
??? server.cpp                     # guideXOSServer main
??? compositor.cpp                 # Window manager
??? desktop_service.cpp            # Desktop environment
??? [other user-mode components]
```

## VM Bootability

**Goal:** System must always be bootable in VM

**Testing:**
```bash
# Build everything
./build.ps1

# Run in QEMU (UEFI boot)
qemu-system-x86_64 -bios OVMF.fd -drive file=fat:rw:ESP,format=raw -m 1024M
```

**Expected behavior:**
1. UEFI firmware loads bootloader
2. Bootloader shows progress, loads kernel
3. Kernel shows boot splash, initializes
4. Kernel shows "waiting for init" (until ELF loader is implemented)
5. (Future) Kernel launches guideXOSServer, desktop appears

## References

- Multiboot2 Specification
- UEFI Specification
- ELF Format Specification
- System V ABI (x86-64)
- MS x64 ABI (for bootloader)
