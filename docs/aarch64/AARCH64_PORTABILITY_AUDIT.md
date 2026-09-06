# guideXOS Server AArch64 Portability Audit

**Audit phase:** AARCH64-0
**Audit date:** 2026-08-29
**Repository:** D:\dev\guideXOSServer_AARCH64
**Branch:** AARCH64_SUPPORT
**Audit outcome:** **B — complete, evidence-based audit; implementation intentionally deferred**

This document records the smallest defensible path from the current AMD64
implementation to the first AArch64 kernel execution. It is an investigation
and roadmap, not an ARM64 port. No production source, build target, package
format, or existing AMD64 path was changed in AARCH64-0.

## Executive answer

The current boot path is an x86-64-only UEFI loader followed by an x86-64
handoff trampoline and an AMD64 kernel entry. The reusable part is the UEFI
service choreography and much of the file/ELF/BootInfo data handling. The
non-reusable part is large and explicit: x86 port I/O, COM1, CR3/PML4 page
tables, MS x64 register conventions, x86 interrupt structures, and the
loader's PCI probing.

The cleanest next step is a separate, deliberately small AArch64 UEFI + phase-1
kernel target that shares only carefully selected firmware-neutral utilities.
It should not link the current full kernel yet. The target should:

1. be discovered by AArch64 UEFI as \EFI\BOOT\BOOTAA64.EFI;
2. load an AArch64 ELF64 kernel and a versioned handoff block;
3. call ExitBootServices exactly once successfully, with retry handling;
4. enter a guideXOS-owned AArch64 entry stub with a 16-byte-aligned stack;
5. establish or explicitly verify the execution state needed by the phase;
6. initialize a polled QEMU virt PL011 console; and
7. print a deterministic AARCH64_PHASE1_PASS marker.

The full kernel should be integrated only after the phase-1 ABI, entry state,
MMU policy, and early console are proven. This avoids turning the existing
non-x86 fallback code into an accidental ARM64 contract.

## 1. Repository safety and baseline

The safety gate was checked before source inspection or modification.

| Item | Starting value |
|---|---|
| Path | D:\dev\guideXOSServer_AARCH64 |
| Required branch | AARCH64_SUPPORT |
| Starting HEAD | 0e6039c0e3f1fd218424ba7367c9e83bd8d73223 |
| Starting subject | Fix ISO smoke CD attachment |
| Upstream | origin/AARCH64_SUPPORT |
| Starting ahead/behind | 0 / 0 |
| Starting worktree | clean (AARCH64_SUPPORT...origin/AARCH64_SUPPORT) |
| Pushes | none |

The branch was exactly correct, so the audit continued. No unrelated branch or
main was modified. The only intended AARCH64-0 modification is this
documentation file.

## 2. Current AMD64 boot path

### 2.1 Firmware-to-kernel sequence

| Sequence point | Repository evidence | Classification | Portability finding |
|---|---|---|---|
| UEFI image entry | guideXOSBootLoader/main.cpp:689, efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE*) | Mostly portable | The symbol and two-argument UEFI entry shape are reusable. The current project is built only as Debug/Release x64. |
| UEFI globals and console | main.cpp:690-693, gST, UEFI Print | Architecture-neutral UEFI usage | Protocol and system-table access are reusable; the local UEFI header and project include paths need AArch64 validation. |
| BootInfo allocation | main.cpp:695-708 | Architecture-neutral | AllocatePages(EfiLoaderData) is reusable, but pointer/size contracts must remain fixed-width and valid after boot services exit. |
| ACPI discovery | main.cpp:667-682, FindRSDP | Platform-specific but reusable behind discovery | Configuration-table scan is reusable. ACPI consumers must not assume PC-only tables or x86 interrupt topology. |
| GOP discovery | main.cpp:718-727, LocateProtocol(GOP) | Platform-specific but reusable behind abstraction | GOP can be shared. It should be optional for AARCH64-1 because virt has no default display device. |
| File access | main.cpp:422-437, LoadFile | Architecture-neutral | Loaded-image protocol, simple filesystem, root open, and kernel.elf access are reusable. |
| Kernel ELF load | guideXOSBootLoader/elf.cpp | Mostly portable | ELF64 segment loading is reusable. It lacks an e_machine check and needs a defined AArch64 link/load policy. |
| Ramdisk load | main.cpp:754-815 | Architecture-neutral | File read, page allocation, and handoff fields are reusable. |
| BootInfo construction | main.cpp:817-829, guidexOSBootInfo.h | Mostly portable | Fields are fixed-width, but MemoryMap currently exposes UEFI descriptor semantics directly. |
| Loader stack | main.cpp:831-930 | Mostly portable with platform assumptions | Allocation and overlap checks are reusable; fixed low addresses and x86 identity-map assumptions are not. |
| Handoff code allocation | main.cpp:932-947 | AMD64-specific | The executable trampoline is x86-64 NASM and cannot be shared with AArch64. |
| PCI/NIC probe | main.cpp:949-1005, pci.cpp | Platform-specific, AMD64-specific access path | Current code uses CF8/CFC port I/O and Intel NIC assumptions. It must not run in AARCH64-1. |
| New page tables | main.cpp:1007-1214, paging.cpp | AMD64-specific | Four-level PML4/PDPT/PD/PT, x86 flags, CR3, and identity mappings are not portable. |
| Exit boot services | main.cpp:569-649, ExitBootServicesWithMemoryMapInBuffer | Architecture-neutral UEFI choreography | GetMemoryMap growth/retry and stale map-key retry are reusable. |
| Pre/post-EBS diagnostics | main.cpp:1216-1317 | Mostly portable | Framebuffer markers are reusable if GOP exists. All boot services must stop after successful EBS. |
| Earliest serial | main.cpp:1319-1365, debug helpers | AMD64/platform-specific | Loader diagnostics use x86 COM1 0x3F8 and port I/O. AArch64 needs PL011 MMIO. |
| Kernel call | main.cpp:1367-1424, handoff_trampoline.asm | AMD64-specific | Saves RCX/RDX/R8/R9, loads CR3, sets RSP, allocates MS x64 shadow space, and calls the kernel. |
| Kernel entry | kernel/arch/amd64/boot.asm:12-35 | AMD64-specific | Executes cli, clears segment registers, sets rsp, and calls kernel_main. |
| Normal kernel | kernel/core/main.cpp:712-1434 | Mostly AMD64 today | The real BootInfo, serial, framebuffer, PIC/PIT, storage, and desktop path is inside ARCH_HAS_PIC_8259. ARM64 falls through a different generic stub. |

### 2.2 Current handoff contract

The effective current AMD64 contract is:

~~~text
UEFI efi_main()
  -> load kernel ELF at an allocated physical base
  -> build x86 identity/page-table mappings
  -> allocate an x86-64 trampoline and stack
  -> ExitBootServices()
  -> BootHandoffTrampoline(kernelEntry, BootInfo, stackTop, Pml4Phys)
       RCX = kernel entry, RDX = BootInfo, R8 = stack, R9 = PML4
       CR3 = PML4
       RSP = stackTop, aligned
       RCX = BootInfo
       call kernel entry
  -> kernel/arch/amd64/boot.asm::_start
  -> kernel_main(BootInfo, unspecified/cleared second argument)
~~~

There is a design contradiction: the loader header says “No identity-mapped
assumptions,” while the trampoline requires the trampoline code, stack, page
tables, kernel, and other handoff objects to be identity-mapped. The AArch64
path must state its translation regime explicitly.

## 3. AMD64/x86 dependency inventory

### 3.1 Bootloader and handoff

- guideXOSBootLoader/handoff_trampoline.asm is explicitly x86_64, MS x64 ABI.
  It uses cli, RCX/RDX/R8/R9, RSP, CR3, in/out, COM1, x86 page-table walks,
  and call r12.
- guideXOSBootLoader/trampoline.asm duplicates the MS x64/CR3/COM1 model.
- guideXOSBootLoader/trampoline_msvc.cpp embeds x86 machine code and uses
  __inbyte, __outbyte, and MS x64 argument registers.
- guideXOSBootLoader/debug_helpers.h and boot_diagnostics.h inspect CR0, CR2,
  CR3, CR4, EFER, PML4 entries, x64 entry bytes, and COM1.
- guideXOSBootLoader/paging.cpp implements 4-level x86 page tables with
  shifts 39/30/21/12 and x86 PTE flags.
- guideXOSBootLoader/pci.cpp uses __indword/__outdword, port addresses
  0xCF8/0xCFC, and Intel E1000 device IDs.
- The loader's fixed stack attempts at 0x200000 and 0x100000 are not a
  general ARM64 allocation policy.

### 3.2 Kernel architecture services

- kernel/core/include/kernel/arch.h detects many architectures, but its
  feature matrix is capability-like only in name. Port I/O, VGA, PIC, and PS/2
  are enabled for x86-family targets; ARM64 framebuffer capability flags are
  false.
- kernel/core/interrupts.cpp contains x86 IDT/GDT/TSS structures, lidt, lgdt,
  ltr, exception stubs, PIC remapping, and port-I/O IRQ handling. Its non-x86
  tail has no ARM GIC dispatch.
- kernel/core/pit.cpp is a real PIT path only when ARCH_HAS_PORT_IO is enabled.
  ARM64 receives a stub rather than the architectural timer.
- kernel/core/include/kernel/serial_debug.h implements only COM1 when
  ARCH_HAS_PORT_IO; every ARM64 serial function is an inline no-op. The
  existing kernel/arch/arm64/serial_console.cpp is not wired into it.
- kernel/core/architecture_detector.cpp correctly guards x86 CPUID code and
  has compile-time architecture cases. This is mostly reusable after
  separating architecture identity from available services.
- kernel/arch/amd64/arch.cpp accesses CR3 and INVLPG.
- kernel/arch/amd64/context_switch.cpp contains ABI-specific context layout.
- kernel/arch/amd64/native_elf_call.asm is an AMD64-only application-stack
  trampoline and explicitly documents the Microsoft x64 ABI.

### 3.3 Instruction, compiler, and ABI assumptions

The inventory found these assumptions that must not leak into common code:

- MS x64 ABI versus AAPCS64 register/stack rules;
- x86 cli/sti, hlt, in/out, CPUID, CRx, MSRs, IDT/GDT/TSS, APIC, IOAPIC,
  PIC, PIT, and RDTSC;
- x86 canonical-address/page-table index assumptions;
- SSE/AVX/FPU state and any future lazy FPU policy;
- compiler intrinsics __readcr3, __writecr3, __invlpg, __inbyte, and __outbyte;
- x86 instruction scanning and relocation repair in
  kernel/core/native_elf_baremetal.cpp;
- inline assembly constraints and register clobbers in hosted NativeElf;
- VirtualAlloc/VirtualProtect, which are host APIs rather than a kernel ABI;
- assumptions about long, size_t, structure packing, or pointer truncation;
- atomics, spinlocks, and barriers where C++ atomics may lower to runtime code;
  and
- stack alignment at handoff, context switch, exception, and app entry.

The ARM64 flags in kernel/arch/arm64/Makefile.arch avoid FP/SIMD and outline
atomics for a small freestanding kernel, but that file is not the active
parent build description for boot.S and does not provide a complete target.

## 4. Architecture-neutral and reusable inventory

Good reuse candidates, subject to contract tests:

- UEFI LoadFile, simple filesystem traversal, and the memory-map/EBS retry
  algorithm;
- GOP descriptor normalization and pixel-format classification;
- ACPI RSDP lookup as a discovery service;
- fixed-width BootInfo serialization and checksum logic, after deciding whether
  to expose raw UEFI descriptors or normalized records;
- bounded ELF header/program-header parsing, PT_LOAD copy/zero semantics, and
  bounds checks;
- ramdisk/file loading;
- architecture-name/ELF-machine mapping already present in elf_validator.cpp
  (0x3E AMD64 and 0xB7 AArch64);
- VFS path logic, filesystem format code, allocator algorithms, kernel object
  lifecycles, app manifest parsing, app discovery, UI layout, and protocol code;
- the existing ARM64 barriers, system registers, cache/TLB, timer, and serial
  code as starting points after platform assumptions are tested; and
- the existing per-architecture directory convention.

Reuse must be selective. LoadElf needs to return an accepted machine type and a
load/virtual-address policy. BootInfo is reusable as a binary layout only after
pointer ownership and memory-map lifetime are stable.

## 5. Build-system audit

### 5.1 Existing build graph

| Layer | Current mechanism | AArch64 implication |
|---|---|---|
| Top-level | Makefile exposes x86, amd64, and QEMU-oriented targets | No top-level ARM64 target. |
| Canonical wrapper | build.ps1, default -Arch amd64 | Builds the UEFI project as x64, copies BOOTX64.EFI, stages AMD64 App Model paths, and launches qemu-system-x86_64. |
| Alternate wrapper | build-uefi.ps1 | Uses x64 MSBuild output and BOOTX64.EFI; Arch only partially reaches the kernel directory. |
| Linux helper | run-uefi.sh | Detects only OVMF x86 firmware and invokes qemu-system-x86_64 -machine pc. |
| UEFI project | guideXOSBootLoader/guideXOSBootLoader.vcxproj | Only Debug x64 and Release x64, v143, x64 EDK2 include paths, /NODEFAULTLIB, EFI subsystem, and NASM -f win64. |
| Kernel parent makefile | kernel/Makefile | Source discovery includes *.asm and lowercase *.s; uppercase arch/arm64/boot.S is omitted. |
| ARM64 fragment | kernel/arch/arm64/Makefile.arch | Describes aarch64-elf tools and local rules, but parent source lists do not consume them; linker-script handling is also not cleanly connected. |
| Dependency gate | guidexos/mbedtls_sources.mk and third_party/mbedtls | Profile exists but the pinned dependency tree is absent; AMD64 and ARM64 make entrypoints stop before compilation. |

### 5.2 Host facts observed

- C:\mingw64\bin\g++.exe and gcc.exe are MinGW-W64 x86_64 tools;
  mingw32-make.exe and NASM are on PATH.
- clang, clang++, ld.lld, llvm-objcopy, and QEMU are not on PATH.
- A usable LLVM installation exists at C:\Program Files\LLVM\bin.
  Clang 22.1.8 accepts --target=aarch64-none-elf and reports normalized target
  aarch64-unknown-none-elf; ld.lld and llvm-objcopy are present.
- QEMU exists at C:\Program Files\qemu; qemu-system-aarch64.exe and
  qemu-system-x86_64.exe are present by full path. The share contains
  edk2-aarch64-code.fd but no adjacent edk2-aarch64-vars.fd.
- msbuild is not on PATH. The current build scripts cannot run end-to-end from
  this shell without locating Visual Studio or changing the build process.
- WSL reports no usable installed Linux distribution; no Linux build was tried.
- The repository OVMF.fd and OVMF_VARS.fd are the existing x86-oriented files
  used by current scripts and are not treated as AArch64 firmware.

### 5.3 Toolchain recommendation

Use one pinned LLVM toolchain for the freestanding AArch64 kernel:

~~~text
clang++ --target=aarch64-none-elf -march=armv8-a -ffreestanding -nostdlib
ld.lld
llvm-objcopy
llvm-readelf / llvm-nm
~~~

Make tool paths explicit/configurable, such as AARCH64_LLVM_ROOT, rather than
requiring a global PATH install. The target probe and isolated compilation of
boot.S, arch.cpp, and serial_console.cpp all succeeded with LLVM 22.1.8. This
proves compiler-front-end viability, not a complete kernel link.

Use an EDK2/UEFI-compatible AArch64 build for the UEFI application. A bare
metal aarch64-none-elf compiler is not by itself a PE/COFF UEFI loader
toolchain. Prefer EDK2 ArmVirt/Clang integration or a separately configured
AArch64 PE/COFF Clang/MSVC build with the local UEFI definitions validated
against EDK2 headers.

Do not silently change the current x64 Visual Studio project to ARM64. Add an
explicit AArch64 loader target or standalone phase-1 wrapper.

## 6. UEFI portability and loader strategy

### 6.1 Shareable UEFI operations

Share eventually:

- efi_main entry plumbing and system-table access;
- loaded-image and simple-filesystem protocol lookup;
- filesystem reads for kernel and ramdisk;
- GOP discovery and descriptor conversion when present;
- configuration-table discovery for ACPI and device-tree records;
- memory-map acquisition, growth, map-key retry, and ExitBootServices; and
- construction of a fixed-width, versioned firmware handoff.

The local Uefi.h defines UINTN as 64 bits under _WIN64 and leaves EFIAPI empty.
That is not proof of AArch64 ABI correctness. The target must validate layout,
function-pointer calls, variadic Print, and PE/COFF relocations against target
firmware headers.

### 6.2 PE/COFF and boot-file facts

UEFI identifies image architecture with the COFF machine field:

~~~text
AMD64/x64: 0x8664, removable-media filename BOOTX64.EFI
AArch64:   0xAA64, removable-media filename BOOTAA64.EFI
~~~

The current project hard-codes x64 in project configuration, NASM format,
include path, output folder, and staging filename. These cannot be toggled
safely with a single source-level architecture flag.

### 6.3 Recommendation: shared core plus architecture-specific edges

Recommend **option C: a shared portable UEFI loader core with separate
architecture-specific entry/handoff/platform modules**.

- Do not duplicate the entire loader forever: extract tested firmware-neutral
  helpers such as file loading, EBS retry, RSDP/DTB discovery, and BootInfo
  construction.
- Do not compile current main.cpp unchanged for AArch64: it includes x86 page
  tables, x86 PCI, COM1, and an x64 trampoline.
- For AARCH64-1, use a separate AArch64 loader target/source set that consumes
  only portable helpers; keep the x64 project and handoff intact.
- Later converge both targets on LoaderPlatform/ArchHandoff contracts after
  both implementations are tested.

The first shared handoff should either normalize the UEFI memory map into
guideXOS-owned records or document the exact UEFI descriptor version/stride and
lifetime. Raw UEFI descriptor layout should not become an accidental permanent
kernel ABI.

## 7. Kernel architecture boundary

The kernel has architecture directories and detection, but not a stable
capability/service boundary. The general shape should be:

~~~text
arch/
  cpu        early CPU state, features, idle
  mmu        address spaces, page tables, TLB, cache maintenance
  exceptions vector setup, frame format, fault dispatch
  interrupts controller setup, masking, EOI, IRQ registration
  timer      monotonic counter and interrupt source
  context    scheduler context and stack construction
  atomics    compiler/runtime-specific wrappers if needed
  smp        CPU bring-up, barriers, IPIs, per-CPU state
  console    early serial/debug output
platform/
  qemu_virt  DTB/ACPI/device discovery and MMIO topology
~~~

Core code should call capabilities such as arch::early_console(),
arch::interrupts(), arch::timer(), and arch::mmu(), not PIC/PIT/VGA/CR3
helpers. The names are illustrative; the important choice is one generic
architecture contract with per-architecture implementations.

Filesystem, allocator policy, object management, manifest parsing, app
discovery, UI layout, protocol-level networking, USB class logic, and storage
abstractions above a block device should remain mostly unchanged. Serial,
interrupts, timers, context switching, mappings, cache/TLB, executable
permissions, PCI access, and exception decoding must move behind the boundary.

## 8. QEMU virt AArch64 target

QEMU documents virt as a generic platform with PL011 UARTs, PCIe, RTC, fw_cfg,
virtio-MMIO transports, and selectable GIC versions. It generates a DTB; only
flash base 0x0 and RAM base 0x40000000 are stable hard-coded addresses, while
other device locations should be read from DTB.

### 8.1 Early UART

The repository ARM64 serial code uses:

~~~text
PL011 MMIO base: 0x09000000
UART clock assumption: 24 MHz
Polled output, 8N1, FIFO enabled
~~~

0x09000000 is suitable as a temporary QEMU-virt constant and matches the
skeleton. Put it in a qemu_virt platform description, not a generic ARM64
driver. Permanent code should prefer DTB or validated firmware discovery and
retain this constant only as a version-pinned fallback.

The UEFI image entry must not assume direct-QEMU DTB semantics in x0. Pass a
DTB/config-table record explicitly when firmware exposes one. AARCH64-1 should
not initialize GIC, PCI, virtio, graphics, or timers merely to print a
polled serial proof.

### 8.2 Pin the first test machine

Use an explicit 64-bit CPU because QEMU documents cortex-a15, a 32-bit CPU, as
the virt default. Pin GICv2 or GICv3 in later interrupt phases; do not depend
on an unversioned default.

Recommended phase-1 command shape, assuming a writable UEFI variable image
outside the repository:

~~~powershell
$qemu = 'C:\Program Files\qemu\qemu-system-aarch64.exe'
$code = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd'
$vars = 'C:\path\to\writable\edk2-aarch64-vars.fd'
$esp = 'D:\dev\guideXOSServer_AARCH64\ESP'
& $qemu -machine virt -cpu cortex-a53 -m 512M -drive "if=pflash,format=raw,unit=0,readonly=on,file=$code" -drive "if=pflash,format=raw,unit=1,file=$vars" -drive "file=fat:rw:$esp,format=raw" -nographic -monitor none -serial stdio -no-reboot
~~~

Stage the loader as ESP\EFI\BOOT\BOOTAA64.EFI and the phase-1 kernel at the
loader's defined path, normally ESP\kernel.elf. A read-only -bios smoke
invocation is acceptable for a first experiment, but repeatable tests should
use a writable AArch64 variable image in a user-owned temporary location.

## 9. AArch64 execution-state requirements

| State | AARCH64-1 decision | Later requirement |
|---|---|---|
| Instruction set | AArch64, not AArch32 | Diagnose unexpected state. |
| Exception level | Report and normalize to EL1; existing boot.S EL drop code must be tested, not assumed. | Reserve EL2 for the chosen platform/virtualization policy. |
| Stack | GuideXOS-owned stack, SP 16-byte aligned at public calls. | Per-CPU, guard, user, and exception stacks. |
| Vector table | Install aligned VBAR_EL1 before unmasking exceptions. | Real ESR/FAR/ELR/SPSR capture and dispatch. |
| MMU | Deliberately defined flat/identity-safe regime; no x86 PML4 or unspecified firmware dependency. | AARCH64-2 owns controlled tables and transition. |
| SCTLR_EL1 | Do not enable M until TTBR/TCR/MAIR/mappings/barriers are installed. | Controlled M/C/I and cache policy. |
| MAIR/TCR | Select device/normal attributes, granule, VA size, and shareability together. | Implement in MMU service. |
| TTBR0/TTBR1 | One identity strategy may suffice for phase 1, but encode it in the entry contract. | Kernel/user split and ASID policy. |
| Granule | 4 KiB to match existing allocation and skeleton. | Verify levels and descriptors. |
| Address size | Skeleton assumes 48-bit VA/PA, but virt phase 1 needs much less; do not claim 48-bit support yet. | Discover and test ID-register/DTB limits. |
| Interrupt mask | IRQ/FIQ/SError masked through serial proof. | Unmask after vectors and GIC. |
| Timer | Not used in phase 1. | ARM architectural timer and discovered IRQ in phases 2/3. |
| Barriers | DSB/ISB around system-register, cache, TLB, and control-state transitions. | Test executable-memory transitions. |
| I-cache | Any copied/modified code must be instruction-visible before branch. | Define IC IVAU/barrier ownership. |

The existing kernel/arch/arm64/boot.S is not a sufficient phase-1 entry:

- it calls _Z11early_printPKc, while serial_console.cpp exports C-linkage
  early_print; an LLVM assembly/nm probe confirmed this mismatch;
- it saves x0 as a DTB-like value, but UEFI handoff needs an explicit BootInfo
  argument contract;
- it calls kernel_main without defining a valid second argument; and
- it combines EL/vector/BSS logic before the UEFI handoff has been proven.

These are AARCH64-1 implementation targets, not AARCH64-0 fixes.

## 10. App Model / NativeElf portability audit

The repository contains hosted experimental NativeElf code and a bare-metal
AMD64-specific loader.

- elf_validator.cpp maps 0x3E to amd64 and 0xB7 to arm64, validates class,
  endianness, and type, rejects ET_DYN/PIE, and checks manifest architecture.
- native_elf_image_loader.cpp parses segments and alignment but does not
  independently read/validate e_machine; it trusts the resolved architecture.
- Hosted native_elf_executor.cpp uses preferred-base mapping, rejects PIE
  because relocations are not implemented, and uses VirtualAlloc/VirtualProtect
  plus an AMD64/Windows trampoline.
- kernel/core/native_elf_baremetal.cpp rejects header.machine values other than
  62, prints machine=amd64, scans x86 instructions to rebase absolute
  addresses, assumes static ET_EXEC, and enters via gxos_native_call_on_stack.
- kernel/arch/amd64/native_elf_call.asm saves AMD64 registers and documents MS
  x64 ABI; no AArch64 equivalent exists.
- sdk/include/guidexos/abi.h defines GX_CALL as ms_abi only under __x86_64__;
  AArch64 uses the compiler default. The ABI name therefore needs an explicit
  architecture/version contract before ARM64 execution.
- Apps/*/app.json and build.ps1 currently stage only bin/amd64/*.elf, although
  package documentation already describes per-architecture binaries.

The future package layout can be:

~~~text
MyApp/
  app.json
  bin/
    amd64/
      myapp.elf
    arm64/
      myapp.elf
~~~

The eventual loader must select by running architecture and declared ABI,
validate ELF class/endianness/e_machine/type/entry/segments/permissions,
reject duplicate or wrong-path binaries, define AAPCS64 LP64 pointer and
structure rules, and provide an AArch64 application-stack trampoline. Do not
implement this package format in AARCH64-0.

## 11. Higher-level subsystem portability

| Subsystem | Classification | Assessment |
|---|---|---|
| VFS/filesystem parsers | Unchanged / small adaptation | Format and path logic are neutral; block devices and DMA alignment are not. |
| Allocator/kernel heap | Small adaptation | Algorithms are reusable; pages, atomics, cache attributes, and executable pages are not. |
| PCI enumeration | Significant adaptation | Current CF8/CFC port I/O and Intel NIC assumptions do not apply; use discovered PCIe/ECAM later. |
| ACPI | Small-to-significant adaptation | RSDP/table parsing is reusable; MADT/APIC/HPET/PC consumers must be generalized. |
| Framebuffer/GOP | Small adaptation, phase-1 blocked | GOP normalization is reusable; virt has no default display device. |
| Compositor/desktop drawing | Small-to-significant adaptation | Reusable after framebuffer/cache/presentation ownership is correct. |
| Input | Significant adaptation | PS/2 is PC-specific; use USB/HID or another ARM platform input path. |
| USB | Significant adaptation | HCI, DMA, and cache coherency need ARM64 work; class logic is more reusable. |
| Networking | Significant adaptation | Protocol logic is neutral; MMIO/DMA/PCI/virtio and interrupts need work. |
| Navigator | Blocked for full desktop | Needs filesystem, crypto, timers, graphics, and app execution. |
| Application discovery | Small adaptation | Discovery is neutral, but current defaults in places are amd64. |
| Desktop/taskbar | Blocked on lower layers | Needs framebuffer, input, timer, allocator, and app launch. |
| Filesystem-backed App Model | Significant adaptation | Discovery is reusable; ARM64 ELF, ABI, selection, and staging are not. |

The realistic route is serial -> MMU/exceptions/timer -> storage/VFS ->
framebuffer/input -> desktop, with native ARM64 applications after process/ABI
work.

## 12. Roadmap

### AARCH64-1 — UEFI, ARM64 entry, serial proof

Use an independent minimal AArch64 loader and kernel. No full desktop, PCI,
GIC, timer, or application loader. Prove BOOTAA64.EFI discovery, ELF64 +
EM_AARCH64 validation, EBS retry, owned stack, EL1 report, PL011 output,
AARCH64_PHASE1_PASS, useful negative diagnostics, and AMD64 regression gates.

### AARCH64-2 — Controlled MMU, exceptions, memory, timer

Prove 4 KiB page tables, MAIR/TCR/TTBR/SCTLR policy, barriers, VBAR_EL1,
ESR/ELR/FAR/SPSR diagnostics, ARM architectural timer, pinned GIC, and a
page-map/allocator smoke test.

The Phase 2 QEMU virt proof pins the first interrupt path to DTB-discovered
GICv2 and the non-secure physical generic timer.  AARCH64-3 should preserve
the generic-kernel boundary while adding DTB-selected GICv3 support and
architecture service interfaces; it should not broaden into scheduler or
userspace work until those interfaces are independently validated.

### AARCH64-3 — Architecture services and scheduler

Move core code to generic CPU/MMU/interrupt/timer/context/console interfaces.
Prove kernel threads, context switching, idle, interrupt masking, and one
timer-driven scheduler test while AMD64 remains green.

### AARCH64-4 — Core kernel and filesystem

Pass the UEFI memory map and ramdisk to the normal kernel. Prove allocator,
block layer, VFS, filesystem mount, and file read/write on virt without x86
port I/O.

### AARCH64-5 — Framebuffer, input, desktop

Discover an explicit QEMU graphics device or firmware GOP, map it correctly,
add one ARM64 input path, and prove framebuffer console, compositor, desktop,
and taskbar smoke markers.

### AARCH64-6 — ARM64 NativeElf/App Model

Enforce e_machine, ABI, type, segments, permissions, stack alignment, and
relocation policy. Document/version the AAPCS64 guideXOS ABI and preserve the
package container semantics.

### AARCH64-7 — First real ARM64 application

Run a static ARM64 app through gx_main, a host call, rendering/logging, exit,
and cleanup. Prove a dual-architecture package selects the correct binary and
fails deterministically for missing/wrong architectures.

### AARCH64-8 — Developer Studio ARM64 compilation

Compile the ARM64 application target from Developer Studio with a pinned
SDK/toolchain. Distinguish host, target, ABI, and package architecture and
validate on virt without AMD64 regressions.

## 13. AARCH64-1 implementation specification

### 13.1 Intended files

No files in this list were changed by AARCH64-0.

Likely new/split loader files:

- guideXOSBootLoader/arm64/main.cpp — AArch64 UEFI orchestration;
- guideXOSBootLoader/arm64/handoff.S — AAPCS64 handoff and entry glue;
- shared helpers extracted from guideXOSBootLoader/main.cpp for file loading,
  EBS retry, configuration-table lookup, and handoff construction; and
- an explicit AArch64 loader project/target emitting BOOTAA64.EFI.

Likely kernel files:

- kernel/arch/arm64/phase1_entry.S, or a focused repair of boot.S;
- kernel/arch/arm64/phase1.cpp;
- kernel/arch/arm64/serial_console.cpp; and
- a minimal phase-1 linker script or isolated phase-1 link mode.

Likely build files:

- kernel/Makefile, for .S discovery, explicit linker-script handling, and a
  minimal source set;
- kernel/arch/arm64/Makefile.arch, for configurable LLVM tools and matching
  parent link/objcopy commands; and
- scripts/build-aarch64-phase1.ps1 or equivalent, without changing AMD64
  defaults of build.ps1.

Do not add complete core main.cpp, GIC, PCI, full filesystem, or NativeElf
execution solely to get the marker. Normal integration belongs to later phases.

### 13.2 Minimal handoff contract

Define and test the contract before implementation:

~~~text
UEFI loader:
  x0 = kernel entry address for selected translation regime
  x1 = pointer to guideXOS BootInfo/handoff block
  x2 = top of guideXOS-owned kernel stack
  x3 = phase/platform handoff flags or zero

Kernel entry stub:
  interrupts masked
  SP = x2, SP % 16 == 0
  establish/verify EL1
  install VBAR_EL1
  preserve x1 as BootInfo pointer
  initialize polled PL011
  call phase1_main(BootInfo*) using AAPCS64
~~~

The exact ordering may change during implementation, but it must be written
once and used by loader, assembly, C/C++, and tests. Do not reuse
kernel_main(void*, uint32_t) until its second argument and state are defined.

### 13.3 Address and MMU policy

The first image must not be positionally ambiguous. Choose and test either:

- fixed QEMU phase-1 load at 0x40000000 with link script and AllocateAddress;
  or
- a genuinely position-independent/relocatable image with AArch64-aware
  absolute-reference and entry handling.

The existing ARM64 linker script uses identity KERNEL_PADDR=KERNEL_VADDR at
0x40000000, while the generic UEFI ELF loader allocates an arbitrary physical
base. These are incompatible unless allocation is pinned or relocation is
defined. This is a phase-1 acceptance item.

### 13.4 Acceptance output

The stable success stream should contain, in order:

~~~text
[guideXOS] AARCH64 UEFI loader entry
[guideXOS] ELF machine: AArch64
[guideXOS] ExitBootServices: OK
[guideXOS] AARCH64 kernel entry
[guideXOS] execution level: EL1
[guideXOS] stack: OK
[guideXOS] firmware handoff: OK
AARCH64_PHASE1_PASS
~~~

The first two loader markers are optional only if firmware console output is
unavailable; post-EBS PL011 markers and the final marker are mandatory.

### 13.5 Failure diagnostics

| Missing/last marker | First diagnostic question |
|---|---|
| No UEFI output | Is image BOOTAA64.EFI, PE machine 0xAA64, and AArch64 firmware running? |
| UEFI menu/shell | Is the FAT ESP mounted and removable-media path correct? |
| Loader entry but no ELF marker | Is kernel.elf present, readable, ELF64 little-endian, and e_machine=EM_AARCH64? |
| ELF loaded but no EBS marker | Did allocation overlap, map grow, or map key go stale? |
| EBS marker but no kernel marker | Is entry executable and valid in the selected translation regime? |
| Kernel marker, stack failure | Is x2 the intended 16-byte-aligned stack top and mapped/non-overlapping? |
| EL2/EL3 reported | Was the normalization policy installed and did it return to EL1? |
| UART stalls/no bytes | Is PL011 base selected correctly and MMIO accessible? |
| Handoff marker but no pass | Is BootInfo valid, packed identically, and checksum/version accepted? |
| Synchronous exception | Print ESR/ELR/FAR; suspect MMU, code address, or I-cache transition. |

## 14. AMD64 regression protection

Every AArch64 phase should run:

1. mingw32-make -C kernel ARCH=amd64 and Mbed TLS profile validation;
2. build.ps1 -Arch amd64, including loader, ISO/ESP staging, and artifacts;
3. existing QEMU AMD64 UEFI boot with repository OVMF files;
4. filesystem/application-discovery and NativeElf smoke tests; and
5. git diff --check plus a check that AMD64 output remains BOOTX64.EFI,
   kernel.elf, and bin/amd64/*.elf.

Keep AMD64 as the reference backend for every generic contract. Do not change
an existing AMD64 BootInfo field, ABI, package path, marker, or QEMU command
without a compatibility test.

## 15. Tests and probes run for AARCH64-0

| Check | Result |
|---|---|
| Branch/path/HEAD/upstream/worktree safety gate | PASS |
| Clang version and --target=aarch64-none-elf | PASS; LLVM 22.1.8, normalized target aarch64-unknown-none-elf |
| Existing ARM64 boot.S isolated compile | PASS |
| Existing ARM64 arch.cpp isolated compile | PASS |
| Existing ARM64 serial_console.cpp isolated compile | PASS |
| llvm-nm boot.S symbol probe | CONFIRMED mismatch: weak _Z11early_printPKc; source exports C early_print |
| mingw32-make -C kernel ARCH=amd64 info | BLOCKED before compilation: missing third_party/mbedtls dependency/profile layout |
| mingw32-make -C kernel ARCH=arm64 info | BLOCKED before compilation by same dependency gate |
| Full AMD64 build/ISO/QEMU boot | NOT RUN: dependency tree and MSBuild prerequisites unavailable |
| Full AArch64 UEFI/QEMU boot | NOT RUN: no AArch64 artifacts; AARCH64-0 does not implement them |
| Push | NOT PERFORMED |

The temporary compiler probes were removed. They prove front-end compilation,
not that the existing parent makefile can build or link the ARM64 kernel.

## 16. References

- [UEFI Specification 2.10](https://uefi.org/specs/UEFI/2.10/) — image types,
  PE machine IDs, boot services, and AArch64 platform sections.
- [UEFI Specification 2.10 PDF](https://uefi.org/sites/default/files/resources/UEFI_Spec_2_10_Aug29.pdf)
  — BOOTAA64.EFI and 0xAA64 image-type table.
- [QEMU virt board documentation](https://qemu.readthedocs.io/en/v9.1.3/system/arm/virt.html)
  — PL011, GIC selection, generated DTB, stable RAM base, and CPU choices.
- [TianoCore ArmVirtQemu DSC](https://github.com/tianocore/edk2/blob/master/ArmVirtPkg/ArmVirtQemu.dsc)
  — QEMU AArch64 UEFI memory base and DTB initial address.
- [Arm AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
  — AArch64 LP64 data/pointer model, registers, and 16-byte stack alignment.
- [Arm AAELF64](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)
  — AArch64 ELF and relocation model for later application work.

## 17. Final conclusion

The smallest clean path is:

~~~text
preserve AMD64
  -> isolate reusable UEFI/file/handoff helpers
  -> add explicit AArch64 PE/COFF loader target
  -> add minimal AArch64 ELF + AAPCS64 handoff
  -> make execution state deterministic
  -> poll QEMU virt PL011
  -> prove AARCH64_PHASE1_PASS
  -> only then integrate MMU/exceptions/GIC/timer and normal kernel
~~~

This keeps the existing AMD64 implementation as a working reference while
making the true portability boundary visible: firmware choreography and
high-level data models are reusable, but entry glue, memory translation,
interrupt/timer services, MMIO/DMA, context switching, and native application
ABI are architecture services that must be implemented and tested explicitly.
