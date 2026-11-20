# SECURITY3G-R1 — Native Timer Preemption Guest Qualification

- **Project:** guideXOS Server
- **Branch:** `SECURITY_SETTINGS`
- **Starting HEAD:** `b9ac8943ab1fa88fb2f7b934ee09cb22ce8d9565`
- **Implementation commit:** `4f8d7c48c7a386f47c85123aa0402fc44b35a9bb`
- **Starting working tree:** clean
- **Outcome:** `SECURITY3G_R1_TIMER_PREEMPTION_PARTIAL`

## 1. Outcome

No guest was launched in this phase. The QAC status command returned exit code 2 with `Coordinator unavailable: Coordinator connection timed out or is unavailable.` The active reservation list, queue, and admission eligibility could not be established. No reservation request was made and no QEMU process was started. This is an infrastructure limitation; it is not evidence of a scheduler defect.

The source and staged positive/negative candidates were audited. The guest-proof kernel, UEFI loader, Native ELF pilot, QEMU executable, and OVMF code firmware match their recorded identities except for a malformed QEMU hash in the original SECURITY3G manifests. R1 independently recorded the correct QEMU hash without changing the original qualification material. The candidate manifest also does not specify an x64 OVMF variable-store image or path; that launch detail remains to be made explicit before a future guest run.

The implementation's timer path, context-switch continuation, xstate setup, pilot proof counters, and lifecycle cleanup were inspected in source. Static inspection did not establish guest behavior and did not identify a defect that could be repaired without guest evidence. No source files were changed. The pilot-only eligibility gate remains in place.

## 2. Baseline and artifacts

The repository was `D:\dev\guideXOSServer_SECURITY_SETTINGS`, branch `SECURITY_SETTINGS`, at the requested report commit. The SECURITY3G implementation commit is the expected `4f8d7c48…`; the kernel source at the starting HEAD matches that commit. The starting worktree was clean. The source and hosted verification record are in [SECURITY3G_TIMER_PREEMPTION.md](SECURITY3G_TIMER_PREEMPTION.md), with the SECURITY3E and SECURITY3F foundations in [SECURITY3E_NATIVE_SCHEDULER_FOUNDATION.md](SECURITY3E_NATIVE_SCHEDULER_FOUNDATION.md) and [SECURITY3F_SCHEDULED_NATIVE_ELF_EXECUTION.md](SECURITY3F_SCHEDULED_NATIVE_ELF_EXECUTION.md).

The guest candidate is assembled under `out/validation/security3g/guest-candidate/`; the root `ESP` and frozen release artifacts were not used or modified. Positive and negative runs have separate writable ESP copies. The canonical candidate ESP and both run copies each contain 52 files / 7,366,643 bytes and have the same R1 tree identity, `FDEB8FE78613B0DDD7A5695F07C0878756CFB0DF20B7CBA2F8ED1EB7F9877E79`. This digest is computed from the sorted relative path, size, and per-file SHA-256 records.

| Artifact | Independently verified SHA-256 | Identity |
|---|---|---|
| SECURITY3G guest-proof kernel | `B36BF3948D877B3C41F8DF9AA59362E1DC0746B5E10D760267F0D318D44A6AAC` | ELF64 AMD64, ET_EXEC, entry `0x100000`; matches the expected historical candidate |
| UEFI loader `BOOTX64.EFI` | `6ED0A867646EAC29DAE3E3DA34DB3B4D50AC9DB2EF9919FD6C1725548472B781` | PE/COFF AMD64 (`0x8664`); isolated x64 Release build |
| `SchedulerPreemptionPilot` | `868521FC6C6F66EF5FE1BC321759CBC796A4E97C7C63EA29F3E818D1F71CD483` | ELF64 AMD64, ET_EXEC, `gx_main`; manifest ID `com.guidexos.schedulerpreemptionpilot` |
| Normal AMD64 kernel | `968F75D0931DCBFD432591A7F69788919570A14752B052D499C28A44EA766C09` | Existing SECURITY3G build artifact |
| Public-preview AMD64 kernel | `98A9C5E3C94575FEDA632E9F1AA10A5113A669E640148C787F189CEE182B8F27` | Existing SECURITY3G build artifact |
| QEMU executable | `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02` | QEMU 11.0.0, `v11.0.0-12122-ga4bb4b10c9` |
| OVMF x64 code firmware | `33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A` | `C:\Program Files\qemu\share\edk2-x86_64-code.fd` |

The original candidate manifests contain `qemuExecutableSha256=A930E028F93D0FA47E4D3E129A2381B0084F81B944A5DDBC8F3AB07C779570`, which does not match the installed executable. The SECURITY3G report's historical QEMU hash matches the installed file. This R1 report uses the independently calculated value above; the original SECURITY3G files were preserved. Independently computed identities and tree hashes are retained in [candidate-artifact-identity.txt](../out/validation/security3g-r1/preflight/candidate-artifact-identity.txt).

`startup.nsh` launches `FS0:\EFI\BOOT\BOOTX64.EFI`. `desktop.cfg` is present and identical in all three ESP copies. The zero-byte `ramdisk.img` is the documented isolated workaround for the inherited staging script's null-byte write behavior. The SECURITY3G report records the intended QEMU configuration as `pc,accel=tcg,smp=1`, 1024 MiB RAM, read-only OVMF code pflash, a writable FAT-backed copy of the ESP, and a QMP endpoint. The positive CPU is `-cpu max`; the negative CPU is `-cpu qemu64`. These are planned settings; R1 did not execute or capture a QEMU command line. The staged manifest identifies the OVMF code image but does not identify an x64 variable-store image/path. No OVMF variable-store artifact is included in the candidate ESP. Record the exact variable/NVRAM configuration in the R1 run manifest before the next launch.

The guest-proof kernel is recorded as built with `-DGXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE`, `-DGXOS_SECURITY3F_SCHEDULED_NATIVE_ELF_GUEST_PROOF`, and `-DGXOS_SECURITY3G_TIMER_PREEMPTION_GUEST_PROOF`. The existing SECURITY3G plan expects QEMU `-cpu max` to expose XSAVE, x87/SSE/AVX state with XCR0 `0x7` and 832 enabled bytes. This is an expected contract, not R1 CPUID/XCR0 evidence. The scheduler's CPU checks require XSAVE and x87/SSE support, validate the CPUID leaf `0xD` state mask and size, and enable AVX state only when supported. PIT initialization configures IRQ0 at approximately 100 Hz. None of these runtime facts was observed in an R1 guest.

## 3. QAC admission and ownership

The documented QAC CLI is `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`; the QAC README documents `status`, `list`, and `queue` for preflight and gives exit code 2 for an unavailable coordinator. The recorded `qac status` preflight completed at 2026-11-19 19:16:16 Pacific (2026-11-20 03:16:16 UTC) with exit code 2 and the unavailable-coordinator message. The captured result is [qac-status.txt](../out/validation/security3g-r1/preflight/qac-status.txt).

Because the coordinator could not establish the current active reservation, queue, or admission state, R1 did not issue a request or start a guest. No R1 reservation ID or owner was created; no QEMU child, PID, QMP endpoint, or guest log exists. There is no R1 reservation to release. The coordinator did not provide a final global QEMU/process census, so global QAC ownership and process status remain unknown. The `qac-final-status.txt` under the original SECURITY3G directory records a historical 2026-11-17 snapshot and is not treated as current R1 state. No other reservation or QEMU process was controlled.

## 4. Implementation review and qualification evidence

The production path was reviewed in `kernel/core/interrupts.cpp`, `kernel/core/native_scheduler.cpp`, `kernel/arch/amd64/native_context_switch.asm`, `kernel/core/native_elf_baremetal.cpp`, and the Native ELF pilot metadata/binary.

- `irq_dispatch` runs the registered IRQ handler, acknowledges the PIC, then calls the scheduler timer hook for IRQ0 with an outermost-interrupt indication. The AMD64 stub saves volatile GPRs and its stack anchor, aligns the temporary C-call stack, restores the interrupted stack, and returns through `iretq`.
- The scheduler setup checks CPUID/XCR0 before enabling the xstate mask. Timer eligibility requires an active, running Native ELF context that explicitly opted into the pilot interval; nested IRQs, transition windows, trusted kernel contexts, and guarded callbacks do not switch. The timer path saves a normal call continuation on the interrupted application stack and switches to the parked root scheduler stack.
- The AMD64 switch assembly saves the nonvolatile GPR continuation and uses `XSAVE64`/`XRSTOR64` with the configured XCR0 mask. The intended resume path returns through the suspended IRQ dispatch/stub and `iretq`. These code paths have not been observed executing in an R1 guest.
- The SECURITY3G pilot is a bounded 8,000,000-iteration non-yielding workload in the existing report. It has no host calls during its measured section. The guest-proof kernel records timer interrupts, decisions, timer-driven switches, deferred requests, resumptions, trusted-task dispatches, desktop pumps/input callbacks, and yield deltas. Its PASS condition requires at least three timer switches and resumptions, a trusted dispatch, desktop pumping, zero measured voluntary yields, and normal completion. A heartbeat alone does not pass.
- The launcher reports the terminal scheduler state, entry stack validation, exit status, stack release, and lifecycle cleanup. Completed scheduler stacks are reaped from the root stack. This is source behavior only for R1; there was no launch or cleanup run.

No R1 serial output, debugger trace, screenshot, QMP event, QEMU process exit code, CPUID record, or XCR0 value exists. In particular, R1 has no evidence of a hardware PIT arriving during the measured workload, a timer-driven switch, alternate-context execution while the pilot remains incomplete, or resumption at the saved instruction state. The implementation audit found no specific repair justified by guest evidence; no source repair was made.

## 5. Positive and negative guest runs

| Run | Result | Evidence |
|---|---|---|
| Positive `-cpu max` boot and scheduler initialization | Not run | QAC coordinator unavailable before admission |
| SECURITY3E scheduler self-test and SECURITY3F cooperative pilot | Not run in R1 | Only inherited SECURITY3F qualification is available |
| SECURITY3G non-yielding Native ELF pilot | Not run | No QEMU child or serial log |
| Genuine PIT-driven scheduler switch and alternate-context work | Not run | No guest evidence; counters are implemented but not observed |
| Pilot resume, GPR/stack, XMM6/YMM6, checksum, and normal exit | Not run | No guest execution or final checksum |
| Lifecycle completion, resource release, and root-stack reaping | Not run in R1 | SECURITY3F cooperative lifecycle evidence does not qualify the timer-suspended path |
| Desktop and service progress during the pilot | Not run | No service counters, input events, QMP interaction, or screenshots |
| Negative `-cpu qemu64` safe behavior | Not run | No negative guest admission or launch |

No QMP input was sent. Interactive desktop responsiveness and actual system-service work are distinct from kernel scheduling progress; none was demonstrated in R1.

## 6. Builds and regressions

There was no source change, so R1 did not rebuild or rerun the SECURITY3G suite. Existing SECURITY3G build logs and regression results were inherited. The normal, public-preview, and guest-proof kernel files in `kernel/build/security3g-*` were rehashed and match their recorded values above. The guest candidate kernel, bootloader, and pilot hashes also match their historical manifest values. The hosted `build.bat`, UEFI bootloader build, pilot build, and test suites were not rerun.

The inherited pre-guest SECURITY3G results record: identity 43/43; authentication/session/authorization 60/60; managed lifecycle 48/48; Native Settings runtime 1,447/1,447 and layout 1,037/1,037; system-service bridge 82/82; plus Native ELF policy, ABI, App Model, filesystem, VFS, and present-validation passes. The native debugger had one Step Out assertion failure and passed on its standalone rerun. Those remain inherited results, not R1 reruns. The original SECURITY3G report also records normal, public-preview, and guest-proof builds, hosted `build.bat`, and isolated UEFI loader/pilot builds as passing.

## 7. Capability matrix

| Capability | R1 status | Supporting evidence / limit |
|---|---|---|
| Cooperative Native ELF scheduling | Partial | Inherited SECURITY3F guest qualification and SECURITY3G regression results; not rerun in R1 |
| PIT timer interrupt | Blocked | No R1 guest boot; source and PIT setup inspected only |
| Genuine timer-driven context switch | Blocked | No guest run or observed switch counter |
| Non-yielding application suspension | Blocked | Pilot artifact verified; not executed |
| Correct application resumption | Blocked | Timer continuation not guest-verified |
| GPR and stack preservation on timer path | Blocked | Assembly/source reviewed; no interrupted-frame guest evidence |
| XMM/YMM preservation on timer path | Blocked | Candidate expects XCR0 `0x7`; no R1 CPUID/XCR0 or pilot state result |
| System-service progress during preemption | Not tested | No guest dispatch/service evidence |
| Interactive desktop responsiveness | Not tested | No input, QMP, or rendered-change evidence |
| Lifecycle completion and stack reaping after timer suspension | Blocked | Cooperative SECURITY3F result is not a timer-path result |
| Unsupported CPU safe behavior | Blocked | Negative CPU guest not run |
| Arbitrary forced termination | Out of scope | Remains unsupported |
| User-mode isolation | Out of scope | Native ELF remains kernel privileged |
| Address-space isolation | Out of scope | Contexts share the kernel address space |

## 8. QEMU/QAC final state and next phase

R1 started no guest, so QEMU exit code, QMP shutdown, child-exit verification, and reservation release are not applicable. QAC returned unavailable during the current-state preflight; a final global status could not be verified. The R1 evidence directory contains the captured preflight and candidate identities. The original SECURITY3G qualification directory was preserved.

The recommendation is to keep preemption restricted to the existing pilot. Repeat SECURITY3G-R1 when QAC can return an authoritative state and grant explicit ownership. Before launch, record the complete QEMU argument vector and OVMF variable-store configuration alongside the corrected QEMU hash. Run the positive `-cpu max` guest first; only after exact ownership and clean shutdown/release, run the separate negative `-cpu qemu64` candidate under a fresh authorized reservation. Do not expand eligibility or claim timer preemption accepted until an authentic non-yielding pilot produces guest evidence for the switch, alternate-context work, correct resume, CPU-state/checksum validation, lifecycle/reaping, clean QEMU exit, negative CPU behavior, and QAC cleanup. SECURITY3H should begin only after those R1 acceptance conditions are complete.
