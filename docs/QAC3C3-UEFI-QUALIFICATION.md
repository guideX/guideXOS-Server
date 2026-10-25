# QAC3C3 UEFI launch qualification — R1

Date: 2026-10-24 (Pacific)
Disposition: **Outcome B — QAC3C3 remains open; bounded admission expired before launch**

## Interrupted-session reconciliation

The first action was read-only. QAC coordinator PID 16064 (2.0.0) was Online. Historical reservation `b78efb643e7947068c58807784a379cf` inspected as `Released`, reason `Registered child process exited.`, with recorded QEMU PID 20052, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, and start time `2026-10-24T16:57:09.4588016Z`. The exact PID was absent, and `127.0.0.1:50114` had no listener. QAC verified the reservation release; no child exit code or QMP quit response was recorded.

The interrupted workspace `qac3c3-s22r10-boot2-744e9cc5a6ae4162a3efde3e41aa1653` remains preserved. Its local record still says `QemuRunning`, `childExitVerified=false`, `reservationReleaseVerified=false`, and `cleanupEligible=false`; the manifest records `guestResult=INCOMPLETE`. These stale local fields were not rewritten to imitate the later QAC release record. No cleanup was attempted.

Its `output\screenshots` directory contains no files, the execution record has no screenshot event, and the QMP transcript has no `screendump` command. The historical run therefore does not qualify screenshot capture.

Four other QAC3C3 execution workspaces had local `RecoveryRequired` states. Their QAC reservations independently inspected as `Released`; recorded PIDs were absent and recorded QMP ports had no listeners. A sixth workspace was staging-only. All six remain retained, with no workspace cleanup or metadata repair.

## Implementation and offline qualification

The existing chain was reviewed from private QAC3C2 staging and manifest validation through mandatory QAC acceptance admission, direct-child QEMU launch, per-run QMP ownership checks, guest observation, graceful QMP shutdown, exact child-exit verification, and reservation release. The runner uses a copied private writable ESP, verified shared read-only ISO and OVMF inputs, argument-list boundaries, run-private outputs, and no unmanaged fallback. QMP candidates are randomized; endpoint ownership is established from the exact QAC child/listener PID and then confirmed through QMP capabilities, `query-name`, and `query-status`. The historical fixed port 65253 and interrupted endpoint 50114 are not reused.

The screenshot path was repaired to normalize Windows separators and quote the full path. Focused checks cover a drive-letter path with backslashes and spaces, the private per-run screenshot destination, manifest screenshot-path traversal, unsafe screenshot labels, and the failure path leaving the guest result `INCOMPLETE`.

Regression results:

- QAC3C3 focused launcher, manifest, QMP, lifecycle, and screenshot-path checks: **74/74 passed**.
- QAC3C2 artifact isolation: **36/36 passed**.
- Existing 80-assertion launcher contract: **80/80 passed**.
- PowerShell parser and launcher parameter checks: **5 files parsed successfully**; the expected parameters were listed.
- QAC source was unchanged; the historical 98-check QAC suite remains the recorded baseline and was not rerun.

## Fresh live attempt

Canonical inputs matched the existing manifest before staging: ESP SHA-256 `5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7` (16 files, 71,435,180 bytes), ISO SHA-256 `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`, and OVMF SHA-256 `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`.

Fresh staging completed under run ID `qac3c3r1-s22r10-boot2-92dcc19172694f958ec75584ad9e6c97`. The workspace has a complete manifest, a private output directory, and a private writable ESP. The QAC3C3 runner requested `guideXOSServer` / `acceptance`; reservation `f51d56d363db4c1e901df8d8e2da94c5` is waiting at queue position 1 behind existing verified acceptance reservation `783b379e44c94f0790d6b8912d7d38d6` (QEMU PID 32672). QAC remains Online with one managed QEMU, one queued request, zero foreign QEMU, and zero blocked or Recovery Required reservations. The existing session and its reservation were left untouched.

The runner recorded `127.0.0.1:57600` as this attempt's unbound QMP candidate. No QAC3C3 child is running to bind it, and the candidate is not treated as reserved until the admitted QEMU child binds it and the runner verifies the listener PID. The historical port 50114 was not reused.

The WPF dashboard was visually observed showing the current managed acceptance row and the QAC3C3 request waiting at queue position 1. New-run child startup, guest screenshot, shutdown, verified release, and minimize/restore observations are pending admission. The queued runner has a 30-minute admission bound and a 15-minute guest-runtime bound after launch; it has not started QEMU while waiting.

## Final disposition — Outcome B

The single bounded live attempt ended at `2026-10-24T23:23:31Z` (16:23:31 Pacific) after its 30-minute QAC admission wait. Reservation `f51d56d363db4c1e901df8d8e2da94c5` became `Expired`, reason `Reservation wait timed out.` The QAC CLI returned 124; the runner then independently inspected QAC and verified the terminal reservation state and release. The request never received a grant, so QEMU was never started: there is no child PID, child start/exit time, guest progress, QMP greeting or command, screenshot, shutdown request, or child exit code. The candidate endpoint `127.0.0.1:57600` remained unbound and was not treated as reserved.

The retained execution record reports `guestResult=INCOMPLETE`, `childExitVerified=false`, `reservationReleaseVerified=true`, `recoveryRequired=false`, and `cleanupEligible=true`. The false child-exit flag means no child existed to verify; it is not evidence of a running or unverified QEMU. The workspace and private ESP were retained. The screenshot output directory is empty. The prior interrupted workspace and all earlier workspaces remain untouched.

After expiry, QAC was Online with one managed session, zero queued requests, zero foreign QEMU processes, and zero blocked or Recovery Required reservations. The only active reservation remained the unrelated verified acceptance session `783b379e44c94f0790d6b8912d7d38d6`, owner PID 23496 and child QEMU PID 32672. It was not controlled. The WPF dashboard showed that same verified row and child PID, coordinator Online, managed sessions 1, queue 0, foreign QEMU 0, and blocked reservations 0; its activity list showed “Reservation wait timed out.” No minimize/restore test was applicable because no QAC3C3 QEMU session became active.

Post-attempt artifact checks passed. The private ESP still contains 16 files / 71,435,180 bytes and hashes to `5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7`, matching its canonical source. The ISO remains `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`; OVMF remains `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`. The execution record reports `canonicalArtifactsUnchangedAfterRun=true`. The retained manifest and execution record agree on the run identity and private ESP path.

Offline screenshot-path qualification and all required regression suites passed as recorded above, but screenshot capture was not exercised live. No UEFI boot or PacMan product result was observed. QAC3C3 is therefore not accepted. `.phase` remains QAC3C3 next/incomplete with overall QAC3 open; QAC3C4 must not begin.

## QAC3C3R3 offline reconciliation — 2026-10-25

### Outcome B — QAC3C3 remains open

The retained QAC3C3R2 boot completed QEMU shutdown and coordinator release for the exact managed execution, but the mandatory WPF completion view after release was not observed. The previously observed dashboard minimize/restore behavior occurred while the acceptance session was active. This report therefore separates infrastructure release from guest acceptance and leaves QAC3C3 open. QAC3C4 must not begin.

### Execution and release evidence

The original run is `qac3c3r2-s22r10-boot2-20261025-1301-544488a076de4301ad6a82f99ca62bec`, owned by `LEON2_RDT\guideX` with token `544488a076de4301ad6a82f99ca62bec`. Its workspace, manifest, owner marker, `.complete` marker, and original `output\execution.json` remain at `%LOCALAPPDATA%\guideXOS\QAC\artifacts\qac3c3r2-s22r10-boot2-20261025-1301-544488a076de4301ad6a82f99ca62bec` and were not rewritten. The execution JSON retains `state=FailedWithRetainedEvidence`, `recoveryRequired=true`, `qacCliReleaseReported=false`, `reservationReleaseVerified=true`, `qemuExitCode=0`, `qacCliExitCode=0`, and `guestResult=FAIL`. It records the runner failure as “QAC inspect says Released but qac run did not report verified release.”

Independent coordinator evidence resolves the exact release state. The run manifest and execution record name reservation `5555811b6f1b438e84f7d2f2648ea24c`, project `guideXOSServer`, mode `acceptance`, QEMU child PID `13804`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, and child start `2026-10-25T20:02:30.413235Z`. The captured `qac inspect` response, written at `2026-10-25T20:08:42.0361615Z`, has the same reservation ID and PID, state `Released`, reason `Managed child exited.`, and owner liveness `Verified`. This supports the classification **QEMU execution completed and QAC release verified** for infrastructure. It does not change the original runner state or guest result.

The QMP transcript records a `quit` request at `2026-10-25T20:08:41.4032962Z`, a `SHUTDOWN` event with reason `host-qmp-quit`, and the successful QMP reply at `2026-10-25T20:08:41.4054053Z`. The QAC runner log records child exit code 0 at `2026-10-25T20:08:41.4713205Z`; QAC CLI exit code is 0. The runner output contains no final `QAC reservation released` CLI line, and the retained coordinator event log has no separate reservation-release journal event for this run. No missing line or journal event is inferred. The independent inspect snapshot is the exact release evidence.

The handoff reported a final census of zero sessions, queued requests, foreign QEMU processes, and listeners on `127.0.0.1:60212`. No final census file was retained or recaptured during this offline closeout, so that census remains handoff evidence rather than independently archived evidence. No new QEMU, reservation, or QMP action was performed.

The append-only source inventory, lifecycle notes, and SHA-256 values for the retained run files are in [the QAC3C3R3 reconciliation record](evidence/qac3c3r3-offline-reconciliation-20261025/README.md). The original execution file’s SHA-256 is `1DB831747DD11F244CF458084012A46EFCFB35829DAD4FBFABFAB319086CF693`.

### Output drain and release identity

The runner now drains stdout and stderr concurrently while the QAC process runs, waits for child exit, and performs a bounded post-exit read to EOF before processing remaining lines and checking CLI exit status. The drain has a validated 1–30 second bound (default 5 seconds) and retains the existing `RecoveryRequired` path when drain completion or release proof is absent. A release line is accepted only when its reservation ID and child PID match the active execution. Final coordinator inspection also requires the exact reservation ID, project, mode, `Released` state, and recorded child PID. Guest result remains independent from integration lifecycle state.

This correction was added after R2 and does not retroactively alter its execution record. The focused QAC3C3 suite passed **85/85**, QAC3C2 isolation passed **36/36**, and the server launcher contract passed **80/80**. Five PowerShell files parsed successfully. A later focused rerun after removing an experimental pipe-capacity fixture produced no output and was stopped after more than two minutes; the successful 85/85 run was from the same focused suite before that fixture was added. The fixture was removed, and no result is claimed for its beyond-capacity stress case. The historical QAC core 98-check suite was not rerun because QAC core source is unchanged.

### Private ESP and canonical artifacts

The canonical ESP tree in `D:\dev\guideXOSServer\out\release-profile\95cf3e2d-778b-4b31-8948-4acdb7cc6aaa\ESP` contains 16 files / 71,435,180 bytes and has tree identity `5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7`, matching the run manifest. The run-private ESP contains 17 files / 71,445,421 bytes, identity `de162001d15517a04b8b70f3cfdac92c8a12cfa16b91c8a6e08490e5e07e26d8`.

All 16 canonical file contents match their private copies. There are no removed or content-modified files. The sole added file is private `NvVars` (10,241 bytes; SHA-256 `ba953f322c467d793e2a35feff699fb4f1c4de04e70427d745cdd4635e1f97c5`), consistent with the manifest’s writable UEFI variable-state behavior. Four private files have later timestamps but unchanged contents and Archive attributes: `build-identity.txt`, `kernel-symbol-audit.txt`, `kernel.elf`, and `Apps/PacMan/resources/audio/startmusicold.wav`. Their timestamp changes are unexplained, but the reviewed writes are confined to the run-private ESP; no shared canonical write is demonstrated. The private ESP and source artifacts were preserved.

Canonical inputs remain unchanged: ESP tree identity `5ffa5777faf76e10633bae441e61e81989d45ffb660aff42feff49674060efe7`; ISO size 90,245,120 bytes and SHA-256 `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`; OVMF size 3,653,632 bytes and SHA-256 `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`. QEMU used `fat:rw` only for the private ESP; ISO and OVMF arguments were read-only.

### Screenshots and guest result

All four current-run PPM screenshots exist in the private run output. Each is a complete P6 image at 1280×800, 3,072,016 bytes total with 3,072,000 RGB pixel bytes. The paths match the four QMP screendump commands and execution events. Their SHA-256 values and file identities are in the reconciliation record. `appmodel-selected-live.ppm` and `start-menu-after-ctrl-esc.ppm` are byte-identical. The images show guest desktop output and the open Start menu; the menu image highlights Calculator, not AppModel or PacMan. They demonstrate desktop and menu progress, not PacMan launch.

The serial log shows PacMan initially discovered (`package=PacMan`, `id=com.guidexos.pacman`, display name `Nexgen PacMan`) and later reports `NO_APPS_DIRECTORY`. There is no `[PACMAN-SMOKE] PASS` or gameplay marker. The guest result is **FAIL**: application discovery did not remain available, and PacMan launch/gameplay acceptance was not established. QEMU exit code 0 is not a PacMan pass.

### Dashboard observation and disposition

The dashboard minimize/restore interaction was observed during the live acceptance session. The post-release completion/empty view was not observed. A bounded read-only attempt to inspect the currently available WPF application timed out in the computer-use helper; after its prescribed reset and single retry also timed out, no further UI actions were taken. QAC inspect independently verifies release, but it does not satisfy the WPF completion-view criterion. This remains a mandatory QAC3C3 qualification gap and is the reason for **Outcome B**.

QAC `.phase` remains unchanged: QAC3C1 and QAC3C2 accepted, QAC3 open, and `Next: QAC3C3`. No QAC3C4 work may begin.
