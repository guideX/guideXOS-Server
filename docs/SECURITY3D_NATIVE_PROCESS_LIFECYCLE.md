# SECURITY3D — Native Process Lifecycle, Ownership, and Authorization

**Outcome:** `SECURITY3D_NATIVE_PROCESS_LIFECYCLE_PARTIAL`

## Scope and result

SECURITY3D adds bounded runtime records for Native ELF code that actually enters and returns through the production AMD64 launcher. The records are named **managed execution instances** because the current launcher runs application code synchronously on the desktop's call stack and does not schedule independent processes.

The implementation provides generation-safe identities, trusted launch-time ownership snapshots, terminal exit/fault status, bounded query snapshots, authorization checks for queries and termination requests, and truthful cleanup reporting. It does not provide independent scheduling or safe termination. Native ELF remains kernel-privileged, so this metadata and API authorization do not isolate it from kernel memory or services.

## Native execution architecture

`kernel/core/main.cpp` initializes VFS, identity, and authentication, discovers Native ELF packages, initializes the desktop, and then runs the desktop input/event loop on the boot thread. Desktop/App Model dispatch reaches `native_elf::launch()` synchronously. The launch guard prevents a nested launch while another package is active.

`kernel/core/native_elf_baremetal.cpp` validates the package, applies the SECURITY3C execution policy, loads the verified image, allocates a 128 KiB application stack, and calls `gxos_native_call_on_stack()`. That assembly trampoline changes RSP for the call and restores the kernel stack when the entry function returns. This is a synchronous stack handoff, not a task context that can be scheduled independently. While an application polls for events, the host callback pumps the desktop on the same call chain.

Normal completion is the actual `gx_main` return value. The ABI `exit` callback records `exitRequested` and an exit code, but it cannot stop code that does not return; the launcher does not treat that flag alone as completion. Native ELF faults observed while the app RIP or stack is in its runtime are captured by the existing fault path and recover to the saved kernel stack. The runtime records the fault vector and error code, cleans up, and returns to the desktop. Other kernel faults still halt. A kernel-privileged infinite loop is not recoverable by this mechanism.

### Scheduler and process-table findings

- `kernel/core/process.cpp` contains a fixed 16-entry stub with `create_init_process()` and a `schedule()` that halts. No production kernel call site uses those functions. SECURITY3D leaves this stub unchanged.
- AMD64 has low-level context save/restore code in `kernel/arch/amd64/context_switch.cpp`, but no production caller creates or switches Native ELF tasks. Its timer hook still has a TODO for scheduling/preemption; the thread-return wrapper halts, and FPU state/TSS stack updates are unfinished. These are not a usable process scheduler.
- The root `scheduler.cpp` belongs to the hosted runtime, not the freestanding AMD64 desktop execution path.
- PIT interrupts provide ticks and wakeups, but do not preempt Native ELF or preserve a schedulable app context. No safe external cooperative stop channel is available while the synchronous app call is active.

## Managed execution records

`kernel/core/managed_execution.cpp` owns a fixed table of 16 records. The Native ELF launcher creates a record only after image, source, and execution-policy checks pass and immediately before it transfers control to `gx_main`. The record moves through `Created` and `Running`, then reaches `Exited` or `Faulted` after the trampoline returns and app-owned cleanup has run. No `Ready`, `Blocked`, `TerminationRequested`, or `Reaped` state is advertised because the current runtime has no corresponding production transition.

Each value snapshot includes:

- A monotonic 64-bit runtime ID, a slot, and a nonzero slot generation. A handle must match all three. Reused terminal slots cannot revive stale handles.
- Kind `NativeElfManagedInstance`, canonical App Model ID, and monotonic start sequence. No wall-clock timestamp is claimed.
- Captured owner kind, stable user ID when authenticated, and session generation provenance.
- Ordinary return status or observed fault vector/error code, plus a resource-cleanup flag. The query does not expose RIP/RSP, credential material, kernel pointers, or invented CPU/memory/thread statistics.

At most one record can be `Running`, matching the synchronous production path. The table never replaces an active record. After the table fills with terminal records, a new start reuses the oldest terminal slot; its former handle becomes stale. A second active start is rejected with `CapacityExhausted`. IDs are never reused; ID exhaustion fails deterministically. Records are volatile runtime state and do not survive reboot.

The existing `LaunchAttributionV1` remains separate historical policy/launch metadata. Its `launchInstanceId` is not a PID and is not used as the managed record handle. The production launcher now emits serial markers for the managed runtime ID, launch-attribution ID, owner snapshot, real completion/fault result, and cleanup result.

## Ownership and session behavior

The launcher obtains attribution from `authentication::Runtime::queryExecutionAuthority()`; the application cannot supply owner IDs, role, or session generation through the Native ELF ABI. Authenticated Administrator, Authenticated Standard User, unassigned interactive, and invalid/unavailable states remain distinct. Role and user ID are historical launch-time metadata. Sign-out, role changes, disablement, or deletion do not rewrite an existing record or transfer it to another account.

The record stores session generation as provenance only. Management operations call `authentication::Runtime::authorizeCurrent()` at the operation boundary, so an invalidated or disabled current session loses future authority even though the historical owner remains queryable to an authorized Administrator. A system owner value is reserved in the record schema, but SECURITY3D does not create synthetic system execution records; current tracked objects are Native ELF launches from the interactive desktop.

## Query and management interface

The bounded service is `managed_execution::Runtime::{query,enumerate,requestTermination}` and is also reachable through the Native ELF provider facade in `native_elf_baremetal.h`. Queries return copied values, never record pointers. The current implementation is serialized by the single-threaded synchronous desktop path; it does not add a lock or claim SMP-safe snapshots.

`authorization::Operation::ProcessQuery` allows an authenticated Standard User to query records with the same trusted owner ID. Administrators can query any nonsecret record. An unassigned desktop can query unassigned execution metadata, but receives no Administrator authority. Enumeration filters records through these checks. Caller-provided identity or role is never accepted.

Termination requests use `ProcessTerminateOwn`, `ProcessTerminateOther`, or `ProcessPrivilegedManagement` as appropriate, with the target owner taken from the stored record. Cross-user Standard User requests are denied; stale handles and invalid sessions are rejected. A protected system target is denied by policy even when an Administrator passed the administrative authorization check. No current system-owned Native ELF record is generated.

For an authorized request against a running Native ELF instance, the provider returns `TerminationUnsupported` and leaves the record in `Running`. It does not claim the request succeeded, alter the instruction pointer, free the app stack, or release executable memory early. After return/fault, audio ownership, window/surface state, stack, image, and verified-image buffers are cleaned up before the terminal snapshot is published.

The service is available for future native consumers such as Task Manager, but no Settings control or Task Manager UI was added. Since application execution is synchronous, desktop code cannot concurrently enumerate a running instance through an independent UI task today; serial lifecycle markers provide runtime diagnostics.

## Native ELF and compatibility

The managed record is created only after the existing SECURITY3C validation and application execution-policy checks accept the package. Development unknown/unsigned compatibility and public-preview PacMan restrictions remain in their existing policies. Launch attribution still records rejected or failed pre-entry attempts without inventing an execution ID. When the app returns or the supported app-fault recovery path runs, the record reflects that result and cleanup state.

No mandatory login, global signature requirement, application trust registry, package-manager restriction, or user-mode boundary was added. Normal launches still work in the unassigned desktop; ownership is recorded as unassigned rather than elevated.

## Verification

- Focused managed execution lifecycle harness: **45/45** checks passed, using the production registry and authentication/authorization APIs. Coverage includes creation, transitions, exit/fault distinction, cleanup state, repeated launches, bounded reuse, stale handles, one-active-instance behavior, standard/admin/unassigned ownership, query filtering, session invalidation, role/account-status history, and honest unsupported termination.
- SECURITY1 identity: **43/43** passed.
- SECURITY2 authentication/session: **60/60** passed.
- SECURITY3C Native ELF execution-policy regression: passed with zero failures.
- App Model identity: **25/25**; file activation: **60/60**; folder activation: **26/26**; application actions: **55/55** passed.
- Native Settings runtime: **1,447/1,447**; layout: **1,037/1,037**; Users model/navigation/interaction: **14/14**, **8/8**, **13/13**; Settings Center: **89/89** passed.
- AMD64 freestanding kernel build: passed, including `managed_execution.cpp` and the Native ELF integration.
- Hosted `build.bat`: passed and produced `guideXOSServer.exe`; existing warnings remain in unrelated hosted UI/parser sources.
- No VFS implementation was changed. QAC was checked read-only and reported online with no active reservation at the final check, but no guest was started: this checkout has no AMD64 UEFI bootloader or ISO artifact to boot the changed kernel through the available UEFI qualification workflow. Therefore no desktop/app lifecycle behavior is guest-proven in this phase. SECURITY3A's pending truncated-slot case and SECURITY3C's guest qualification remain separate.

## Capability matrix

| Capability | SECURITY3D result | Evidence and limit |
|---|---|---|
| Real scheduler-backed processes | **Not implemented** | Native ELF is called synchronously; AMD64 task switching/preemption hooks are incomplete and unused by this path. |
| Managed execution-instance tracking | **Implemented, synchronous only** | Production Native ELF create/start/return/fault/cleanup integration with bounded snapshots. No independent task context. |
| Ownership metadata | **Implemented for interactive Native ELF launches** | Captured from the trusted authentication runtime. System-owned record creation is not yet used. |
| Authorization-controlled operations | **Query implemented; termination request authorization implemented** | Own/cross-user query checks are enforced. An authorized termination request returns unsupported because no safe stop mechanism exists. |
| Cooperative termination | **Not implemented for external management** | `host_exit` is an application callback and cannot stop a noncooperating app; no external signal is consumed. |
| Forced termination | **Not implemented** | No instruction-pointer changes or unsafe stack/resource release. |
| User-mode isolation | **Not implemented** | Native ELF currently executes with kernel privilege. |
| Memory/address-space isolation | **Not implemented** | Apps share the kernel address space and can potentially bypass ordinary kernel API checks. |

## Recommended follow-up

### SECURITY3E — scheduler-backed execution

Build on the kernel's actual AMD64 interrupt and context conventions: per-thread kernel stacks, complete saved register/FPU state, TSS.RSP0 updates, interrupt-safe ready queues and locks, timer EOI, lifecycle exit/reap, and desktop event dispatch that can yield without holding UI/VFS locks. Add a cooperative cancellation point only after a real running task can return to the scheduler safely. Migrate Native ELF execution only after this foundation can preserve and restore every context it switches.

### SECURITY4 — isolation boundary

Introduce user-mode execution, separate address spaces, validated syscall/service entry points, memory ownership, and capability checks before describing Native ELF code as contained. Role-based process API checks alone cannot constrain malicious kernel-privileged Native ELF code.
