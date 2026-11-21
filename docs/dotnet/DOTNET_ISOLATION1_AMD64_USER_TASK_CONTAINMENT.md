# DOTNET_ISOLATION1 — AMD64 User Task Containment

**Result: BLOCKED at the AMD64 execution-boundary prerequisite.** This report records the mandatory source and branch reconnaissance for the requested milestone. No kernel source was changed, no candidate was built, no hosted isolation tests were run, and no QAC reservation or guest boot was attempted. The existing NativeAOT review and three modified generated object files were preserved.

The stopping point is architectural, not a QAC admission failure: the current AMD64 branch has no CPL 3 transition or TSS, no independently owned address-space root, and no timer path that preempts an executing task. Adding a fixture or owner ledger before these foundations would not produce the requested containment evidence.

## A. Starting state and implementation inventory

### Repository state

| Item | Starting value |
| --- | --- |
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| HEAD | `f9737a29271b457a38abd0368510b924cc331d1f` |
| Working tree | Three modified generated objects and the pre-existing untracked architecture review document |

Protected object hashes observed during this task:

| File | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |
| `docs/dotnet/NATIVEAOT_ARCHITECTURE_AND_APPLICATION_ISOLATION_REVIEW.md` | `ECBE6A1D622AD1484B517CF9E097BEF474F20C39D0D907D3B863B8953E192A6D` |

The review document was present before this task and remains unchanged. No output directories, guest artifacts, or other worktrees were modified.

### Current AMD64 implementation

| Area | Current branch finding | Consequence for this milestone |
| --- | --- | --- |
| GDT and privilege stack | `kernel/core/interrupts.cpp` defines a three-entry GDT and installs only kernel code/data selectors `0x08`/`0x10`. There is no TSS descriptor or `ltr` initialization. The syscall entry comments in `kernel/arch/amd64/syscall.cpp` still list loading `TSS.RSP0` as a TODO. | CPL 3 cannot safely enter the kernel on a kernel-owned privilege-transition stack. |
| Trap evidence | The page-fault stub forwards vector, error code, RIP, and RFLAGS, but not saved CS or a normalized general-register frame. The ordinary page-fault handler halts. There is no task-owner fault retirement path. | A user protection fault cannot yet be distinguished and retired through an owner-specific recovery boundary. |
| Interrupt and preemption | PIT IRQs call `process::timer_tick()`, which expires scheduler waits. `process::schedule()` switches cooperative kernel thread contexts. Neither call path preempts a running thread. `arch::amd64::context::arch_timer_tick()` contains a scheduler-preemption TODO. | A genuinely non-cooperative loop cannot be retired by the current timer path. |
| Task context | `kernel/core/process.cpp` owns kernel-thread records and a cooperative scheduler. AMD64 context records inherit the current CR3; the context-switch code has a TODO to update TSS.RSP0. | Existing task ownership does not provide an independent user execution context. |
| Address spaces | `kernel/core/address_space.cpp` has a single `g_current`. Mapping, unmapping, query, and flag-update reject any owner except that singleton. `destroyCurrent()` only invalidates the singleton record. | There is no independent root creation, shared supervisor mapping construction, or owner-specific page-table teardown. |
| Frame/mapping ownership | The frame allocator accounts for VM, page-table, and kernel frame classes and tracks whether a frame is mapped. Mapping ownership is global to the current address space; no per-user-task mapping ledger exists. | It cannot yet report and reclaim every mapping and table owned by one retiring task. |
| Native ELF / App Model | Existing native ELF and trusted NativeAOT lifecycles use the existing kernel execution and address-space path. The architecture review records the resident NativeAOT single-image guard and confirms that logical application generations do not create a process. | Keep these launch paths intact; they cannot be used as substitutes for a user task. |

The current QAC phase-2 worktree is `codex/qac-c176-retained-session` at `79070db55997cd867ee79134ee505b39ea5df35d`. A path-scoped diff found no AMD64, process, address-space, or native ELF implementation changes to reuse from it.

### Local sibling mechanisms inspected

| Checkout | Revision | Findings and reuse decision |
| --- | --- | --- |
| `D:\dev\guideXOSServer_AARCH64_STUDIO_RUN_CONTRACT` | `fd7aa8fc17528098` | Contains `RUNSAFE3B` and a real scheduled EL0 child path. Its owner/execution IDs, generation checks, deadline eligibility, fault evidence, terminal observation, and reap semantics are useful design references. Its EL0/EL1 vectors, TTBR tables, exception frames, and `eret` transitions are architecture-specific; no code was copied. |
| `D:\dev\guideXOSServer_AARCH64` | `873dde6a3ed3bee509f374fcdca7090c` | A separate AArch64 implementation checkout. Its architecture-specific execution machinery is not a source for AMD64 assembly or page tables. |
| `D:\dev\guideXOSServerV0.5_PHASE30B_OWNER_RUNNER` | `c2dada8520c58f7e` | Contains application runtime identities and generation-checked native development run/debug handles, plus owner-close and cleanup-completion paths. Its compiler build manager handles queued or safe-step cancellation; production synchronous compiler work is not force-terminated. These mechanisms manage an in-kernel development application or a compiler operation, not an independently scheduled CPL 3 task. Reuse the owner/generation and terminal-cleanup principles only; no code was copied. |
| `D:\dev\guideXOS_Developer_Studio` | `058c83fe1ae270a1` | Hosted Studio build jobs retain owner/runtime checks and process/worker cleanup. They do not define the Server kernel's user-task or AMD64 trap ABI. Not reusable as a kernel task implementation. |

The requested RUNSAFE3 precedent was found in the AArch64 Studio run-contract checkout, specifically its `RUNSAFE3B` diagnostics and `phase2_el0_runtime.cpp`. It provides analogous behavior but no AMD64 primitives. No branch was merged or changed.

## B. Implementation summary

No kernel implementation was started. This is deliberate: each proposed first edit is coupled to missing mechanisms that determine whether entry can return safely. A TSS-only change would not create a private root or interrupt-time retirement. A page-table-only change would not permit safe user entry or protect the desktop from an unresponsive task. A simulated owner state machine would not test real trap, timer, or frame behavior.

No public SDK or ABI, production launcher, NativeAOT runtime, generated object, or unrelated worktree was changed. The existing single-image guard and trusted composite path remain as they were at the recorded HEAD.

### Minimum integrated AMD64 dependency before fixture work

The next implementation should establish one coherent AMD64 task boundary containing:

1. User code/data GDT descriptors, a loaded TSS, and a per-running-task `RSP0` on a kernel-owned stack.
2. Normalized exception/IRQ frames that preserve the user CS/CPL, error code, fault address, registers, and user return state, while keeping genuine kernel faults fatal.
3. An interrupt-return or scheduler context-switch path that can leave a user task on a timer interrupt and continue on a kernel-owned context before any task memory is freed.
4. A second address-space root with the existing kernel mappings supervisor-only, owner-tracked user mappings, and complete rollback/destruction through the existing frame ledger.
5. An opt-in native fixture lifecycle with generation-validated completion, user-fault and deadline termination, exactly-once retirement, and safe same-VA reuse.

These pieces need a shared AMD64 trap/task contract. Implementing an isolated subset without that contract is not a safe diagnostic lane.

## C. Hosted verification

| Verification | Command | Result |
| --- | --- | --- |
| DOTNET_ISOLATION1 hosted cases | Not run; no source implementation or genuine owner API exists to test | 0 tests, 0 assertions, 0 injected failures |
| Candidate build / linked validation | Not run | No candidate identity or output was created |
| Existing native and NativeAOT regression suites | Not rerun; no relevant source interface was changed | No new result claimed |

There are no new focused test files and no test failures to report. This is not a hosted pass; it is an unimplemented and unverified milestone.

## D. Guest evidence

| Evidence field | Result |
| --- | --- |
| Diagnostic candidate identity or hash | None; candidate not built |
| QAC coordinator health/admission | Not queried; no guest candidate reached admission |
| Reservation / attempt guard | None requested or created |
| Guest boot identity | None; no boot attempted |
| Raw serial, QMP, or witness artifacts | None |
| Test-owned QEMU process | None started by this task |

The owner's report that QAC and other workloads were idle was treated as context, not a reservation. Since no guest execution was attempted, no QAC state was changed and there is no reservation to release. The one-boot authorization remains unused.

## E. Resource accounting

No diagnostic task was created, so this task acquired no user image, stack, page-table, or task frames and installed no test mappings. There is no before/after diagnostic frame delta, no mapping-owner ledger, and no safe-VA-reuse observation to report.

The existing global `FrameAccounting` covers allocated/free frames, VM frames, page-table frames, kernel frames, aggregate mapping count, and releases. It does not associate every leaf mapping and page-table frame with an independently destructible address-space owner. The existing NativeAOT VM accounting and resident image allocations are shared baseline resources and were not measured in a new candidate.

## F. Compatibility results

No source or binary candidate was changed or built, so this task produced no fresh compatibility result. The following retained evidence is summarized in the pre-existing architecture review and remains limited to its recorded workload:

- C181R1 demonstrated five logical managed applications within one composite executable and shared runtime; it did not establish independent address spaces.
- C187 executed a separately compiled CounterApp after linking it into that composite executable; it did not run a separately deployed ELF as a private user task.
- C104 recorded rejection of a second independent NativeAOT image with `Busy`; it did not demonstrate a second successful image lifetime.
- R36 exercised the trusted managed composite and its recorded dispatch path, not an isolated user task.

These are retained results, not executions of a DOTNET_ISOLATION1 candidate. Existing behavior is unmodified because no source code changed.

## G. Acceptance scorecard

| Astra Section E requirement | Status | Evidence |
| --- | --- | --- |
| 1. Independently owned CPL 3 task, root, private writable mappings, supervisor-only kernel mappings, and permission evidence | **FAIL** | Source inspection finds only the three-entry kernel GDT, no TSS load, and one `AddressSpace` owner. No CPL 3 entry exists in this path. |
| 2. Normal exit, recoverable owned fault, and kernel-deadline retirement in one boot | **FAIL** | The page-fault path halts; the PIT path only processes wait deadlines; no timer preemption switches away from an executing task. No fixture boot occurred. |
| 3. Safe-context cleanup, exact resource accounting, stale-handle rejection, and clean VA reuse | **BLOCKED** | A single address-space root and aggregate mapping/frame accounting cannot support or evidence owner-specific retirement. |
| 4. Same existing native witness responds to fresh input after every termination and remains responsive | **BLOCKED** | There was no isolated diagnostic run to interleave with the witness. Existing retained native-app behavior is not evidence for this sequence. |
| 5. Existing native and trusted managed regressions on the touched interfaces/candidate | **PARTIAL** | Prior reports contain native and trusted composite evidence, but no new candidate or touched-interface regression was run here. |

Overall DOTNET_ISOLATION1 acceptance is **BLOCKED / NOT ACCEPTED**. No same-boot execution evidence exists.

## H. Remaining work and final engineering questions

The next smallest engineering dependency is the integrated AMD64 task boundary listed in Section B: TSS-backed privilege transitions, full trap frames, timer-driven preemption/retirement, and a separately owned root with mappings that can be reclaimed after switching to a safe kernel context. Only after that is built and hosted rollback/accounting tests pass should the three-mode ELF fixture and witness candidate be assembled for one fresh QAC admission and at most one boot.

1. **Can guideXOS regain control from an unresponsive user-mode application without cooperation?** No demonstrated capability yet. This AMD64 path has no user-mode task entry; its timer does not preempt an executing task.
2. **Can a user-mode application fault without terminating the kernel or disrupting an existing native application?** Not yet. There is no owned user-fault path; the current page-fault handler halts.
3. **Can the kernel reclaim and safely reuse the terminated application's resources?** Not yet. No private address-space lifetime or owner-specific map/table reclamation exists.
4. **Which RUNSAFE3 or Developer Studio mechanisms were reused, adapted, or found incompatible?** None were copied or adapted in code. RUNSAFE3B's owner/generation/deadline/fault/reap design and Developer Studio's generation-checked owner cleanup are useful patterns. Their AArch64 transition code, hosted worker lifecycle, and compiler-step cancellation are incompatible with an AMD64 process boundary.
5. **What remains before an independently compiled NativeAOT executable can safely run in the new boundary?** First qualify the native AMD64 task boundary and diagnostic lifecycle. Then build a private NativeAOT image path with owner-scoped image/stack mappings, runtime startup/TLS/PAL state, exception metadata and unwind behavior, and a clear GC/runtime lifetime policy. Multiple concurrent managed runtimes or cross-process GC are separate follow-on work.

