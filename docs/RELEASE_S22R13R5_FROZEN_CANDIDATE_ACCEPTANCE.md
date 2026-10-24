# guideXOS Server v0.3 — S22R13R5 Frozen Candidate Acceptance

**Disposition: FROZEN_CANDIDATE_GUEST_ACCEPTED (Outcome A).** One storage-backed cold boot of the unchanged PacMan-only candidate completed the focused guest acceptance path. The guest booted to a responsive desktop, retained the five-mount VFS state and /Apps, passed the bounded Start open-close-reopen gate, opened Settings and navigated to Display, ran PacMan with observable gameplay response, exited with a production runtime-cleanup marker, loaded the PacMan manifest through Navigator, and returned to Start with PacMan still listed. Together with the two successful R1 PacMan gameplay and Navigator boots, this closes the focused guest acceptance gate. It allows transition to S22R14 release-wide qualification; it does not authorize publication.

The /Documents and recent-history limitations, and the Native ELF release-owner decisions below, remain open. Their disposition belongs in release-wide qualification.

## 1. Repository provenance and scope

- Repository: D:\dev\guideXOSServer, branch main.
- Starting and ending HEAD: 454c8f14dc2048a7174cc517c3499ef62c521a5f.
- Initial tracked worktree changes: 0. Final tracked worktree changes: 0.
- The previously reported 9,876 untracked paths were preserved. The final untracked count is 10,009: the preserved baseline plus 132 files in the R5 evidence directory and this report.
- The server repository had no .phase file. The sibling QAC .phase file was read and left unchanged; it still records QAC3 open and Next QAC3C3.
- No branch, worktree, release source, kernel, ISO, dist, publication state, or release-readiness marker was changed. No push or publication occurred.
- The requested S22R12 through S22R13R4 reports were reviewed. R1 supplies two successful candidate gameplay and Navigator boots; R2 has an incomplete Settings repeatability check; R3 documents oversized relative PS/2 movement rejection and an unresolved later Start click; R4 qualifies the paced input method only on its diagnostic v2 image.

This report and run evidence are left as untracked phase artifacts. A local commit was not made because the acceptance evidence bundle includes the raw per-run logs and screenshots and remains a reviewable working-tree artifact.

## 2. Frozen candidate and receipt verification

The only boot image was:

D:\dev\guideXOSServer\out\release-profile\0eed0ed2-df00-403a-9646-0dffb22be3bb\artifacts\guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso

Its SHA-256 before and after the QAC run is:

b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af

The before and after full-hash captures are [candidate-hash-before.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/candidate-hash-before.txt) and [candidate-hash-after.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/candidate-hash-after.txt). The ISO sidecar receipt has the same hash. The adjacent release manifest identifies the ServerV03PacManOnly candidate-only profile and exactly one Native ELF package, PacMan. The candidate ESP app directory list contains PacMan only; Developer Studio is absent. Build ID is 0eed0ed2-df00-403a-9646-0dffb22be3bb; source commit is ea786e2da4da01a57b1ab00429c8453edc9446ad. These values and the private ESP file inventory are recorded in [preflight.json](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/preflight.json).

R4 diagnostic v2 was not used as guest acceptance evidence. Its ISO SHA-256 is 0ac0c662775e20f23a12e5b7377cb54d4bb27b7e1b9c5727699196c55a78eb96. The R5 launch vector names the frozen candidate above as a read-only CD-ROM; no diagnostic image was attached.

## 3. QAC ownership and guest configuration

Immediately before admission, QAC was online with no active reservation, QEMU process, or queued request. One exclusive acceptance reservation was admitted:

- Reservation: 565c10df59bd46aba0d04ac3b87564e7.
- Verified owner: LEON2_RDT\guideX, host PID 3324.
- QEMU child: PID 21340, C:\Program Files\qemu\qemu-system-x86_64.exe, started at 2026-10-24 14:45:42.6634062 UTC.
- The guest shut down through the owned QMP endpoint at 2026-10-24 15:00:09.6760753 UTC. QEMU exit code was 0; QAC released the reservation.
- Post-run QAC was online with zero QEMU processes, zero active sessions, and zero queued or blocked reservations. The matching QEMU process count was zero.

The run used QEMU 11.0.0 with machine pc,accel=whpx, two vCPUs, 2048 MiB RAM, standard VGA at 1280×800, headless display with QMP screendumps, networking disabled, read-only OVMF firmware, and the exact candidate ISO as a read-only CD-ROM. A fresh private writable directory-backed FAT ESP was used for the guest's storage. Its 16-file inventory and per-file hashes are in preflight.json. Serial, QEMU diagnostics, QMP commands, and ownership checks were captured under [boot1 evidence](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/); the full launch vector is [qemu-launch.json](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qemu-launch.json).

The preflight, live reservation, QAC runner output, post-run status/list/queue, process identity, and input-ownership trace are preserved in the same directory. See [qac-run.log](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qac-run.log), [qac-post-status.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qac-post-status.txt), [qac-post-list.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qac-post-list.txt), [qac-post-queue.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qac-post-queue.txt), and [qac-ownership-trace.jsonl](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qac-ownership-trace.jsonl). QEMU logged its host-side warning “Ignoring request for interrupt vector 0”; the guest completed the required interactions and exited cleanly.

## 4. Transferred QMP input method and Start control gate

The R4 phase-local helper was copied unchanged into the R5 evidence directory. Its SHA-256 is 8083cb0a9db9441ab8124bb4fb250b8b44b138d18090d5248d0ee48003c23366, matching the R4 helper. R5 did not change the kernel or the helper. The [R5 helper](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qmp-input.ps1), [QMP transcript](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qmp-transcript.jsonl), and ownership trace retain the exact action sequence.

The transferred contract was:

- Use the guest relative PS/2 mouse path. Split movements so neither axis exceeds 49 units. R4 reached the Start target near (54,780) with move 10,10, twelve move -49,30 actions, then move -8,10. R4 Start-to-Settings used six 49,-40 steps plus 2,-2; System-to-Display used four -46,-49 steps, four 0,-40 steps, then -1,-2. The PacMan menu target was near (150,627). Accepted packets in the R4 trace were about 0.46–0.50 seconds apart. R5 reused the Start approach 10,10 / twelve -49,30 / -8,10, then used screenshot-guided corrections 25,-15, 20,-20, -22,15, and 0,5 before clicking. Later actions used the same bounded step size and screenshot-confirmed targets.
- Take a guest framebuffer screenshot to confirm the target before clicking. R4 also confirmed the PacMan menu row near (150,627). R5 used screenshot feedback and small corrections where the cursor was not initially visible at the edge; it did not reuse a presumed coordinate without visual confirmation.
- Each click sent left-button down, waited 500 ms, revalidated the same QAC reservation and QEMU child, sent left-button up, then allowed 500 ms for the UI response. If the helper failed while ownership remained valid, its fail-safe attempted button release. Ownership was checked before every input action. All UI assertions below rely on visible state transitions or guest runtime records, not QMP acknowledgments alone.

The required candidate input gate passed:

| Transition | Guest evidence |
| --- | --- |
| Open Start | Click completed at 14:49:47.503 UTC; [start-menu-after-first-click.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/start-menu-after-first-click.png) shows the open menu. |
| Close Start | Click completed at 14:49:56.541 UTC; [desktop-after-start-close.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/desktop-after-start-close.png) shows the menu closed. |
| Reopen Start | Click completed at 14:50:04.243 UTC; [start-menu-after-reopen.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/start-menu-after-reopen.png) shows the menu open again. |

Each transition was performed once with the bounded helper. The closed and reopened states were confirmed visually before continuing.

## 5. Guest acceptance results

### Boot, desktop, and VFS

The frozen candidate completed UEFI boot, entered kernel_main, initialized the framebuffer, and rendered the desktop and taskbar. The [initial desktop screenshot](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/desktop-initial.png) corroborates the guest-visible state; serial diagnostics provide the storage evidence.

Serial checkpoints after startup, Start refreshes, and Navigator interaction consistently report five valid mounts: /, /system, /certs, /config, and /config/navigator. The ATA device remains present and mount/device generations match. /Apps resolves on the root FAT32 volume with volumeMounted=yes. Directory enumeration returns three entries, the complete PacMan manifest is 956 bytes, registeredPacMan=yes, registeredCount=1, and Start-menu availability remains true. No S22R12 mount corruption recurred. See [acceptance-extract.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/acceptance-extract.txt) and the full [serial log](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/serial.log).

### Settings

Settings opened through the Start menu and rendered the System page; [settings-open.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/settings-open.png) shows the page. The Display entry was selected and [settings-display-page.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/settings-display-page.png) confirms the Display page. Its framebuffer details showed the active 1280×800, 32-bit display configuration.

The existing System and Display navigation worked in both directions. See [settings-system-after-display.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/settings-system-after-display.png) and [settings-display-after-navigation.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/settings-display-after-navigation.png). Settings closed through its visible close control; [desktop-after-settings-close.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/desktop-after-settings-close.png) shows desktop recovery.

### PacMan launch, gameplay, and exit

PacMan was visible in Start before launch. The menu row target was confirmed from the guest screenshot, then PacMan was launched through the normal Start workflow. The dispatch record identifies Nexgen PacMan as a NativeElfApp. The production loader reports PASS for ELF64 AMD64, three segments, and an app-owned stack at /Apps/PacMan/bin/amd64/pacman.elf.

The first post-launch capture was still on the desktop while initialization continued; after a bounded additional wait, [pacman-after-launch-wait.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/pacman-after-launch-wait.png) showed the initialized game window. A paired left-key event produced visible gameplay progress: the score changed from 0 to 70 in [pacman-after-left.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/pacman-after-left.png).

Escape returned to the desktop, shown in [desktop-after-pacman-exit.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/desktop-after-pacman-exit.png). Unlike R4's visual-only close, R5's production serial log also records “PacMan Escape pressed” and “lifecycle PASS window/resource cleanup complete.” Runtime cleanup is therefore confirmed for this candidate boot.

### Navigator lookup and final Start/VFS state

Navigator opened from Start. The address field displayed the exact URL file:///Apps/PacMan/app.json; see [navigator-address-entered.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/navigator-address-entered.png). After Enter, [navigator-file-url-result.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/navigator-file-url-result.png) showed the PacMan manifest. Serial confirms /Apps/PacMan/app.json opened, the parser began with 956 bytes, completed parsing, and Navigator load completed.

Navigator closed to the desktop; see [desktop-after-navigator-close.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/desktop-after-navigator-close.png). Start was reopened after the app and Navigator interactions. [start-menu-all-programs-final.png](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/start-menu-all-programs-final.png) visibly lists Nexgen PacMan and Settings. The final serial checkpoints still report all five mounts, /Apps mounted, and PacMan registered. The final menu and VFS checks pass.

## 6. Consolidated qualification matrix

R1, R2, R3, and R5 used the same unchanged candidate ISO: guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso, build ID 0eed0ed2-df00-403a-9646-0dffb22be3bb, SHA-256 b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af. R4's interaction result below belongs only to its diagnostic v2 ISO.

| Series run | Exact image identity | Result and evidence boundary |
| --- | --- | --- |
| S22R13R1 Boot 1 | Frozen candidate; hash above | Candidate cold boot, VFS and PacMan registration pass; PacMan gameplay and Navigator lookup pass; Settings System page smoke passed. |
| S22R13R1 Boot 2 | Frozen candidate; hash above | Candidate cold boot, VFS and PacMan registration pass; second PacMan gameplay and Navigator lookup pass. Settings selection was incomplete because the earlier pointer path was unreliable. |
| S22R13R2 Boot 3 | Frozen candidate; hash above | Boot, VFS, /Apps, and PacMan registration/Start refresh pass. Settings, PacMan runtime, and Navigator were not attempted because app selection was blocked by input automation. |
| S22R13R3 Boot 4 | Frozen candidate; hash above | Boot, VFS, /Apps, and PacMan registration pass. A bounded Start open-close-reopen sequence initially worked; a later Start click failed to change state. Settings and app-runtime checks stopped at that gate. R3 isolated oversized relative-movement rejection but left the later click cause unresolved. |
| S22R13R4 diagnostic v2 | Diagnostic-only ISO; SHA-256 0ac0c662775e20f23a12e5b7377cb54d4bb27b7e1b9c5727699196c55a78eb96 | Input-path qualification only: Start, Settings Display navigation, PacMan gameplay input, and visual app exit. It did not boot the frozen candidate. R4 boot1 diagnostic v1 was pointer calibration only and was not used for click assertions. Navigator in R4 was prior R1 evidence, not a v2 result. |
| S22R13R5 frozen candidate, evidence boot1 (Boot 5 in the candidate series) | Frozen candidate; hash above | Boot, stable five-mount VFS and /Apps, Start open-close-reopen, Settings Display and System↔Display navigation, PacMan launch/gameplay/exit with runtime cleanup, Navigator manifest lookup, final Start registration, and final VFS checks all pass. |

The R1 report retains the direct evidence for both successful PacMan gameplay and Navigator boots. R4 supplies a proven interaction procedure but is not counted as candidate execution. R5 is the first run in this series to complete the remaining Settings and critical application path on the exact frozen candidate.

## 7. Remaining functionality limitations

### /Documents

The guest serial log again records /Documents directory creation failing with FAT_CREATE_INVALID_NAME while the block layer reports BLOCK_OK. R3 established the cause: the FAT short-name generator rejects a base name longer than eight characters, and Documents has nine. Desktop standard-folder enumeration consequently did not discover the expected Documents directory. This is a product behavior limitation, not a QEMU artifact or harmless diagnostic.

The user-visible effect is that the standard Documents folder path is unavailable for expected file browsing and storage through the desktop/File Explorer workflow. Whether this blocks v0.3 depends on the promised Files/Documents release contract. If Documents is required by that contract, remediation is mandatory before release. Deferral is possible only if the release owner explicitly narrows or accepts the contract and records that disposition. R5 made no filesystem change and does not silently accept this limitation.

### /.startmenu_recent

The FAT writer continues to reject the dotted filename used for recent-program persistence with FAT_FILE_WRITE_INVALID_NAME and VFS_ERR_INVALID. Start-menu app discovery, PacMan registration, launch, and the tested menu flows still work. Recent-launch history cannot be relied on to persist through menu refresh or restart, so the Recent Programs view may be empty or stale. R5 did not modify the filesystem or classify this as fixed.

## 8. Native ELF security and architecture decisions

The guest acceptance result does not resolve the preserved S22R6–S22R13 design boundaries:

- Native ELF applications execute with kernel privilege.
- Native application isolation is absent.
- Writable application storage can introduce additional discoverable packages.
- Runtime ABI negotiation does not safely distinguish every incompatible application.
- Release packaging checks establish package composition, not runtime security.

The release owner must decide the trusted package composition, whether writable-storage packages may execute, whether third-party Native ELF applications are supported, and the ABI compatibility guarantees and enforcement policy. These decisions remain required for release-wide qualification regardless of this functional guest pass.

## 9. S22R14 entry disposition

Advance to S22R14 release-wide qualification with the focused guest gate recorded as FROZEN_CANDIDATE_GUEST_ACCEPTED. No further identical cold boot is indicated: R1 already contributes two candidate gameplay/Navigator boots and R5 closes the remaining candidate Settings and critical-path evidence gap.

S22R14 should record explicit release-owner dispositions for /Documents, /.startmenu_recent, trusted package composition, writable-storage execution policy, third-party Native ELF support, and ABI compatibility. Keep the candidate hash fixed during that work unless a separately authorized change is made. Outcome A does not authorize publication or changing readiness markers.

## 10. Evidence index and cleanup

Primary R5 evidence is under [docs/evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/). Useful artifacts include:

- [qemu-launch.json](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qemu-launch.json)
- [serial.log](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/serial.log)
- [qmp-transcript.jsonl](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qmp-transcript.jsonl)
- [qac-ownership-trace.jsonl](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/qac-ownership-trace.jsonl)
- [acceptance-extract.txt](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/acceptance-extract.txt)
- [final Start menu screenshot](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/start-menu-all-programs-final.png)
- [PacMan gameplay screenshot](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/pacman-after-left.png)
- [Navigator manifest screenshot](evidence/s22r13r5-frozen-candidate-acceptance-20261024/boot1/screenshots/navigator-file-url-result.png)

The QAC reservation ended with child exit code 0 and was released. Final QAC status, list, queue, and zero-process capture are preserved alongside the run. Full candidate SHA-256 before and after boot matched. Final tracked worktree state is clean; no commit was created and nothing was pushed.


