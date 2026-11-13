# S22R22R1 — Persistent Documents Guest Acceptance Closure

**Outcome A — `PERSISTENT_DOCUMENTS_GUEST_ACCEPTED`**
**Acceptance date:** 2026-11-12 (America/Los_Angeles)
**Candidate:** S22R22 Model B public-preview PacMan-only AMD64 ISO

## Result

The unchanged S22R22 candidate completed the required Notepad save/reopen sequence and retained both documents across two independently QAC-managed cold boots on the same data image. The second boot reopened both files in Notepad, passed the approved PacMan lifecycle smoke, and shut down through the guest desktop. The runner recorded QMP quit acknowledgment, exact QEMU child exit, and QAC reservation release for both accepted runs.

The work closes ordinary document persistence for this test profile. It does not qualify crash or power-loss durability, application-settings migration, physical-disk use, or general Native ELF isolation.

## Source and candidate identity

| Item | Identity |
|---|---|
| Main checkout at start | `main`, `20c4564d5953627428f391af7b641af7e3b61c92` |
| Candidate source | `eb61ca10b48c0e96e1a04a302e82d6c52b828f41` |
| Build ID / profile | `3b07fcc2-581e-4a02-992c-31f2862e34e0` / `ServerV03PublicPreviewPacManOnly` |
| Candidate ISO | `E:\CodexArtifacts\S22R22\source-final-eb61ca10\out\release-profile\3b07fcc2-581e-4a02-992c-31f2862e34e0\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso` |
| ISO identity | 90,245,120 bytes; SHA-256 `efa290538ef3155d264bf0b26923933bd2c9332aa5574e5be977be9d7a9c948c` |
| Embedded EFI image | 90,177,536 bytes; SHA-256 `cde8f79d2202f5e2108470500a52295321355dd03163657ddc7c7b7995686348` |
| Kernel / ramdisk | `e4ef32d04fff2a89d9344c65bf26052509e331c241d0de9d8ee7f4837bd514b2` / `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| PacMan manifest / ELF | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` / `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |
| OVMF code | `C:\Program Files\qemu\share\edk2-x86_64-code.fd`; SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a` |

The release manifest marks this a candidate-only bootable UEFI ISO and lists only PacMan as a Native ELF package. Both accepted QAC manifests bind the same ISO and OVMF hashes. The candidate was not rebuilt or replaced, and no kernel or release-readiness files were changed.

## QAC failure attribution and recovery

Three runner issues were reproduced before the clean accepted pair. Their logs and workspaces remain outside the repository under `E:\CodexArtifacts\S22R22R1\qac`; none is counted as an accepted run.

| Attempt | First failure and guest state | QAC disposition |
|---|---|---|
| `s22r22r1-coldboot1-docs-20261112-6ee06519a0694e55a965ccdcce0bcd11` | After Notepad/PacMan interaction and normal guest shutdown, shutdown-event parsing accessed `.event` on an ordinary QMP response record that had no such property. The runner stopped before recording child exit/release. | Reservation `c69e1c30b030454c96a9d0ad453c81fb` later inspected as `Released`, reason `Registered child process exited`, nondegraded. |
| `s22r22r1-accepted-coldboot1-20261112-10aa71d8e55e4f56b9c1ed575ac39d3d` | The runner was started without an interactive TTY; stdin ended before guest acceptance. This was an invocation error, not a guest result. | The exact QAC owner, child, and listener were checked before a QMP quit was used to reconcile the startup. Reservation `51f2d7cecf9749aeb900f88c52aab7e3` inspected as `Released`, reason `Managed child exited`, nondegraded. |
| `s22r22r1-closure-coldboot1-20261112-6facfcf058d645238d853b0efa944825` and `s22r22r1-final-coldboot1-docs-20261112-f57657a8441f4e409e7e19d776c83237` | The desktop emitted the exact normal shutdown marker and QMP reported `SHUTDOWN guest=true reason=guest-shutdown`, but QEMU remained active after the configured 30/60-second wait. The launch arguments include `-no-shutdown`, so guest shutdown stops the VM without exiting the QEMU process. The runner was missing the final emulator quit step. | Reservations `66607a63b0034a48bfe56e96178299fe` and `312fa87f4c434bcb8db0534381dc47c3` later inspected as `Released`, reason `Registered child process exited`, nondegraded. These attempts are retained as runner failures, not passes. |

The runner now accepts this button-release race only when both the exact QMP guest-shutdown event and exact normal desktop serial marker are present. It revalidates the QAC reservation, direct child, and QMP listener, sends QMP `quit`, records its acknowledgment, and then verifies child exit and reservation release. Local input validation errors are tagged and recoverable at the console; ownership/protocol failures still propagate. The event parser tolerates QMP response and STOP records without an `event` property, mouse delta validation rejects overflow safely, and the input allowlist includes Backspace for real Save As editing.

The `RecoveryRequired` state in the failed attempt manifests was the runner's fail-safe state: it could no longer prove a complete supervised shutdown/release, so it preserved the workspace and refused normal completion. The first run stopped at QMP event parsing; the later attempts stopped at interactive-input EOF or the bounded child-exit timeout. QAC kept each reservation protected while its registered child was active. After owner-safe reconciliation and child exit, read-only `qac inspect` calls reported all four failed-attempt reservations `Released`, nondegraded; no unresolved reservation remained. The coordinator stayed `Online`, with zero QEMU processes, foreign processes, reservations, and managed sessions. This distinguishes the runner's unresolved completion record from QAC's later reconciled reservation state.

The focused QAC launcher suite passed **166/166** checks after the final fix. Its synthetic transcript includes a QMP return record, `SHUTDOWN`, `STOP`, and the VM-not-running button-release error; it verifies that QMP quit occurs only after the strict shutdown evidence. The live accepted boot also exercised this exact path.

## Persistent test volume and original document

One continuous image was used; it was not recreated, formatted, restored, or copied between accepted boots.

| Item | Evidence |
|---|---|
| Image | `E:\CodexArtifacts\S22R22\persistent-data\guidexos-user-data.img`, raw, 134,217,728 bytes (128 MiB) |
| Stable device identity | ATA serial `GXOS-DATA-V1`; guest selected it as `ata1s` |
| Filesystem | FAT32, label `GXOSDATA`, marker `GXOSDAT.ID`, and `APPDATA` directory |
| Guest paths | FAT root mounted at `/Documents`; `/AppData` aliases `/Documents/APPDATA` |
| Final image SHA-256 | `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148` |

The historical S22R22 test document retained in [the implementation report](RELEASE_S22R22_DOCUMENTS_IMPLEMENTATION.md) was 161 bytes. Its exact documented content was:

```text
Welcome to guideXOS Notepad!

File/Edit menus available.
Right-click for context menu.
Ctrl+S to save, Ctrl+O to open.

Type here...s22r22persistencepayloadalpha
```

That historical content had already been replaced by earlier incomplete S22R22/S22R22R1 attempts before the final accepted pair. At the start of the final accepted Boot 1, `/Documents/UNTITLED.TXT` contained the previously persisted 49-byte v4 test payload (`s22r22r1originaldocumentexactbytesalpha20261112v4`, SHA-256 `e6c9d544da5d8dc90d5bbed705a5d806bddfe5d48a8354a678f4a70f8e18fbaa`). It was opened and checked in Notepad before editing. The earlier task-start 47-byte fixture (`s22r22r1originaldocumentexactbytesalpha20261112`) and its hash are retained in the preceding QAC attempt evidence.

The historical document identity is 161 bytes. Reconstructing the exact displayed content above as UTF-8 with LF line endings and no trailing newline yields SHA-256 `6f51fdf8372847a50464fce5f8b007526581a40ea2632620f21ff3af8465fd2a`; this identifies the documented historical bytes, not a claim that those bytes remained on the disk when the final accepted pair began.

## Accepted cold boots

| Boot | Run / QAC identity | Guest result and lifecycle |
|---|---|---|
| 1 — edit and save | Run `s22r22r1-verified-coldboot1-20261112-b71364b697fd4ef0af3f06504c95a093`; reservation `3c9692d1d1a84016856c7a153ed8b08a`; QEMU PID 21316; QMP `127.0.0.1:58799`. | Runner state `Completed`, guest result `PASS`, child exit verified, reservation release verified, QAC CLI exit 0. QAC independently reports `Released`, child exit 0, nondegraded. |
| 2 — verify persistence | Run `s22r22r1-verified-coldboot2-docs-20261112-4ccee337b448462792814ac40acc991f`; reservation `efbb0c2516314b428afedc7885e16681`; QEMU PID 14448; QMP `127.0.0.1:62440`. | Runner state `Completed`, guest result `PASS`, child exit verified, reservation release verified, QAC CLI exit 0. QAC independently reports `Released`, child exit 0, nondegraded. |

The stopped image hash after Boot 1 was `4f1a6f0f4d35dc347b57268f95f56b63bd116c6aaf5fae482162b0aa69c4add1`. Boot 2 required that exact hash as its input and attached the same image writable. After Boot 2, QAC was stopped and `PyFatFS 1.1.0` opened the image with `read_only=True`.

### Boot 1 — real Notepad interaction

1. Serial confirmed `GXOS-DATA-V1`, FAT32 label `GXOSDATA`, `/Documents`, and `/AppData` → `/Documents/APPDATA`.
2. Notepad opened `/Documents/untitled.txt`; the visible v4 payload matched the pre-edit bytes above.
3. The payload was replaced through Notepad’s Edit → Select All and Save menus with `s22r22r1originaldocumentexactbytesalpha20261112v5`. Notepad was closed and reopened from the guest UI; the screenshot showed `/Documents/untitled.txt` with the v5 payload.
4. A new Notepad document was created and saved through Save As as `/Documents/second3.txt`, with payload `s22r22r1secondfilepersistdelta20261112`. It was closed and reopened in another Notepad instance; the title and payload were visible.
5. PacMan launched from the approved start-menu entry, received keyboard input, returned through Escape, cleaned up its window/resources, and emitted `[PACMAN-SMOKE] result=PASS proof=approved-entry-clean-return-presented-frame-key-events`.
6. The guest Shutdown action produced `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`. The runner observed QMP `SHUTDOWN`/`STOP`, sent identity-checked QMP `quit`, and completed QAC verification.

### Boot 2 — Notepad persistence check

1. The same device identity remounted at `/Documents` after a separate cold boot.
2. Notepad opened `/Documents/untitled.txt` and showed the exact v5 payload.
3. Notepad opened `/Documents/second3.txt` and showed the exact second payload.
4. PacMan again emitted the approved PASS marker after presented frames, keyboard input, clean return, and lifecycle cleanup.
5. Normal guest desktop shutdown, QMP quit acknowledgment, QEMU exit 0, and QAC release were verified.

Guest screenshots are in each run’s `output\screenshots` directory. Key files include `final-run1-original-reopened.ppm`, `final-run1-second3-reopened.ppm`, `final-run2-original-opened.ppm`, and `final-run2-second-opened.ppm`. The complete `execution.json`, QAC manifest, serial log, QMP transcript, and host execution log are retained under each run workspace named above.

## Independent byte verification and filesystem observations

Final read-only PyFatFS results after Boot 2:

| Path | Bytes | SHA-256 | Exact contents |
|---|---:|---|---|
| `/UNTITLED.TXT` | 49 | `780851faf6dcc16eef51b63dd46d7351ac8558b0db31a71be3b7744c6f7612c2` | `s22r22r1originaldocumentexactbytesalpha20261112v5` |
| `/SECOND3.TXT` | 38 | `8dbf3b7aba257e9b090dea1e9337857a2ee93eb494aa860584ff27be0790c165` | `s22r22r1secondfilepersistdelta20261112` |

The image root also contains earlier test artifacts, including `SECOND.TXT`, `SECOND2.TXT`, and `/APPDATA/UNTITLED.TXT`; they were not removed or treated as part of the accepted pair. No host share, USB/physical disk, network adapter, additional writable executable source, or guest COM2 host channel was attached.

## Native ELF security regression

`pwsh -NoProfile -File scripts\test-native-elf-security-policy.ps1` passed all **27** existing hosted policy cases (`failures=0`, `nativeEntryCalls=4`). The cases retain approved PacMan identity, resource and ABI checks, writable-source/alias rejection before native entry, and fail-closed package inventory behavior.

Both guest serial logs show the approved `/Apps/PacMan` package and clean lifecycle. The QAC manifest attaches the release ISO read-only and gives write access only to the separate test data image. No live guest spoofing attempt was made from the writable disk; that negative remains hosted evidence. The package-integrity policy does not establish process isolation or recovery from a Native ELF that stops responding.

## S22R23 handoff

No settings migration was started in this closure. The current source/report audit identifies these candidates and constraints:

| Data | Current implementation | S22R23 work |
|---|---|---|
| Notepad documents | Raw user bytes at the VFS path chosen through Save/Save As; accepted under `/Documents`. No separate Notepad settings file exists. | Keep documents in `/Documents`; surface exact write, flush, and no-space errors. |
| Desktop appearance | Shell reads/writes `/desktop.cfg`, `/display-options.cfg`, `/wallpaper.id`, `/desktop.wallpaper.id`, `/desktop.taskbar.position`, `/desktop.background.scale`, and `/desktop.system.icons`. Public-preview root is read-only; legacy display configuration remains unsupported. | Owner-select the supported subset, define schema/migration, and route approved writes under `/AppData` using FAT-supported 8.3 names. Preserve the read-only display boundary until separately approved. |
| Desktop icon layout / shortcuts | Save/load code exists for `/.desktop_icons` and `/desktop.shortcuts`; persistent writes under the current read-only root are not qualified. | Decide whether either feature is supported, then migrate only the chosen data and test full-shutdown reload and visible failures. |
| Settings / Control Panel | Settings model/UI state exists; the public-preview System view is read-only. No supported per-profile persistence path was qualified. | Add persistence only for owner-approved settings; do not infer persistence from UI state. |
| PacMan data | No supported score or settings save path was identified for this profile. | Make no persistence claim without a separate feature decision. |
| Recent Programs | `/.startmenu_recent` exists as an optional text path; earlier FAT 8.3 constraints prevented qualification. | Keep optional unless the owner changes its status. |

The writable data volume remains unauthorized as a Native ELF source. The release owner’s high-priority unresponsive-Native-ELF requirement needs separate evidence-based work on scheduling/preemption, termination, cleanup, and isolation; this closure did not redesign those systems.

## Limits and remaining owner decisions

This pass demonstrates ordinary file persistence after a Notepad save, normal guest shutdown, and an independent cold boot on the tested QEMU/OVMF profile. It does not establish power-loss durability, host-crash or forced-reset recovery, FAT32 transaction atomicity, physical-disk safety, or behavior on other hardware/hypervisors.

The existing release decisions remain separate: privileged Native ELF residual-risk acceptance, signing-key custody and verification-key distribution, distribution trust/channel, final deployment-contract approval, and publication authorization for an exact final artifact. This task did not promote or publish the candidate.

## Final QAC state

After both accepted reservations and all preserved failed attempts were inspected, QAC reported `Coordinator: Online`, zero QEMU processes (zero foreign), zero reservations, and zero managed sessions. Every reservation listed above is `Released` and nondegraded. The accepted runs each have runner state `Completed`, guest `PASS`, exact child exit verified, and reservation release verified.
