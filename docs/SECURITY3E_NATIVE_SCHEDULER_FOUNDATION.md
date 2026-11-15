# SECURITY3E — Native Scheduler Foundation

**Project:** guideXOS Server
**Branch:** `SECURITY_SETTINGS`
**Baseline:** `7d7a7b78ac0497039d165baed868d6aa79f2621a`
**Status:** Foundation implemented and guest qualified; Native ELF scheduler integration remains a later phase

## 1. Runtime architecture findings

The AMD64 graphics desktop is owned by the boot thread running `kernel_main` in `kernel/core/main.cpp`. It initializes the desktop, enables the PIC/IDT and PIT, then services input, system requests, networking, desktop state, drawing, and presentation from one loop. The timer wakes that loop; it does not dispatch another task.

Desktop app dispatch calls `native_elf::launch()` synchronously. The loader in `kernel/core/native_elf_baremetal.cpp` creates its per-launch runtime, allocates a 128 KiB app stack, and calls the app entry through `gxos_native_call_on_stack` in `kernel/arch/amd64/native_elf_call.asm`. The app runs at kernel privilege in the shared address space. Its event-poll callback pumps desktop work on the same call chain. The SECURITY3D managed-execution record remains attribution and lifecycle metadata, not a scheduler object or PID.

The existing `kernel/core/process.cpp` table is still a stub and remains untouched. The old `kernel/arch/amd64/context_switch.cpp` is also disconnected from production execution: it uses shared static entry/argument values, has unfinished return, FPU, TSS, and timer hooks, and its switch assembly assumes SysV argument registers even though the freestanding MinGW target uses the Microsoft x64 ABI. The hosted root `scheduler.cpp` starts host threads and is unrelated to this kernel path.

The AMD64 PIC IRQ stubs save volatile general registers, call a bounded C handler, restore registers, and return with `iretq`. CPU exception stubs save all general registers and switch to the kernel-owned IST exception stack before reporting. The PIT handler only increments ticks and reports its heartbeat. Neither path invokes a scheduler. Kernel AMD64 code is compiled with `-mno-mmx -mno-sse -mno-sse2`; the interrupt handlers likewise do not own or save FPU/SIMD state.

The current AMD64 kernel has one shared kernel address space, global device and service state, no per-context TLS contract, and no per-task TSS.RSP0 update. Same-ring interrupts stay on the interrupted context's stack; CPU exceptions use the configured IST stack. There is no safe mechanism to stop an arbitrary kernel-privileged Native ELF that never returns or yields.

The AMD64 entry and linker path does not run C++ `.init_array` constructors. A freestanding global runtime object therefore begins as zero-filled BSS unless its explicit initialization method establishes nonzero sentinels. The scheduler initializes all context records and its invalid-current-slot/runtime-ID state inside `Runtime::initialize()` for this boot contract.

## 2. Implementation

`kernel/core/native_scheduler.cpp` adds a single-CPU, fixed-capacity cooperative scheduler for trusted kernel work. The AMD64 implementation has eight slots and allocates one 64 KiB stack for each live context. It scans state in round-robin order instead of maintaining a mutable run queue, so a context cannot acquire duplicate ready entries.

`kernel/arch/amd64/native_context_switch.asm` implements the actual switch using the Microsoft x64 ABI. At a normal call boundary it saves and restores the nonvolatile GPRs `RBX`, `RBP`, `RSI`, `RDI`, and `R12`–`R15`; the saved stack contains the return address and therefore the continuation RIP. Caller-saved GPRs and status flags follow the ordinary ABI contract. The initial stack frame enters a trampoline with the expected 16-byte alignment and Microsoft ABI shadow space. The context pointer is carried in `R12`, so separate contexts do not share pending-entry globals.

The scheduler validates saved stack bounds and the saved return address against the kernel text range. Low/high stack canaries detect boundary corruption at dispatch and resume points. Finished and invalid contexts are reclaimed only after execution has returned to the root desktop stack. Completed lifecycle records remain queryable until their slot is reused; handles combine a monotonic runtime ID, slot, and nonzero generation. `initialize()` explicitly resets every slot and nonzero runtime sentinel because the freestanding AMD64 boot path does not execute static constructors.

The supported SIMD/FPU contract is deliberately narrow: scheduler-managed entries must not use FPU, MMX, SSE, AVX, or change MXCSR. No SIMD/FPU state is saved. The AMD64 build flags disable these code generators for current kernel C++, and the scheduler translation unit fails compilation if those flags are removed. These are trusted kernel tasks, not a sandbox; arbitrary inline assembly or future code compiled with different flags would violate the contract.

Yield is valid only from a running context with interrupts enabled. Creation and dispatch also reject interrupt-disabled contexts. The switch does not save a private `RFLAGS.IF`; all contexts share the kernel interrupt state and the timer cannot switch contexts. Yield points must be explicit and must not cross locks or critical sections that are not designed for suspension. The scheduler does not enforce lock ownership.

## 3. Production and desktop integration

After the COM2 system-service bridge is initialized, the AMD64 desktop creates a trusted kernel-system context that services `system_service_bridge::poll()` and `ipv4::poll_network()`, then yields. The main desktop loop dispatches it at the previous polling safe point. Input, keyboard/mouse routing, desktop focus, compositor state, drawing, and presentation retain the boot thread as their single owner. Both service calls finish before the cooperative yield; they are not run concurrently with the desktop or with each other. Guest serial evidence confirms the service context entered after creation while PIT heartbeats continued.

If scheduler initialization or task creation fails, the main loop retains the original direct polling calls. If the service context ends or its stack integrity check fails, the loop logs the event and falls back to direct polling. A synchronous Native ELF launch still suspends the root loop; its existing app event callback pumps desktop work but does not dispatch the new service context. Therefore COM2/network servicing can pause during that synchronous launch.

## 4. Context identity and lifecycle

The scheduler's generation-safe `Handle` is a **kernel execution-context ID**, not a PID, process ID, or SECURITY3D managed-execution ID. Snapshots identify the context as `TrustedKernelSystem`; they carry no App Model ID, user owner, or session attribution. Native ELF continues to use the existing SECURITY3C launch checks and SECURITY3D synchronous lifecycle records; no second record is created for one app execution.

The lifecycle is `Ready → Running → Ready` on yield, or `Ready/Running → Completed` on normal return. A stack-boundary or saved-context integrity failure becomes `Faulted`; that label does not claim CPU exception recovery. `requestTermination()` returns `Unsupported` for a valid handle without changing its state. There is no forced termination, process ownership query API, or process-management authorization change.

## 5. Faults and resource limits

Stacks are heap allocations with canaries, not guard pages. A canary or saved-context failure prevents rescheduling and allows reaping from the root stack, but it cannot undo memory corruption that occurred before detection. A CPU exception in a scheduler context still follows the existing kernel exception path and is kernel-fatal unless it is the separately recognized Native ELF fault path. Faults are not treated as automatically recoverable.

No allocation occurs in IRQ context. Context allocation is accepted only from the root context with interrupts enabled. Active stacks are never freed. Capacity exhaustion, allocation failure, stale handles, invalid yield state, and unsupported termination have explicit results.

## 6. Native ELF compatibility

Native ELF is **not scheduled by this phase**. It continues to execute synchronously with kernel privilege and the existing 128 KiB launch stack, SECURITY3C pre-entry policy, SECURITY3D attribution, and existing observed-fault behavior. It can access kernel memory, disable interrupts, monopolize the CPU, or hang the kernel. A separate kernel stack is not isolation. Migrating Native ELF requires a later review of guest ABI and host-callback assumptions, nested desktop servicing, fault ownership, FPU/SIMD, global state, and lifecycle/resource cleanup.

## 7. Preemption readiness

Timer-driven preemption is not implemented and is not safe to enable by calling the current timer hook. It would require at least:

- A complete interrupt frame and context switch from IRQ entry, with every required GPR, segment/base, flags, and stack state restored correctly.
- An FPU/SIMD ownership policy with correct `FXSAVE`/`FXRSTOR` or `XSAVE`/`XRSTOR` handling for every preemptible context, including Native ELF.
- Audited interrupt masking, PIC/APIC EOI, scheduler reentrancy, interrupt nesting, and nonpreemptible critical sections.
- Synchronization for shared VFS, network, desktop, and device state, plus rules preventing locks from spanning yields.
- Per-context kernel-stack/TSS handling if privilege transitions are introduced, and safe task exit/reaping and termination boundaries.
- A privilege boundary and address-space design before claiming application containment or kernel survival after an app fault/hang.

## 8. Build and guest artifact identity

The newer `build.ps1 -ReleaseProfile Development` path rejects this checkout's Developer Studio package because it does not match the exact Phase 25 signed-off manifest/ELF pair. Its subsequent PacMan export also encountered a missing generated CMake file in the isolated checkout. The older `build-uefi.ps1` builds the UEFI loader from this repository's `guideXOSBootLoader` source and stages an ESP directory. Its first isolated attempt exposed that the locally provisioned Mbed TLS source tree is untracked and therefore absent from a clean worktree; the pinned local source/profile is copied into the isolated worktree for the final build. The original ESP and frozen release artifacts are not used as build destinations.

The candidate is assembled from the source-built UEFI loader, a freestanding AMD64 kernel built with `GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE`, and the isolated worktree's ESP content. The final kernel, bootloader, QEMU, OVMF, and serial-log evidence are recorded in the qualification results below. No prebuilt guest kernel is substituted. QEMU is launched only through an exclusive QAC-managed session. The root ESP and the frozen release candidate were not used as build destinations.

## 9. Capability matrix

| Capability | SECURITY3E status | Evidence / limit |
|---|---|---|
| Synchronous Native ELF execution | **Verified preserved by host regressions** | Existing loader and managed-execution path are unchanged; app execution remains synchronous. |
| Independent native execution contexts | **Guest qualified** | Separate bounded heap stacks, saved context frames, and local-state retention passed in QAC guest. |
| Cooperative scheduling | **Guest qualified for trusted kernel work** | Round-robin dispatch, yield, resume, completion, and root-stack reaping passed; production service context entered. |
| Timer-driven preemption | **Not implemented** | PIT remains tick-only. |
| Process ownership | **Unsupported** | Contexts are trusted kernel-system work, not processes. |
| Safe cooperative termination | **Unsupported** | Completion by return only; termination request is explicit `Unsupported`. |
| Forced termination | **Unsupported** | No safe mechanism for arbitrary kernel code. |
| User-mode isolation | **Not implemented** | Kernel privilege is shared. |
| Separate address spaces | **Not implemented** | All contexts share the kernel page tables. |
| Kernel survival after application hangs | **Not established** | Native ELF still runs synchronously and can monopolize the CPU. |

## 10. Verification record

The following source-level suites passed in this turn:

- SECURITY1 identity: **43/43**.
- SECURITY2 authentication/session: **60/60**; this also exercises current SECURITY3B authorization decisions.
- SECURITY3C Native ELF policy: **PASS, zero failures**.
- SECURITY3D managed lifecycle: **45/45**.
- Native Settings runtime: **1,447/1,447**.
- Native Settings layout: **1,037/1,037**.
- Settings Users model/navigation/layout: **14/14, 8/8, 13/13**.
- App Model identity: **25/25**; file activation: **60/60**; folder activation: **26/26**; default handlers/persistence: **54/54**; application actions: **55/55**; URI/protocol: **41/41**.
- Native ABI layout, native present validation, native filesystem contract, and native debugger runtime: **PASS**.
- Hosted `build.bat`: **built successfully**.
- AMD64 freestanding kernel, normal profile: **built successfully**.
- AMD64 freestanding kernel with `GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE`: **built successfully**.
- QAC-managed UEFI guest: **booted to desktop**; serial self-test **PASS**; production service context **created and entered**; PIT heartbeats continued; QMP pointer movement and a double-click opened File Explorer. The rendered guest capture is `out/validation/security3e/qac-desktop-file-explorer-v6.png`; serial output is `out/validation/security3e/qac-serial-v6.log`; QEMU `guest_errors` log is empty.
- QAC reservation `77d67cf0fae046b3be6529d842345e4e` exited with code 0 and released; final QAC status: **0 managed, 0 foreign QEMU processes**.

Final guest build identity and SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| `ESP/kernel.elf` | `CE640AFD91FC4640D533A5B9DD24B9E16C87A8524130490DD449E8551AA6C7F2` |
| `ESP/EFI/BOOT/BOOTX64.EFI` | `0A1A23CA1773CA0599EB191B58A6E2D39C76FC4079849B6270876BE30CB18545` |
| QEMU `qemu-system-x86_64.exe` (11.0.0) | `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02` |
| OVMF `edk2-x86_64-code.fd` | `33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A` |
| QAC serial log | `9FA86CC64FAB2D66C09DB64E508C709A807156764A115A5B7C6932CBC8D6F312` |
| Desktop/File Explorer screenshot | `E8A1EF97685B0B8C30E51CA1061BCB64CD97879D791529DB5082633D3908DE40` |
| QEMU `guest_errors` log (empty) | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |

ESP build metadata: identity `GXOS-LARGE-FILE-PASTE-TRACE-V1`, probe `GXOS-LFPASTE-20260726-02`, built `2026-11-15T22:40:38.8418352Z`. The ESP kernel hash matches the source kernel hash. QEMU ran with `pc,accel=tcg`, 1024 MiB, std VGA, OVMF code as read-only pflash, and the isolated ESP directory as FAT media. The only QMP endpoint was loopback port 45903 during the QAC-owned run.

## 11. Next phase

Keep the desktop and kernel-system services single-owner while adding more kernel tasks only at explicit safe points. SECURITY3F should investigate a minimal Native ELF cooperative host path that retains the current SECURITY3C policy and SECURITY3D record without duplicating attribution. Preemption, forced termination, and user-mode isolation should remain separate projects with their own state-preservation and privilege-boundary designs.
