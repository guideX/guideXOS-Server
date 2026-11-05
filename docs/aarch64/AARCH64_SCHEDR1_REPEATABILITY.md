# AARCH64-SCHEDR1: Scheduler Repeatability and Stall Attribution

| Field | Value |
| --- | --- |
| Outcome | `ARM64_SCHEDULER_REPEATABILITY_VERIFIED` |
| Root-cause class | E — diagnostic/harness observation |
| Repository | `D:\dev\guideXOSServer_AARCH64` |
| Branch | `AARCH64_SUPPORT` |
| Starting source HEAD | `a0945ec01391bb845740e76edb1b83932c968f8a` |
| Starting worktree | clean |

## Result

The two reported Phase 4 stalls were not reproduced as scheduler, exception-return, IRQ, timer, or VFS defects. The 120-second guest runtime limit stopped a live guest before it reached the continuation and pass markers. An instrumented QAC run captured the guest during continued timer and scheduler activity at that deadline. Three subsequent clean, exclusive QAC runs of the preserved, non-instrumented Phase 4 kernel all completed the full acceptance workload.

The justified correction is to use a finite 300-second runtime bound in the Phase 4 runners. The three qualifying runs completed in about 151–155 seconds, while the historical successful run completed in 57.6 seconds. Under different host load, the old 120-second bound was too short. No scheduler, context ABI, interrupt-ordering, or VFS behavior was changed. The normal acceptance kernel SHA-256 remains identical to the preserved BOOTDTB3 kernel.

## Historical BOOTDTB3 runs

All three historical boots used the same UEFI, kernel, ramdisk, QEMU GICv2 profile, and BOOTDTB3 candidate manifest. They used fresh UEFI variable files and 120-second guest runtime bounds.

| Boot | QAC reservation | Result | QAC child runtime | Evidence directory |
| --- | --- | --- | ---: | --- |
| 1 | `ba493fbf08ff4b119b0de04c1aeb0dcd` | Phase 4 pass; 10,000 ticks and 10,000 preemptions | 57.6 s | `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-194441-502` |
| 2 | `a1aef4d739ca4eb99f49a346ad567bb6` | Runtime deadline; no pass marker | 120.8 s | `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-194744-664` |
| 3 | `a806585f44f047629f67bd3d939d9090` | Runtime deadline; no pass marker | 120.7 s | `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-195049-201` |

The last common *printed* operation was `[VFS] Opened: /phase4/hello.txt`. That line is not the CPU stop point. It is emitted by `kernel::vfs::open()` after the FAT driver returns a file handle, but before the VFS handle table is populated and before `open()` returns. The scheduler/VFS worker does not print again until the continuation checks finish, so the quiet serial interval hid ongoing work.

The original timeout logs contain no live scheduler counters. Therefore, the exact in-flight state of historical boots 2 and 3 cannot be recovered from those logs. The matching instrumented replay below establishes that the same observed boundary can occur while the guest is still progressing; the three fresh full-runtime runs then confirm that the preserved kernel reaches the pass marker when allowed to run longer.

## Continuation and exception-return contract

The Phase 4 bootstrap sequence in `kernel/arch/arm64/phase4_main.cpp` configures and starts the architectural timer, enables IRQs, then calls `kernel::scheduler::start()`.

1. `kernel::scheduler::start()` selects an enabled runnable task, records the context switch, and calls `context_switch(&s_bootstrap_context, next->context)`. This suspends the bootstrap call.
2. An IRQ enters the current-EL vector in `kernel/arch/arm64/phase3_entry.S`. The vector saves the interrupted GPRs into its exception frame and calls `phase3_exception_dispatch()`.
3. `kernel/arch/arm64/phase3_exceptions.cpp` reads and validates the GIC acknowledgement, dispatches the registered timer IRQ, then writes the matching EOI before returning the selected context.
4. The scheduler saves the interrupted task's frame and normally returns the next task's prepared interrupt context. The vector restore path selects that task's stack, restores ELR_EL1/SPSR_EL1 and GPRs, and executes `eret`.
5. At the 10,000-preemption target, `kernel::scheduler::timer_interrupt()` marks preemptive completion and selects the prepared completion-task interrupt context. The completion task returns to the saved bootstrap context using `kernel::scheduler::return_to_bootstrap()`.
6. Only after that context switch restores the suspended `scheduler::start()` call can `phase3_main()` validate completion, counters, VFS progress, and stack integrity, then print `exception return: PASS scheduler continuation resumed` and `AARCH64_PHASE4_PASS`.

This is a normal scheduler continuation after a task-to-bootstrap context switch. It is not a direct exception return into the bootstrap stack. No task-context ABI or assembly restore code was modified.

## Diagnostic instrumentation and captured state

The bounded diagnostic build adds a fixed 216-byte record behind `GXOS_AARCH64_SCHEDR1_DIAGNOSTICS`. It records scheduler/IRQ/VFS checkpoints, current and selected task state, timer and scheduler counters, acknowledgement/EOI counts, and saved exception context values. The IRQ path emits no serial output. The normal acceptance build leaves this macro disabled, and its kernel hash is byte-for-byte the preserved baseline.

Two fresh diagnostic runs used the established BOOTDTB3 QAC acceptance project and the GICv2 QEMU profile. Both reached the 120-second runtime deadline while the QAC-owned child was running; QAC verified child exit and reservation release. The first run (`1d027ca0e78941f2867a91d8d4e7e533`) captured CPU registers, but its initial `pmemsave` path formatting did not produce the diagnostic record. The runner was corrected to normalize the path and specify the record length in hexadecimal. The follow-up run (`37e3c49b6fd2442a9e9e8664ade84079`) captured and decoded the complete record at:

`out/aarch64-schedr1-diagnostic-capture-20261105/bootdtb2-guest-20261105-083522-158/`

The record was stable (`sequence=19952`, consistent) and showed:

| Measure | Captured value | Interpretation |
| --- | ---: | --- |
| Diagnostic stage | `IRQ_EOI_DONE` | Guest was in repeated timer-dispatch activity. |
| IRQ entries / valid acknowledgements / EOIs | 9,976 / 9,975 / 9,975 | One IRQ entry was in flight when QMP paused the guest. |
| Invalid acknowledgements | 0 | No spurious or malformed GIC acknowledgement was seen. |
| Last IRQ / acknowledgement | PPI 30 / `0x1e` | Architectural physical timer interrupt. |
| Timer callbacks / preemptions | 9,975 / 9,975 | Continued interrupt and scheduler progress at the 120-second deadline. |
| Context switches | 19,980 | Task rotation was continuing. |
| Current/interrupted task | ID 1, runnable | The interrupted task remained schedulable. |
| Runnable tasks / task count | 5 / 5 | Runnable workers, VFS worker, and completion task remained present. |
| Selected destination | valid; SP `0x43243fd0`, PC `0x400069d8` | PC resolves to `phase3_preemptive_step`; saved destination context was valid. |
| Destination PSTATE | `0x60000005` | EL1h with interrupt masks clear for resumed work. |
| VFS stage / handle | `FS_OPEN_RETURNED` / 0 | Open completed, handle 0 was published and returned. |
| VFS reads / enumerations / failures | 3,540,561 / 221,285 / 0 | The VFS workload had continued substantially beyond the open log. |
| Unexpected IRQs / exceptions | 0 / 0 | No unhandled IRQ or exception explained the delay. |

The paused QEMU register snapshot had `PC=0x40004898`, within `phase2_gic_acknowledge()` (symbol range `0x4000484c`–`0x400048a4`), with `X0=0x1e` and the expected masked IRQ-handler PSTATE. This is an in-flight timer acknowledgement, consistent with the one-entry difference in the record. It is not evidence of a stopped CPU or a lost EOI. QEMU's HMP `info irq` returned no data and `info timers` is unsupported in this build; the guest record supplies the timer and GIC evidence.

## VFS and scheduler interaction

The log inside `vfs::open()` occurs after `fs_fat::open_file()` succeeds. The VFS then initializes the handle entry (`open`, mount, flags, position, size, filesystem handle, and path) and returns the handle. The instrumented stage `FS_OPEN_RETURNED` and handle `0` prove the caller received that handle.

The VFS worker then repeatedly seeks, reads the fixture and EOF, verifies the contents, and enumerates the directory every 16 reads before yielding through `note_execution()`. The captured millions of successful reads/enumerations show that this work was progressing. At the snapshot, interrupted task ID 1 was one of the preemptive workers, not the VFS worker (ID 4). The base Phase 4 workload has one VFS worker; its other worker tasks and the completion task do not access VFS. The audited Phase 4 VFS path contains no spinlock, mutex, interrupt-disable, or preemption-disable section that could remain held across this yield/IRQ path. The VFS globals are shared, but the exercised base workload has a single VFS caller.

The diagnostic destination frame was valid and pointed to a runnable worker. The GIC handler completed 9,975 acknowledgement/EOI pairs with zero invalid acknowledgements; the final entry was paused in the acknowledgement routine. The exception counter remained zero. These observations do not support a VFS lifetime defect, a blocked task, a bad runnable selection, or an exception-return fault.

## Fresh native repeatability boots

The acceptance candidate reused the preserved BOOTDTB3 kernel and UEFI artifacts, with no Studio payload. It used a fresh UEFI variable copy for every run, QEMU `virt,gic-version=2,acpi=off`, Cortex-A53, 512 MiB, a 1,800-second finite QAC admission window, and a 300-second finite runtime bound.

The first fresh run (`out/aarch64-schedr1-acceptance-20261105/bootdtb2-guest-20261105-085652-115`, reservation `0d53da2c49f049078ff545b1e247323e`) passed but QAC reported that exclusive conditions were degraded because it briefly detected one unmanaged QEMU process. The process identity was not retained in the runner log; after the owned child exited, QAC reported zero foreign QEMU processes and no managed sessions. To preserve the exclusive-run requirement, that pass is retained as extra evidence and excluded from the three-run count.

| Qualifying run | QAC reservation | Evidence directory | Child runtime | Ticks | Preemptions | Context switches | VFS reads / enumerations | Unexpected IRQs / exceptions |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | `c7fb9a560eda4cb7a38995332cdf8af5` | `bootdtb2-guest-20261105-090025-881` | 150.7 s | 10,000 | 10,000 | 20,006 | 4,768,813 / 298,050 | 0 / 0 |
| 2 | `6a6968ab8bda4d7b8305dcfb4e0bdd61` | `bootdtb2-guest-20261105-090339-208` | 153.4 s | 10,000 | 10,000 | 20,006 | 4,775,625 / 298,476 | 0 / 0 |
| 3 | `986c710dd9a2419691b44b3df0bcfa95` | `bootdtb2-guest-20261105-090717-043` | 154.7 s | 10,001 | 10,000 | 20,006 | 4,809,968 / 300,622 | 0 / 0 |

All three logs independently contain valid DTB handoff/header and parser success, `GIC: OK version=2`, physical timer programming at 62.5 MHz with PPI 30, `exception return: PASS scheduler continuation resumed`, `scheduler/VFS integration: PASS`, and `AARCH64_PHASE4_PASS`. Each QAC log records an exclusive grant, the managed QEMU child launch and exit with code 0, and reservation release, with no degradation warning. Each session record has `timer_enabled=true`, at least 10,000 timer ticks, 10,000 preemptions, 20,006 switches, zero unexpected IRQs/exceptions, and the same candidate-manifest SHA-256.

The normal candidate does not add raw GIC acknowledgement/EOI counters to its serial format. Its three successful 10,000-tick runs exercised the existing GICv2 acknowledge → registered timer handler/scheduler → EOI path without unexpected IRQs or exceptions. The diagnostic replay separately measured the GIC acknowledgement/EOI path directly at the timeout boundary.

## Candidate identities and preserved evidence

**Normal acceptance candidate:** `out/aarch64-schedr1-acceptance-20261105/`

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `BOOTAA64.EFI` | 12,800 | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| `kernel.elf` | 210,840 | `fb0908e8511369284b440c054a946560d76b149903d57adb5c2590b75f006f3a` |
| `esp/ramdisk.img` | 67,108,864 | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |
| `candidate-manifest.json` | — | `ec73fc2ecfe0b7931cb20fb70e896b08174bc12c5ef902252b8aa41988d0eb3b` |

The kernel hash matches the preserved BOOTDTB3 baseline. The captured firmware-source DTB remains unchanged at SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`.

**Instrumented diagnostic candidate:** `out/aarch64-schedr1-diagnostic-capture-20261105/`

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `BOOTAA64.EFI` | 12,800 | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| `kernel.elf` | 211,168 | `b18a6f3f6759b8d1e6e027677748c3ee1ed7b7768132340f4d57c4ba6c60520c` |
| `esp/ramdisk.img` | 67,108,864 | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |
| `candidate-manifest.json` | — | `30f55a3509d3bd961837fb3f9b1234756d0a096305f3d2101cc6b2095d6a517b` |

The historical runs, both diagnostic runs, and every fresh acceptance run retain independent guest logs, QAC logs, and `session.json` files beneath their respective `out/` directories. QAC reported online with zero managed sessions, queued reservations, blocked reservations, or foreign QEMU processes after acceptance cleanup.

## Hosted regression and build evidence

- Phase 2 parser and GICv2 hosted controls: 51 assertions passed.
- Captured firmware-DTB parser regression: 52 assertions passed against the preserved fixture.
- Phase 3 scheduler hosted contract: `AARCH64_PHASE3_HOST_CONTRACT_PASS`.
- Phase 4 memory/VFS and allocator negative controls: passed in the normal freestanding candidate build.
- SCHEDR1 scheduler host simulation: `AARCH64_SCHEDR1_HOST_PASS`; it exercises production scheduler interfaces with simulated IRQ/context-switch callbacks and explicitly does not claim native EL1 exception execution. It covers initial scheduler continuation, synthetic VFS-stage preemption, wake/block transitions, completion handoff, teardown/stale-wake rejection, and repeated initialization.
- GICv3 hosted suite: 62 assertions passed.
- Studio v0.5 ABI2 adapter suite: 104/104 checks passed.
- Phase 10 and Phase 11 freestanding build/control suites: passed. The Phase 11 run used `-SkipDeveloperStudio`; no Studio guest was started.
- Phase 12 SDK alignment: both pinned SDK layouts, AArch64 compile probes, incompatibility negative control, and 9/9 v0.5 document-activation checks passed. The Phase 12 build and pinned Studio AArch64 compile/link/package controls passed; no Phase 12 guest was started.
- The standalone `test-aarch64-phase12-sdk-alignment.ps1` printed PASS for all checks but returned process code 1 because its expected failing negative compiler probe remained in PowerShell's `$LASTEXITCODE`. The integrated Phase 12 build reran the alignment preflight and completed successfully with exit code 0.
- The normal Phase 4 acceptance build completed freestanding ARM64 compilation and linking with the diagnostic macro disabled, producing the preserved `kernel.elf` hash.

## Remaining qualification

SCHEDR1 Phase 4 repeatability is complete for the authorized QEMU GICv2 profile. GICv3 guest qualification, Developer Studio guest/runtime qualification, and physical hardware qualification remain separate acceptance work. No GICv3 guest, Developer Studio guest, tablet, or Raspberry Pi was used in this milestone.

The work is committed locally on `AARCH64_SUPPORT`; nothing was pushed.
