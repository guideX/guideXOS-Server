# S22R14R1R4 — QMP Recovery and Guest Acceptance

**Outcome: `QMP_RECOVERED_GUEST_PARTIAL` (Outcome B).** The corrected runner established owned QMP sessions and continued the unchanged repaired candidate into guest execution. Guest boot and static VFS/package smoke checks passed. The live Settings modern and legacy paths, COM2 service, PacMan gameplay, and Navigator file-URL flow were not all demonstrated, so stack guest requalification is incomplete.

## Repository provenance and scope

- Repository: `D:\dev\guideXOSServer`, branch `main`.
- Baseline HEAD before this work: `7f4cb15ac0aec665f96a001e451c7176977c5e55`.
- Candidate manifest source commit: `5b62216efe78158fb12707c126ec38582faecf59`.
- The candidate ISO and canonical release inputs were read-only. No kernel or ISO was regenerated.
- The 13,029-path preservation inventory completed with zero missing paths. The pre-existing untracked `assets/Backgrounds/troncity.png` remains untracked and unchanged.
- No `dist`, release readiness, or publication state was changed. No branch switch, reset, cleanup, or push occurred.

## Candidate identity

The receipt-selected candidate is:

`out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`

| Input | SHA-256 | Result |
|---|---|---|
| ISO | `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` | Matches receipt and requested identity |
| Kernel | `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` | Matches receipt |
| OVMF | `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792` | Matches recorded input |
| ESP tree | `cf8fe881beb41ea5b3114a39d3dcd029cd9a96b129463bfe7dfffd9009bf4e4f` | All 16 receipt files match |

Receipt checks also found PacMan as the only Native ELF package, Developer Studio not selected, and all recorded release artifacts matching. These identities and per-file checks are retained in `out/s22r14r1r4-qmp-recovery-20261105/preflight/candidate-release-input-verification.json`. The earlier accepted ISO and its guest evidence were not substituted for this candidate's results.

## Port selection, exclusions, and listener ownership

`Get-QacS22R10QmpPortCandidate` binds an OS-selected ephemeral TCP port on IPv4 loopback (`127.0.0.1`), records that endpoint, and closes the temporary listener before QEMU binds it. QEMU receives the same loopback address and port. The three captured prelaunch `netsh interface ipv4 show excludedportrange protocol=tcp` snapshots showed exclusions at `80`, `443`, administered `50000–50059`, and dynamic `54596–55195`. The selected QMP ports `53835`, `50366`, and `49711` were outside those ranges. Each run recorded its chosen endpoint and QEMU's actual command line.

There remains an inherent check-to-bind race because the temporary socket must be released before QEMU can claim its port. The runner does not treat the probe as a reservation: bounded listener polling, direct QAC child identity checks, listener PID ownership, connection to the exact expected endpoint, and QMP protocol negotiation must all succeed. A mismatch fails closed. No Windows exclusions, dynamic ranges, firewall settings, or ownership guards were changed.

The R3 Windows discovery fixture's null-`State` case was encountered live: the targeted `-State Listen` query returned no row while an unfiltered scoped query found the endpoint. Discovery now accepts that row only as a connection candidate; the exact QAC parent/child, executable, start time, endpoint listener PID, and successful QMP connection remain required before control. A port-only match is not authorization.

## Runner tests and narrow corrections

The requested hosted suite first passed 125/125 assertions on the existing runner. The first live admission then exposed a missing set of mutable runtime fields in the listener-observation object (`ProcessAlive` and related diagnostics), which raised before QMP connection. Those fields were initialized on the returned object and a focused assertion was added.

The next live session showed the existing second-QMP-client input helper could not obtain a greeting while the runner's QMP connection was open. To keep control on the owned connection, the runner gained bounded relative mouse movement and paired clicks, with fresh QAC/child/listener ownership verification before control and before button release. A later live console interruption also exposed that an empty line could terminate the interactive loop; blank lines are now ignored. The key allowlist now includes only the additional QMP key names needed for the specified URL (`slash`, `dot`, `shift-semicolon`, and `shift-letter`). These are runner/operator changes; there were no kernel or release-artifact changes.

The final hosted suite passed **133/133** assertions. Output is retained at `out/s22r14r1r4-qmp-recovery-20261105/preflight/hosted-runner-tests-empty-key-fix-pass.txt`. Earlier failed and intermediate runs remain alongside it. `git diff --check` also passed before commit.

## QAC and QMP lifecycle

Three QAC-managed launch records were retained. The first did not reach QMP or `cont`; the next two reached guest execution.

| Run | QAC reservation / QEMU PID | Endpoint | Result |
|---|---|---|---|
| `s22r14r1r4-qmp-20261105-5cae6ff2-6e44f301d8564ebfabc2802eb2c1b346` | `80f54a7e1dd940e0a0657e4d31a4478d` / `23936` | `127.0.0.1:53835` | Runner failed before handshake when it assigned a missing `ProcessAlive` property. No `cont` was sent. `qemuExitCode` was not captured; QAC later reported `Released`, exact child exited, owner exited. |
| `s22r14r1r4-qmp-20261105-live-fix1-f3d08183a24b42c1b288cf099953626b` | `3d7d1c9b96b9450c9e91cdc26931b6d2` / `22460` | `127.0.0.1:50366` | Greeting, capabilities, instance name, and `prelaunch` status verified; ownership rechecked; `cont` sent; `running` confirmed. Guest desktop appeared. A separate input client timed out without sending input, so this guest was classified incomplete/tooling-failed and then shut down through the runner. QMP `quit` was acknowledged, QEMU exited `0`, exact release was verified, cleanup was eligible. |
| `s22r14r1r4-qmp-20261105-inputfix2-d866acddd3a74f3ea23e33e2cc6c4e55` | `5736effb619142fcbac2ff783bd68501` / `27004` | `127.0.0.1:49711` | Full greeting/capabilities/name/paused-status/ownership/`cont`/running sequence succeeded. The console sent relative pointer events and a paired left-button down/up. A blank console line during interaction diagnostics then raised `Cannot bind argument to parameter 'Text' because it is an empty string`; the runner exited while QEMU was active. The run's `qemuExitCode` was not captured. Independent QAC inspection afterward showed `Released`, exact child exited, owner exited, and no managed session remained. The run record correctly remains `RecoveryRequired`/not cleanup-eligible because it did not observe a verified QAC release itself; the evidence workspace was retained. |

The actual QEMU command lines and QMP transcripts are under each run's `output/`. In the successful starts, QMP returned the greeting, `qmp_capabilities`, `query-name`, and `query-status=prelaunch`; the runner reverified ownership immediately before the single `cont` and confirmed `query-status=running`. The final run's `execution.json` records first guest progress and the QMP event transcript. The final guest boot count is **two**; the first failed launch never continued into guest execution. No further live launches were made after the empty-line failure was isolated.

At closeout, QAC reported the coordinator online with zero reservations, zero managed sessions, and zero QEMU children. Reservations from the failed/ended runs were inspected as `Released`; no other agent's session was interrupted.

## Guest evidence and remaining coverage

The successful guest sessions reached firmware, kernel, desktop initialization, and the desktop. The second run's `desktop-ready.png` shows the flower wallpaper, Trash and File Explorer icons, and the taskbar. The pointer moved visibly to the taskbar; QMP acknowledged the paired click, but no post-click screen was captured before the operator interruption. Keyboard response was not exercised in a live guest after the URL-key fix.

Serial evidence from the final run is in `runs/s22r14r1r4-qmp-20261105-inputfix2-d866acddd3a74f3ea23e33e2cc6c4e55/output/serial.log`:

- The S22R11 application checkpoints show five mounted VFS entries (`/`, `/system`, `/certs`, `/config`, `/config/navigator`), consistent generations, and a valid `/Apps` lookup.
- `/Apps/PacMan/app.json` opened; its manifest was complete; PacMan's Native ELF was discovered; `registeredPacMan=yes` and `menuPacManAvailable=yes` remained true after the Start-menu refresh (`menuCount=16`).
- The built-in `[NAVIGATOR-SMOKE] result=PASS` marker and typography checks passed. This is kernel smoke evidence, not evidence that the Navigator UI opened the requested file URL.
- No `S22R12`, `KERNEL-PANIC`, panic, or exception marker appeared in the captured serial log. No storage corruption was reported by the recorded mount/package checkpoints.

| Required path | R1R4 result |
|---|---|
| Settings modern Display | **Not runtime-qualified.** The desktop booted, but the interrupted run did not open Settings or exercise Display. Static guarded usage was 11,512/16,384 bytes with 4,872 bytes estimated headroom. |
| Settings legacy Display | **Not runtime-qualified.** Source inspection identifies the existing guest path Start menu → Control Panel → Display Options (`kernel/core/desktop.cpp`, including the Control Panel item handler); it was not opened in the guest. Static guarded usage was 11,896/16,384 bytes with 4,488 bytes estimated headroom. No legacy qualification is inferred from the modern path. |
| COM2 storage service | **No runtime invocation.** The release QEMU configuration exposes the guest's COM1 through `-serial file:...` and has no COM2 chardev/backend or guest diagnostic surface. The existing host-side bridge smoke launches a different kernel configuration, so it cannot establish this candidate's COM2 execution. Static guarded usage was 7,768/16,384 bytes with 8,616 bytes estimated headroom. |
| PacMan | **Registration and VFS only.** Package discovery and Start-menu registration survived refresh. The Start entry was not visually opened; gameplay input, clean app exit, and a runtime cleanup marker were not observed. |
| Navigator | **Kernel smoke only.** The UI was not opened and `file:///Apps/PacMan/app.json` was not entered or displayed. The new URL key tokens passed hosted assertions but were not exercised in a live session. |

Stack values are compiler-derived/static guard estimates, not runtime high-water measurements. The stack guard passed with the 4,096-byte minimum; its output is `out/s22r14r1r4-qmp-recovery-20261105/preflight/stack-budget-guard.txt`.

## Independent release blockers carried forward

- `_wcsicmp` isolated-build reproducibility failure
- `/Documents` functionality limitation
- `/.startmenu_recent` persistence failure
- Kernel-privileged Native ELF execution
- Writable package discovery
- Insufficient cross-version Native ABI negotiation
- Missing Native ELF application isolation

## Recommended next Main Integration phase

Keep this candidate and the QMP recovery evidence fixed. Use one bounded, QAC-owned guest session to exercise the now-supported mouse and URL-key controls through Settings modern Display, the existing Control Panel → Display Options legacy route, PacMan launch/gameplay/exit, and Navigator's requested file URL. Capture a screenshot and guest-side evidence after each path. Treat COM2 as a separate explicit coverage decision: the current release guest configuration has no COM2 route, so runtime qualification requires an already-supported invocation path or separate authorization to establish one. Do not infer COM2 completion from the static guard or the other stack paths.

The narrow R1R4 commit closes the specific runner initialization and input-console gaps. It does not close the repaired-kernel guest qualification blockers or change release readiness.
