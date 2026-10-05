# Disk Manager Phase DM33 — Long Operation UX

## Outcome and phase gate

**Outcome B.** DM33's cooperative blank-media job, immutable target binding, bounded progress accounting, safe pre-commit cancellation, deterministic tests, and production UI integration are implemented. The mandatory real guest UI qualification did not complete: although the proof kernel autolaunched Disk Manager and enumerated a disposable 600 MiB USB disk, the headless QEMU input path used here did not activate the disk row or action controls. Consequently there is no real USB progress/cancel, format lifecycle, removal/replacement, or close-during-operation qualification. `.phase` remains `last_completed=DM32`, `next_expected=DM33`.

## Starting state and architecture

The phase gate was checked in `D:\dev\guideXOSServer_DiskManagerImprovements`: branch `DISK_MANAGER_IMPROVEMENTS`, starting HEAD `d9afed96763a2be238d1b58d8b4519d725dc0ff9`, clean worktree, `.phase` DM32→DM33, no existing DM33 report, and no cached ahead/behind divergence. One normal SSH fetch attempt failed with `Permission denied (publickey)`; remote freshness is unknown. The starting point followed the user's TortoiseGit sync.

The desktop uses an event loop and kernel-app `update()`/draw dispatch; no suitable kernel worker or general job scheduler was found for this UI operation. The implementation uses one bounded cooperative step per Disk Manager update. Storage services already provide the destructive-operation lease, registration/incarnation pin, target identity revalidation, block transfer-size limits, and private KnownZero authorization. The job owns its target and result, so later UI selection does not supply operation identity. Application shutdown requests safe cancellation; the headless qualification gap means the user-visible active-close behavior remains unverified.

## Job and progress model

`Fat32FormatJob` binds the operation kind, full format request and exact target/partition snapshot, operation lease/pin, logical geometry, start tick, scan offset/total, result, and cancellation/commit state. The job is statically embedded in Disk Manager; it is bounded and the deterministic size check confirms it is under 2 KiB. One active destructive operation is allowed by the existing exclusive lease and job state guard.

States are Idle, Scanning, ReadyToCommit, Completed, Failed, and Canceled. Each update reads one transfer bounded by the 1 MiB scan buffer and the transport's maximum transfer. Progress is derived from exact scanned logical sectors/bytes, clamped to total, and exposed as phase plus percent and verified amount. The UI presents a progress bar and textual status, and exposes Cancel only while the scan can stop safely. A distinct READY_TO_COMMIT state creates an observable cancellation boundary after the final scan transfer. Formatter commit disables cancellation and consumes the private one-use KnownZero token.

Cancellation is cooperative at a bounded I/O boundary. It invalidates any in-progress authorization, releases the held operation lease and target pin, and performs no formatter writes or publication Flush. Nonblank, read error, removal, and identity changes retain distinct service results. Closing/shutdown calls cancel; the active-close explanation and behavior need a real GUI run before acceptance.

## Deterministic evidence

The final storage suite passes **987 checks, 0 failures**. USB transport checks include 249 checks; USB trace classifier tests pass 7/7, and the verifier fixture test passes. Added tests cover cancellation before I/O, during scan and at the final-transfer boundary; immutable request binding; single-job/reentrant begin; monotone exact progress and retry; nonblank media; 4Kn; and removal/replacement identity protection. The scan remains full-range, detects nonzero bytes, and does not enlarge the bounded buffer. These tests establish service-level safety, not guest GUI acceptance.

Logs are in ignored `out/dm33/`, including `storage-suite-final.log`, `kernel-amd64-build-final.log`, `uefi-x64-release.log`, and `build-bat.log`. The production AMD64 kernel build passed, `build.bat` passed, and UEFI x64 Release passed. `build.ps1 -Arch amd64` retains its known unrelated PacMan undefined-symbol failure (`pacman_audio_load_resources` and `pacman_audio_submit`); no PacMan code was changed. NVMe write and Flush proof flags remain zero; no physical host storage was used.

## QEMU attempt and limits

An explicit proof-only kernel hook launched the normal Disk Manager resolver and logged success; the hook was removed from `kernel/core/main.cpp` after the run. QEMU UHCI enumerated the disposable blank 600 MiB image at `out/dm33/gui-proof/usb-blank-600m.raw` as a writable device. The initial guest screenshot is `out/dm33/gui-proof/usb-cancel-initial.png`. Keyboard and attempted absolute-pointer QMP events failed to move the selection from the boot disk, so no long scan was started. Thus there is no measured DM33 USB or AHCI throughput, no progress repaint count, no zero-write/Flush cancellation observation, no KnownZero runtime evidence, no successful FAT32 mount/file-I/O/restart, and no real hot-remove/replacement proof. The QEMU block statistics at this point had zero USB writes; they are only a pre-operation baseline, not cancellation proof.

The separate phase baseline reports remain applicable for unchanged behavior: DM32's GUI GPT repair qualification, prior Quick Reformat qualification, GPT Repair service regression, Delete Partition service regression, and current ATA/AHCI/4Kn transport tests. DM33's cooperative orchestration still needs targeted real-device lifecycle regressions before phase closeout.

## Remaining gates and next step

Before Outcome A, qualify on a working guest input route: several live USB progress repaints, actual rendered Cancel with unchanged image/zero formatter writes and publication Flushes, retry, successful format plus mount/file I/O and restart verification, USB removal and same-capacity replacement safety, close behavior, duplicate submission, selection changes, and a faster AHCI run. Capture progress, QMP, serial, block statistics, and verifier evidence. Then update this report and `docs/DISKMANAGER_IMPLEMENTATION.md`, rerun required builds/tests as needed, and only then advance `.phase` to DM33→DM34.

Recommended DM34 direction remains Partition Wipe / Full-Volume Erase, only after DM33's runtime gates close. No DM34 work is included here.
