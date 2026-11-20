# Server v0.3 — S22R27 Private Address-Space Foundation

## Construction and ownership

`kernel/core/include/kernel/task_address_space.h` and `kernel/core/task_address_space.cpp` provide a small internal AMD64 address-space API for future tasks. A zero-initialized, kernel-owned `TaskAddressSpace` descriptor must remain alive through teardown. The implementation requires an initialized frame owner, the current kernel CR3 as its source root, NX enabled, and a safe TLB invalidation configuration.

Creation allocates an owned page-table root, copies the current root entries, and rejects any kernel root entry with user permission. The task receives only a private subtree at PML4 index 255, beginning at `0x00007F8000000000`. That window must be empty in the kernel root. Existing kernel mappings stay supervisor-only and shared; their page-table and leaf ownership is not transferred to the task. This fixed window is a bounded fixture interface, not a general virtual-memory manager.

The task may map only frames owned by its task ID as private code, data, or stack. Page-table pages are allocated separately as `PageTableStorage`. Each private mapping is checked for canonical virtual address, 4 KiB alignment, ownership, duplication, and available arena capacity. Other owner types and aliases that bypass ownership are rejected.

## Effective permissions

AMD64 permissions are checked along the complete translation path, including the PML4, PDPT, page directory, and leaf. The implementation does not treat the leaf U/S bit as sufficient. Task code is user-readable and executable but read-only. Data and stack are user-readable and writable with NX set. The fixture keeps a guard page unmapped. Kernel identity mappings remain supervisor-only and fail user access checks.

Mapping is rejected if NX cannot be enabled or if the platform reports an unsupported TLB invalidation configuration. PCID-enabled operation is not accepted by the current invalidation contract. Queries understand existing supervisor large-page mappings; private task mappings use 4 KiB pages, and unsupported large-page construction is rejected.

## Rollback and teardown

Private intermediate tables are allocated transactionally. If a later table allocation, permission check, or collision check fails, only tables created by that operation are unwound. The caller retains ownership of a leaf frame when a map operation fails, so it can retry or explicitly release that frame; a failed map never silently consumes it.

Teardown first refuses an active root and validates the private subtree and ownership metadata. It then invalidates every private virtual mapping before clearing its mapped state and releasing task leaf frames, child tables, and root. The copied kernel root entries are retained and never recursively freed. Stale translations therefore cannot survive into frame reuse under the supported single-CPU, non-PCID contract. Invalidations and local owner mutations are not an SMP shootdown protocol.

## Real-source diagnostic fixture

`run_task_address_space_fixture` constructs a root, maps one code frame, one data frame, and one stack frame, leaves the guard page absent, verifies effective read/write/execute permissions and a supervisor kernel mapping, destroys the address space, and checks that ownership accounting returned to baseline. The hosted regression repeats setup and teardown and exercises collisions, invalid ranges, unsupported configuration, exhaustion, failed intermediate-table allocation, and invalidation accounting. It links the production implementation sources rather than a parallel model.

The fixture does not load or jump to code, install a task CR3, create a CPL3 entry frame, or claim a hardware page-fault result. It is an ownership and page-table construction diagnostic only.

## Context-switch and CPL3 prerequisites

The corrected S22R25 switch remains a dormant Microsoft x64 voluntary call-boundary switch. It preserves the eight nonvolatile GPRs, but does not save a user interrupt frame, sanitized RFLAGS, segment state, or asynchronous CPU state. The current freestanding build disables compiler SIMD use; a SIMD-enabled scheduler will need a defined x87/XMM/MXCSR/XSAVE policy.

An eventual task switch must pair the owned root with the task lifetime, switch CR3 only at a safe kernel boundary, retain a task-owned user stack, and use a separate kernel-owned entry stack. TSS `RSP0` must point to that task's kernel stack before user entry. A user trap or syscall must save the full user return frame and return to a controlled kernel continuation. Faulted or exited spaces must be reaped while executing on a different kernel stack, after interrupt and TLB synchronization requirements are satisfied.

Those entry, exit, fault, TSS, and reaper contracts are not implemented here. S22R28 should add valid GDT user selectors, per-task `RSP0`, kernel entry stacks, sanitized user entry/return frames, controlled exit, user-origin fault classification, and safe off-stack teardown. Preemptive scheduling and non-cooperative termination should follow only after those contracts and full asynchronous register ownership are validated. PacMan remains on its existing ring-0 path.
