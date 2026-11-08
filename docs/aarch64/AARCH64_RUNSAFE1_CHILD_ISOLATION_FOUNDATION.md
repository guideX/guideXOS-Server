# AArch64 RUNSAFE1 — NativeElf Child Isolation Foundation

## Outcome

**Outcome B — `EL0_ISOLATION_DESIGN_ACCEPTED_IMPLEMENTATION_PENDING`.** The unsafe Developer Studio Run service is now fail-closed, truthful rejection is covered by a hosted control, and the two hosted/build integration failures recorded by R2B2 were corrected. The current kernel provides enough ARMv8-A translation machinery to support a bounded EL0 prototype without moving the existing low identity-mapped kernel, but it does not yet provide the child page-table builder, lower-EL exception path, or scheduler address-space ownership needed to implement that prototype safely.

This work did not start QEMU or request QAC admission. No compiled child was loaded or entered. R1e GUI, R2A project/editor, and the accepted R2B1 Build candidate and evidence were preserved. Preflight was on `AARCH64_SUPPORT` at `cac567e8644066f01e55a55f4c8aa4f7ce3bd8a9`, clean, one commit ahead of `origin/AARCH64_SUPPORT` (zero behind). No reset, rebase, push, or branch change was made.

## Unsafe Studio Run path and gate

The previous source path was:

```text
Developer Studio Run controller / BuildResult
  -> v0.5 464-byte callback table
  -> typed adapter to the AARCH64 400-byte ABI table
  -> phase12_run_prepare(projectId)
  -> phase12_run_start(handle)
  -> phase12_run_package(package, nested=true)
  -> phase9_load_and_run
  -> arm64_load_image
  -> gxos_native_call_on_stack(entry, context, stackTop)
  -> BLR entry at EL1
```

`phase12_run_prepare` previously refreshed the App Model, resolved a package only by `projectId`, and prepared a job. `phase12_run_start` called the nested package path synchronously in the Studio task. The Studio `poll` callback therefore could not run until the child returned. `request_close` did not interrupt the active entry.

The current guard rejects valid Run preparation before VFS refresh or package lookup. It reports state `FAILED`, `GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE`, an explicit message that isolated EL0 execution and bounded task ownership are unavailable, and `exitCode = -1`; it creates no handle and no job. The independent `start` guard refuses any pre-existing matching handle without setting `started`, and its callback has no nested loader call or synthesized exit result. Prepare and start share the private policy in `kernel/core/include/kernel/aarch64_studio_run_gate.h`. `phase9_load_and_run` has a separate runtime-kind backstop that refuses a `Phase12Runtime` marked as a Studio child before privileged host setup or image loading. Build callbacks and Build publication are unchanged.

There is no production path from Studio's Run callback into a child entry after this change. The generic App Model `phase9_run_application` path still calls the common ARM64 NativeElf loader at EL1 for the existing Phase 9/10/11 application workload. That path is not an EL0 sandbox and remains a separate kernel-privileged NativeElf capability; it was not used to run a Developer Studio child in this milestone.

## Existing privilege and memory model

Source reviewed: `kernel/arch/arm64/phase2_mmu.cpp`, `phase5_mmu_permissions.cpp`, `phase3_entry.S`, `phase3_exceptions.cpp`, `context_switch.cpp`, `kernel/core/common_scheduler.cpp`, and `native_elf_baremetal_arm64.inc`.

| Area | Implemented behavior | Consequence for a child |
| --- | --- | --- |
| Exception level | Kernel tasks and NativeElf entries execute at EL1. Scheduler-created initial PSTATE is EL1h. | No production EL0 entry has been established. |
| Address translation | One global four-level, 4 KiB translation root; VA size 48 bits, PA size 40 bits. TTBR0 is active. TCR sets EPD1, so TTBR1 is intentionally unused. | There is no per-task root or address-space switch. |
| RAM | DTB-discovered RAM is identity mapped. Default AP permissions are EL1 read/write with no EL0 access. Kernel text/vectors and rodata are made EL1 read-only. | Kernel memory is reachable at EL1 from the child call; it is not reachable at EL0 under current AP bits. |
| Devices | UART, GIC and virtio MMIO are identity mapped as device memory with PXN and UXN. Their AP permissions remain privileged. The framebuffer has a separate non-executable attribute when built. | Devices are reachable by privileged kernel code. Current descriptors do not grant EL0 access, but the map is global rather than child-owned. |
| App image | `arm64_load_image` requires ELF64 little-endian `ET_EXEC`, `EM_AARCH64`, a fixed minimum load address, a valid executable entry, bounded ranges, no overlapping byte ranges, and no writable-plus-executable PT_LOAD. Nested Studio Run uses `0x51000000`. Pages are allocated from the kernel allocator and mapped in the shared root. | Image permissions describe EL1 access; executable pages are EL1 executable and data pages are EL1 writable or read-only. They are not user mappings. |
| App stack | The runtime owns a separate 16-page stack with canaries. The allocator gives page alignment; the bridge receives a 16-byte-aligned top. The stack is marked writable and non-executable in the shared root. | It is a separate allocation, not a user-owned virtual stack. |
| Permissions / TLB | `phase2_mmu_set_range_permissions` mutates leaves in the one shared root and has no user-access argument. Its supported range is limited to the low 2 GiB. It follows updates with `DSB ISH; TLBI VMALLE1; DSB ISH; ISB`. | It cannot create an isolated address space or EL0 access permissions. |
| Exceptions | `VBAR_EL1` is installed. Current-EL synchronous/IRQ slots have handling. Lower-EL vector slots branch to a fatal path; synchronous dispatch otherwise halts. There is no EL0 SVC dispatcher or recoverable child-fault path. | Neither normal child return nor child faults can be contained through an EL0 exception boundary today. |
| Scheduling | The common single-core scheduler saves/restores kernel task contexts and interrupt frames, but the context contains no TTBR root/ASID ownership. Timer IRQs preempt kernel tasks. | It cannot schedule a child-owned address space, charge an execution budget to it, or terminate a non-cooperative user loop. |

The MMU is configured with identity mappings and WXN enabled. Normal RAM maps use the normal WBWA attribute; device regions use the device attribute; page descriptors carry AP and PXN/UXN permissions. The current user permission encoding is not “EL0 with a separate root”: the permission update helper only chooses executable and writable and retains EL1-only AP settings. Changing only `SPSR_EL1` to EL0 would therefore cause the child to fault on its code, stack, or context, while leaving exception handling and scheduling incomplete.

The design is architecturally feasible without relocating the existing low kernel. A child-owned TTBR0 root can retain the identity virtual addresses required by kernel code and exception vectors, map kernel RAM and device pages as EL1-only, and grant EL0 permissions only to the child image and stack. It must omit kernel aliases from EL0 access, ensure no page is both writable and executable, and map no device page with EL0 permission. A bounded first prototype can continue to leave TTBR1 disabled. It still needs a new root/table allocator and explicit context ownership; the existing MMU root/table arena is global and not cloneable by a child.

## Minimum EL0 design

1. Allocate an address-space root and owned lower-level tables for exactly one child. Keep kernel virtual addresses identity mapped with EL1-only AP permissions so exception entry, scheduler code, kernel stack, and required EL1 data remain available. Device mappings used by handlers remain EL1-only and are never child-accessible.
2. Preserve the compiler/loader fixed image base `0x51000000`. Reject a segment if page rounding creates permission overlap or W+X. Map text EL0-readable/executable and EL1-readable/non-executable (`PXN=1`, `UXN=0`); map data EL0-readable/writable and execute-never (`PXN=1`, `UXN=1`); map read-only data read-only and execute-never. Keep the owned child stack in distinct RW/XN pages, with 16-byte-aligned initial SP and guard/unmapped boundary pages where the allocator permits.
3. Revalidate ELF header, architecture, package ABI, entry-in-executable-page, arithmetic, physical ownership, page-granular overlap, and exact base before committing any user leaf. Never pass kernel `gx_host_calls` pointers to EL0. The first ABI needs only a small user context and return/exit service.
4. Extend the lower-EL AArch64 vectors and exception dispatcher for SVC, timer IRQ, instruction/data abort, and other synchronous faults. Save the full user register frame and `ELR_EL1`/`SPSR_EL1`; attribute every exception to the current child task. Faults become a child result and must not resume a damaged frame.
5. Add address-space ownership to the scheduler task context. On a switch without ASIDs, write the selected TTBR0 root and use the required translation-table barriers and EL1/EL0 TLB invalidation before returning. The current global permission helper's flush is useful precedent, not a finished context-switch protocol. No SMP shootdown is needed for this single-core prototype.
6. Enter with `ERET` using an EL0t PSTATE, the validated entry in ELR, aligned user SP, and only child-visible register values. The normal `gx_main` AAPCS64 return value should flow to a user return veneer that invokes the exit SVC; alternatively, a defined exit service carries a signed result in `x0`. The kernel records a result only after observing that SVC or another real termination event.

This follows Arm's EL0/EL1 privilege and SVC model, translation descriptor permissions, 4 KiB granule, and exception-vector layout. The current 4 KiB translation granule and AP/PXN/UXN fields are defined in the kernel sources above. See Arm's [Memory Management guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf), [Exception Model guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Exception%20model.pdf), and [AArch64 SVC system-call guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Armv8-A%20Instruction%20Set%20Architecture.pdf).

## Kernel service boundary

The existing `gx_host_calls` table is a set of direct kernel function pointers for the privileged ABI. It is not safe to expose to an EL0 client. The initial EL0 program needs no GUI, VFS, or network service. Use one controlled `SVC #0` ABI with a small fixed service number in a documented register (proposed `x8`) and arguments in AAPCS64 registers. Support only `EXIT` initially; return its result only from the actual SVC trap. Reject unknown service numbers and invalid caller/task state.

Any later pointer-bearing service must validate overflow-safe user ranges against the current child's mappings, check length caps and direction, copy input into kernel-owned buffers, and copy output back only after revalidating the destination. The dispatcher must bind the request to the active task, package identity, and owned address space; reject cross-task handles; and propagate service errors without treating them as process completion. No user function pointer is invoked in EL1.

## Task lifecycle, bounds, and cleanup

The current `phase12_run_start` is synchronous; the current Studio `request_close` cannot stop it. The scheduler does timer-preempt kernel tasks, but there is no child-specific budget or termination path. The first safe task design should use explicit states:

```text
Created -> Prepared -> Queued -> Running -> Exited | Faulted | TimedOut | Cancelled
                                      \-> AllocationFailed | Rejected
Every terminal state -> CleaningUp -> Cleaned
```

Studio's start call should only enqueue an independently owned child task and return. Poll should read its snapshot; close should set a cancellation request. A timer-driven scheduler budget can stop even a non-cooperative EL0 loop by saving its lower-EL frame and choosing Studio/kernel work instead of rescheduling the child after its deadline. This is not cancellation support until that production switch and terminal-state path exist. Reclaim the child root, page tables, image, stack, and handles only after the task is not current and the kernel root/context is active. Completion becomes visible after the exit SVC, fault dispatch, or scheduler-enforced timeout/cancellation and cleanup.

No generic process manager is proposed. A single-child task record, single address-space owner, finite tick budget, and one exit SVC are enough for the first native qualification.

## Build-to-Run identity

The compiler already has a SHA-256 implementation, reads artifact bytes back from the VFS, checks the returned byte count, and records path, size, architecture and digest in its Build snapshot. A successful dual-target package also records `packageGeneration` equal to the Build handle. The Studio controller adds BuildResult source membership and artifact validation before creating a Run request.

The bare-metal Run service did not enforce that identity: it resolved the App Model package by project ID, did not compare the requested path/size/architecture/ABI/digest with the resolved executable, and copied the requested digest into the snapshot without proving equality. A failed later Build removes project-local stale outputs, while a previously published package may remain. Thus a path string or caller-supplied digest is not identity proof, and VFS replacement between validation and load is not excluded.

Before enabling Run, bind a successful Build generation to the Studio owner and project, exact saved-source membership identity, target, ABI, artifact byte size, and SHA-256. Clear the binding on any failed Build or changed input. At prepare, re-read the same VFS artifact and compare its bytes, size, manifest/ELF identity, and exact `0x51000000` load contract. Keep the verified bytes immutable through load (or load from a one-shot immutable artifact handle) so path substitution cannot occur between validation and mapping. Revalidate if VFS content changes. Existing compiler hashes and Build handle/package generation can support this without changing the compiler's output format; the Run service still needs the binding and byte handoff.

## Truthful completion semantics

The prior Studio callback set `exitCode = 42` whenever the loader returned success. The loader did not expose a generic child return value and used 42 as a Phase 9 workload expectation. That sentinel is not evidence that a Studio child executed. The gated path now reports `FAILED/LAUNCH_UNAVAILABLE` and `exitCode = -1`; it cannot report child completion.

After EL0 integration, normal `gx_main` return must reach the defined return veneer and an EL0-to-EL1 exit SVC carrying the actual AAPCS64 `x0` result. An explicit exit service uses the same result contract. Keep separate terminal reasons for successful return, child fault, timeout, cancellation, loader rejection, allocation failure, and unsupported runtime. Set `COMPLETED` and a nonnegative exit result only after a real return/exit event. Rejection/fault/timeout/cancel results have no synthetic exit code.

## Implemented and pending

Implemented in this milestone:

- Studio Run fails closed at prepare and start, plus the nested Phase 9 loader boundary.
- Rejection carries an explicit runtime-safety message, `LAUNCH_UNAVAILABLE`, `FAILED`, and `exitCode = -1`.
- Hosted adapter controls cover the denial decision, explicit message, failed state, no exit-42 result, and completed cleanup semantics.
- A production source regression checks that prepare rejects before VFS refresh/package resolution, start cannot reach the nested loader or synthesize exit 42, and the Phase 9 loader backstop precedes privileged host setup.
- Phase 9 builder now includes `sdk/include`, the Phase 9-only compiler target declaration, and `aarch64_gui_render.cpp`, which the linked kernel needs for SurfaceRegistry and host callback publication.
- Native filesystem contract harness links `app_payload_resolver.cpp` and `kernel/core/native_architecture.cpp`, then runs the resulting executable after a successful link.

Not implemented or not qualified:

- Child-owned page-table construction, EL0 AP mappings, EL0 entry/return, lower-EL vectors/SVC, user-copy helpers, per-task TTBR/ASID switching, fault containment, timer-enforced child budgets, cancellation, and native teardown.
- Build-generation/source/artifact identity binding between the successful Build and Run service; immutable validated bytes through load.
- Native EL0 or memory-isolation guest evidence. No guest run is claimed.
- Generic Phase 9 NativeElf applications remain EL1-capable; this document's gate is specifically for Developer Studio child Run.

## Hosted controls and regressions

Completed for this milestone:

- Phase 2 memory/exception host validation: **51 assertions passed**; the same build compiled and linked the ARM64 EFI loader and Phase 2 kernel.
- Phase 3 scheduler contract controls passed; the same build compiled and linked the ARM64 EFI loader and Phase 3 kernel. The three-boot guest proof was not run.
- GICv3 hosted register-model suite: **62 assertions passed**.
- Phase 9 packaged build and lifecycle host controls passed, including native ABI layout; ARM64 EFI, kernel, and app artifacts linked. The guest lifecycle boots were not run.
- Phase 10 host controls passed for ARM64/AMD64 resolution and missing payload, unknown architecture, wrong machine, traversal, malformed/oversized manifest, duplicate declaration, legacy resolution, and discovery. Native ABI layout passed.
- Phase 11 compiler build completed and staged its multi-architecture package; the included Phase 10 host controls and ABI layout test passed. The guest compiler workflow was not run.
- Phase 12 pinned Studio ABI1 incompatibility negative control passed; ABI3 adapter suite passed **181/181**; production Run source gate passed; pinned Studio AArch64 NativeElf compile/link and package staging passed.
- Native filesystem contract harness linked and executed: **PASS**.
- Captured DTB decoder passed on the preserved `firmware-source.dtb` (223 properties; SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`).
- Freestanding ARM64 EFI/kernel compile and link passed in the Phase 2, Phase 3, Phase 9, Phase 11, and Phase 12 production packaging build recipes.

The Phase 9 builder correction adds the omitted SDK include path and the narrow compile/link dependencies exposed when rebuilding from a clean output directory. The independent filesystem harness correction adds the manifest resolver and NativeArchitecture implementation sources it was missing. These are kept separate from the Studio isolation guard and regression.

No QEMU guest was started and no QAC admission was requested. The guest-only Phase 2/3/9/11 workflows were intentionally not claimed as passed. Tests for page-table permissions, EL0 kernel/MMIO denial, separate address-space ownership, SVC validation, user-pointer copy handling, Build artifact staleness or post-validation modification, bounded cancellation/timeout, actual exit delivery, and lower-EL fault recovery remain pending until those native components exist; no pointer-range mock is presented as isolation evidence.

## Smallest next native qualification gate

Implement one child-owned TTBR0 root with kernel identity mappings restricted to EL1 and a user-only fixed-base image/stack; add lower-EL synchronous and IRQ exception entry with one `EXIT` SVC; extend a single scheduler task context to own/switch the root and enforce a timer budget; bind the exact successful Build artifact bytes to that task; and report the actual SVC return only after teardown. Then add a guest test that proves user access to kernel RAM/MMIO faults, child code is EL0, Studio remains schedulable during an infinite loop, cancellation/timeout releases ownership, and return values other than 42 are preserved. Do not enable Studio Run before those checks pass.
