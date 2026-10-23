# guideXOS Server v0.3 — S22R13R1 PacMan Guest Acceptance

**Disposition: `GUEST_ACCEPTANCE_DEFERRED`.** Two independent QAC-managed, storage-backed cold boots of the exact corrected S22R12 candidate completed. Both retained the five-entry VFS mount table and `/Apps`, registered PacMan, loaded and ran the Native ELF package, demonstrated live gameplay with keyboard input, exited to a responsive desktop, and loaded the PacMan manifest through Navigator's `file://` URL. Boot 1 also passed Settings smoke. Boot 2's Settings click was not completed because the headless QMP pointer path became unreliable while navigating the Start menu. No recurrence of the S22R12 mount corruption or other new product defect was demonstrated. One known legacy FAT sidecar write failure is recorded below.

This report records the runtime evidence gathered after S22R13 was deferred solely for QAC admission contention. It does not change the release candidate or authorize publication.

## 1. Starting provenance and candidate identity

- Repository: `D:\dev\guideXOSServer`, branch `main`.
- Preflight HEAD: `44243b6d47af5c969972f68daed0f5200958fbea`, the requested expected HEAD.
- The tracked worktree was clean at preflight. The 9,280 pre-existing untracked paths were preserved. No branch switch, reset, clean, stash, rebase, merge, cherry-pick, or push was performed. The repository has no server `.phase` guard; the sibling QAC ownership state was not edited.
- S22R12 and S22R13 reports were read before guest execution: [S22R12 VFS investigation](RELEASE_S22R12_VFS_MOUNT_INTEGRITY.md) and [S22R13 deferred acceptance](RELEASE_S22R13_PACMAN_CORRECTED_GUEST_ACCEPTANCE.md).
- Both boots used the exact S22R12 candidate at `out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`.
- SHA-256 before and after the runs: `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af`. This matches the S22R13 receipt. No ISO rebuild or substitution occurred.
- The original S22R9 ISO remained unchanged at SHA-256 `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`.
- No source, `dist`, or release-readiness marker was modified for this continuation.

## 2. QAC ownership and admission

The established exclusive QAC workflow was used. Reservation ownership was checked before each QEMU launch. Only the QEMU child under the current verified reservation was shut down; other owners were left untouched.

| Boot | Reservation / owner | QEMU child | Admission and release |
| --- | --- | --- | --- |
| 1 | `4f17779acbd14810b4deb3d26a3a20a3`; verified `LEON2_RDT\guideX` PID 21640 | PID 10628 | Initially queued once at position 1 behind another verified guest, then admitted at 2026-10-23 02:34:40 UTC. QMP `quit` produced exit code 0 and a clean QAC release at 03:01:30 UTC. |
| 2 | `702d260dd3cf43e387c714c78fafcabb`; verified `LEON2_RDT\guideX` PID 13496 | PID 18264 | QAC was free at preflight; admitted at 2026-10-23 03:03:45 UTC. QMP `quit` produced exit code 0 and a clean QAC release at 03:21:06 UTC. |

After Boot 1, QAC admitted reservation `df33741be5f142b3a0a027db32bfd73c` for another owner. Before Boot 2 shutdown, reservation `72c66d32bdf649daa836059d1d341120` was waiting; QAC admitted it immediately after our clean release. Both were left untouched. QAC scheduling, admission rules, and ownership mechanisms were not changed.

Reservation evidence is under [Boot 1](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/) and [Boot 2](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/), including preflight snapshots, reservation-owner records, runner logs, and post-shutdown QAC snapshots.

## 3. Guest configuration

Each run used a fresh QEMU process and a new private writable ESP directory copied from the existing candidate ESP. The 16 files in each private ESP matched the candidate tree hash `8bc7c6d0784162851ed5f155775da6cf8510e4f9965cf494fb193889764ea39c`. The shared ISO and OVMF firmware were read-only; no canonical ISO or shared ESP source was written.

- QEMU 11.0.0; machine `pc,accel=whpx`; AMD64; 2 vCPUs; 2048 MiB RAM.
- Exact candidate ISO as a read-only CD-ROM; OVMF firmware read-only, SHA-256 `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`.
- Fresh private writable directory-backed FAT ESP per boot; networking disabled.
- Serial capture, QMP control/screendumps, and QEMU guest-error diagnostics enabled.

The actual argument vectors and candidate/ESP identities are preserved in each boot's `qemu-launch.json`, artifact manifest, and staging record. These were independent cold starts, not warm resets or combined observations.

## 4. Independent cold-boot results

| Check | Boot 1 | Boot 2 |
| --- | --- | --- |
| Fresh QEMU process | PASS, PID 10628 | PASS, PID 18264 |
| Firmware and kernel | PASS; boot reached desktop | PASS; boot reached desktop |
| Desktop and input | PASS; desktop initialized and accepted QMP keyboard/mouse input | PASS; desktop initialized and accepted keyboard/mouse input |
| Startup typography smoke | `typography.phase7a.result=PASS` | `typography.phase7a.result=PASS` |
| Navigator startup smoke | `result=PASS` | `result=PASS` |
| Mount invariants and `/Apps` | PASS, details below | PASS, details below |
| PacMan from Start menu, gameplay, exit, rediscovery | PASS | PASS |
| Navigator `file://` URL | PASS | PASS |
| Settings | PASS | Incomplete: headless pointer automation did not reach the Settings item after the other checks |
| Unexpected panic / VFS mount loss | None observed | None observed |

Boot 1 evidence: [serial log](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/serial.log), [desktop](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/screenshots/desktop-after-boot.png), [Start menu with PacMan](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/screenshots/start-menu-pacman-exact.png), [gameplay](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/screenshots/pacman-after-15s.png), [Navigator result](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/screenshots/navigator-file-url-result.png), and [Settings](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/screenshots/settings-open.png).

Boot 2 evidence: [serial log](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/serial.log), [ready desktop](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/desktop-ready-35s.png), [Start menu with PacMan](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/start-menu-pacman.png), [gameplay after keyboard input](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/pacman-after-left.png), [desktop after game exit](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/pacman-exited.png), [Start-menu rediscovery](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/start-menu-after-game-exit.png), and [Navigator result](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/navigator-file-url-result.png).

## 5. VFS integrity and `/Apps` persistence

In both serial logs, diagnostics report the same five valid mounts before and after startup smoke and repeated Start-menu refreshes: `/`, `/system`, `/certs`, `/config`, and `/config/navigator`. The mount count remained 5; active aliases remained valid; device and mount generations matched; the ATA device remained present. No S22R12-style count/slot corruption, loss of `/Apps`, or mount-resolution failure recurred.

`/Apps` resolved to the root FAT32 volume with `volumeMounted=yes`, matching device/mount generations, and a successful directory traversal. Enumeration returned 3 entries, found PacMan, and read the complete 956-byte `/Apps/PacMan/app.json` manifest. Startup registration reported `registeredPacMan=yes`, count 1. The actual Start-menu refresh after PacMan exited continued to show PacMan in both screenshots.

Both logs report `typography.phase7a.result=PASS` and the Navigator startup smoke result `PASS`. The Boot 2 `file:///Apps/PacMan/app.json` interaction rendered the manifest after gameplay and exit; subsequent diagnostics still show the five-entry mount table and `/Apps` available.

## 6. PacMan launch and gameplay

PacMan was selected from the guest Start menu in each boot. The serial trace records `LaunchDispatch` to `NativeElfApp`, successful ELF64 AMD64 loading with three segments from `/Apps/PacMan/bin/amd64/pacman.elf`, complete game initialization, a compositor window of 480×640, and repeated presented frames.

- Boot 1: the visible PacMan surface rendered and progressed during the 15-second observation; keyboard direction input was delivered. ESC returned to the desktop with the app taskbar entry gone. The desktop remained responsive, and the subsequent Start-menu refresh still listed PacMan.
- Boot 2: the visible game showed score progression from 0 to 130 after a left-direction input; serial records the input down/up events and continuing frames. ESC exited with result `0x00000000`, `fault=none`, and `requested=1`. The desktop returned and PacMan remained in the refreshed Start menu.

This is direct launch, rendering, input, gameplay, exit, and rediscovery evidence; package registration alone was not used as a gameplay result.

## 7. Navigator and Settings checks

Navigator was launched from the actual guest Start menu in both sessions. In each, the address bar accepted `file:///Apps/PacMan/app.json` and Navigator rendered the package manifest. The serial diagnostics before and after the interaction retained the same mount table and `/Apps` resolution.

Boot 1 opened Settings from the Start menu. The System page displayed guideXOS, kernel version 0.1.0, and AMD64 (x86-64); closing it returned to a responsive desktop. Boot 2 Settings was not completed. The headless QMP pointer moved through the Start-menu navigation inconsistently and dismissed the menu before Settings could be selected. Keyboard input was available and passed for PacMan and Navigator, but this Settings selection step remains unverified in Boot 2. This is an automation limitation, not evidence of a Settings or candidate defect.

## 8. Failures and limitations

- **No recurrence of VFS mount corruption.** Both independent boots retained all five mount entries and `/Apps`; PacMan remained discoverable after refresh. No panic or guest storage/mount failure affecting the acceptance path was observed.
- **Known optional legacy sidecar write failure.** Both logs report failure to persist `/.startmenu_recent` with `FAT_FILE_WRITE_INVALID_NAME`; the FAT filename writer does not support this dotted filename form. The S9R assessment already records the same FAT compatibility limitation for legacy dotted sidecar paths ([assessment](SETTINGS_S9R_NATIVE_INTEGRATION_ASSESSMENT_20261011.md)). It does not change mount metadata, `/Apps`, PacMan registration, or the tested app flows. Recent-program persistence should not be claimed as passing.
- Startup also reports unavailable optional legacy desktop sidecar lookups before the configured paths are ready; required mounts and paths subsequently pass. These lookup messages are retained in the serial logs.
- QAC runner logs contain QEMU's host-side warning `Ignoring request for interrupt vector 0`; the guest reached desktop and completed the recorded runtime checks. No guest panic or corresponding product failure was observed.
- Boot 2 Settings selection remains incomplete due to headless pointer automation. QAC released cleanly, and the next reservation was left to its verified owner.

No source correction, candidate change, or causal claim relating these known limitations to the S22R12 stack-corruption defect is proposed.

## 9. Remaining release and security gates

These runtime results do not authorize publication. The S22R6–S22R13 architecture findings remain:

- Native ELF code executes with kernel privilege.
- Writable storage can introduce discoverable native packages.
- Application isolation is not implemented.
- Runtime ABI negotiation remains insufficient to protect against incompatible host-table layouts.
- The S22R12 stack-frame correction addresses the demonstrated mount-table overwrite; it does not solve these broader architectural limitations.

Release-owner disposition is still required for trusted package composition, writable-storage execution policy, third-party Native ELF support, and the ABI compatibility guarantees. RC publication/readiness gates remain separate and unchanged.

## 10. Recommended Main Integration continuation

Keep this candidate and its identity unchanged. When QAC is available, perform one more fresh storage-backed cold boot with the same ISO SHA-256 to complete the Boot 2 Settings smoke and repeat the critical mount, `/Apps`, Start-menu, PacMan launch/gameplay/exit, and Navigator checks. Use the established qualified interaction path or a graphical guest interaction method for Settings. If that run passes, pair it with Boot 1 as the two complete accepted sessions and update the disposition to `PACMAN_GUEST_ACCEPTED`. If a new failure appears, preserve the first failing operation and classify it by layer before proposing any source change. Do not rebuild or publish this candidate as part of that follow-up.
