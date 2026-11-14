# Server v0.3 — S22R26 CPL3 Guest Qualification

**Outcome C — `USER_MODE_EXECUTION_PREREQUISITE`.** No diagnostic guest was launched. The current kernel cannot safely own and reclaim user-task frames or page tables, and it lacks a valid user entry/exit path. A guest attempt would not qualify hardware-protected task execution and a user fault could not be safely reaped.

## Repository and artifact provenance

- Starting source: branch `main`, HEAD `f5cb9f4c50aabec24e5c3b42b1a0247bba3605c4`.
- Accepted S22R23 ISO identity remains SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`; it was neither opened for destructive diagnostics nor modified.
- No S22R26 diagnostic ISO, release package, or guest evidence artifact was created. There is no diagnostic ISO hash.
- The hosted context-test executable is not a guest artifact. Its SHA-256 is `9297837e57cc1bc32aa25889e7d17a92a66fb4f33ce6877d57e1f4a160ea6f6f`.
- The freestanding kernel ELF used for build and symbol validation is 2,806,352 bytes with SHA-256 `fe73801461cf63c2e6e56926610579bd67618f4d2f8f9e0f10210fb98c3a01ec`. It was built outside the repository and not packaged into an ISO.

## QAC state and execution decision

The live QAC dashboard was inspected on 2026-11-14. It reported the coordinator **Online**, foreign QEMU count **0**, managed sessions **0**, queued reservations **0**, blocked reservations **0**, and the acceptance lane **Available**. No reservation was created and no QAC state was changed. The local shell had no `qemu-system-x86_64` command on PATH; the QAC dashboard is the available managed route.

Although the lane was available, guest execution was not safe to attempt because the hosted/freestanding prerequisites for a real protected task are absent. QAC remains available and no guest ownership needs cleanup. This decision does not infer qualification from an empty QEMU census.

## Guest evidence

| Required capability | Result |
| --- | --- |
| AMD64 guest boot and kernel initialization for an S22R26 diagnostic image | Not attempted; no diagnostic image was built. |
| Task-owned user code/data/stack allocation and a private CR3 | Not implemented; no physical frame owner or task address-space teardown exists. |
| TSS `RSP0`, valid CPL3 selectors, sanitized entry frame, and hardware transition | Not implemented. Existing GDT contains ring-0 selectors and a loaded TSS, but `RSP0` is unset and there are no ring-3 descriptors. |
| Verified user privilege and bounded computation | Not demonstrated. |
| Kernel-mediated exit and off-stack cleanup | Not implemented. The existing AMD64 syscall entry is a placeholder; its exit handler halts. |
| CPL3 supervisor-memory write rejection and user-fault recovery | Not attempted. No safe user exception classification or task reaper exists. |
| Timer entry, interrupt-return restoration, or independent scheduling progress | Not demonstrated. The freestanding scheduler is inactive. |
| Continued kernel/desktop operation after task completion or fault | Not demonstrated for a protected task. |

No guest feature is claimed as S22R26 evidence. The production PacMan path was not changed; its prior candidate evidence remains attached to that exact accepted image only.

## Why this is Outcome C

The first blocking prerequisite is page ownership. The boot handoff exposes an EFI memory map, but the kernel does not seed an allocator from it. The fixed kernel heap is eight-byte aligned kernel BSS, the MMIO table pool is a private diagnostic pool, and huge-page reservation/mapping code contains placeholders. None can safely supply reclaimable user frames.

Further prerequisites remain after frame ownership: task-private page tables with full-path U/S validation; explicit user code/data/stack W^X policy and a guard page; valid ring-3 selectors; per-task kernel stack and TSS `RSP0`; a bounded trap gate and a saved kernel continuation; user-origin page-fault/general-protection/invalid-opcode classification; safe terminal state and reaping from a different stack. Existing `SYSCALL/SYSRET` setup is not usable and assumes a user selector that currently overlaps the TSS descriptor.

## Smallest next qualification path

S22R27 should first implement one authoritative source of owned 4 KiB frames from validated EFI descriptors or a bounded, explicitly reserved EFI page arena. It should prove that every kernel mapping remains supervisor-only and every user mapping is owned, permission-checked at all four levels, zeroed before use, and reclaimable without releasing shared/kernel pages. Hosted tests should cover page ownership, permission-path walks, rollback on allocation failure, and repeated map/unmap cycles.

Only after that foundation passes the freestanding build and stack audit should S22R27 add ring-3 GDT selectors, a task-owned kernel stack and TSS `RSP0`, one narrow `int`-gate exit operation, and a user-origin exception path that returns to a kernel-owned continuation. Then build a separately identified diagnostic ISO and use a QAC acceptance reservation to show positive CPL3 execution and controlled exit. Attempt a separate supervisor-write fault test only after its classification and cleanup path is safe.

This is still not a non-cooperative termination test. Timer-driven scheduling, safe asynchronous context, SIMD/FPU ownership, cancellation points, and a task reaper must be demonstrated before launching a task that does not cooperate. Do not treat QEMU shutdown as task recovery.

## Baseline and release boundary

The accepted PacMan-only candidate, persistent-data images, S22R18 Native ELF integrity enforcement, public package policy, and release-readiness markers were left unchanged. No production signing, publication, promotion, or release authorization is implied. Existing Server v0.3 functionality is preserved by keeping the existing ring-0 PacMan route and package checks intact.
