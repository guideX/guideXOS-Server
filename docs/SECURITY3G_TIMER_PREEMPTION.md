# SECURITY3G — Timer Driven Preemption

**Project:** guideXOS Server

**Branch:** `SECURITY_SETTINGS`

**Starting source:** `f63be397b5bad9ea07d0d45feaa5ef55896d744f`
**Outcome:** `SECURITY3G_TIMER_PREEMPTION_PARTIAL`. The implementation, builds, and hosted regressions are complete. Guest qualification was not run because QAC kept the development request queued behind an unrelated acceptance reservation; the owned request was cancelled before launch. No guest preemption result is claimed.

## Scope and baseline

SECURITY3F added the bounded AMD64 scheduler and authentic scheduled Native ELF execution, but an application still had to yield. SECURITY3G extends that scheduler for one controlled Native ELF proof pilot. Guest qualification remains outstanding. It does not make all kernel or Native ELF execution preemptible and does not provide process isolation or forced termination.

The scheduler remains single CPU and round robin. It has eight context slots, one reserved for a trusted kernel task, seven slots for managed Native ELF work, 64 KiB stacks with guards, generation checked handles, and root stack reaping. The PIT runs at approximately 100 Hz, giving a 10 ms interrupt interval.

## Interrupt and context findings

- AMD64 IRQ0 enters the common IDT stub. Same privilege level entry pushes RIP, CS, and RFLAGS on the interrupted stack. The stub saves volatile registers plus its R12 stack anchor, aligns a temporary C stack, reserves Microsoft x64 shadow space for the kernel build, and restores the exact interrupted stack before `iretq`.
- `irq_dispatch` runs the registered PIT handler and acknowledges the PIC before asking the scheduler to consider a switch. The scheduler hook is called only for the outermost IRQ0. Nested interrupts, the scheduler transition window, kernel owned contexts, ineligible Native ELF contexts, and guarded application callbacks cannot switch.
- The timer path does not reinterpret the hardware frame as a cooperative frame. It saves a normal AMD64 call continuation for `gxos_context_switch_from_timer` on the interrupted Native ELF stack. The hardware `iretq` frame and IRQ stub saves stay below that continuation. When the application is selected again, the continuation returns through `irq_dispatch`, the stub restores the saved registers, and `iretq` restores the exact interrupted RIP, RSP, and RFLAGS.
- The runtime marks the application `Ready` and parks on the existing root scheduler stack. The existing round robin dispatcher can then run the trusted system I/O task and desktop pump before the application is selected again. Stack cleanup still occurs only after the context is completed and execution has returned to the root stack.
- A short transition flag and CLI windows protect scheduler state changes. The timer path enables interrupts only after transferring to the parked root scheduler. On re-entry to a timer-suspended context, interrupts remain disabled until its original `iretq` restores RFLAGS. This avoids resuming the interrupted IRQ chain with an altered IF state.
- The architecture is ring 0 with a shared address space and no per-context CR3, FS/GS base, or privilege stack. The pilot is controlled and trusted kernel-privileged code. This work does not make arbitrary or malicious Native ELF code safe to preempt.

## Preemption policy and safe boundaries

`Runtime::timerInterrupt` increments the timer counter, then requires all of the following before switching:

1. The IRQ is outermost and is the PIT IRQ.
2. The runtime is initialized and a scheduler managed task is active.
3. The running context is a `NativeElfApplication` context in `Running` state.
4. That context explicitly opted into preemption.
5. The bounded preemption-disable depth is zero.

If a timer arrives while an eligible application is inside a protected host callback, the runtime records a deferred request and returns to the callback. Leaving the outermost guard services the pending request through the root scheduler. Guards nest to a maximum depth of 32; underflow, overflow, and unsafe transitions return errors. The guard does not keep hardware IRQs globally disabled.

The guarded Native ELF callbacks protect lifecycle, window/compositor, file access, and other shared host state while they mutate or inspect it. The `host_file_read_all` loop is not held under one broad guard; each underlying read has its own narrow guard. Cooperative yield suspends and restores a callback guard around the explicit yield, preserving SECURITY3F behavior.

Eligibility is set only by the built in pilot package (`com.guidexos.schedulerpreemptionpilot`) and only while its measured interval is active. It is cleared at the end marker and at context creation. Legacy synchronous Native ELF and ordinary managed Native ELF applications remain cooperative.

## CPU state preservation

Both cooperative and timer context switches use the existing per-context, 64-byte aligned XSAVE areas and switch with the runtime XCR0 mask. AMD64 kernel C++ is built with SSE/MMX code generation disabled. Initialization requires CPUID XSAVE and supported x87/SSE state, enables AVX state when the CPU exposes it, checks the requested XCR0 mask against CPUID leaf `0xD`, and rejects an area smaller than 576 bytes or larger than 16 KiB. Unsupported configuration fails scheduler initialization cleanly; there is no incomplete FXSAVE fallback.

With QEMU `-cpu max`, the expected guest contract is XCR0 `0x7` and 832 bytes. The pilot saves and checks its stack local and checksum, XMM6, and—when CPUID and XGETBV confirm OS enabled AVX—the full YMM6 marker. It restores its own ABI nonvolatile XMM6 before returning. The actual CPU feature and state proof remains pending the guest run.

XCR0 is global to this single CPU. Context switching preserves every state component enabled in XCR0, subject to the 16 KiB bound. A Native ELF application is still ring 0 and could change privileged CPU state or corrupt kernel memory; the eligibility restriction is an execution policy, not a security boundary.

## Pilot and observability

The package `Apps/SchedulerPreemptionPilot` is launched through the normal desktop Native ELF launcher and scheduler. Its deterministic assembly workload runs 8,000,000 iterations between `SECURITY3G-NONYIELD-BEGIN` and `SECURITY3G-NONYIELD-END`. It makes no host calls and performs no cooperative yield during that interval. The runtime records timer interrupts, preemption decisions, actual timer driven switches, deferred requests, application resumptions, trusted kernel dispatches, desktop pumps, pointer callbacks, and yield count.

Pilot PASS requires at least three timer driven switches and resumptions, a trusted kernel task dispatch, desktop pumping, zero voluntary yields in the measured interval, correct final checksum and CPU state, normal application return, and released scheduler stack. Timer heartbeat counts alone cannot pass the check.

The opt in scheduler self-test also waits for a real PIT tick while a Native ELF test context is inside a nested guard. It requires a deferred request, safe service on guard exit, and successful resumption. It has a finite spin bound so a missing PIT does not hang boot indefinitely.

## Lifecycle, desktop, and service behavior

Preempted Native ELF work remains an existing scheduled managed execution with its original generation checked handle and stack. A timer switch makes the context ready; it does not complete or terminate the app. Normal return records the exit result, transitions the managed lifecycle, checks stack guards, and reaps the stack from the root scheduler. SECURITY3F's synchronous legacy path remains available and is not preemptible.

During a timer suspension, `drive_scheduled_native_elf` continues the regular desktop pump and dispatches the ready trusted network/system bridge context. The pilot records these advances while its own context is still active. QMP mouse and keyboard interaction will be tested during guest qualification.

## Build and regression evidence

Builds completed successfully from the implementation worktree:

| Build | Result |
|---|---|
| AMD64 normal freestanding kernel | PASS; SHA-256 `968F75D0931DCBFD432591A7F69788919570A14752B052D499C28A44EA766C09` |
| AMD64 public-preview security profile | PASS; SHA-256 `98A9C5E3C94575FEDA632E9F1AA10A5113A669E640148C787F189CEE182B8F27` |
| AMD64 guest proof with SECURITY3E/3F/3G scheduler checks | PASS; SHA-256 recorded below |
| Hosted `cmd /c build.bat` | PASS |
| UEFI x64 Release bootloader in isolated output directory | PASS |
| SECURITY3G Native ELF pilot | PASS; ELF64 x86-64 with `gx_main` entry |

Relevant hosted regressions completed:

- Identity foundation: 43/43.
- Authentication/session and authorization suite: 60/60.
- Managed execution lifecycle: 48/48.
- Native ABI layout: PASS.
- Native ELF app identity: 25/25.
- App Model default handlers: 54/54; application actions: 55/55; URI, folder, and file activation: PASS.
- Native ELF release policy: PASS.
- Native Settings runtime: 1,447/1,447; layout: 1,037/1,037.
- Native filesystem contract: PASS.
- Native present validation: PASS.
- Hosted VFS read: 4/4; kernel mount table integrity: 19/19.
- System service bridge: device 14/14, storage 15/15, combined 82/82.
- Native debugger runtime: one run failed at the Step Out breakpoint assertion; a subsequent standalone rerun passed. No debugger sources were changed.

The positive and negative guest images are prepared but were not launched. The guest scheduler self-test, SECURITY3F app, legacy synchronous app, SECURITY3G pilot, unsupported CPU result, and QMP interaction remain unqualified.

## Guest candidate and artifact identity

The candidate is assembled only under `out/validation/security3g/guest-candidate/ESP`. The repository root `ESP` was not modified. The guest uses a zero-byte scratch `ramdisk.img` because the inherited UEFI build script passes null bytes to `WriteAllBytes`; the isolated candidate keeps the required file and avoids changing unrelated release tooling. It contains the freshly built bootloader, current guest proof kernel, and 46 application files including the existing SECURITY3F scheduler pilot, legacy Hello World, and the new SECURITY3G pilot. Separate writable positive and negative QEMU ESP copies were prepared; neither was run.

| Artifact | SHA-256 |
|---|---|
| SECURITY3G guest proof kernel (flags recorded below) | `B36BF3948D877B3C41F8DF9AA59362E1DC0746B5E10D760267F0D318D44A6AAC` |
| UEFI bootloader | `6ED0A867646EAC29DAE3E3DA34DB3B4D50AC9DB2EF9919FD6C1725548472B781` |
| SchedulerPreemptionPilot ELF | `868521FC6C6F66EF5FE1BC321759CBC796A4E97C7C63EA29F3E818D1F71CD483` |
| QEMU 11.0.0 executable | `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02` |
| OVMF x64 code firmware | `33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A` |

Guest kernel flags: `-DGXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE -DGXOS_SECURITY3F_SCHEDULED_NATIVE_ELF_GUEST_PROOF -DGXOS_SECURITY3G_TIMER_PREEMPTION_GUEST_PROOF`. The implementation source commit is `4f8d7c48c7a386f47c85123aa0402fc44b35a9bb`, based on `f63be397b5bad9ea07d0d45feaa5ef55896d744f`. The out-of-tree artifact manifest records the source and binary hashes.

Kernel builds used MinGW-w64 GCC 15.2.0. The pilot was emitted as AMD64 ELF64 with Clang/LLD 22.1.8. The UEFI bootloader used the repository's x64 Release MSBuild project in an isolated output directory.

The planned QEMU configuration is `pc,accel=tcg,smp=1`, OVMF code firmware as a read only pflash device, 1024 MiB RAM, a FAT-backed scratch ESP, and a QMP endpoint. Positive CPU configuration is `-cpu max`; negative configuration is `-cpu qemu64`. QAC was online, with unrelated acceptance reservation `09730975edc945f1a9dfc7f4556ba97a` running. This work's development request `df1272707f8242f49639ccb1ea333a8f` waited at queue position 1 for over 15 minutes and was cancelled before launch. QAC confirmed the request as `Cancelled` and retained the other reservation; no guest QEMU process was started by this work.

## Capability matrix

| Capability | SECURITY3G status |
|---|---|
| Cooperative scheduler contexts | Implemented; SECURITY3F foundation retained |
| Scheduled Native ELF execution | Implemented; production launcher and managed lifecycle |
| Timer driven context switching | Implemented for eligible Native ELF; not guest qualified |
| Non-yielding workload preemption | Implemented in pilot; not guest qualified |
| Desktop and trusted service progress during preemption | Instrumented; not guest qualified |
| Deferred preemption | Implemented with bounded nesting and a real-PIT self-test; not guest qualified |
| Safe nested preemption guards | Implemented to depth 32; self-test is in guest proof build but was not run |
| Forced termination of a running Native ELF | Not implemented |
| User mode isolation | Not implemented; code remains ring 0 |
| Address-space isolation | Not implemented; contexts share the kernel address space |
| Recovery from malicious kernel-privileged Native ELF code | Not guaranteed |

## Qualification outcome and SECURITY3H

The result is `SECURITY3G_TIMER_PREEMPTION_PARTIAL` because QAC did not grant a guest slot. The source implements the bounded timer switch path and the guest-proof build succeeds, but the behavior, CPU-state proof, negative CPU path, and QMP desktop interaction remain unverified in a guest. Do not claim Outcome A from this report. Re-run the prepared positive and negative candidates under QAC to qualify the hardware interrupt path and confirm that `-cpu qemu64` rejects scheduler initialization without launching the pilot.

Recommended SECURITY3H scope: add multicore-safe per-CPU state and an APIC timer only after a clear SMP requirement; define safe cancellation and fault recovery for a suspended context; add user-mode execution and address-space isolation as a separate design; audit additional kernel critical paths before broadening eligibility; and measure latency/fairness under sustained load. Do not describe SECURITY3G preemption as process isolation.
