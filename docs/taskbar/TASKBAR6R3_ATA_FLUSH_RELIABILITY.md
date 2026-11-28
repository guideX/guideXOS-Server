# TASKBAR6R3 — ATA Flush Reliability

- **Outcome:** `TASKBAR6R3_PARTIAL`
- **Integration recommendation:** Not ready for controlled integration
- **Date:** 2026-11-17
- **Repository:** `D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`
- **Branch:** `TASKBAR_IMPROVEMENTS`
- **Starting HEAD:** `62c393748c444b0d188ea48ef31dd7b4499aef64`

## Disposition

TASKBAR6R3 reconstructs the R2 failure path, verifies the existing FAT rollback behavior, and adds compile-time-only ATA flush diagnostics. It does not establish why the QEMU IDE device stayed busy in the failing R2 run, and it makes no production storage repair. The QAC coordinator was unavailable during this run, so the diagnostic kernel could not be booted and no new raw-device or native taskbar persistence result is claimed.

The original R2 evidence remains a genuine product failure: three taskbar save attempts each ended in an ATA flush timeout; the requested replacement was not saved, while the previously committed settings remained intact. This must stay open until a QAC-managed run captures the device status and repeated native saves survive cold restarts.

## Starting state and candidate identity

- The starting worktree was clean on `TASKBAR_IMPROVEMENTS` at `62c393748c444b0d188ea48ef31dd7b4499aef64` (`TASKBAR6R2: qualify native taskbar persistence`). It was one commit ahead of `origin/TASKBAR_IMPROVEMENTS`.
- No release candidate, ISO, or frozen v0.3 artifact was changed.
- The R2 raw image used for identity comparison is `out/taskbar/TASKBAR6R2/guest-volume-fat32-fresh.img`, 536,870,912 bytes, SHA-256 `8468F2B9A615333F10FA2ED73027A381195FC8A8389D0AC2A90948FDA4CA126E`.
- The production AMD64 kernel rebuilt successfully at `kernel/build/amd64/bin/kernel.elf`, SHA-256 `608C2C62C0C66BF294DD28CEB58A517200569A15799D8C6587A8B3CE88653A1E`.
- The qualification-only diagnostic kernel is `out/taskbar/TASKBAR6R3/native-diag/bin/kernel.elf`, SHA-256 `60BBAC1BE885CB85D91A5E340567C13E06EE339C41C47E70C080E8ECE1C1D367`.
- The installed QEMU reports version 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`).

## Original R2 failure reconstruction

R2 used QEMU's legacy IDE controller with a writable raw FAT32 disk. The guest mounted the primary-master device `ata0m`; its partition starts at sector `0x800`. QEMU's UEFI boot path and the guest logs identify the same primary-master IDE location. This is separate from the earlier `fat:rw:` directory-backed rename failure.

The final R2 serial record in `out/taskbar/TASKBAR6R2/guest-fat32-r2-final-retry-serial.log` shows:

1. FAT directory metadata write returned `FAT_FILE_WRITE_OK` (`FPASTE_DIRECTORY_METADATA_END`, around line 993).
2. The temporary settings-file create reached `LFPASTE_FLUSH_BEGIN stage=create`.
3. Its flush returned status `0x02` (`BLOCK_ERR_TIMEOUT`); VFS reported `FAT_FILE_WRITE_IO_TIMEOUT`.
4. The desktop retried as attempt 2, and that attempt's flush also returned `0x02`.
5. The desktop retried as attempt 3, and that attempt's flush again returned `0x02`.
6. The desktop returned `taskbar settings persistence failed status=FFFFFFF2` (`VFS_ERR_IO_TIMEOUT`).

Thus the “three retries” are three total settings-save attempts at the desktop persistence layer (initial attempt plus retry attempts 2 and 3). The ATA flush callback contains no retry loop and no reset/recovery action. The R2 log does not contain ATA command sequence IDs, command opcode, raw status/error registers, or timing, so it cannot show whether the device remained busy, returned an error, completed before the guest observed it, or exceeded a wall-clock deadline.

The last operation reported successful before the first failed flush was the FAT metadata update. Its successful return does not prove durable completion because the following cache flush failed. R2's post-shutdown inspection found the previous `/DESKTOP.CFG` unchanged (Bottom / Standard / icons-only / auto-hide Off), `/DESKTOP.TMP` empty, and the requested Top value absent. An earlier profile and Calculator pin had survived a cold boot; that successful earlier cycle does not clear the later failure.

## Actual controller and driver path

The production path is `kernel/core/ata.cpp`:

- PCI class `01/01` selects the legacy IDE path; the primary command/control bases are `0x1F0` and `0x3F6`.
- ATA PIO sector writes issue one sector command at a time and poll for command-idle completion.
- `ata_pio_flush()` writes a cache-flush command to the command/status port and polls the status register until BSY clears, with a limit of 500,000 port reads.
- The callback is polling-based; this driver does not wait on an ATA completion interrupt.
- The opcode is selected from `dev.lba48`: `0xEA` (FLUSH CACHE EXT) when true, otherwise `0xE7` (FLUSH CACHE). R2 did not record IDENTIFY word 83, so the actual opcode used in the failing guest is not established.

The code inspection found several issues that need to be measured before assigning causality:

- The flush timeout is an iteration count, not an elapsed-time deadline. Its wall-clock duration varies with execution and device-model progress.
- `ata_pio_flush()` does not explicitly select its requested master/slave device before issuing the command, although devices share channel registers.
- The wait returns success when BSY clears without checking ERR or DF. This can misreport a rejected flush as successful; it does not explain the R2 timeout by itself.
- The block layer and ATA PIO callbacks do not serialize commands per IDE channel. R2 provides no evidence that commands overlapped.

These are confirmed code properties, not a confirmed root cause for the R2 timeout. The R2 QEMU debug logs are empty and no QEMU-side IDE trace was preserved, so backend completion cannot be correlated with guest observation.

## Bounded diagnostics added

`kernel/core/ata.cpp` now supports `ATA_QUALIFICATION_DIAGNOSTICS`, disabled by default. The diagnostic build records:

- ATA device name, legacy IDE PIO channel, target master/slave, the pre-command device/head register, opcode, flush sequence, and IDENTIFY word 83 with LBA48/FLUSH CACHE EXT capability bits.
- Status before issue, immediately after issue, and at completion/timeout; final BSY, DRQ, DRDY, ERR, and DF flags; and the error register.
- The last write's issued/completed sequence and LBA, poll count and existing `500000` limit, PIT tick start/end/elapsed values, and the fact that the current timeout has no monotonic deadline.
- Up to eight periodic status samples collected in memory and printed after the wait, avoiding serial output inside the polling loop. Flush records are capped at 128; later records are suppressed after one cap notice.
- Completion mode and recovery state (`poll-status-bsy`, `recovery=none`).

This instrumentation does not change the command, timeout, retry, or recovery behavior. It is not enabled in the normal production AMD64 kernel. No native output was collected because QAC was unavailable.

## FAT32 update and rollback behavior

The failed R2 save stopped while creating the temporary settings file and propagated the timeout through FAT/VFS to the desktop. The previously committed settings remained intact. TASKBAR6R3 reran the actual hosted FAT/VFS persistence sources against a fresh copy of the prepared FAT32 image and passed **64/64 checks**, including injected flush-timeout propagation, preservation of the original file and bytes, and a later successful retry after the injected fault cleared.

This validates the filesystem transaction behavior on a hosted image callback. It does not exercise ATA PIO, QEMU's device cache, or cold-restart persistence. The existing FAT update path does not justify a claim of power-loss atomicity beyond the observed preservation behavior.

## Raw FAT32 and taskbar qualification

No R3 guest was started. The QAC CLI was present at `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`, but `status`, `list`, and `queue` each returned `Coordinator unavailable`. No R3 reservation was obtained, no QEMU process was started, and the active global QAC inventory could not be verified while the coordinator was offline. The R2 closeout records that its own reservations were released with zero managed sessions.

Consequently, R3 performed no native raw-device write/flush cycle, taskbar UI save, clean shutdown, cold restart, or post-restart content verification. R2's three failed saves remain the last native result. No native screenshots or new raw FAT32 checksums are claimed. QEMU's separate `fat:rw:` rename behavior remains unqualified and was not retested.

## Regression and build results

- Taskbar application model: **113 checks passed**.
- FAT persistence: **64 checks passed, 0 failures**.
- App Model identity: **25/25**.
- App Model application actions: **55/55**.
- Compositor pointer/buttons: **PASS**.
- Window UI regression: **PASS**.
- Compositor resize notification: **PASS**.
- Hosted Server build (`cmd /c build.bat`): **PASS**; existing compiler warnings remain.
- Production AMD64 kernel build (`mingw32-make -C kernel ARCH=amd64 all`): **PASS**.
- Diagnostic AMD64 kernel build (`mingw32-make -C kernel ARCH=amd64 BUILD_DIR=../out/taskbar/TASKBAR6R3/native-diag EXTRA_CFLAGS=-DATA_QUALIFICATION_DIAGNOSTICS all`): **PASS**.
- `git diff --check`: **PASS** after the source and documentation updates.
- No ATA-driver status-transition or timeout-boundary regression was added because the cause has not been established and there is no current host harness that executes the production ATA port-I/O path.

The R3 test logs and executables, hosted build log, and diagnostic kernel are under `out/taskbar/TASKBAR6R3/`; `qualification-evidence.txt` records commands, hashes, and totals. The production kernel output is at `kernel/build/amd64/bin/kernel.elf`, and the hosted executable is the build script's root `guideXOSServer.exe` output. The hosted FAT test uses `fat-regression-clean.img`, a zero-offset prepared FAT32 volume. An initial attempt with the partitioned guest disk could not open it as a zero-offset FAT volume; a second attempt used a previously mutated test copy and exposed stale-fixture assumptions. Both are excluded from the passing 64-check result; the clean-image rerun passed.

## Remaining taskbar gaps

Native application grouping, full preview interaction, overflow pagination, broad auto-hide behavior, and real File Explorer copy/move progress remain unqualified as recorded in TASKBAR6R2. Fixing the storage defect alone will not close those acceptance gaps.

## Acceptance gates

| Gate | Status | Evidence / reason |
|---|---|---|
| R3-G1 — Original failure reconstructed or conclusively characterized | **PARTIAL** | Save/FAT/VFS/desktop sequence and retry layers reconstructed; ATA opcode and register/timing evidence are absent from R2. |
| R3-G2 — Root cause established | **PARTIAL** | Driver timing, selection, status-check, and serialization gaps are confirmed in source, but none is proven to have caused the observed timeout. |
| R3-G3 — Correct repair implemented or backend defect isolated | **BLOCKED** | No repair was made without causal evidence; no QAC run was possible. |
| R3-G4 — Repeated raw FAT32 write/flush operations succeed | **BLOCKED** | Requires a real QAC-managed guest-device run; hosted FAT tests do not substitute. |
| R3-G5 — Taskbar settings survive multiple native save/reboot cycles | **BLOCKED** | No R3 QAC session was available. R2 had a later failed update. |
| R3-G6 — Previous configuration protected on failed writes | **PASS** | R2 raw-volume failure preserved the prior settings; current hosted FAT rollback checks passed. |
| R3-G7 — FAT and storage regressions pass | **PARTIAL** | FAT/VFS passed 64 checks; no ATA-driver regression could be run without hardware state or a production-path test seam. |
| R3-G8 — Hosted and native AMD64 builds pass | **PASS** | Hosted app, production kernel, and diagnostic kernel builds passed. |
| R3-G9 — QAC ownership and resource cleanup verified | **BLOCKED** | Coordinator unavailable before reservation; no R3 resources were acquired, but the live global inventory could not be queried. |
| R3-G10 — Mainline integration readiness reassessed | **PASS** | Not ready: storage cause and native persistence remain open; local refs are stale and SSH authentication failed. |

## Mainline assessment

The configured origin is `git@github.com:guideX/guideXOS-Server.git`. A read-only `git ls-remote` retry failed with `Permission denied (publickey)`, so remote refs were not refreshed. At preflight, the branch was one commit ahead of `origin/TASKBAR_IMPROVEMENTS` and eight commits ahead of `origin/main`; after the R3 local delivery commit, it is two commits ahead of the taskbar tracking ref and nine commits ahead of the main tracking ref. These are local snapshot comparisons, not a current remote assessment.

Do not merge or push. The open raw-volume timeout, absent native repeated-save proof, and remaining taskbar acceptance gaps are sufficient to keep the branch out of controlled integration regardless of eventual merge-tree compatibility.

## Next milestone

**TASKBAR6R3R1 — QAC-assisted diagnostic raw FAT32 requalification.** Once the existing QAC coordinator is available, acquire an exclusive acceptance reservation, use a disposable copy of the identified raw FAT32 volume and the diagnostic kernel hash above, capture the ATA trace during repeated write/flush cycles, then return to the real taskbar UI for multiple save and cold-restart cycles. Correlate guest traces with bounded QEMU IDE diagnostics. Make a production repair only after the trace distinguishes a guest polling/selection/serialization defect from a backend completion delay; then add a focused regression for that demonstrated cause.
