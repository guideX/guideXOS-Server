# Architecture Compliance Checklist

Quick reference for verifying guideXOS component compliance.

## Bootloader Checklist

### Must Do ?
- [ ] Initialize firmware services (UEFI/BIOS)
- [ ] Load `kernel.elf` from ESP/boot partition
- [ ] Parse ELF headers and load segments
- [ ] Collect boot information (memory map, framebuffer, ACPI)
- [ ] Create `BootInfo` structure with firmware-neutral types
- [ ] Load ramdisk.img as opaque blob (optional)
- [ ] Exit boot services
- [ ] Set up initial page tables
- [ ] Jump to kernel entry point with `BootInfo*`

### Must NOT Do ?
- [ ] Reference guideXOS Server in any way
- [ ] Parse or load server binary
- [ ] Know about server architecture
- [ ] Implement policy decisions
- [ ] Implement UI beyond debug markers
- [ ] Expose UEFI/BIOS types in BootInfo
- [ ] Call firmware services after handoff

### Verification
```bash
# No server references
grep -ri "server" guideXOSBootLoader/
# Expected: no matches

# No UEFI types in BootInfo
grep "EFI_" guideXOSBootLoader/guidexOSBootInfo.h | grep -v "pragma"
# Expected: no matches
```

## Kernel Checklist

### Must Do ?
- [ ] Define `kernel_main(void* bootinfo, uint32_t magic)`
- [ ] Receive and validate `BootInfo` structure
- [ ] Initialize minimal architecture (CPU, interrupts)
- [ ] Initialize minimal memory management (PMM, VMM)
- [ ] Initialize minimal process management
- [ ] Parse ramdisk from `BootInfo->RamdiskBase`
- [ ] Load `/sbin/guideXOSServer` ELF from ramdisk
- [ ] Set up user-mode page tables
- [ ] Map framebuffer to user space
- [ ] Create server process as PID 1
- [ ] Jump to user mode at server entry point
- [ ] Enter idle loop handling syscalls

### Must NOT Do ?
- [ ] Expose BootInfo to server
- [ ] Implement sophisticated scheduler
- [ ] Implement complex drivers
- [ ] Implement desktop/GUI logic
- [ ] Pass firmware structures to server
- [ ] Run server in kernel mode

### Verification
```cpp
// Kernel entry signature
extern "C" void kernel_main(void* bootinfo, uint32_t magic);  ?

// Server launch does NOT pass BootInfo
void launch_init(const BootInfo* env) {
    load_elf(...);
    enter_usermode(entry);  // No BootInfo parameter ?
}
```

## BootInfo Checklist

### Must Contain ?
- [ ] Magic number (firmware-neutral)
- [ ] Version number
- [ ] Size and checksum
- [ ] Boot mode (enum, not firmware type)
- [ ] Memory map (as generic pointer)
- [ ] Memory map entry count and size
- [ ] Framebuffer info (optional, generic)
- [ ] ACPI RSDP pointer (standard)
- [ ] Ramdisk location (optional, opaque)
- [ ] Reserved fields for future use

### Must NOT Contain ?
- [ ] `EFI_*` types
- [ ] `BIOS_*` types
- [ ] Firmware-specific structures
- [ ] Direct hardware pointers (beyond framebuffer)
- [ ] Policy information
- [ ] Server-specific data

### Verification
```cpp
// All fields are primitive types or enums
struct BootInfo {
    uint32_t Magic;          ? Not EFI_UINT32
    BootMode BootMode;       ? enum, not EFI type
    uint64_t MemoryMap;      ? Not EFI_MEMORY_DESCRIPTOR*
    uint64_t FramebufferBase; ? Not EFI_PHYSICAL_ADDRESS
    // ...
};
```

## Server Checklist

### Must Do ?
- [ ] Define `int main(int argc, char** argv)` (standard)
- [ ] Request framebuffer via syscall
- [ ] Request hardware access via syscalls
- [ ] Use IPC for inter-process communication
- [ ] Run in user mode (ring 3)
- [ ] Be boot-agnostic (no boot knowledge)

### Must NOT Do ?
- [ ] Access BootInfo structure
- [ ] Access UEFI/BIOS services
- [ ] Directly access hardware
- [ ] Know about boot process
- [ ] Run in kernel mode
- [ ] Bypass syscall interface

### Verification
```bash
# No BootInfo references in server
grep -ri "BootInfo" server.cpp compositor.cpp desktop_service.cpp
# Expected: no matches

# No firmware references
grep -ri "EFI_\|UEFI\|BIOS" server.cpp
# Expected: no matches
```

## Communication Checklist

### Bootloader ? Kernel ?
- [ ] Pass `BootInfo*` as parameter
- [ ] Pass magic number for validation
- [ ] Ensure BootInfo survives ExitBootServices
- [ ] Set up stack and page tables
- [ ] Jump to kernel virtual entry point

### Kernel ? Server ?
- [ ] Load server ELF from ramdisk
- [ ] Set up user-mode page tables
- [ ] Map framebuffer to user space
- [ ] Pass `argc`, `argv` (standard parameters)
- [ ] Do NOT pass BootInfo
- [ ] Jump to user mode

### Server ? Kernel ?
- [ ] Use syscalls for all hardware access
- [ ] Use IPC for inter-process communication
- [ ] Never access BootInfo
- [ ] Never directly access hardware

## Isolation Checklist

### Layer Boundaries ?
- [ ] Bootloader knows about UEFI/BIOS
- [ ] Kernel knows about BootInfo
- [ ] Server knows NOTHING about boot
- [ ] Apps know NOTHING about boot
- [ ] BootInfo NEVER escapes kernel

### Data Flow ?
- [ ] UEFI ? Bootloader (firmware services)
- [ ] Bootloader ? Kernel (BootInfo)
- [ ] Kernel ? Server (syscalls)
- [ ] Server ? Apps (IPC)
- [ ] NO shortcuts or layer violations

## Testing Checklist

### Boot Test ?
- [ ] Firmware loads bootloader
- [ ] Bootloader loads kernel
- [ ] Kernel receives BootInfo
- [ ] Kernel validates BootInfo
- [ ] Kernel shows boot splash
- [ ] No crashes

### Integration Test ? (when ELF loader ready)
- [ ] Kernel loads server from ramdisk
- [ ] Kernel creates server process
- [ ] Server runs in user mode
- [ ] Server requests framebuffer
- [ ] Desktop appears
- [ ] No layer violations

### Regression Test ?
- [ ] Bootloader still compliant
- [ ] BootInfo still abstract
- [ ] Kernel still minimal
- [ ] Server still isolated
- [ ] System still boots

## Common Violations to Avoid

### ? Bootloader Violations
```cpp
// DON'T: Load server directly
LoadFile(&ServerFile, L"guideXOSServer.exe", ...);  ?

// DON'T: Reference server
if (server_config.enabled) { ... }  ?
```

### ? BootInfo Violations
```cpp
// DON'T: Expose UEFI types
struct BootInfo {
    EFI_MEMORY_DESCRIPTOR* MemoryMap;  ?
    EFI_GRAPHICS_OUTPUT_PROTOCOL* GOP; ?
};
```

### ? Kernel Violations
```cpp
// DON'T: Pass BootInfo to server
void launch_server(BootInfo* info) {
    exec("server", info);  ?
}

// DON'T: Implement desktop in kernel
void kernel_main(...) {
    draw_desktop();  ?
    handle_windows(); ?
}
```

### ? Server Violations
```cpp
// DON'T: Access BootInfo
extern BootInfo* g_bootInfo;  ?

// DON'T: Access hardware directly
volatile uint32_t* fb = (uint32_t*)0xB8000;  ?

// DON'T: Know about boot
if (boot_mode == UEFI) { ... }  ?
```

## Quick Fixes

### If bootloader references server:
1. Remove server code
2. Move to kernel (if truly needed)
3. Or delete (if not needed)

### If BootInfo has UEFI types:
1. Change to `uint64_t` pointers
2. Change to enums for constants
3. Document type in comments

### If kernel passes BootInfo to server:
1. Remove BootInfo parameter
2. Provide data via syscalls instead
3. Map resources to user space

### If server accesses BootInfo:
1. Remove BootInfo access
2. Request via syscall instead
3. Make server boot-agnostic

## Approval Checklist

Before committing changes:

- [ ] Bootloader checklist passed
- [ ] Kernel checklist passed
- [ ] BootInfo checklist passed
- [ ] Server checklist passed
- [ ] Communication checklist passed
- [ ] Isolation checklist passed
- [ ] No common violations present
- [ ] All tests pass
- [ ] Documentation updated

## Current Status

| Component | Compliant | Notes |
|-----------|-----------|-------|
| Bootloader | ? Yes | Fully compliant |
| BootInfo | ? Yes | Perfect abstraction |
| Kernel | ? Yes | Minimal and correct |
| Server | ? Yes | Properly isolated |
| Overall | ? **PASS** | Architecture clean |

## References

- **`BOOTLOADER_COMPLIANCE.md`** - Detailed bootloader review
- **`MINIMAL_KERNEL_SPEC.md`** - Kernel specification
- **`COMPLIANCE_SUMMARY.md`** - Full compliance report
- **`ARCHITECTURE.md`** - System architecture
- **`QUICK_REFERENCE.md`** - Quick reference

---

**Last Updated**: 2024
**Architecture Version**: 1.0
**Status**: ? COMPLIANT
