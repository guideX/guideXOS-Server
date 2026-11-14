# AArch64 RUNSAFE3-B Native Scheduled EL0 Qualification

- **Outcome:** `SCHEDULED_EL0_NATIVE_REPEATABILITY_VERIFIED` (Outcome A).
- **Qualification date:** 2026-11-14.
- **Repository / branch:** `guideXOSServer_AARCH64` / `AARCH64_SUPPORT`.
- **Starting revision:** `ecd6a143088aee48e0009574ab7c18436825005c`, equal to `origin/AARCH64_SUPPORT` at start; clean worktree.
- **Frozen candidate source:** `a4125ff8017277430e077296eaf6cc6b138eb9e5`; clean worktree at build and every QAC admission.
- **Scope:** native scheduled execution of four internal integer-only EL0 qualification payloads under the existing QAC-managed AArch64 QEMU path. This is not Developer Studio Run qualification or physical-board qualification.

The candidate source is six local commits above the starting revision. This report is committed separately after the candidate was frozen and the three boots completed. No reset, rebase, branch change, or push was performed.

The six code commits, in order, are `fe9dfc89` (scheduled lifecycle and QAC diagnostic path), `6417c896` (readiness before the first task), `377dab77` (finite payload duration and terminal diagnostics), `ef11c7c9`, `b39808f6`, and `a4125ff8` (reliable source-revision identity in the freestanding build).

## Candidate identity

Candidate output is preserved at `out/aarch64-runsafe3b-candidate-v6-20261114/`. The candidate manifest records GICv2 `virt,gic-version=2,acpi=off`, Cortex-A53, AArch64 UEFI, source revision `a4125ff…`, and a clean source tree. All three qualifying boots used this same candidate directory and manifest.

| Artifact | SHA-256 |
| --- | --- |
| Candidate manifest | `6ee288a6462d0520ae5b47af757c65de7bdb6181ea42cedcca1d93c3635872d1` |
| Kernel ELF | `ce40b3e6d8c01e23f449751becf01a0c419511005583850357a431b05fc19890` |
| EFI loader | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| Ramdisk | `a18430a5e563a2b658a4aa596ce10d5f768a1dd167acd8038943eecc8a007c74` |
| Studio package manifest | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| Pinned Studio ELF (packaged, not executed) | `8e4f74feebe4b3d3f245f31560584b565fdba5743be3a61f0b83b4725dbde679` |

The internally constructed payload hashes are:

| Payload | SHA-256 |
| --- | --- |
| Existing trusted SVC exit-42 control | `9adb67ab60e0329266473bb0c419ff4167df6fb77c59fec2aff1de57421dd442` |
| FINITE scheduled fixture | `ac183615bdb72fe0e55dc0c21f880e080e4e72542b981eaeb5794eafa7c6bfa5` |
| TIMEOUT non-cooperative fixture | `5e55acf39da5fa4a8dbbfd53d0a339e87a5f25b442bc0f8d0516d34e7f025a7c` |
| CANCEL non-cooperative fixture | `41642cfece0226d7ee1a7b181d2f113cd7950af482fdedf66e09fedfdf7b4236` |
| FAULT protected-memory fixture | `1b7bd3ae1fb5cbf918ef782996048cc8c8e3f090ddb0a7ebcd1cb64f10f6e897` |

The candidate manifest also records QEMU SHA-256 `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748`, EDK2 code firmware SHA-256 `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d`, and the fresh-vars template SHA-256 `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f`. Each session used a fresh copy of the vars template.

## Native execution path

The runtime uses the common Phase 3 scheduler and existing RUNSAFE2 address-space machinery:

1. The current EL1 kernel task owns creation and control. A fixture is checked against the bounded integer-only validator, receives a unique execution identity, private root, one RX code page, one RW/XN stack page, guard gaps, and a separate 64 KiB EL1 task/exception stack. The child root retains shared kernel mappings with their EL1-only permissions; no child MMIO mapping is added.
2. `start` prepares the task's interrupt context and moves it from `Created` to `Ready`. The scheduler selects the task, validates owner and address-space identity, activates its root, installs the initial `SP_EL0`, `ELR_EL1`, and `SPSR_EL1`, restores x0-x30, and executes `eret` to EL0.
3. The physical timer is routed through the registered Phase 4 IRQ handler. On a lower-EL timer interrupt, the handler acknowledges and rearms the timer, samples `CNTPCT_EL0`, saves x0-x30 plus `ELR_EL1`, `SPSR_EL1`, and `SP_EL0` in the child context, and restores the kernel TTBR0 root before common scheduling selects a kernel task or another child.
4. A resumed child has its identity and root checked again. The exception-return path installs its saved kernel stack, user stack, PC, PSTATE, and general registers before `eret`. This preserves the FINITE loop's x9 countdown and condition flags through real preemption and resumption.
5. SVC exit, timeout, cancellation, and data abort each produce a distinct terminal result. The kernel root is active before cleanup. User pages and private page tables are released once; only after the task is inactive and cleanup succeeds does `reap` release the EL1 task stack and reclaim its scheduler record.

The initial context uses EL0t PSTATE `0x340`. The supported image profile is integer-only; the kernel uses `-mgeneral-regs-only`, and unsupported SIMD/FPU or privileged instructions remain rejected. No SIMD/FPU preservation is claimed. The scheduler contract remains single-core; no SMP synchronization guarantee is claimed.

## Timer and budget accounting

The native guest reported `CNTFRQ_EL0=62,500,000 Hz`; Phase 4 programmed the physical timer for 100 microseconds and PPI 30. The programmed interval is therefore 6,250 counter ticks. The runtime accepts budgets from 1 ms through 60 s (default 500 ms), converts microseconds to counter ticks using checked arithmetic, and rounds a fractional tick up.

The scheduler stores `CNTPCT_EL0` when an EL0 child starts or resumes and charges the unsigned counter delta at the next child timer interruption or final synchronous return. Time while another task is selected is not charged to the child. The sample includes the short EL1 exception-entry/dispatch work before the counter read. Therefore `scheduled_ticks` is **scheduled-residency elapsed counter time**: it is not wall-clock lifetime and not a precise count of CPU cycles. Per-child `timer_irqs` counts timer interrupts observed while that child was running; continuation `timer_delta` is the separate global scheduler timer count. Unsigned subtraction handles a short counter wrap, and accounting uses saturating addition.

The three 2 ms timeout runs terminated inside the guest without a child SVC. Their actual charged counter deltas were 1,006,181, 981,294, and 242,831 ticks (about 16.099 ms, 15.701 ms, and 3.885 ms at the observed frequency), against a requested 125,000 ticks. The child timer-interrupt counts were 1, 1, and 3. These records prove guest-side budget detection and finite termination, not a strict real-time latency bound: the measured delay exceeded the nominal 100 microsecond timer interval, and these emulated boots do not establish a worst-case response ceiling.

## Three qualifying QAC boots

Every session obtained an exclusive QAC acceptance grant before QEMU launch, associated its QMP endpoint with the exact QAC-managed child, used unique logs and firmware variables, and ended with an acknowledged QMP quit, verified QEMU and QAC exit code 0, released reservation, and final ownership reconciliation. QAC status after each run showed zero QEMU processes, managed sessions, and reservations. These are the only runs counted toward the three-boot acceptance.

| Boot | QAC session ID | Reservation | QEMU child PID | Guest / managed lifetime | Child cleanup / release | Outcome |
| --- | --- | --- | ---: | ---: | ---: | --- |
| 1 | `runsafe3b-20261114-142957-456` | `f9f07067f83f44b59d9fc0d73195b386` | 22716 | 27.253 / 29.347 s | 3.935 / 0.942 s | `QAC_BOUNDED_LIFECYCLE_VERIFIED` |
| 2 | `runsafe3b-20261114-143121-042` | `ea0ff57605bc499994946f1733de2ff9` | 27608 | 17.596 / 18.952 s | 3.125 / 0.805 s | `QAC_BOUNDED_LIFECYCLE_VERIFIED` |
| 3 | `runsafe3b-20261114-143226-702` | `878c8296a69b4f6c9a2777a297bd710d` | 24996 | 12.032 / 14.434 s | 4.788 / 1.168 s | `QAC_BOUNDED_LIFECYCLE_VERIFIED` |

The corresponding QAC CLI PIDs / QMP ports were `26892 / 53833`, `24308 / 53871`, and `17432 / 53896`. Each QAC record reports endpoint association with the exact child PID in the table and `qmp_quit_response_kind=Acknowledged`.

All three logs contain the native positive SVC exit-42 control and the two RUNSAFE2 permission-denial controls before the scheduled fixtures. The kernel-data and GIC Distributor reads produced lower-EL permission faults (`ESR=0x9200000f`); the GICD probe targeted `0x08000000`. Each negative record reports restored kernel root and cleanup.

### Per-child guest evidence

The IDs below are the actual guest log values. Acceptance checks validate relationships and state rather than requiring those particular numeric IDs. The child owner was task 5 in these runs; every child had two owned user pages, four private page-table pages, a 64 KiB kernel stack separate from its user stack, and a distinct address space.

| Boot | FINITE: scheduled ticks / budget; preemptions / timer IRQs / entries / resumes; kernel work delta |
| --- | --- |
| 1 | `8,624,107 / 312,500,000; 8 / 8 / 1 / 8; 4,140,872` |
| 2 | `8,156,061 / 312,500,000; 8 / 8 / 1 / 8; 4,303,151` |
| 3 | `8,973,886 / 312,500,000; 97 / 97 / 1 / 97; 4,097,325` |

Each FINITE child reached `Completed`, issued SVC with its actual x0 result 42, showed at least one lower-EL timer preemption and resume, made kernel work progress, restored the kernel root, released 2 user pages and 4 page-table pages, and was reaped with its task record reclaimed.

| Boot | TIMEOUT: requested / consumed counter ticks; child timer IRQs; kernel work delta |
| --- | --- |
| 1 | `125,000 / 1,006,181; 1; 348,165` |
| 2 | `125,000 / 981,294; 1; 469,270` |
| 3 | `125,000 / 242,831; 3; 141,634` |

Each TIMEOUT record shows `state=timed_out`, `result=none`, `no_svc=1`, one actual child entry, nonzero saved x9 from the non-cooperative loop, cleanup, root restoration, and task reap. Other kernel work advanced while the child ran. The host-side QAC runtime limit did not terminate these children; the guest emitted all scenario results before graceful QMP shutdown.

| Boot | CANCEL: scheduled ticks; owner request / child timer IRQs / entries / resumes; kernel work delta |
| --- | --- |
| 1 | `1,000,400; owner 5 / 1 / 1 / 0; 412,128` |
| 2 | `1,010,993; owner 5 / 1 / 1 / 0; 506,317` |
| 3 | `249,726; owner 5 / 3 / 1 / 2; 177,031` |

Before requesting cancellation, the independent owner observed the child `Preempted` after a real timer IRQ and verified its payload had set x0 to `0x43`. The non-cooperative payload did not issue SVC. Every record ends `Cancelled`, `result=none`, with cleanup, root restoration, and task reap.

| Boot | FAULT: child state and syndrome | Kernel work delta |
| --- | --- | ---: |
| 1 | `Faulted`, data abort; ESR `0x9200000f`, FAR `0x40000000`, ELR `0x4000000004`, SPSR `0x340` | 445,148 |
| 2 | `Faulted`, data abort; ESR `0x9200000f`, FAR `0x40000000`, ELR `0x4000000004`, SPSR `0x340` | 529,586 |
| 3 | `Faulted`, data abort; ESR `0x9200000f`, FAR `0x40000000`, ELR `0x4000000004`, SPSR `0x340` | 119,434 |

`ESR=0x9200000f` identifies a lower-EL data abort with a level-3 permission fault and read access (`WnR=0`). The guest recorded no fabricated exit value. Each child fault was attributed to its own identity, the kernel translation root was restored, 2 data pages and 4 page-table pages were released, and the scheduler task was reaped.

After all four scenarios each boot independently showed continuing scheduler activity:

| Boot | Global timer delta | Context-switch delta | Kernel work delta | Unexpected IRQs / exceptions |
| --- | ---: | ---: | ---: | ---: |
| 1 | 7 | 7 | 498,612 | 0 / 0 |
| 2 | 7 | 7 | 489,523 | 0 / 0 |
| 3 | 7 | 7 | 29,087 | 0 / 0 |

The final guest markers identified source `a4125ff8017277430e077296eaf6cc6b138eb9e5` and reported `finite_preempts=8`, `8`, and `97`, respectively, with `timeout_enforced=1`, `cancellation_forced=1`, `fault_isolated=1`, and `cleanup=1`.

## QAC evidence and preserved artifacts

Full QAC `session.json`, serial guest log, QAC stdout/stderr, and per-boot EDK2 vars copies are preserved under:

- `out/aarch64-runsafe3b-native-v6-20261114/runsafe3b-20261114-142957-456/`
- `out/aarch64-runsafe3b-native-v6-run2-20261114/runsafe3b-20261114-143121-042/`
- `out/aarch64-runsafe3b-native-v6-run3-20261114/runsafe3b-20261114-143226-702/`

Guest log SHA-256 values are `cfa18ce51eec8283b2832cc246f26910f52fe789b4f927ffcb20c38a2b5f9d1e`, `7ef348100be769189a2c59d0b1bf4bc0d64ba9b0bc3ba0fc78ce7c75fb72e3e8`, and `b78f73288caa70feca0de5f8b12d904592d374a582dd4957dd8d77f6e1e54035`, in boot order. Each session record includes the exclusive grant, QAC/child PIDs and start times, QMP association, candidate hashes, guest evidence, shutdown acknowledgement, exits, release deadlines, and final QAC status.

## Non-qualifying diagnostic attempts and repairs

Two earlier QAC boots are preserved but do not count toward repeatability:

| Session | Candidate source | Earliest observed failure | Result |
| --- | --- | --- | --- |
| `runsafe3b-20261114-140936-148` | `fe9dfc89b8fe95aa29076f4f43d7e8d59fe963a6` | Boot reported `[guideXOS] scheduled EL0 runtime: unavailable` before creating a scheduled child. Readiness incorrectly required a nonempty scheduler task count during kernel initialization. | QAC supervised and released the child/reservation; not a scheduled-child test. |
| `runsafe3b-20261114-141449-098` | `6417c89691b20b26089b7eab20012b6fc77140c8` | RUNSAFE2 controls passed and FINITE reached `CHILD READY`, then the generic acceptance error appeared before a terminal result. This candidate did not preserve enough counters to establish which FINITE assertion failed. | QAC supervised and released the child/reservation; later runs use a longer fixture and report failed terminal diagnostics. |

Those diagnostic sessions had QAC CLI PID / QMP port / reservation / QEMU child PID `22688 / 53755 / c1a8330127984734be0e2d433e88afca / 392` and `3904 / 60720 / c53e264506034f76b49a1113971c645b / 11848`, respectively. Both final QAC records show the managed child exited and the reservation was released.

The readiness check was corrected to test initialized EL0 runtime services rather than task count, with a host regression for readiness before the first task exists. The FINITE payload was lengthened to cross real timer slices and preserve its countdown/condition flags across preemption. Failure diagnostics now retain and print task state, result, counter accounting, preemptions, timer IRQs, register snapshots, ESR/ELR/FAR, root restoration, and cleanup.

Candidate preparation also found a Windows PowerShell/Clang argument-quoting issue in embedding a commit hash as a `-D` string. The builder now generates a build-local source-identity header and passes its include directory as a single `-I<path>` argument. The final v6 build records the exact frozen revision and passed the complete candidate build. The intermediate compile failures did not launch QEMU.

## Hosted and build regression evidence

These results are hosted or build evidence; only the QAC serial records above establish actual guest execution.

| Gate | Result |
| --- | --- |
| RUNSAFE3-A production-backed EL0 suite | PASS, 215 assertions; scheduler timer/exception behavior here is host simulation. |
| EL0 source/architectural contract checks | PASS; no hardware permission claim from these checks. |
| RUNSAFE3-B serial evidence parser regression | PASS for four terminal outcomes, source/child identity, continuation, and hexadecimal syndrome parsing. |
| Phase 2 host controls | PASS, 51 assertions. |
| Captured firmware source DTB with production Phase 2 parser | PASS, 52 assertions. |
| Phase 3 scheduler controls | PASS. |
| Phase 4 allocator/VFS controls and GICv2 GICC/GICR resource-selection controls | PASS. |
| GICv3 hosted suite | PASS, 62 assertions. |
| Phase 9 build and lifecycle controls | PASS. |
| Phase 10 resolver/ELF host controls and freestanding payload builds | PASS. |
| Phase 11 freestanding kernel/EFI and payload build | PASS. |
| Phase 12 SDK alignment and native ABI layout | PASS, 9/9 document activation checks and native layout tests. |
| Studio v0.5 adapter and Studio model/build suite | PASS, 181 adapter checks; pinned Studio ELF linked and staged. |
| Studio Run fail-closed controls | PASS; Run remains disabled. |
| Native filesystem contract | PASS. |
| Final RUNSAFE3-B candidate build | PASS from the frozen clean source revision; manifest and all guest artifacts hashed. |

An exploratory Phase 7 build made while locating the captured firmware DTB stopped at the unrelated existing `native_elf_baremetal_arm64.inc:6` undeclared `compiler` reference. Phase 7 is outside the requested Phase 9–12 candidate path; its failure was not changed or counted as a RUNSAFE3-B pass. The captured DTB was instead tested against the production Phase 2 parser, which passed 52 assertions. Previously accepted R1e GUI, R2A editor/VFS, and R2B1 Build evidence remains in its original reports; this qualification did not re-run those full guest workflows.

## Boundaries and RUNSAFE4 work

- **Developer Studio Run remains disabled.** The Studio ELF was built and packaged for the production candidate profile but was not launched. These four fixtures are internal trusted byte arrays; this is not arbitrary ELF loading or proof that compiler-produced Studio output satisfies the narrow EL0 instruction validator.
- **Legacy Phase 9 NativeElf remains privileged at EL1** in the shared kernel translation regime. This scheduled EL0 runtime does not make that path isolated.
- **Build-to-Run identity is not wired.** RUNSAFE4 still needs a source-matched built-artifact identity and trust/validation record carried through the Studio adapter into an asynchronous scheduled child; it must report the actual child result and reject stale or mismatched outputs. Run must never fall back to the existing privileged EL1 call path.
- **The syscall and ISA profiles remain narrow.** RUNSAFE3-B adds no general syscall surface, no arbitrary application loader, and no SIMD/FPU state preservation. RUNSAFE4 must define and validate any broader compiler-generated instruction and service profile before enabling real Studio Run.
- **Concurrency and timing limits remain.** This is single-core only. The QEMU boots demonstrate finite timeout enforcement but not a strict maximum response latency; the measured 2 ms budget runs exceeded the requested budget by the values above. A hard deadline requirement needs a separately measured and enforced latency bound. No physical AArch64 board was tested.

## Outcome

All four required scenarios and continuing kernel scheduling passed in three fresh exclusive QAC-managed AArch64 guest boots using one frozen candidate. The evidence supports `SCHEDULED_EL0_NATIVE_REPEATABILITY_VERIFIED`; it does not promote hosted simulation to native proof or imply Studio Run safety. The code candidate identity is `a4125ff8017277430e077296eaf6cc6b138eb9e5`; the final repository HEAD also includes this qualification report.
