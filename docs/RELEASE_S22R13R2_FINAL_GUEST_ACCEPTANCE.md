# guideXOS Server v0.3 — S22R13R2 Final Guest Acceptance

**Disposition: SETTINGS_ACCEPTANCE_DEFERRED (Outcome B).** QAC admitted one independent cold boot of the unchanged PacMan-only candidate. Startup completed, the five-mount VFS table stabilized, /Apps resolved, PacMan registered, and Start-menu refresh retained PacMan. The headless input path did not reliably select Settings or the other Start-menu entries, so Settings page interaction, PacMan gameplay, and Navigator interaction were not completed in this boot. No S22R12 VFS corruption or candidate runtime regression was demonstrated.

This run preserves the S22R13R1 guest results and adds a third startup/VFS/Start-menu observation. It does not close the focused guest-acceptance gate or authorize publication.

## 1. Repository and candidate provenance

- Repository: D:devguideXOSServer; branch main; starting HEAD dd2f31fd7a99d0742531c3f8fdadf07efc396c2e, exactly the requested checkpoint.
- Tracked worktree was clean at preflight. The server repository has no .phase file. QAC remained on main at 96287244ec4d5cfc08150103938a6f0405dd11e6 with a clean tracked worktree. Its .phase remained unchanged: QAC1, QAC2, QAC3A, QAC3B, QAC3C1, and QAC3C2 accepted; QAC3 open; next QAC3C3.
- All 9,280 pre-existing untracked paths were preserved. The 56 new S22R13R2 evidence files are retained under [the run evidence directory](evidence/s22r13r2-final-guest-acceptance-20261023/); they were not staged with this report.
- The exact candidate was verified before execution and again afterward:
  - Path: out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso
  - SHA-256: b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af
  - Build ID: 0eed0ed2-df00-403a-9646-0dffb22be3bb; profile ServerV03PacManOnly; candidateOnly=true; source commit ea786e2da4da01a57b1ab00429c8453edc9446ad.
- No ISO rebuild or substitution occurred. The original S22R9 ISO remains SHA-256 e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b. No source, dist, or release-readiness marker was changed.

The full post-run repository and hash check is in [final-state.json](evidence/s22r13r2-final-guest-acceptance-20261023/final-state.json). Artifact staging identity is in [artifact-manifest.json](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/artifact-manifest.json).

## 2. QAC ownership and guest configuration

The existing exclusive acceptance workflow was used through QAC CLI run. At preflight QAC 2.0.0 was online with one verified StellarNavigator acceptance reservation and QEMU child PID 20132. That owner was left untouched; QAC reported zero foreign QEMU processes. Our one request queued at position 1 and was granted without a second queue attempt.

| Event | UTC result |
| --- | --- |
| Request / queue | 2026-10-23 22:35:03.3435673; reservation 1adb5ec436cf4aa5afb1ad44638d4a10; position 1 |
| Admission | 2026-10-23 22:35:06.0858387 |
| QEMU child | PID 9776; started 2026-10-23 22:35:06.1119421; C:Program Filesqemuqemu-system-x86_64.exe |
| Shutdown / release | QMP quit; child exit code 0 at 2026-10-23 22:44:39.0626388; QAC reported reservation Released |

The QAC owner was PID 21028, owner liveness Verified. The first launch line included a transient degraded-state warning because the QEMU process census was incomplete; the subsequent reservation inspection reported degraded=false and Verified ownership. QAC ended online with zero QEMU processes, zero active sessions, and an empty queue. The running and final QAC records are retained in the evidence directory.

The actual guest used QEMU 11.0.0 with machine pc,accel=whpx, two vCPUs, 2048 MiB RAM, std VGA at 1280×800, and headless display with QMP screendumps. Networking was disabled. The exact candidate ISO was attached read-only as CD-ROM; the OVMF image was read-only (SHA-256 2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792). A fresh private 16-file writable ESP copy was mounted as the IDE storage device; its pre-boot tree hash matched 8bc7c6d0784162851ed5f155775da6cf8510e4f9965cf494fb193889764ea39c. Serial, QEMU diagnostics, and QMP evidence were written to the per-run evidence directory. The actual QEMU argument vector is preserved in [qemu-launch.json](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/qemu-launch.json).

## 3. Cold-boot, VFS, and /Apps results

This was a fresh QEMU process and private ESP, not a warm reset or a continuation of either S22R13R1 guest.

- UEFI loaded the bootloader and kernel; the desktop initialized and rendered. The [desktop screenshot](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/screenshots/desktop-initial.png) shows the ready desktop.
- The startup typography smoke and Navigator startup smoke both reported PASS.
- Checkpoint A records the root FAT32 mount before the RAM-backed wallpaper volume and aliases are mounted. At checkpoint B the VFS table contains five entries: /, /system, /certs, /config, and /config/navigator. Checkpoints C and D retained the same five active entries and matching device generations.
- /Apps resolved through the root FAT32 volume. Enumeration found three entries, found PacMan, and read the complete 956-byte PacMan manifest. Registration reported registeredPacMan=yes and registeredCount=1. After the Start-menu refresh, diagnostics still reported menuPacManAvailable=yes and menuCount=16. The [Start-menu screenshot](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/screenshots/start-menu-pacman-settings-visible.png) directly shows PacMan and Settings in the menu.
- No mount-count/slot corruption, loss of /Apps, panic, or S22R12-style failure was observed.

The raw [serial log](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/serial.log) and [acceptance extract](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/serial-acceptance-extract.txt) retain the A–D checkpoints. QEMU logged its host-side warning “Ignoring request for interrupt vector 0”; the guest continued through desktop startup and the recorded checks.

## 4. Settings acceptance and input limitation

The guest Start menu opened with QMP keyboard input. The screenshots and [QMP transcript](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/qmp-transcript.jsonl) record these outcomes:

- Meta_L opened the Start menu.
- Tab dismissed the menu.
- Down left the Calculator row highlighted and did not move the selection.
- QEMU reported only a PS/2 mouse through info mice. The launch used display none and exposed no VNC endpoint. Relative HMP mouse movement did not reliably hover or select the Settings row.

Settings was not launched in S22R13R2. Its window rendering, functional-page navigation, an in-page control/navigation interaction, closing Settings, and post-close desktop responsiveness are unverified on this boot. These automation results are not evidence of a Settings product defect. The exact attempts are summarized in [settings-input-attempts.md](evidence/s22r13r2-final-guest-acceptance-20261023/boot3/settings-input-attempts.md).

S22R13R1 Boot 1 opened Settings and displayed the System page, then returned to a responsive desktop. Boot 2 did not complete Settings because pointer automation was unreliable. The R1 System-page observation remains accepted at the level reported there; it does not supply the in-page control/navigation interaction required for this S22R13R2 gate.

## 5. Critical runtime path comparison

S22R13R1 Boot 1 and Boot 2 each used a fresh storage-backed cold boot of the same candidate. Both passed PacMan discovery, launch, visible gameplay input/progress, clean exit, Start-menu rediscovery, and Navigator file:///Apps/PacMan/app.json lookup. Their VFS diagnostics retained the five mounts and /Apps after Navigator interaction.

S22R13R2 Boot 3 adds fresh startup evidence through Start-menu refresh: /Apps remains mounted and PacMan remains registered and visible. The input limitation prevented selection of an app entry, so PacMan launch/gameplay/exit and Navigator file:// lookup were not repeated. Boot 3 therefore does not count as another gameplay boot.

| Acceptance item | S22R13R1 Boot 1 | S22R13R1 Boot 2 | S22R13R2 Boot 3 |
| --- | --- | --- | --- |
| Independent cold boot; desktop ready | PASS | PASS | PASS |
| Five-mount VFS table and /Apps | PASS | PASS | PASS at checkpoints B–D |
| PacMan registered and present after Start-menu refresh | PASS | PASS | PASS at startup / refresh |
| PacMan launch, gameplay progress, and clean exit | PASS | PASS | NOT ATTEMPTED: entry selection blocked by input automation |
| Navigator file:// lookup; VFS intact afterward | PASS | PASS | NOT ATTEMPTED: entry selection blocked |
| Settings window / System page display | PASS; closed to desktop | INCOMPLETE | NOT ATTEMPTED |
| Settings in-page control or navigation interaction | NOT RECORDED | NOT VERIFIED | NOT VERIFIED |
| Desktop responsiveness after Settings close | PASS | NOT VERIFIED | NOT VERIFIED |
| S22R12 mount corruption | None observed | None observed | None observed |

References: [S22R13R1 report](RELEASE_S22R13R1_PACMAN_GUEST_ACCEPTANCE.md), [Boot 1 serial and screenshots](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/), and [Boot 2 serial and screenshots](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/). S22R13R1 remains the source for the two successful gameplay boots and Navigator checks; those observations are not re-attributed to Boot 3.

## 6. Known persistence limitation and other startup diagnostics

The S22R13R1 logs from both boots recorded the FAT writer rejecting /.startmenu_recent with FAT_FILE_WRITE_INVALID_NAME. S22R13R2 did not launch an app, so it did not exercise that write path. Its early startup trace does contain a “No mount point for path: /.startmenu_recent” lookup before the root volume was mounted. This does not establish a fix or a new writer result. Recent-program persistence remains open and is not claimed as passing; the R1 logs remain the direct evidence for the FAT filename limitation. It did not affect the required VFS, /Apps, PacMan discovery, or other completed checks.

A separate startup diagnostic reports creation of /Documents failed with FAT_CREATE_INVALID_NAME, followed by desktop-folder enumeration failure. This is outside the focused PacMan acceptance path; it is retained in the serial log for separate review if Documents/Files behavior enters release scope. No causal investigation or source change was made in this phase.

## 7. Security limits and release-owner decisions

This guest run does not change the existing architecture findings:

- Native ELF applications execute with kernel privileges.
- Writable application storage can introduce additional native packages.
- Runtime ABI negotiation is insufficient for all cross-version packages.
- No application process isolation has been established.

Release-owner disposition remains required for trusted first-party package composition, writable-storage execution policy, third-party Native ELF support, and ABI compatibility guarantees. Guest acceptance does not authorize publication.

## 8. Recommended S22R14 entry conditions

Keep the candidate identity unchanged and complete the outstanding focused acceptance with an interaction method that can reliably select Start-menu entries, such as a previously qualified manual graphical path or supported guest keyboard navigation. Capture a Settings page change or control interaction, close Settings, verify the desktop remains responsive, then complete one storage-backed pass through PacMan launch/gameplay/exit, Navigator file:// lookup, post-Navigator VFS state, and Start-menu rediscovery.

Enter S22R14 release-wide qualification only after that focused gate is closed and the release owners have recorded their security and compatibility decisions. Keep publication as a separate release-owner decision.
