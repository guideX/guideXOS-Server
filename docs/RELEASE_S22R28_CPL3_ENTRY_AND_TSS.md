# S22R28 — CPL3 Entry and TSS

## Status

**Outcome B: `CPL3_FOUNDATION_IMPLEMENTED_GUEST_PENDING`.** The development-only implementation builds and its hosted contracts pass. No QAC guest was started because the coordinator had an owner-verified acceptance reservation active; the reservation was left untouched. Actual BootInfo memory-map behavior, processor CPL3 entry, and post-entry desktop operation therefore remain unqualified.

## Provenance and boundaries

- Starting branch: `main` at `bc2acf5adba271b0e7f8d288c8a52498dc1bbb3b`; the starting tracked worktree was clean and `main` was one commit ahead of `origin/main`.
- The pre-existing untracked `assets/Backgrounds/troncity.png` was preserved.
- The accepted S22R23 ISO at `E:\CodexArtifacts\S22R23\candidate-source\out\release-profile\4c9988cf-5d0d-414d-afe8-81f7351c2706\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso` was verified at 90,245,120 bytes with SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`. It was not modified.
- Diagnostic activation is opt-in with `CPL3_DIAGNOSTIC=1`; the normal build keeps the diagnostic call and DPL3 exit gate disabled. This is not a Native ELF loading route.
- Diagnostic freestanding `kernel.elf`: 2,840,424 bytes, SHA-256 `264701d1d30e8a44702da6f375b6beabe37e655f26cda7d82e735eadecfbad64`. Default diagnostic-off `kernel.elf`: 2,829,792 bytes, SHA-256 `2b9c39a27d082e03f9b9c37eb1c091f6fe879c83275fd63c31fe5c5d002268e3`. Both are ELF64 AMD64 ET_EXEC images with entry `0x100000`; the diagnostic image has no ELF relocations and exports the entry, exit, dispatcher, and dedicated-stack symbols.
- No loader, BootInfo, release-profile, signing, readiness, or `dist` content was changed.

## Memory ownership and address spaces

The diagnostic reuses S22R27's bounded `EfiLoaderData` frame owner and private page-table implementation. It does not invent a map or safe physical range. Before the owner is enabled, the AMD64 boot path verifies BootInfo v3 magic/version/size/checksum and memory-map/task-arena flags; validates descriptor dimensions and overflow; rejects arena overlap with the memory map, kernel image, ramdisk, BootInfo, boot stack, framebuffer ranges, and the traversed page-table tree; then verifies writable supervisor identity mappings for the memory map and arena. The owner keeps its metadata in kernel BSS.

Those checks are present in the real UEFI boot path, but they have not been observed against a guest's actual firmware map in this phase. The hosted S22R27 regression exercises the production allocator and page-table code, including effective U/S, R/W, and NX permissions, shared supervisor mappings, rollback, and teardown. Guest validation is still required before treating the real boot map as qualified.

## GDT and TSS

The AMD64 GDT now contains these entries:

| Selector | Descriptor | Access | Use |
| --- | --- | --- | --- |
| `0x08` | kernel code | `0x9A`, L=1, D/B=0 | CPL0 code |
| `0x10` | kernel data | `0x92` | CPL0 data |
| `0x18` / `0x1B` | user data | `0xF2`, flat | DPL3 data, RPL3 selector |
| `0x20` / `0x23` | user code | `0xFA`, L=1, D/B=0 | DPL3 code, RPL3 selector |
| `0x28` | 64-bit TSS | available `0x89`, then busy `0x8B` | privilege stack and IST |

The two-slot TSS descriptor is checked for the exact base, 103-byte limit, access byte, and zero reserved fields. The `ltr` result is checked with `str`. The TSS I/O-map base is set to `sizeof(TSS64)` while the descriptor limit ends at `sizeof(TSS64)-1`, leaving user I/O unavailable. User RFLAGS is sanitized to bit 1 only, so IF and IOPL remain clear.

The TSS IST1 points at a 16 KiB kernel exception stack used by the existing IRQ gates. The task's `RSP0` is set immediately before entry to the top of a separate 16 KiB kernel-owned entry stack in `protected_task.asm`; it is never a user stack or the desktop boot stack. The diagnostic is single-task and non-preemptive, so this static entry stack is not yet a per-task concurrent stack allocator.

## Bounded task and entry

One static descriptor owns a task identity, S22R27 address-space state, code/data/stack frame identities, expected exit state, saved trap frame, result, and lifecycle/fault status. Each of three diagnostic cycles creates one task address space and allocates one frame each for code, writable data, and writable stack. The user window is fixed and bounded:

- Code page: user, read-only, executable.
- Data page: user, writable, NX.
- Guard page: absent.
- Stack page: user, writable, NX.
- Shared kernel mappings: supervisor-only and writable where required.

The payload is a fixed 52-byte instruction sequence. It changes the owned data value from 41 to 42, records its live CS value in the same task-owned page, and invokes the sole permitted trap. It has no heap, graphics, VFS, library, Native ELF, or kernel-callback dependency.

The entry helper saves the kernel continuation, RFLAGS, and FS/GS base values; disables interrupts; clears the user FS/GS bases; loads DPL3 data selectors; clears all GPRs; and constructs a five-word `iretq` frame for CS `0x23`, SS `0x1B`, the checked user RIP/RSP, and IF=0/IOPL=0 RFLAGS. The saved entry and stack arguments are held in saved nonvolatile registers while the MSR sequence reuses RCX/RDX. The hosted contract validates the selector, canonical-address, code-range, stack-range, alignment, and IRET-frame rules.

## Stack evidence and limits

GCC `-fstack-usage` measurements for this diagnostic build reported 896 bytes for `kernel_main`, 96 bytes for `run_cpl3_diagnostic`, 288 bytes for the bounded lifecycle-cycle routine, 80 bytes for the user-exit dispatcher, 144 bytes for cleanup, and at most 240 bytes in the listed address-space routines. The C++ entry wrapper also saves eight nonvolatile GPRs and reserves the required 32-byte Microsoft x64 shadow area. The lifecycle, RSP0 entry, and exception stacks are separate 16 KiB regions.

These are individual C++ frames plus explicit assembly stack facts, not a whole-kernel call-chain proof. The established 16 KiB boot-stack budget and 4,096-byte minimum headroom are not being re-certified by these numbers. User-origin fault logging runs on the dedicated exception stack and has not been stress-tested in a guest.

## Production behavior and next milestone

The production Native ELF runtime remains the existing PacMan-only CPL0 path. No arbitrary program is loaded into CPL3, and no PacMan migration, scheduler, timer preemption, fault recovery, or non-cooperative termination is claimed.

S22R29 should first qualify timer-driven scheduling prerequisites: save/restore the full GPR and hardware return frame, CR3, FS/GS bases, task stacks, and defined interrupt state; establish a safe preemption boundary that does not interrupt kernel critical sections; and define Ready/Running/Blocked/Exited ownership for kernel-mediated resources. A future user task also needs per-task x87/SSE state policy (and XSAVE/FXSAVE setup); this kernel build disables MMX/SSE use in kernel code, but that is not user FPU-state qualification. Measure desktop latency under a bounded workload, then reserve a separate milestone for a CPL3 busy-loop containment test. Voluntary exit in S22R28 does not prove recovery from a non-cooperative task.
