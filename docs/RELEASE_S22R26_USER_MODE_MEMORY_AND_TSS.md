# Server v0.3 — S22R26 User-Mode Memory and TSS Audit

**Disposition: Outcome C — `USER_MODE_EXECUTION_PREREQUISITE`.** The current AMD64 image has no owned 4 KiB physical-page allocator or per-task address-space lifecycle. The kernel therefore cannot prove which pages it may expose to CPL3, build or tear down private page tables safely, or reclaim task frames. No CPL3 transition was enabled.

## Provenance and scope

- Repository: `D:\dev\guideXOSServer`.
- Starting branch and HEAD: `main`, `f5cb9f4c50aabec24e5c3b42b1a0247bba3605c4`.
- The only pre-existing untracked file was `assets/Backgrounds/troncity.png`; SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`. It was preserved.
- The accepted S22R23 ISO remains identified by SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`, as recorded in the preceding qualification. This work created no ISO and changed no release, signing, publication, or `dist` state.
- This is a source and build audit. It does not claim guest execution or protection enforcement.

## Existing paging and physical memory

The UEFI bootloader provides a memory-map pointer, descriptor count, and descriptor size in `guideXOS::BootInfo`. The AMD64 kernel does not turn that map into an allocator with page ownership. Its current CR3 is inherited from the bootloader.

`guideXOSBootLoader/paging.cpp` creates identity mappings for a selected list of ranges. Its 4 KiB leaves are installed as `PTE_P | PTE_W`; it does not set `PTE_U` or `PTE_NX`. The selected mappings include kernel/boot ranges, a fixed allocator region, page tables, and device regions; this is not a complete physical-memory map. The current supervisor-only leaves prevent CPL3 access today, but there are no intentional user mappings or user-page ownership checks.

The kernel heap in `kernel/core/cxx_runtime.cpp` is a 512 MiB static kernel-image BSS array on AMD64. Its free-list allocator aligns allocations to eight bytes. It does not return isolated physical pages, and the heap is kernel-owned storage. Reclassifying pages from it as user memory would risk aliasing heap metadata and live kernel allocations.

Other existing memory code does not supply a general page owner:

- `kernel/core/mmio.cpp` has a 64-page, statically backed page-table pool for the optional QEMU VirtIO MMIO mapping path. Its mappings are supervisor-only, and it manages a dedicated MMIO window rather than process memory.
- `kernel/core/hugepages.cpp` uses example physical addresses in its reservation routines and returns success from mapping stubs; it is not a usable physical allocator or page-table manager.
- `read_cr3`, `write_cr3`, and TLB invalidation primitives exist, but there is no owned address-space abstraction, shared-kernel-map lifetime, per-task mapping ledger, or teardown contract.

The memory prerequisite is therefore real and specific: an authoritative frame owner must reserve kernel image/BSS, boot and interrupt stacks, the bootloader handoff and page tables, devices/MMIO, and all other non-free EFI ranges before it may allocate task-owned frames. Those frames also need a supervisor-accessible kernel mapping for zeroing, page-table construction, and cleanup. No current subsystem performs those steps. The heap, MMIO pool, and huge-page placeholders must not be substituted for that owner.

## Mapping protection required for a task

A future task root must preserve the current kernel mappings as supervisor-only at every level of the four-level translation path. A user leaf is accessible only when the U/S bit is set at every traversed PML4E, PDPTE, PDE, and PTE. Validation must inspect the full path, not only the leaf or an ownership flag in a task structure.

The minimum task mapping contract is:

| Region | Owner | User access | Writable | Executable |
| --- | --- | --- | --- | --- |
| Kernel text, data, heap, boot and interrupt stacks | Kernel | No | Per-region policy | Per-region policy |
| User diagnostic code | Task | Yes | No | Yes |
| User diagnostic data | Task | Yes | Yes | No |
| User stack | Task | Yes | Yes | No |
| Stack guard | None / unmapped | No | No | No |
| Page-table pages and device mappings | Kernel | No | Per-region policy | No for MMIO |

The current loader maps ranges writable and executable. `PTE_NX` is defined by the loader, while NX detection and EFER.NXE handling appear in the optional MMIO mapper; that does not establish a general task W^X policy for the normal kernel image. A task mapper must verify CPU NX support, enable NXE before installing NX leaves, reject unsupported CPUs, and enforce text/data/stack permissions without creating user-accessible aliases to kernel-owned pages. Address-space teardown must release only task-owned frames and private table pages, never shared kernel tables or kernel-owned mappings.

## GDT, TSS, IDT, and privilege transition

`kernel/core/interrupts.cpp` installs a five-slot AMD64 GDT: null, ring-0 code selector `0x08`, ring-0 data selector `0x10`, and the two-slot TSS descriptor at selector `0x18`. It executes `ltr 0x18`. There are no ring-3 code or data descriptors. `TSS64.rsp0` is zero-initialized and has no task update path. `ist1` points to the top of one shared 16 KiB exception stack; the IRQ gates use IST1.

The exception and IRQ IDT entries are installed with type attributes `0x8E` (present, interrupt gate, DPL0). There is no DPL3-callable exit gate. The existing exception stubs and `gxos_native_exception_dispatch` handle the current ring-0 Native ELF path; they do not classify and terminate a CPL3-owned task.

`kernel/arch/amd64/syscall.cpp` is not a usable fallback. Its `syscall_entry` immediately returns `-ENOSYS` with `sysretq`, the default exit handler halts, and yield does not schedule. Its STAR setup assumes user selectors beginning at `0x18`, which collides with the current TSS descriptor. No user pointer validation or task-owned return context exists.

A valid transition still needs explicit ring-3 selectors and a DPL3-controlled entry mechanism; a populated and loaded TSS with task-owned `RSP0`; a per-task kernel entry stack; canonical, mapped RIP/RSP; sanitized RFLAGS; an audited trap frame; and an `iretq`-based return to the saved kernel continuation. I/O permissions must remain denied. The current code does not meet those conditions.

## Minimum next engineering step

Before enabling `iretq` to CPL3, add one authoritative, bounded source of page-aligned task frames. It must be seeded only from validated EFI conventional-memory descriptors (or a bounded EFI-allocated diagnostic arena explicitly reserved in the handoff), exclude every boot/kernel/device/page-table range, and make allocated frames kernel-accessible without exposing that mapping to CPL3. Record each frame's owner and type, reject duplicate frees, and make partial task construction rollback-safe.

Use that same owner for private page-table pages and user code/data/stack frames. Clone or share only kernel mappings that remain supervisor-only; keep ownership of shared tables distinct from task-private tables; and walk every permission level when validating a user mapping. Add repeated allocate/map/unmap/free and injected-allocation-failure tests before implementing the TSS transition. Do not enable the user entry path while any part of the owner or mapping walk is synthetic or ambiguous.

## Related audit records

- `docs/RELEASE_S22R25_PROTECTED_EXECUTION_ARCHITECTURE.md`
- `docs/RELEASE_S22R25_PROTECTED_EXECUTION_QUALIFICATION.md`
- `docs/RELEASE_S22R24_NATIVE_ELF_EXECUTION_AND_SCHEDULER_AUDIT.md`
- `docs/RELEASE_S22R18_PUBLIC_PREVIEW_SECURITY_ENFORCEMENT.md`
