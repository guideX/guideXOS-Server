# TASKBAR6R4 — ATA Diagnostic and Persistence Acceptance

- **Outcome:** `TASKBAR6R4_STORAGE_DEFECT_OPEN`
- **Date:** 2026-11-19
- **Branch:** `TASKBAR_IMPROVEMENTS`
- **Starting commit:** `607dc40e79ee85e8f96058249e9a782bf3a32555`
- **Initial worktree:** clean; branch was two commits ahead of `origin/TASKBAR_IMPROVEMENTS`.

## 1. Result

The R2 failure remains **unresolved at the guest/controller level**. R2 proved that three settings-save attempts on `ata0m` returned an ATA flush timeout and that the previous settings file remained intact. This is a significant repeated native storage defect, so the outcome is `TASKBAR6R4_STORAGE_DEFECT_OPEN`. Its logs do not contain the ATA status/error transitions needed to distinguish a device that stayed busy from an ATA error, an incorrect selection, or another controller-state problem.

This milestone makes focused production correctness repairs and verifies their shared production state machine with 80 hosted checks. It also verifies production and diagnostic AMD64 builds. QAC was unavailable, so no R4 guest was started. The raw FAT32 sequence, R4 diagnostic capture, taskbar saves, and genuine cold restarts were not run. No root cause is claimed and mainline storage acceptance remains open.

**Root cause and confidence:** not established. Confidence in any specific explanation for the R2 timeout is low because the R2 logs contain no ATA status/error, opcode, or timing trace, and R4 could not obtain a guest run. The confirmed selection, capability, status-checking, and serialization defects are not proven causes of that failure.

## 2. Preflight and candidate artifacts

The starting checkout was `D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`, branch `TASKBAR_IMPROVEMENTS`, at the requested commit. The worktree was clean before R4 edits. No frozen v0.3 release artifact was changed.

The existing R3 diagnostic artifact was inspected and contained the `ATA-DIAG` instrumentation. It was left intact at `out/taskbar/TASKBAR6R3/native-diag/bin/kernel.elf` (SHA-256 `60BBAC1BE885CB85D91A5E340567C13E06EE339C41C47E70C080E8ECE1C1D367`). After the ATA implementation changes, the R4 production and diagnostic kernels were rebuilt so the candidate includes the exact code under review:

| Artifact | Path | SHA-256 | Size |
|---|---|---|---:|
| Production AMD64 ELF | `kernel/build/amd64/bin/kernel.elf` | `7B64B1C4A857CF1733B9773ACE346F8665D02ED373FE4B24CACC0E1E3734CBB0` | 2,865,200 bytes |
| Diagnostic AMD64 ELF | `out/taskbar/TASKBAR6R4/native-diag/bin/kernel.elf` | `A0C57630D35E4F6D8A5AC13DC1ACB741ED2067BB295CAA9D806D0D10F8C04524` | 2,873,896 bytes |

The diagnostic build enables `ATA_QUALIFICATION_DIAGNOSTICS` with `EXTRA_CFLAGS=-DATA_QUALIFICATION_DIAGNOSTICS`. ATA records are written to the guest serial console. They are bounded to 128 flush records per boot, with at most eight status-transition samples per flush. Production builds keep the detailed trace disabled and emit a short failure record only.

## 3. QAC and guest setup

The QAC CLI was present at `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`. `qac status`, `qac list`, and `qac queue` each exited 2 with “Coordinator unavailable: Coordinator connection timed out or is unavailable.” The preflight record is `out/taskbar/TASKBAR6R4/qac-preflight.log`.

No exclusive reservation was obtained and no guest or QEMU process was launched for R4. The local QEMU process count was zero during preflight, but global QAC inventory could not be queried while the coordinator was down. There is therefore no R4 reservation to release. The installed QEMU is `11.0.0 (v11.0.0-12122-ga4bb4b10c9)` at `C:\Program Files\qemu\qemu-system-x86_64.exe`; this is host installation evidence, not evidence of an R4 guest run.

The established R2 configuration used an AMD64 UEFI/OVMF guest, 1024 MiB RAM, 1280×800 GOP, PS/2 input, QMP on loopback port 49231, and a writable 512 MiB raw FAT32 volume. It is the intended configuration for a managed retry. R4 did not instantiate it. The R2 raw image `out/taskbar/TASKBAR6R2/guest-volume-fat32-fresh.img` remains unchanged at 536,870,912 bytes and SHA-256 `8468F2B9A615333F10FA2ED73027A381195FC8A8389D0AC2A90948FDA4CA126E`.

## 4. Prior guest evidence and device identity

R2 serial evidence identifies the storage path as the driver's legacy IDE PIO channel, primary command block at I/O `0x1F0`, primary control block at `0x3F6`, device `ata0m` (primary master), with the FAT32 partition beginning at sector `0x800`. UEFI reported the disk on its primary/master ATA path. R2 did not preserve the PCI IDE controller vendor/device ID, IDENTIFY word 83, or a QEMU-side ATA trace; the exact emulated PCI controller model and flush opcode in the failing run are unknown.

In `out/taskbar/TASKBAR6R2/guest-fat32-r2-final-retry-serial.log`, FAT metadata writes succeeded before the flush callback returned `0x02` (`BLOCK_ERR_TIMEOUT`). The initial save and desktop retry attempts 2 and 3 all failed at flush. VFS and the desktop reported the failure. After shutdown, `/DESKTOP.CFG` still contained Bottom / Standard / icons-only / auto-hide Off; `/DESKTOP.TMP` was empty and the requested Top value was absent. The earlier Calculator pin and a previously saved profile had survived an earlier cold boot, but the later failed update remained a product defect. The related QEMU debug logs are empty, so they cannot distinguish backend completion from guest polling behavior.

The original failure is **not reproduced or conclusively characterized by R4**. It remains unknown whether the flush stayed busy, completed without being recognized, returned ERR/DF, targeted another drive, exhausted an overly short poll budget, or overlapped another channel command.

## 5. ATA review and production repairs

The driver uses legacy ATA PIO. Master/slave devices share each channel's task-file registers. Reads, writes, and flushes now hold a per-channel lock for the full transaction. The kernel's current cooperative scheduler and IRQ handlers do not issue block-device operations; the lock protects against task-level interleaving and master/slave register changes. Before selecting a flush target, the state machine waits for the channel's active BSY/DRQ state to clear. It writes Device/Head `0xA0` for master or `0xB0` for slave, applies the driver's 400 ns settling delay, reads Device/Head back, and refuses to issue a flush when the selected DEV bit does not match the requested target.

ATA command selection now follows IDENTIFY DEVICE word 83. Bit 12 is recorded as FLUSH CACHE support and bit 13 as FLUSH CACHE EXT support; EXT is not inferred from LBA48 alone. The driver uses the advertised EXT opcode when available, otherwise the advertised base command, and returns `BLOCK_ERR_UNSUPPORTED` if neither is reported. This corrects a confirmed capability-selection defect. R2 did not preserve IDENTIFY data, so there is no evidence that this defect caused the R2 timeout. T13/1699-D Revision 4a identifies these word 83 bits separately and notes that cache flush may take longer than 30 seconds ([IDENTIFY word 83 and FLUSH CACHE timing](https://crs.s3lab.io/cs4348/2024/fall/_static/read/hardware/ATA8-ACS.pdf)).

Flush completion is no longer “BSY cleared means success.” While BSY is set, other status bits are ignored. After BSY clears, ERR and DF return `BLOCK_ERR_IO`; ERR/DF status and the Error register are retained in the trace. A non-data flush ending with DRQ set is treated as an I/O failure. Missing-device status and not-ready completion remain distinct from timeout and device error. Success requires BSY clear, DRQ clear, DRDY set, and no ERR/DF. PIO read/write wait helpers also preserve I/O and not-ready failures instead of converting all unsuccessful waits into timeout.

The operational flush timeout is 6,000 PIT ticks (60 seconds with the kernel's configured 100 Hz PIT), used only after the PIT heartbeat has been observed and interrupts are enabled. Unsigned elapsed-time subtraction handles counter wrap. When the PIT deadline is unavailable, the driver retains the previous bounded 500,000-poll fallback; a 5,000,000-poll emergency guard bounds a PIT that unexpectedly stops advancing. In those states the kernel has no calibrated monotonic TSC or other reliable time source available, so the poll fallback remains CPU-speed dependent rather than pretending to provide a real-time deadline. The fallback and emergency guard have not been measured in a guest. Hosted tests validate the deadline, timeout boundary, wrap handling, and poll-guard behavior, not real controller timing.

There is no internal flush retry or speculative controller reset. A subsequent filesystem/caller retry can issue a new flush after an earlier ATA error. Errors propagate through the block-device flush callback and FAT/VFS save path. No cache flush was disabled and no storage backend was changed.

## 6. Diagnostics

With `ATA_QUALIFICATION_DIAGNOSTICS`, each flush trace includes channel and port identity, controller path, device name, master/slave target, selected Device/Head value and target-match result, opcode, advertised cache-flush capabilities, status before selection/after selection/after command/final, Error register before and after, BSY/DRQ/DRDY/ERR/DF state, poll count, PIT start/end/elapsed ticks, deadline availability and timeout source, command-issued and observed-BSY flags, last preceding write issued/completed state, retry count, and final block result. State transitions are recorded once rather than printing every status poll. The trace count and number of samples are bounded.

No R4 guest consumed these diagnostics. Consequently there is no new ATA status transition, timing observation, preceding-write observation, or actual controller/device capability record from R4.

## 7. FAT and settings failure safety

The existing settings replacement path writes a temporary file, flushes the relevant block device through FAT/VFS, and only then commits the replacement. An ATA flush failure is returned to the caller; it is not converted into a success. R2 demonstrated that a failed raw-volume update preserved the previously committed settings and removed/left empty the temporary file after shutdown.

The hosted FAT persistence regression passed 64/64 checks using the actual FAT/VFS persistence sources and a disposable copy of the prepared FAT32 image. Its coverage includes injected write/flush failures, timeout propagation, rollback, and preservation of the original file contents. This is filesystem callback coverage, not ATA PIO or raw guest persistence. R4 did not perform deterministic native ATA fault injection, so native temporary-file cleanup, LFN association behavior under an ATA failure, and preservation after a real device flush error remain unqualified. No power-loss atomicity claim is made.

The QEMU `fat:rw:` directory-backed rename issue remains a separate, unqualified backend issue. R4 found no evidence linking it to the raw IDE flush timeout and did not expand its investigation.

## 8. Qualification results

R4 raw FAT32 guest operations: **not run**. There were zero R4 raw-volume write/flush operations, consecutive flushes, settings replacements, directory checks, or cold-boot content checks. No R4 disposable guest disk or guest screenshot was produced. No R4 native taskbar settings save was attempted: Boot 1, Boot 2, and Boot 3 are all pending. The prior R2 result remains the only native taskbar persistence evidence.

The production state machine is exercised by `tests/ata_pio_flush_test.cpp` through the same inline logic used by `ata.cpp`, with a bounded fake port/clock adapter. It passed **80 checks, 0 failures**, including master/slave and primary/secondary transitions, target readback mismatch, waiting for BSY before reselection, status interpretation, ERR/DF and Error-register capture, missing device, immediate completion, deadline and poll-guard timeouts, completion just before and at the deadline, uint64 wrap, flush opcode capability selection, and a later caller-issued flush after an earlier error.

## 9. Gate matrix

| Gate | Status | Evidence / reason |
|---|---|---|
| R4-G1 — Diagnostic candidate and QAC setup verified | **PARTIAL** | Production and diagnostic candidates are rebuilt and hashed. QAC `status`, `list`, and `queue` timed out; no exclusive reservation or guest setup was possible. |
| R4-G2 — Real ATA flush diagnostics captured | **BLOCKED** | No R4 guest ran; the diagnostic output was not captured from a controller. |
| R4-G3 — Original failure reproduced or conclusively characterized | **PARTIAL** | R2 failure and save/FAT/VFS path are reconstructed; raw ATA status, error, opcode, and timing evidence are missing. |
| R4-G4 — Selection and completion correctness established | **PARTIAL** | Production state machine and focused tests cover selection, status/error handling, and serialization. No real controller observation validates the target or transitions. |
| R4-G5 — Timeout handling validated | **PARTIAL** | PIT deadline, wrap, boundary, fallback, and emergency guard are covered by hosted tests and AMD64 builds; guest timekeeping and controller duration remain unmeasured. |
| R4-G6 — Supported production repairs implemented and verified | **PASS** | Focused selection, capability, completion, timeout, serialization, and propagation fixes pass 77 ATA checks and both AMD64 builds. This is code-level verification only. |
| R4-G7 — Repeated native raw FAT32 operations pass | **BLOCKED** | Requires a QAC-managed guest; hosted FAT checks do not substitute. |
| R4-G8 — Multiple taskbar saves survive genuine cold restarts | **BLOCKED** | No R4 native save or cold restart was possible. |
| R4-G9 — Failed persistence preserves previous configuration | **PARTIAL** | R2 raw failure preserved the old profile; hosted FAT rollback tests pass. No R4 native ATA fault injection was run. |
| R4-G10 — Regressions and builds pass | **PASS** | Existing model/FAT/App Model/UI regressions pass; ATA suite is 77/77; hosted and AMD64 builds pass. |
| R4-G11 — QAC resources released and accounted for | **PARTIAL** | R4 acquired no reservation and started no local guest, so there was no R4 session to release. Global coordinator inventory was unavailable. |
| R4-G12 — Mainline readiness reassessed | **PASS** | Reassessed as **not ready for storage acceptance/integration** until managed raw FAT32 and cold-restart qualification completes. |

## 10. Regression and build totals

| Check | Result |
|---|---:|
| ATA PIO flush state machine | 80 checks, 0 failures |
| Taskbar application model | 113 passed |
| FAT/VFS persistence | 64 checks, 0 failures |
| App Model identity | 25/25 |
| App Model application actions | 55/55 |
| Compositor pointer/buttons | Passed |
| Window UI regression | Passed |
| Compositor resize notification | Passed |
| Hosted build (`cmd /c build.bat`) | Passed; `Build successful: guideXOSServer.exe` |
| AMD64 production build | Passed; `kernel/build/amd64/bin/kernel.elf` |
| AMD64 diagnostic build | Passed; `out/taskbar/TASKBAR6R4/native-diag/bin/kernel.elf` |
| `git diff --check` | Passed |

## 11. Evidence paths and remaining work

- `out/taskbar/TASKBAR6R4/qac-preflight.log`
- `out/taskbar/TASKBAR6R4/ata-pio-flush-test.log`
- `out/taskbar/TASKBAR6R4/native-amd64-build.log`
- `out/taskbar/TASKBAR6R4/diagnostic-amd64-build.log`
- `out/taskbar/TASKBAR6R4/hosted-build.log`
- `out/taskbar/TASKBAR6R4/taskbar-model-final.log`
- `out/taskbar/TASKBAR6R4/fat-persistence-final.log`
- `out/taskbar/TASKBAR6R4/app-model-identity-final.log`
- `out/taskbar/TASKBAR6R4/appmodel-actions-final.log`
- `out/taskbar/TASKBAR6R4/compositor-pointer-final.log`
- `out/taskbar/TASKBAR6R4/window-ui-final.log`
- `out/taskbar/TASKBAR6R4/compositor-resize-final.log`
- `out/taskbar/TASKBAR6R2/guest-fat32-r2-final-retry-serial.log`
- `out/taskbar/TASKBAR6R2/guest-fat32-r2-final-boot-serial.log`
- `out/taskbar/TASKBAR6R3/qualification-evidence.txt`
- `docs/taskbar/TASKBAR6R2_NATIVE_ACCEPTANCE_AND_INTEGRATION_READINESS.md`
- `docs/taskbar/TASKBAR6R3_ATA_FLUSH_RELIABILITY.md`

Native grouping and group selection, full hover preview, overflow pagination, full auto-hide/reveal behavior, and File Explorer copy/move progress remain open from R2. They were not exercised in R4 and are not accepted by these storage changes.

**Recommended next milestone:** `TASKBAR6R4R1 — QAC-managed raw FAT32 ATA diagnostic and persistence requalification`.

**Mainline recommendation:** keep this branch local until QAC becomes available and the rebuilt diagnostic candidate records the real `ata0m` flush lifecycle. Then perform the bounded raw FAT32 write/replace/flush sequence and inspect the volume, followed by the requested multi-save PS/2 taskbar sequence and at least two genuine cold restarts. If those runs identify a device/controller failure, preserve its trace before retrying and make only the evidence-supported follow-up repair. Reassess integration only after those guest gates pass.
