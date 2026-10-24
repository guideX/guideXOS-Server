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
