# S22R14R1R2 — QMP Startup and Stack Guest Qualification

## Disposition

**Outcome C — `QMP_STARTUP_BLOCKER_REMAINS`.** The confirmed empty-list parameter-binding defect is fixed in the tracked QMP helper, and the runner now records listener observations and distinguishes child exit from listener delay. Hosted checks pass. The second live run still does not establish why the configured listener was not recognized. Under the phase instructions, no further QAC reservation or guest attempt was made.

There were **zero candidate guest boots** in this phase. The ISO and its packaged kernel remain unchanged. Settings, COM2, PacMan, Navigator, and guest VFS behavior remain unqualified for this candidate.

## 1. Provenance, repository, and phase ownership

- Server repository: `D:\dev\guideXOSServer`.
- Starting branch and HEAD: `main`, `d3cd0f12b07e6028f5c41d4fa874586872415a77`, exactly the required HEAD. This is also the final HEAD before this phase's local commit.
- The candidate source commit `5b62216efe78158fb12707c126ec38582faecf59` is an ancestor of the required HEAD.
- The Server tracked worktree was clean at preflight. The Server repository has no `.phase` marker.
- Existing Server worktrees on `guideXOSServer-gfx1`, `guideXOSServer-vfs1`, and `guideXOSServer_INPUT_RELIABILITY` were left untouched. The separate QAC repository was on clean `main` at `1ea193258690f9e7b39390b101a117166ef8bbdd`; its phase marker remains QAC3 open, next QAC3C3. No QAC repository source, branch, or marker was changed.
- The recorded pre-existing inventory contains 13,029 untracked paths. All 13,029 still exist. A newer untracked file, `assets/Backgrounds/troncity.png`, appeared after that inventory was captured; it was left untouched and excluded from this commit. Existing ignored `out/` artifacts and the failed-runner copies were preserved; no `dist`, publication state, release manifest, or readiness marker was changed.
- No branch switch, worktree creation, reset, clean, stash, rebase, merge, cherry-pick, or push was performed.

## 2. Candidate identity and integrity

The exact existing stack-repaired candidate was used in both November 4 QAC launches. It was not rebuilt.

| Item | Verified value |
|---|---|
| Build ID / profile | `6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d` / `ServerV03PacManOnly` |
| ISO | `out/release-profile/6ad9bb77-12ce-4b4d-8ed0-56ff3944ee5d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso` |
| ISO size / SHA-256 | 90,245,120 bytes / `2fe2f5eda87c9a80164715a156af5de12c7cf3891d29a961c40012c9d032241e` |
| Kernel ELF SHA-256 | `b5e029d23f9c21b5ff0838d9388322bf95b211bf3ef98145ddd344170dfe95dd` |
| Candidate source | `main` / `5b62216efe78158fb12707c126ec38582faecf59` |
| Native ELF packages | PacMan only; Developer Studio is not selected and is absent from the ESP |

The ISO hash matches both the receipt and `.sha256` sidecar. All 16 ESP input files in the packaging receipt match their recorded sizes and hashes. The receipt records a bootable UEFI no-emulation image and an embedded EFI image SHA-256 of `ebaa999bf918040d12b3a7450cb68bdcc8e2ecd313155b216f3459a3656eeb41`. The kernel hash matches the receipt and the S22R14R1 stack-audit kernel. The previously accepted ISO (`b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af`) remains separate historical evidence and is not used to qualify this candidate.

## 3. Starting stack evidence

The existing receipt and static stack guard confirm the Settings and COM2 repairs are present in this candidate's kernel. The latest recorded estimates are:

| Path | Guarded usage | Headroom against 16,384 B stack |
|---|---:|---:|
| Settings modern Display | 11,512 B | 4,872 B |
| Settings legacy Display Options | 11,896 B | 4,488 B |
| COM2 storage service | 7,768 B | 8,616 B |

All three exceed the required 4,096 B headroom in the static guard. They are compiler estimates with enumerated callers and reserves, not measured runtime high-water marks or a whole-kernel proof. The candidate had no guest execution in this phase, so these do not establish runtime safety.

## 4. Prior reservation and QEMU launch reconstruction

The earlier reservation `3db20d3f6ad94171916b2a470a64ca01` expired before a child was launched. It produced no guest boot. The two subsequent QAC-managed launches are reconstructed from their execution records, QAC logs, child arguments, and retained post-run inspections:

| Run | Reservation / child | Sequence and first failure |
|---|---|---|
| 1 | `48a521ae53b54531bba42dfc06f05565` / PID `25808` | QAC was online with no sessions or queue. It granted acceptance, launched and registered the direct `qemu-system-x86_64.exe` child, and reported its PID, executable, and start time. The recorded command line included `-qmp tcp:127.0.0.1:53062,server=on,wait=off` and `-S`. The listener query produced an empty list. Passing that list to a mandatory PowerShell `Connections` parameter raised “Cannot bind argument … because it is an empty array.” The runner stopped before endpoint verification, TCP connection, greeting, capabilities, status, or `cont`. |
| 2 | `62db03804b484717bd7534ede8a77a77` / PID `16104` | A run-specific helper copy allowed empty arrays, so listener polling continued for the configured 15-second bound. QAC again reported a granted reservation and registered direct child. The QEMU arguments configured `127.0.0.1:62003` with `server=on,wait=off`, plus `-S`. The runner never accepted an owned listener and entered `RecoveryRequired` at `2026-11-04T23:16:52Z`. It did not connect to QMP or send any protocol command. |

The first run's exact exception proves the listener collection was empty at the failed probe. For the second, the saved runner returned only an ownership boolean; it did not preserve the socket rows or distinguish an absent listener from a wrong address, wrong PID, ambiguous result, query error, or late readiness. Its transcript, serial log, and screenshots are empty. The saved QEMU debug log contains only startup diagnostics and no recorded socket-bind error. Therefore the second failure cannot be classified more narrowly from retained evidence.

The phase runner's exact arguments configured QMP over loopback TCP. The QAC CLI contract grants the reservation before starting QEMU, creates QEMU as its direct child, registers the child, and reports reservation ID, child PID, executable identity, and start time. The CLI does not allocate or return a QMP endpoint; the runner supplies that endpoint in QEMU's argument list. No stale or incomplete QAC endpoint response was observed.

QEMU's `-S` means its virtual CPUs do not start at launch. It does not prove the host-side QMP listener exists or is ready. QEMU documentation describes `-S` as holding CPU execution after machine initialization ([QEMU managed startup](https://www.qemu.org/docs/master/system/managed-startup.html), [QEMU command line](https://www.qemu.org/docs/master/system/qemu-manpage.html)). A QMP connection must independently deliver a greeting, then accept `qmp_capabilities` before other commands ([QMP specification](https://www.qemu.org/docs/master/interop/qmp-spec.html)). Neither failed run reached those protocol stages.

## 5. Runner contract audit and root-cause disposition

The inspected S22R14R1R1 runner used a randomized loopback TCP endpoint, included it in the actual QEMU argument vector, and kept the guest paused with `-S`. After QAC reported a registered child, it validated the child PID, parent QAC CLI PID, executable, start time, private ESP, read-only ISO, and read-only OVMF arguments. It polled `Get-NetTCPConnection -State Listen -LocalPort <configured-port>` every 100 ms, with a 15-second deadline, and accepted a listener only when state, loopback address, port, and owning PID matched the QAC child.

Once that listener was accepted, the runner would connect, parse the QMP greeting, negotiate capabilities, verify the per-run QEMU name, and use `query-status` to confirm `prelaunch` or `paused`. It then sent one `cont` and required `query-status` to report `running`. Serial output was separately monitored for guest-side progress. QMP protocol success alone was not treated as guest boot.

The first defect is isolated and fixed: an empty listener list must be an ordinary “not ready yet” observation, not a parameter-binding exception. The second live failure is not isolated: the historical runner discarded the evidence needed to tell whether QEMU failed to bind, Windows listener discovery failed, ownership did not match, or readiness exceeded the bound. No evidence supports changing shared QAC infrastructure, widening the port search, or guessing a cause. The available evidence shows QAC admitted and registered both children according to its documented contract.

## 6. Tooling changes

The tracked server-side runner and helper received a narrow update:

- `Test-QacS22R10QmpListenerOwner` accepts an empty collection and returns false, allowing the caller's bounded retry to continue.
- `Get-QacS22R10QmpListenerObservation` distinguishes `listener-absent`, `listener-wrong-address`, `listener-wrong-owner`, `listener-ambiguous`, and `listener-owned-by-child`. The observation includes only the configured endpoint, count, and classification; it does not log unrelated process identities.
- The runner records an early child exit separately from listener timeout. At the startup deadline it records the configured endpoint, bounded probe count, last listener classification, and listener count. It also reports a changed listener owner as a specific fail-closed error after connection.
- A child exit during connection/greeting or capability/identity negotiation gets its own event and error stage.
- Immediately before continuation, the runner re-inspects the same QAC reservation and rechecks the exact child-owned listener. Continuation remains gated behind QAC reservation identity, direct-child identity, exact listener ownership, QMP greeting/capabilities, per-run QEMU identity, and paused status. A failed gate sends no `cont`.

The historical S22R14R1R1 run copies under `out/s22r14r1r1-stack-qualification/` were left intact as evidence. The tracked runner update was not deployed into those copies and was not exercised against QEMU. No QAC repository or coordinator source was modified.

## 7. Hosted runner checks

The existing PowerShell launcher suite passed **102/102 assertions** using synthetic process/listener records and controlled loopback QMP fixtures. Coverage includes empty listener results; delayed empty-to-owned observations; wrong PID, wrong address, and ambiguous listener results; per-run endpoint exclusion; direct-child and reservation identity checks; missing and malformed greetings; capability negotiation failure; QMP name validation; paused-state validation; a single paused-to-running fixture transition; bounded startup polling and diagnostics; child-exit separation; and source-order assertions that the runner rechecks its reservation/listener and sends one `cont` only after ownership and paused-state checks, then confirms `running`.

The fixtures did not start QEMU, contact the QAC coordinator, request a reservation, or reproduce the Windows socket table state from the failed QEMU children. The suite therefore verifies the fixed binding behavior, the new classification code, the protocol handler, and runner control-flow contracts; it does not resolve the second live listener failure.

## 8. QAC admission, ownership, and cleanup

Both actual launches had exclusive acceptance reservations. QAC logged the reservation grant and registered each exact QEMU child. Independent final inspections show both reservations `Released` with reason `Registered child process exited.` The runner's two execution records still say `RecoveryRequired` and do not claim child-exit or release verification, because the owner process did not report a final release line; those records are preserved. The later QAC inspections independently establish release. The concurrent Server acceptance reservation and AARCH64 queued request in the saved preflight were left to their own owners; their final records show the Server child exited and the AARCH64 request was cancelled by its client.

The final saved coordinator snapshot at `2026-11-04T23:22:12Z` was Online with zero reservations, managed sessions, queued requests, blocked requests, foreign QEMU processes, or QEMU children. A fresh read-only status/list/queue check after the hosted suite at `2026-11-05T04:02:36Z` again reports Online with zero managed sessions, reservations, queued requests, or QEMU processes. No reservation is held by this phase.

## 9. QMP handshake and continuation results

Neither live run reached a QMP connection. No greeting was received, no capability negotiation occurred, no `query-name` or `query-status` was sent, and no `cont` was sent. There was no risk of an unauthorized continuation command: both runs stopped before connecting, and the corrected tracked runner sends continuation only after all ownership and paused-state gates.

## 10. Guest boot count and evidence

The phase includes three reservation attempts across the preceding follow-up and this phase: the October reservation expired before launch, and two November reservations created QEMU children. The number of candidate guest boots is **zero**. `-S` kept the launched guests from executing CPU instructions because no continuation was issued. No UEFI transfer, kernel initialization, desktop startup, or guest-side progress was recorded.

## 11. Settings modern and legacy coverage

- **Settings modern Display:** not executed in the guest. The existing static guard is 11,512 B with 4,872 B headroom.
- **Settings legacy Display Options:** not executed in the guest. The existing static guard is 11,896 B with 4,488 B headroom.

Neither path has runtime stack high-water evidence. No modern or legacy result is inferred from the other path or from hosted tests.

## 12. COM2 storage service coverage

The COM2 service was not reached. Attempt 2 configured the existing COM2 endpoint at `127.0.0.1:49202`, but the runner never resumed QEMU and no service listener or guest response was observed. The non-destructive storage snapshot client was not run. The existing static guard remains 7,768 B with 8,616 B headroom; there is no guest invocation or error-path evidence.

## 13. PacMan, Navigator, and VFS regression

None of these guest checks ran. There is no observed Start-menu refresh, PacMan registration, UI launch, gameplay input, cleanup marker, Navigator launch, `file:///Apps/PacMan/app.json` lookup, `/Apps` availability, five-mount consistency, or post-interaction desktop/VFS responsiveness. The earlier accepted candidate's historical evidence remains tied to its older ISO hash.

## 14. Remaining stack qualification gaps

The precise acceptance checks still open are:

1. Stage the updated runner into a fresh phase-local qualification copy without altering the retained failed-run artifacts.
2. Obtain a fresh QAC snapshot and make one bounded QAC-managed startup attempt only after the updated listener observations are present. Capture the exact endpoint classification at each probe. Stop at the first failed handshake stage; do not retry unchanged startup behavior.
3. Verify QAC reservation, direct-child PID/parent/executable/start time, actual QEMU arguments, and exact listener ownership.
4. Complete greeting, capabilities, run-name, paused-state, single continuation, and QMP running-state checks.
5. Require guest serial progress before counting a boot; then execute the modern and supported legacy Settings paths, the safe COM2 storage workflow, PacMan and Navigator checks, and VFS integrity checks in that same owned session.
6. Preserve screenshots, serial, QMP transcript, QAC records, candidate hashes, and independently verified QEMU exit and reservation release.

If the next probe still cannot classify the actual listener state, stop and hand that exact snapshot to the qualification tooling owner. Do not infer a QAC coordinator defect without evidence that QAC's child-registration or process-identity contract failed.

## 15. Independent release blockers

These remain open and outside this QMP phase:

- `_wcsicmp` isolated-build reproducibility failure.
- `/Documents` functionality limitation.
- `/.startmenu_recent` persistence limitation.
- Kernel-privileged Native ELF execution.
- Writable application package discovery.
- Insufficient cross-version Native ABI negotiation.
- No enforceable Native ELF process isolation.

No blocker was accepted, remediated, or moved to release readiness here.

## 16. Recommended next Main Integration phase and repository closeout

The next phase should use the tracked listener-classification changes in a new isolated runner copy, preserve the exact `2fe2f5…241e` candidate, and acquire no more than one QAC reservation for a fully observable handshake. Continue into stack guest qualification only if ownership, protocol, paused state, and actual guest execution are all established. If the listener state remains absent or mismatched, stop and route the targeted endpoint/process observation to the QAC qualification tooling owner. Do not change shared QAC infrastructure unless that evidence demonstrates a coordinator defect.

The release remains blocked. This phase does not close either stack blocker or overall release acceptance.

The validated runner/helper/test changes and this report are committed locally on `main`. The report commit is local only; nothing was pushed. Pre-existing untracked evidence remains under its existing `out/` paths and was not added to Git; the newer `assets/Backgrounds/troncity.png` also remains untracked and untouched. The final commit SHA and final tracked worktree state are reported with the phase summary.
