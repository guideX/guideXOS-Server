# Server v0.3 — S22R24 Native ELF Execution and Scheduler Audit

**Outcome C — `APPLICATION_ISOLATION_PREREQUISITE`.** The current AMD64 Native ELF path cannot keep the desktop responsive to an arbitrary CPU-bound application and cannot safely force-stop an application. The timer and context-switch code do not form an active kernel scheduler, and Native ELF code runs with kernel privilege in the kernel address space.

## Repository and baseline provenance

- Repository: `D:\dev\guideXOSServer`; branch: `main`.
- Starting HEAD: `9cf3deabc938f0fc4e60d2b4135e6950af697fb1`, matching the requested S22R23 handoff. At preflight, `main` was four commits ahead of `origin/main`; tracked files were clean. The only untracked path was the user file `assets/Backgrounds/troncity.png`; it was preserved unchanged.
- The S22R23 candidate was built from source commit `67dc2e111ea4d6e7208a8d5fac7dbd1a389d8491`, profile `ServerV03PublicPreviewPacManOnly`, build ID `4c9988cf-5d0d-414d-afe8-81f7351c2706`.
- Its security profile is `S22R18-PacMan-ReadOnly-Boot-Image-v1`.
- The candidate ISO is `E:\CodexArtifacts\S22R23\candidate-source\out\release-profile\4c9988cf-5d0d-414d-afe8-81f7351c2706\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso`, 90,245,120 bytes. Its adjacent `.sha256` receipt and candidate manifest both record SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`. A read-only `Get-FileHash` check of the ISO matched that full receipt value.
- The embedded kernel identity in the build receipt is 2,812,208 bytes, SHA-256 `9d2f17c33f157a566b6ee513196b5b74831bf8532699be78538b5979886fc166`. The manifest contains only the PacMan Native ELF package. This phase did not replace or modify the ISO.
- The accepted S22R22 persistent data image remains `E:\CodexArtifacts\S22R22\persistent-data\guidexos-user-data.img`, SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148` at preflight and after the hosted test. The FAT/VFS test wrote only a disposable copy.

The reviewed prior evidence remains consistent: S22R6 and S22R7 identify that an app stack and callback table do not create a privilege boundary; S22R8 selected a PacMan-only trusted-first-party shipset without claiming isolation; S22R18 enforces package/source integrity, not process protection; S22R21 and S22R22R1 qualify normal PacMan launch/return and persistence; S22R23 qualifies `/AppData` settings across cold boots. None qualifies arbitrary Native ELF recovery.

## Native ELF call path and ownership

The production desktop path is synchronous:

1. The desktop launch resolver selects a `NativeElfApp` and `try_launch_kernel_app()` calls `native_elf::launch()` in [desktop.cpp](../kernel/core/desktop.cpp#L9136).
2. `launch()` serializes launch attempts, checks the discovered package and, for the public-preview profile, rechecks the package source and manifest.
3. `run_package()` creates a stack-local `Runtime`, loads and verifies the ELF image/resources, allocates a separate **128 KiB app stack**, installs the host callback table, and calls `gxos_native_call_on_stack()` in [native_elf_baremetal.cpp](../kernel/core/native_elf_baremetal.cpp#L2627).
4. [native_elf_call.asm](../kernel/arch/amd64/native_elf_call.asm) saves the kernel caller's callee-saved registers and stack pointer, replaces `RSP` with the app stack, and directly calls the ELF entry point. It does not change CPL or CR3.
5. PacMan polls host events and presents frames. `host_poll_event()` and `host_present_frame()` call `pump_desktop()`, which polls input, routes mouse events, redraws the desktop, and pumps audio. This is cooperative work performed inside the app's synchronous call chain.
6. On a normal return, control reaches `run_package()` again. It stops audio owned by this runtime, closes and deletes its `NativeWindowOwner`, frees the app stack/image/verified image, clears `s_activeRuntime`, and clears the exception-return hook. Only then does `native_elf::launch()` and the desktop launch dispatcher return.

The app stack is separate storage, but `Runtime`, kernel globals, compositor/window data, VFS state, and kernel code remain in the same address space. The app entry is an ordinary function call at kernel privilege. The accepted SHA-256 allowlist restricts which image enters; it does not constrain what that code can access after entry.

### ELF loading and relocation

The bare-metal loader accepts the implemented AMD64 static ELF subset: ELF64, little-endian, `ET_EXEC`, AMD64, bounded program headers and load image, at least one `PT_LOAD`, and an entry point inside an executable load segment. It zeroes a contiguous kernel-heap image buffer, copies the load segments, and calls the rebased `e_entry`. It does not implement `PT_INTERP`, a dynamic linker, or general dynamic relocation records. For the static package, `rebase_static_absolute_words()` applies its constrained rebasing rules to supported absolute-address encodings and aligned data words; it is a loader operation, not an application protection boundary. The loaded image remains in the kernel address space with no per-app page tables or hardware-enforced per-segment permissions. The S22R7 report records an older entry-segment validation gap; current source has `entryInExecutableSegment` and rejects a layout when it is false.

Under the S22R18 public-preview profile, the source and manifest are verified against the approved read-only boot package and the approved PacMan identity is checked again immediately before entry. This closes package substitution within the qualified source contract. It does not change the loader's ring-0 call or shared-memory behavior after entry.

The Native ELF runtime has a launch-in-progress guard, active-runtime pointer, exit/fault flags, and a `NativeWindowOwner` state. It has no PID-backed lifecycle state machine or independently addressable task. The host `exit` callback records an exit request; it cannot unwind or stop the app if the app does not return. Kernel window ownership and audio owner IDs support ordinary cleanup, but there is no universal cancellation/reaper registry. Root `task_manager.cpp` and the root `scheduler.cpp` are hosted components; they are not the bare-metal Native ELF task manager.

## Interrupt, scheduling, and context-switch findings

- `kernel_main()` initializes the IDT/PIC, enables interrupts, registers the PIT handler, and runs one desktop loop that polls input, services bridges/network, ticks the desktop, pumps audio/presentation, and then halts. Native launch is reached from that loop's desktop dispatch; the loop does not continue until `native_elf::launch()` returns.
- The installed AMD64 PIC gates in [interrupts.cpp](../kernel/core/interrupts.cpp#L362) enter `irq_dispatch()`. `main.cpp` registers `pit::irq_handler()` directly. The PIT runs at 100 Hz, increments `s_ticks`, and prints a heartbeat every 100 ticks. That is timekeeping/diagnostics, not a task switch.
- A separate `kernel/arch/amd64/syscall.cpp::handle_interrupt()` helper calls `context::arch_timer_tick()`, but the active registered PIT path has no scheduler hook. `arch_timer_tick()` itself contains only TODOs for EOI and scheduler preemption in [context_switch.cpp](../kernel/arch/amd64/context_switch.cpp#L385). `arch_set_timer()` is also a stub.
- `kernel/core/process.cpp::schedule()` is a stub that halts the CPU. The AMD64 `sys_yield()` returns success without scheduling, and `sys_exit()` halts forever. Neither is used by the Native ELF host ABI.
- The AMD64 `SwitchContext` path saves callee-saved registers for a voluntary function-call switch. It does not preserve an arbitrary interrupt frame. `FullContext` save/restore is marked incomplete; restore does not perform a proper `iretq`. The new-thread entry/argument are held in global `s_pending_entry`/`s_pending_arg`; a returning thread halts; architecture cleanup does not own/reclaim a stack; FPU switching and TSS `RSP0` updates remain TODOs. No runnable queue or task lifecycle connects these primitives to the PIT.
- The existing `switch_context` inline assembly documents and reads System V argument registers (`RDI`/`RSI`). The AMD64 build uses `x86_64-w64-mingw32`; `g++ -Q --help=target` reports `-mabi=ms`, and no SysV override is set. The generated `context_switch.o` disassembly confirms the switch entry reads `[rdi]` and `[rsi+0x30]`, while a C++ caller passes its two arguments in `RCX`/`RDX`. Therefore the low-level voluntary switch entry is ABI-incompatible with the configured kernel build and cannot be safely activated without correction and a real context-switch regression.
- Root `scheduler.cpp` is a hosted `std::thread` worker queue. It is not part of the freestanding kernel's source set and cannot schedule the kernel desktop or a bare-metal Native ELF. The hosted Task Manager/ProcessTable has no termination handle for the stack-local bare-metal `Runtime`.
- Global interrupts are enabled during ordinary desktop execution, and the Native ELF trampoline does not disable them. Therefore a PIT IRQ can run while app code leaves interrupts enabled, but the IRQ returns to the interrupted app instruction. Since the app executes at CPL0, it can also execute privileged instructions such as `cli` and prevent timer/input IRQ service entirely.
- The 128 KiB Native ELF stack is dynamically allocated runtime storage. It is distinct from the established 16 KiB kernel stack contract. The exception stubs switch to a shared 16 KiB exception stack for C++ diagnostics, but push the initial exception frame onto the interrupted stack before switching. There is no per-task kernel stack or task-owned interrupt return frame.

The mere presence of PIT interrupts, `SwitchContext`, `arch_switch_to()`, or a hosted task manager does not establish preemption. The active freestanding launch and timer paths do not schedule another context.

## Failure classes

| Failure class | Current behavior and recovery boundary |
|---|---|
| Normal exit | PacMan handles Escape/Close through host event polling, returns, and reaches ordinary runtime cleanup. S22R21/S22R22R1/S22R23 guest evidence qualifies this normal path. |
| Stops processing input | If the app stops calling host callbacks, desktop input dispatch/redraw and the main loop are blocked; IRQs may only buffer device input while enabled. If the app continues callbacks but ignores events, the UI may be pumped, but the kernel has no request that can stop the app. |
| CPU-bound infinite loop | No active scheduler can preempt the call. Timer IRQs, when enabled, return to the same app instruction. App code can mask them. There is no in-kernel termination path. |
| Application deadlock | A cooperative `host_wait_for_close()` loop can poll and halt between checks, but it still depends on the app entering that callback. A user-mode deadlock inside the app blocks desktop dispatch; a stall inside a host callback may hold kernel state. No independent service scheduler is established by this path. |
| Invalid memory access/CPU exception | The AMD64 exception stub records a frame on a kernel exception stack. `gxos_native_exception_dispatch()` attempts a return through the trampoline when an active runtime exists and either RIP is in the loaded app image or the interrupted stack is in that runtime's 128 KiB app stack. Other exceptions are treated as kernel faults and halted. The recovery jump abandons the app call stack; if the fault interrupted a kernel callback after it acquired a lock or partially changed shared state, this path does not unwind that state. No controlled fault guest test was run in S22R24, so this is existing narrow code behavior, not a general safe-recovery qualification. |
| Kernel memory corruption | Not contained. Ring-0 app code can write kernel memory, other runtime data, or shared structures and can bypass callbacks. Continuing to schedule after such corruption would not prove safety. |

## Minimum survivability contract

| Requirement | Current status |
|---|---|
| Desktop/input continue while an app stops servicing events | **No guarantee.** The desktop is called synchronously from the app path and depends on host callbacks. |
| User can request termination independently of the app | **No.** Close/Escape is an event for the app to consume. |
| Kernel can stop the app without reboot | **No general mechanism.** Normal return and a narrow exception trampoline are the only observed exits. |
| App-owned windows/resources are released | **On normal return**, the one registered window owner and audio owner are cleaned. There is no general handle/lock/task ownership and cancellation registry. |
| Other services remain schedulable | **No.** No independent bare-metal task scheduler was found; the main loop is blocked. |
| Documents and settings remain intact | The accepted normal PacMan/data path is qualified. There is no isolation guarantee against privileged buggy/malicious code, so this is not guaranteed under arbitrary app behavior. |
| Another supported app launches afterward | **After normal return only.** No forced termination followed by relaunch is available or qualified. |

## Minimum implementation strategies

| Strategy | Achievable guarantee | Missing work / limitation |
|---|---|---|
| A — cooperative termination | A well-behaved app can poll a kernel-owned close/cancel request and return. PacMan already cooperates with Escape/Close. | Cannot regain control from an infinite loop or a callback-free app. It does not protect kernel memory. |
| B — preemptive managed native task | With real interrupt-frame scheduling, per-task stacks, and a runnable desktop context, the kernel could regain CPU time from an app that leaves interrupts enabled. This can improve responsiveness. | Requires an ABI-correct AMD64 switch entry, a real scheduler, asynchronous context save/restore, task/stack lifetime, deferred scheduling from IRQs, interrupt/lock rules, and bounded kernel critical sections. Ring-0 code can still mask interrupts and corrupt memory. Preemption alone does not make forced termination safe. |
| C — protected native execution | User-mode execution, per-app page tables, kernel-only pages, controlled traps/syscalls, and task-owned kernel stacks can contain ordinary app faults and block arbitrary kernel writes/privileged instructions. | Requires a small but real protection boundary, mediated ABI, verified load permissions, page-fault containment, a task scheduler, and ownership-aware lifecycle cleanup. It need not be a general POSIX process system. |

## Recommendation

The present implementation cannot meet the release owner's recovery requirement before v0.3 through a timer watchdog or a safe `KillTask` patch. A separate S22R25 execution-protection milestone is required before claiming that an unresponsive Native ELF cannot make the desktop/kernel unavailable. Implement a minimal protected execution boundary and lifecycle, rather than claiming reliable recovery from the current ring-0 call model.

Preserve the S22R18 PacMan-only integrity/source checks, S22R23 persistent Documents/AppData behavior, Model B public-preview direction, QEMU reference environment, deferred display decisions, and current publication/signing boundaries. No arbitrary feature gate or user-facing warning is indicated by this audit.
