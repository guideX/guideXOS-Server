# guideXOS Server v0.3 — S22R13R3 Guest Input and Acceptance

**Disposition: `INPUT_OR_RUNTIME_CAUSE_UNRESOLVED` (Outcome C).** One bounded QAC cold boot of the unchanged corrected candidate confirmed the five-mount VFS state and `/Apps`/PacMan registration after Start-menu refresh. Investigation isolated why the prior large diagonal PS/2 pointer moves were dropped, and small relative steps moved the guest pointer to a screenshot-confirmed target. Start opened, closed, and reopened in the first click sequence, but a later bounded click retry did not open it despite QMP acknowledgments and an explicit button release. The remaining control path is not reliable enough to continue Settings or runtime acceptance, and the cause of that missed toggle is not established. UI acceptance stopped after that result.

The corrected candidate has four cold-boot observations across R1, R2, and R3. R1 still supplies two successful PacMan/Navigator runtime boots and one Settings System-page display. R3 does not add a Settings or PacMan runtime pass. This report does not authorize release-wide qualification or publication.

## 1. Repository and candidate provenance

- Repository: `D:\dev\guideXOSServer`; branch `main`; starting HEAD `4c03670ee2c65a120cf0eb56d69f68b342f77eff`.
- The tracked worktree was clean before guest work. The only tracked change from this phase is this report; it is committed locally after review. No source or test files changed.
- The 9,280 paths in the R2 pre-existing-untracked manifest remain present. All 56 R2 evidence files also remain present. R3 added 48 files under the evidence directory linked below. The untracked count before committing this report was 9,384.
- Candidate path: `out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`.
- SHA-256 verified before the QAC boot and again after it: `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af`.
- Build identity remains `0eed0ed2-df00-403a-9646-0dffb22be3bb`, profile `ServerV03PacManOnly`, candidate-only, source commit `ea786e2da4da01a57b1ab00429c8453edc9446ad`. The ISO was not rebuilt, patched, or replaced.
- No branch/worktree operation, `dist` modification, readiness-marker change, or QAC-repository change was made.

R3 evidence is retained in [the boot evidence directory](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/), including the exact launch vector, staged-artifact manifest, QAC snapshots, serial log, QEMU diagnostics, QMP transcript, ownership trace, helper, and screenshots.

## 2. What failed in the earlier input attempts

R1 Boot 2 and R2 Boot 3 used QEMU HMP `mouse_move dx dy` commands. The R1 transcript contains large deltas such as `mouse_move -510 218`, `mouse_move -400 400`, and `mouse_move 6500 -9000`; R2 tried `mouse_move -320 130`, `mouse_move -700 6300`, and `mouse_move -10000 10000`. HMP help and QEMU documentation identify `mouse_move` as a relative move command. These values were treated as coordinate-like displacements without first splitting them into safe packets.

The R3 QEMU configuration was inspected instead of assuming a tablet was present. QMP `query-mice`, HMP `info mice`, and the QEMU device tree showed one current `QEMU PS/2 Mouse` (`absolute:false`) and a PS/2 keyboard. There was no USB tablet, absolute pointer, or VNC display. The guest VGA framebuffer was 1280×800; screenshots were captured from that framebuffer with QMP `screendump`. QMP `input-send-event` returned `{}` for accepted requests, but that response alone does not establish that the guest UI changed. The event and routing semantics were checked against the [QEMU QMP Reference](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html) and [QEMU Monitor documentation](https://www.qemu.org/docs/master/system/monitor.html).

The inspected path was:

1. The phase-local PowerShell helper sent QMP commands over loopback. R3 used `input-send-event` for relative motion and explicit left-button down/up events; R1/R2 mostly used HMP `mouse_move`, `mouse_button`, and `sendkey`.
2. QEMU exposed relative PS/2 movement, not host screen coordinates. The helper used screenshot feedback to calibrate guest pointer position; it did not apply a stale host-window scaling factor.
3. `kernel/core/main.cpp` initializes the PS/2 mouse and registers/unmasks IRQ12. The R3 serial log records IRQ12 activity during input. `kernel/core/ps2mouse.cpp` decodes each packet, inverts Y, clamps each axis to ±50, and then discards a packet when *both* clamped axes are at 50 (lines 294–304). A large diagonal event therefore reaches the discard condition after clamping. `kernel/core/input_manager.cpp` polls the updated PS/2 position and maps it as relative input; `DisplayInputMapper::mapRelativePointer` adds the delta and clamps the virtual desktop position.
4. The screenshot loop captured the guest framebuffer after commands. R3 saved PPM screenshots and PNG previews alongside every material input sequence.

This confirms a guest-side edge case that explains the earlier large diagonal moves: after the per-axis clamp, a large diagonal can be classified as a corrupted packet and dropped. It does not prove that this is the only cause of the earlier Settings failures or the later missed Start toggle.

## 3. Bounded R3 control test and result

The phase-local helper was adjusted to reject any relative movement with an axis outside `-49..49`; movement to the Start target was split into small steps. The cursor moved from the desktop center to the Start button, and the screenshot confirmed its position. In the measured range, QMP `rel` `+40` on Y moved the visible cursor about 80 screen pixels; the earlier X `+80` event moved it about 100 pixels because the guest clamps the input to 50. The observed scale for safe deltas was approximately two screen pixels per input unit. These are measured results for this guest mode, not a host-coordinate conversion rule.

The same helper checks that the exact QAC reservation and child PID still match before sending input. A click sends left-button down, waits 500 ms, rechecks ownership, sends button up, and waits 500 ms. If an operation errors while the button may be down, its `finally` handler sends an up event only after revalidating the same reservation and child. No input is sent after QAC release.

The control sequence produced both positive and negative guest evidence:

| Action | Guest result |
| --- | --- |
| First click on the screenshot-confirmed Start target | Start menu opened; see [first open](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/screenshots/start-menu-pointer-first.png). |
| Second click, after the down/up pair | Start menu closed; see [closed desktop](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/screenshots/desktop-after-start-close.png). |
| Third click | Start menu reopened; see [repeat open](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/screenshots/start-menu-pointer-repeat.png). |
| Later bounded retry with the slower down/up timing | Start remained closed despite QMP acknowledgments and a recorded button-up; see [final retry screenshot](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/screenshots/start-menu-pointer-delayed-repeat.png). |

The first open/close/reopen cycle proves that mouse input reached and changed the guest UI. The later missed toggle means the same control was not reliable across the bounded retry. The rate-limited serial IRQ12 messages are not timestamped per button transition, so they cannot resolve whether that missed event was lost in QEMU's PS/2 delivery or later in guest widget processing. No additional blind UI attempts were made.

Keyboard input did not provide a reliable fallback. A direct QMP key event did not open Start. HMP `sendkey meta_l` opened Start in one attempt, but repeats sometimes closed it or left the screen unchanged; `tab` dismissed the menu and `down` did not reliably navigate it in R2. No undocumented key binding was used.

### Follow-up for the guest-control tooling owner

Do not repeat a full Server acceptance boot with the same helper sequence. First add a bounded, state-verified click probe to the guest-control helper:

1. Reproduce with the same candidate, QEMU 11.0.0, `-display none -vga std`, and the sole current relative PS/2 mouse. Use the R3 screenshot-confirmed Start target and one QAC-owned guest.
2. Record the QMP `query-mice` result, exact down/up TX/RX, helper timing, and a guest screenshot after each transition. The observed QMP returns were successful, but a later screenshot remained on the desktop.
3. Keep the movement split to at most 49 units per axis. Before each test, neutralize any potentially held button only after confirming the same reservation and QEMU child still own the guest. Do not inject after release.
4. Have the helper stop on the first state mismatch and report the last confirmed state. A control test passes only after three consecutive screenshot-verified open/close transitions, with button-up confirmed after every click.

The unresolved boundary is QMP button delivery versus guest widget processing; QEMU documentation says the optional device routing fields matter when input routing or multiple same-kind devices are configured. R3 observed one current mouse and no configured tablet, so the absence of a device selector is not yet a confirmed cause. The test above should determine whether a targeted QMP input route or guest-side event tracing is needed. No QAC scheduler, admission, process cleanup, or isolation code change is proposed.

## 4. R3 QAC and guest execution

QAC 2.0.0 was checked immediately before admission. It was online with zero QEMU processes, zero active sessions, zero queued reservations, and zero blocked requests. One acceptance reservation was requested and admitted; no other owner's guest was interrupted.

| Item | R3 result |
| --- | --- |
| Reservation | `bbea71e6df654a998a8350d8c37186ad`, project `guideXOSServer`, mode `acceptance` |
| Owner | `LEON2_RDT\guideX`, PID 15608; owner liveness `Verified` |
| QEMU child | PID 4868, QEMU 11.0.0 |
| Child start | 2026-10-24 01:52:14.4797996 UTC |
| Shutdown | Owned QMP `quit`; child exit code 0 at 2026-10-24 02:01:15.4686117 UTC |
| Release | QAC reported `Released`; final coordinator status: zero QEMU processes, zero sessions, zero queue, zero blocked requests |

The runner emitted a transient warning that its first process census was incomplete. Immediate reservation/list inspection showed the exact child PID, verified owner, and `degraded=false`; post-run `inspect` reported `Released`, exit reason `Managed child exited`, and owner liveness `Verified`. Full preflight, ownership trace, runner output, and post-run status/list/queue/inspect are in the evidence directory.

The guest used two vCPUs, 2048 MiB RAM, `pc,accel=whpx`, standard VGA, headless QMP screendumps, networking disabled, a read-only candidate CD-ROM, read-only OVMF firmware (SHA-256 `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`), and a fresh private writable 16-file ESP copy (tree SHA-256 `8bc7c6d0784162851ed5f155775da6cf8510e4f9965cf494fb193889764ea39c`). The exact QEMU arguments and image identities are in [qemu-launch.json](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/qemu-launch.json).

## 5. R3 startup results and acceptance not attempted

The guest reached the desktop. Startup diagnostics show the root mount at checkpoint A, then the five expected mounts (`/`, `/system`, `/certs`, `/config`, and `/config/navigator`) at checkpoints B–D. `/Apps` resolved on the root FAT32 volume, enumeration found PacMan and its full 956-byte manifest, and Start-menu refresh retained `registeredPacMan=yes` and `menuPacManAvailable=yes`. The [serial log](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/serial.log) records these checkpoints; the [Start-menu screenshot](evidence/s22r13r3-guest-input-acceptance-20261023/boot1/screenshots/start-menu-pointer-repeat.png) shows PacMan and Settings entries.

The desktop rendered and remained responsive to the successful Start open/close events. After the repeated click failed, the primary input gate was not met, so this boot did not continue into Settings, PacMan launch/gameplay/exit, Navigator `file:///Apps/PacMan/app.json`, or post-Navigator VFS checks. Those items remain unverified in R3; R1 evidence is not re-attributed to this boot.

## 6. Consolidated acceptance matrix

| Acceptance item | R1 Boot 1 | R1 Boot 2 | R2 Boot 3 | R3 Boot 4 |
| --- | --- | --- | --- | --- |
| Independent cold boot and desktop ready | PASS | PASS | PASS | PASS |
| Five mounts and `/Apps` | PASS | PASS | PASS at checkpoints B–D | PASS at checkpoints B–D |
| PacMan registration after Start refresh | PASS | PASS | PASS | PASS |
| PacMan launch, gameplay input, clean exit | PASS | PASS | Not attempted; entry selection blocked | Not attempted; input gate stopped the run |
| Navigator `file://` lookup and VFS intact afterward | PASS | PASS | Not attempted | Not attempted |
| Settings window and System page | PASS; returned to desktop | Incomplete | Not attempted | Not attempted |
| Settings functional page navigation/control interaction | Not recorded | Not verified | Not verified | Not attempted |
| Repeated Start control demonstrated reliable | Not evaluated with a bounded control test | No; pointer automation unreliable | No; pointer automation unreliable | No; one later click failed to change state |
| S22R12 mount corruption | None observed | None observed | None observed | None observed |

The R1/R2 details and direct evidence remain in [S22R13R1](RELEASE_S22R13R1_PACMAN_GUEST_ACCEPTANCE.md) and [S22R13R2](RELEASE_S22R13R2_FINAL_GUEST_ACCEPTANCE.md). R1 supplies the two previously accepted PacMan/Navigator boots. R3 contributes only startup/VFS/registration and bounded input evidence.

## 7. `/Documents`, `.startmenu_recent`, and release gates

R3 reproduces the `/Documents` startup failure: `create_directory_path_status` returns `FAT_CREATE_INVALID_NAME`; `make_short_name` rejects a base name longer than eight characters, and `Documents` has nine. The later desktop-folder enumeration reports no discovered standard folders. This is a confirmed product limitation for the standard Documents/File Explorer path, not a QEMU or test-environment artifact and not a harmless log message. Its release-blocker status depends on the promised scope of Files/Documents; the release owner must record that scope and disposition before calling it benign. No filesystem code was changed.

R1 continues to document the separate `/.startmenu_recent` persistence failure: the FAT writer rejects the dotted name with `FAT_FILE_WRITE_INVALID_NAME`. R3 did not exercise or fix that persistence path.

The existing architecture and security gates remain open:

- Native ELF runs with kernel privilege; application isolation is absent.
- Writable ESP packages can be discovered at runtime.
- Runtime ABI compatibility negotiation remains insufficient.
- Release-owner security disposition for package composition, writable-storage execution, third-party Native ELF, and ABI compatibility is pending.

## 8. Main Integration continuation

Keep the candidate hash unchanged. Resolve the guest-control follow-up above before spending QAC time on another acceptance run. Once a supported interaction path passes the three-cycle Start control test, use one storage-backed cold boot to complete the missing Settings page interaction and the critical PacMan/VFS/Navigator path. The run should stop at the first input state mismatch. In parallel, obtain release-owner disposition for `/Documents`, `.startmenu_recent`, and the outstanding security/compatibility gates.

R3 is Outcome C because pointer-movement failure was isolated and worked around, but the remaining missed Start click is not explained. Do not advance to S22R14 release-wide qualification or publish readiness markers on this evidence.
