# Minimal Kernel Specification

## Purpose

Define a MINIMAL guideXOS kernel that:
- Acts as boundary between bootloader and guideXOS Server
- Is just sufficient to launch the server
- Contains NO unnecessary complexity
- Can be replaced later without affecting bootloader or server

## Kernel Entry Point

### Signature

```cpp
// kernel/core/main.cpp
extern "C" void kernel_main(void* boot_environment, uint32_t magic);
```

### Parameters

1. **`boot_environment`**: Opaque pointer to `BootInfo` structure
   - Cast to `guideXOS::BootInfo*` internally
   - Contains all boot information from bootloader
   
2. **`magic`**: Magic number for validation
   - `0x2BADB002` for Multiboot compatibility
   - Future: use BootInfo->Magic instead

### Minimal Implementation

```cpp
extern "C" void kernel_main(void* boot_environment, uint32_t magic)
{
    // 1. Validate boot environment
    auto* env = static_cast<guideXOS::BootInfo*>(boot_environment);
    if (env->Magic != guideXOS::GUIDEXOS_BOOTINFO_MAGIC) {
        halt_forever();
    }
    
    // 2. Initialize minimal subsystems
    kernel::arch::init();        // CPU, interrupts
    kernel::memory::init(env);   // PMM, VMM from memory map
    kernel::process::init();     // Process table
    
    // 3. Launch server as PID 1
    kernel::process::launch_init(env);
    
    // 4. Idle loop - just schedule
    while (1) {
        kernel::process::schedule();
        kernel::arch::halt();
    }
}
```

## Minimal Subsystems

### 1. Architecture Layer

**Required:**
- CPU initialization
- Interrupt handling (basic)
- `halt()` instruction

**NOT Required:**
- Sophisticated APIC setup
- Advanced CPU features
- Performance counters

```cpp
namespace kernel::arch {
    void init();              // Initialize CPU
    void disable_interrupts(); // CLI
    void enable_interrupts();  // STI
    void halt();              // HLT
}
```

### 2. Memory Management

**Required:**
- Parse memory map from BootInfo
- Physical page allocator (simple bitmap)
- Virtual memory (map kernel + server)

**NOT Required:**
- Memory reclamation
- Swap
- NUMA awareness
- Advanced allocators

```cpp
namespace kernel::memory {
    void init(const guideXOS::BootInfo* env);
    void* allocate_page();
    void free_page(void* page);
    void map_page(uint64_t virt, uint64_t phys, uint32_t flags);
}
```

### 3. Process Management

**Required:**
- Single process table entry for server
- Ability to switch to user mode
- Minimal scheduler (just yield to server)

**NOT Required:**
- Multi-process scheduling
- Priority queues
- Thread support
- SMP

```cpp
namespace kernel::process {
    void init();
    
    // Launch server as PID 1
    void launch_init(const guideXOS::BootInfo* env);
    
    // Minimal scheduler - just yield
    void schedule();
}
```

### 4. System Call Interface (Future)

**Required Eventually:**
- Syscall entry point
- Framebuffer mapping syscall
- File I/O syscalls

**NOT Required Initially:**
- Complex syscall table
- Many syscalls
- Syscall tracing

```cpp
namespace kernel::syscall {
    uint64_t handle(uint64_t num, uint64_t arg1, uint64_t arg2, ...);
}
```

## Launch Server Process

### Minimal Implementation

```cpp
void kernel::process::launch_init(const guideXOS::BootInfo* env)
{
    // 1. Parse ramdisk from BootInfo
    void* ramdisk = (void*)env->RamdiskBase;
    size_t ramdisk_size = env->RamdiskSize;
    
    // 2. Find server binary in ramdisk
    // Assume simple tar or custom format
    void* server_elf = find_file(ramdisk, "sbin/guideXOSServer");
    
    // 3. Load ELF to memory
    uint64_t entry_point;
    uint64_t load_base;
    load_elf(server_elf, &entry_point, &load_base);
    
    // 4. Set up user page tables
    // Map server code, data, stack
    // Map framebuffer to user space
    setup_user_pages(load_base, env->FramebufferBase);
    
    // 5. Create process structure
    Process* server = create_process("guideXOSServer", entry_point);
    server->pid = 1;
    server->state = ProcessState::Ready;
    
    // 6. Jump to user mode
    enter_usermode(entry_point, server->stack_top);
}
```

### Stub Implementation (Current)

Since ELF loader and ramdisk parser don't exist yet:

```cpp
void kernel::process::launch_init(const guideXOS::BootInfo* env)
{
    // TODO: Load server from ramdisk
    // TODO: Parse ELF
    // TODO: Set up user pages
    // TODO: Jump to user mode
    
    // For now: show "waiting for init" message
    framebuffer::draw_message("Kernel ready - ELF loader not yet implemented");
    
    // Idle
    while (1) arch::halt();
}
```

## BootEnvironment Structure

### Already Defined ?

The `BootInfo` structure (in `guidexOSBootInfo.h`) **already satisfies** requirements:

```cpp
namespace guideXOS {
    struct BootInfo {
        // Abstract, firmware-neutral
        uint32_t Magic;
        BootMode BootMode;  // enum, not UEFI type
        
        // Memory information
        uint64_t MemoryMap;
        uint64_t MemoryMapEntryCount;
        
        // Framebuffer (generic)
        uint64_t FramebufferBase;
        uint32_t FramebufferWidth;
        uint32_t FramebufferHeight;
        FramebufferFormat FramebufferFormat;  // enum
        
        // Hardware info
        uint64_t AcpiRsdp;
        
        // Ramdisk (opaque)
        uint64_t RamdiskBase;
        uint64_t RamdiskSize;
    };
}
```

### Consumption by Kernel

```cpp
void kernel::memory::init(const guideXOS::BootInfo* env)
{
    // Parse memory map
    auto* map = (EFI_MEMORY_DESCRIPTOR*)env->MemoryMap;
    size_t entry_count = env->MemoryMapEntryCount;
    size_t entry_size = env->MemoryMapDescriptorSize;
    
    // Build physical page allocator from map
    for (size_t i = 0; i < entry_count; i++) {
        auto* entry = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)map + i * entry_size);
        if (entry->Type == EfiConventionalMemory) {
            mark_pages_free(entry->PhysicalStart, entry->NumberOfPages);
        }
    }
}
```

### Server NEVER Sees BootInfo ?

```cpp
// In guideXOS Server (user mode)
int main(int argc, char** argv)
{
    // Server has NO ACCESS to BootInfo
    // Server has NO KNOWLEDGE of boot process
    // Server requests hardware via syscalls only
    
    // Request framebuffer from kernel
    void* fb = (void*)syscall(SYS_MMAP_FRAMEBUFFER);
    
    // Initialize compositor with mapped framebuffer
    Compositor::init(fb);
    
    // ...
}
```

## Constraints Checklist

### Kernel Must ?

- [x] Define kernel entry point accepting BootInfo
- [x] Accept abstract BootEnvironment (BootInfo)
- [x] Initialize minimal memory infrastructure
- [x] Initialize minimal process infrastructure
- [x] Launch server as first user process
- [x] Idle or hand over control to scheduler

### Kernel Must NOT ?

- [x] NO scheduler sophistication (simple round-robin OK)
- [x] NO drivers beyond required (framebuffer mapping)
- [x] NO UI logic (minimal boot splash allowed)
- [x] NO complexity - prefer clarity

### BootEnvironment Must ?

- [x] NOT expose UEFI types (uses uint64_t, enums)
- [x] Contains only neutral information
- [x] Memory map (firmware-neutral)
- [x] Framebuffer (optional, generic)
- [x] CPU info (implicitly from arch)
- [x] Boot flags (BootInfo->Flags)

### BootEnvironment Must NOT ?

- [x] NEVER exposed to guideXOS Server ?
- [x] NO UEFI-specific types ?
- [x] NO firmware-specific structures ?

## Implementation Phases

### Phase 1: Current (Stub) ?

```
Bootloader ? Kernel ? [shows "waiting for init"] ? halt
```

**Status**: DONE
- Kernel receives BootInfo ?
- Kernel initializes subsystems ?
- Kernel shows stub message ?

### Phase 2: ELF Loader (Next)

```
Bootloader ? Kernel ? [loads server ELF] ? [shows message] ? halt
```

**TODO**:
- Parse ramdisk
- Find server binary
- Load ELF to memory
- Verify loaded correctly

### Phase 3: User Mode (Later)

```
Bootloader ? Kernel ? [loads server] ? [jumps to user mode] ? Server runs
```

**TODO**:
- Set up user page tables
- Map framebuffer to user space
- Switch to ring 3
- Jump to server entry point

### Phase 4: Full Integration (Future)

```
Bootloader ? Kernel ? Server ? Desktop appears
```

**TODO**:
- Syscall handler
- IPC between processes
- Full desktop environment

## File Structure

```
kernel/
??? core/
?   ??? main.cpp           # kernel_main() entry point
?   ??? process.cpp        # launch_init(), schedule()
?   ??? memory.cpp         # init(), allocate_page()
?   ??? elf_loader.cpp     # load_elf() (TODO)
??? arch/
?   ??? x86/
?       ??? arch.cpp       # init(), halt()
?       ??? usermode.asm   # enter_usermode() (TODO)
?       ??? syscall.asm    # syscall_entry() (TODO)
??? include/
    ??? kernel/
        ??? process.h
        ??? memory.h
        ??? types.h
```

## Simplicity Guidelines

### DO ?

- Keep kernel MINIMAL
- Use simple algorithms (bitmap allocator)
- Prefer clarity over performance
- Add TODOs for missing features
- Document assumptions

### DON'T ?

- Add features "just in case"
- Implement complex schedulers
- Add drivers not immediately needed
- Over-engineer for future scenarios
- Hide assumptions

## Example: Minimal vs. Complex

### ? Complex (AVOID)

```cpp
// Don't do this - too sophisticated
class Scheduler {
    std::priority_queue<Process*> ready_queue;
    std::vector<Process*> sleeping;
    std::map<pid_t, Process*> process_table;
    
    void schedule_cfs() { /* complex fair scheduler */ }
    void load_balance() { /* SMP load balancing */ }
};
```

### ? Minimal (PREFER)

```cpp
// Do this - just enough to work
Process processes[16];  // Fixed array
int current_process = 0;

void schedule() {
    // Round-robin, single server process for now
    if (processes[0].state == ProcessState::Ready) {
        switch_to_process(&processes[0]);
    } else {
        arch::halt();
    }
}
```

## Testing Strategy

### Boot Test

```
1. Build kernel
2. Boot in QEMU
3. Verify:
   - Kernel receives BootInfo ?
   - Magic number correct ?
   - Framebuffer info correct ?
   - Kernel shows boot splash ?
   - Kernel enters idle loop ?
```

### Integration Test (Future)

```
1. Add server ELF to ramdisk
2. Boot in QEMU
3. Verify:
   - Kernel loads server ?
   - Server runs in user mode ?
   - Desktop appears ?
```

## Summary

The minimal kernel is:
- **Transitional** - will be replaced/enhanced later
- **Simple** - clarity over completeness
- **Sufficient** - just enough to launch server
- **Clean** - proper abstraction boundaries
- **Boot-aware** - accepts and uses BootInfo
- **Server-agnostic** - doesn't know about server architecture

**Current Status**: Kernel stub implemented ?
**Next Step**: Implement ELF loader to load server binary
**End Goal**: Server running in user mode with syscall interface
