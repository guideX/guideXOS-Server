# Disk Manager Phase DM33 — Long Operation UX

## Outcome and phase gate

**Outcome B.** DM33's cooperative blank-media job, immutable target binding, bounded progress accounting, safe pre-commit cancellation, deterministic tests, and production UI integration are implemented. This continuation qualified current-source AHCI/ATA lifecycle behavior and additional build regressions, and reached the production FAT32 format-options dialog for an exact disposable USB target in QEMU. The cooperative job was not started, so real progress/cancel, retry, removal/replacement, and close-during-operation qualification remain incomplete. `.phase` remains `last_completed=DM32`, `next_expected=DM33`.

## Starting state and architecture

The continuation gate was checked in `D:\dev\guideXOSServer_DiskManagerImprovements`: branch `DISK_MANAGER_IMPROVEMENTS`, starting HEAD `8847973a07280fde6efa94384736ef379d9dff51` (`disk-manager: add responsive long-operation workflow`), `.phase` DM32→DM33, and the pre-existing DM33 report edit was preserved. The worktree initially contained only that report modification. Cached upstream divergence was 0 ahead / 0 behind. One normal fetch failed with `Permission denied (publickey)`; per the continuation instructions this is non-blocking and credentials/remotes were left unchanged. The prior report incorrectly described an earlier starting commit/state; this continuation records the actual gate.

The desktop uses an event loop and kernel-app `update()`/draw dispatch; no suitable kernel worker or general job scheduler was found for this UI operation. The implementation uses one bounded cooperative step per Disk Manager update. Storage services already provide the destructive-operation lease, registration/incarnation pin, target identity revalidation, block transfer-size limits, and private KnownZero authorization. The job owns its target and result, so later UI selection does not supply operation identity. Application shutdown requests safe cancellation; the headless qualification gap means the user-visible active-close behavior remains unverified.

## Job and progress model

`Fat32FormatJob` binds the operation kind, full format request and exact target/partition snapshot, operation lease/pin, logical geometry, start tick, scan offset/total, result, and cancellation/commit state. The job is statically embedded in Disk Manager; it is bounded and the deterministic size check confirms it is under 2 KiB. One active destructive operation is allowed by the existing exclusive lease and job state guard.

States are Idle, Scanning, ReadyToCommit, Completed, Failed, and Canceled. Each update reads one transfer bounded by the 1 MiB scan buffer and the transport's maximum transfer. Progress is derived from exact scanned logical sectors/bytes, clamped to total, and exposed as phase plus percent and verified amount. The UI presents a progress bar and textual status, and exposes Cancel only while the scan can stop safely. A distinct READY_TO_COMMIT state creates an observable cancellation boundary after the final scan transfer. Formatter commit disables cancellation and consumes the private one-use KnownZero token.

Cancellation is cooperative at a bounded I/O boundary. It invalidates any in-progress authorization, releases the held operation lease and target pin, and performs no formatter writes or publication Flush. Nonblank, read error, removal, and identity changes retain distinct service results. Closing/shutdown calls cancel; the active-close explanation and behavior need a real GUI run before acceptance.

## Deterministic evidence

The final storage suite passes **987 checks, 0 failures**. USB transport checks include 249 checks; USB trace classifier tests pass 7/7, and the verifier fixture test passes. Added tests cover cancellation before I/O, during scan and at the final-transfer boundary; immutable request binding; single-job/reentrant begin; monotone exact progress and retry; nonblank media; 4Kn; and removal/replacement identity protection. The scan remains full-range, detects nonzero bytes, and does not enlarge the bounded buffer. These tests establish service-level safety, not guest GUI acceptance.

Logs are in ignored `out/dm33/` and `out/dm33-regression/`. The storage suite (987/987), USB classifier (7/7), isolated AMD64 kernel build, `build.bat`, and UEFI x64 Release passed. Current-source AHCI lifecycle with mount/write/read/remount/restart and independent verification passed on 600 MiB; current-source ATA lifecycle and verification passed on a 68 MiB image; current-source Quick Reformat AHCI lifecycle/restart/verification passed. An earlier 600 MiB ATA blank-format attempt timed out and is not counted. `build.ps1 -Arch amd64` retains the unrelated PacMan undefined-symbol failure (`pacman_audio_load_resources` and `pacman_audio_submit`); generated outputs were restored and hash-verified. DM30 GPT Repair, DM25 Delete, DM30 read-only, DM18 NVMe, and DM24 4Kn guest gates remain unrun. NVMe write and Flush proof flags remain zero; no physical host storage was used.

## QEMU attempt and limits

A proof-only kernel hook launched the normal Disk Manager resolver and logged success; the hook and telemetry instrumentation were removed from tracked sources, whose hashes match HEAD. The continuation used QMP keyboard events to open the production format-options dialog for a verified blank USB target. Fixture `out/dm33/continuation-20261005/usb-blank-100m.raw` is a 100 MiB device with one 99 MiB MBR partition at LBA 2048, serial `DM33USB01`, and an all-zero partition. Its initial SHA-256 was `6C9C1B76E2FA53426E562D5B3C6D9930A61DB285D2FC674E843408559F6EB1AD`; after the guest was quit, the hash remained identical, confirming no format operation was issued. Screenshots and QMP/serial artifacts are in that ignored evidence directory. The rendered Format FAT32 confirmation control was not reliably activated, so the cooperative job never began: no progress repaint, cancellation guarantee, retry, successful format, KnownZero runtime evidence, or DM33 GUI hotplug proof is claimed. Existing service-level and transport lifecycles do not substitute for the cooperative GUI path.

### Format-options focus audit and bounded fix

The real format-options dialog contains the editable volume-label field, Cancel, and Format FAT32 buttons. Before this fix, initial focus was the label field; text entry/backspace applied there. Tab only reselected that field for Format and could not reach either button. Enter bypassed focus and directly invoked the formatter, while Space was not routed for this dialog. Escape canceled the dialog; Cancel and Format already had ordinary `onWidgetClick` handlers. The production control dispatcher therefore existed, but deterministic keyboard focus did not.

The dialog-local fix keeps the label field as initial focus and adds forward focus cycling Label → Cancel → Format FAT32 → Label. The existing focus color marks the active button. Enter and Space on Cancel or Format dispatch through their normal widget click handler; Enter on the editable label does not start formatting. Text edits are accepted only while the label field is focused. This remains modal under the existing dialog key handler. The focused control can now be established deterministically, but this continuation stopped before obtaining a post-fix rendered focus screenshot and real Format activation, so no GUI-to-job telemetry or runtime acceptance is claimed yet.

QMP preflight passed greeting, capabilities, query-status, screendump, harmless key input, second query-status, and second screendump using the persistent connection. Evidence is `out/dm33/continuation-20261005/qmp_preflight.result.txt` and its two PPM captures.

The separate phase baseline reports remain applicable for unchanged behavior: DM32's GUI GPT repair qualification, prior Quick Reformat qualification, GPT Repair service regression, Delete Partition service regression, and current ATA/AHCI/4Kn transport tests. DM33's cooperative orchestration still needs targeted real-device lifecycle regressions before phase closeout.

## Remaining gates and next step

Before Outcome A, first rebuild and capture the post-fix rendered Format focus, then activate that real widget and qualify several live USB progress repaints, actual rendered Cancel with unchanged image/zero formatter writes and publication Flushes, retry, successful format plus mount/file I/O and restart verification, USB removal and same-capacity replacement safety, close behavior, duplicate submission, and selection changes. Capture progress, QMP, serial, block statistics, and verifier evidence. Then update this report and `docs/DISKMANAGER_IMPLEMENTATION.md`, rerun required builds/tests as needed, and only then advance `.phase` to DM33→DM34.

Recommended DM34 direction remains Partition Wipe / Full-Volume Erase, only after DM33's runtime gates close. No DM34 work is included here.
