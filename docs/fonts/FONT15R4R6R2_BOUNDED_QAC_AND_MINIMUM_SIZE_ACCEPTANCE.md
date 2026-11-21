# FONT15R4R6R2 — Bounded QAC and Minimum-Size Fonts Acceptance

**Latest status: `FONT15R4R6R2_QAC_UNAVAILABLE`.** The latest retry passed Gate 1 (119/119 host tests) and verified the production candidate (24/24 files), but its single bounded QAC request remained queued and was canceled and reconciled by exact ID. No guest was launched in that retry. A prior retry did boot the candidate and reach Settings → Personalization → Fonts, but did not reach the required 560×360 geometry or exercise the minimum-size controls. `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open. Broader v0.3 release gates remain open.

## Historical baseline attempt — `FONT15R4R6R2_QAC_UNAVAILABLE`

**Outcome: `FONT15R4R6R2_QAC_UNAVAILABLE`.** Gate 1 passed. The bounded QAC request remained queued behind another managed guest through the 180-second admission window, then was cancelled and reconciled by its exact request ID. No guest was launched, and no minimum-size Fonts controls were exercised. The `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` gate remains open.

## 1. Repository state and scope

- Checkout: `D:\dev\guideXOSServer_FONT_SUPPORT`
- Branch: `FONT_SUPPORT`
- HEAD at task start: `05fdcc925b9cc39ec467186e28586356edfa8ea7`
- Initial working tree: clean; no staged, unstaged, or untracked changes.
- Implementation commit: `198764d19fc5d5b130d0afd9503413a4f9887aa7`
- Production kernel, PS/2, desktop input, compositor, Settings, and TrueType sources were not changed. The production-source delta from the accepted candidate revision is empty; no full OS rebuild was needed.

The historical R6 and R6R1 artifact directories were preserved. The R2 candidate, runner records, and QAC snapshots are under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2`.

## 2. Gate 1 — Host acceptance runner

### Bounded QAC admission integration

`tools/fonts/font15r4r6r2_acceptance.py` is the R2 guest acceptance entry point. It starts one QAC `run --project guideXOSServer --mode acceptance -- <QEMU> <arguments>` process, writes the long-lived process output to files, discovers the reservation ID by matching the QAC run owner PID, and delegates the sole admission decision to `wait_for_owned_admission` in `out/font15r1-validation/guest/font15r1_qac_admission.py`.

The entry point has no direct or unmanaged QEMU launch fallback. A request must have an exact reservation ID, matching owner PID, verified owner, and a fresh, non-degraded `Running` child before guest input is authorized. Queued state never authorizes a guest. The controller uses the original monotonic deadline, bounds coordinator calls and polling, cancels only the exact request, and reconciles cancellation against authoritative QAC state. Late or ambiguous ownership blocks input and requires reconciliation. Reservation snapshots now preserve QAC request/grant timestamps, owner and child start times, and child executable identity.

The R6 PowerShell wrapper remains a preserved historical artifact outside the repository. As documented by R1, it could block reading the long-lived QAC run output before deadline cancellation and reconciliation. R2 neither calls nor falls back to that wrapper; its QAC command is launched with file-backed output and its request lifecycle is handled by the bounded controller. Deterministic runner tests exercise QAC launch construction, owner-specific request discovery, late responses, cancellation races, exact-request cleanup, and failure-closed behavior.

### Pointer-settling integration

The R2 interactive path obtains a fresh initial framebuffer and cursor measurement, verifies QMP ownership, then invokes the qualified `settle_pointer` helper. It permits one click or wheel action only when fresh observations place the cursor inside the requested hit region before the deadline. Partial, ambiguous, stale, overshot, timed-out, or disconnected states block the action; failed input is not retried. Session input also requires a currently authorized QAC admission.

The integration tests exercise the runner's click authorization and wheel path, including a settled click, partial and ambiguous movement, delayed completion, loss of QMP ownership, deadline expiry, and prevention of duplicate clicks. Cleanup tests verify QAC release and closed endpoints, and ensure a foreign process reusing an endpoint is not touched.

### Host regression results

Command executed after the final implementation changes:

```text
python -B -m unittest tests.test_font15r2r1_qmp_safety tests.test_font15r1_pointer_settling tests.test_font15r1_qac_admission tests.test_font15r4r6r2_acceptance_runner -v
```

| Suite | Result |
|---|---:|
| Existing QMP safety | 46/46 |
| R1 pointer settling | 18/18 |
| R1 QAC admission | 15/15 |
| R2 runner integration | 31/31 |
| **Total** | **110/110** |

The first three suites reproduce the R1 baseline of 79/79. All 31 new runner integration tests passed. No test was skipped to hide a failure.

Later guest attempts exposed two host-runner gaps. The QMP endpoint verifier now waits, within its existing bound, for an empty listener table to populate while still rejecting a nonempty table owned by another PID. The R2 session now has a bounded `drag_control` path that requires a fresh settled cursor at the resize grip, sends one QMP drag, and uses only fresh read-only observations to confirm the destination. The full documented host suite passed **119/119**, with focused QMP/R2 runner suites at **86/86** and focused drag tests at **7/7**. The final full-suite log is under `retry3\host-tests\full-gate-119-tests-after-fixture-bound.txt`. The silent QMP fixture's timeout was increased from 200 ms to 1 second after a full-suite scheduling flake; the test still asserts timeout and transport closure. No production OS code changed.

## 3. Gate 2A — Production candidate prepared

The candidate was copied from the previously qualified R6 production candidate, not from the instrumented diagnostic kernel:

- Candidate root: `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2\candidate\candidate`
- Source candidate: `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6\run1\candidate\candidate`
- Candidate production-source HEAD: `ec221271e1d0c2f918202c56ba899b497cb172ae`
- Source execution HEAD: `5d4449e92e1e895cca1d60535a3857340af3f156`
- Candidate manifest: 24 files; all 24 hashes matched the source candidate with no missing, extra, or changed files.
- Kernel SHA-256: `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`
- Bootloader SHA-256: `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0`
- OVMF SHA-256: `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792` (read-only pflash)
- QEMU: version 11.0.0; SHA-256 `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`
- QAC CLI SHA-256: `268dfe0a8a315aa2912c67665df77061f51609b65a94a0596024c12b114800f0`
- Networking disabled; candidate ESP used as a disposable writable copy; initial framebuffer configured to 1280×800.
- Planned QMP endpoint: `127.0.0.1:62897`; planned display endpoint: `127.0.0.1:35697` (TCP 41597).

The launch plan and full file manifest are preserved as `guest\qemu-launch-plan-font15r4r6r2.json` and `guest\candidate-preflight-font15r4r6r2.json` in the R2 artifact directory.

## 4. Gate 2B — QAC unavailable; no guest interaction

The initial QAC preflight was online and idle. Before the R2 request was granted, another managed acceptance guest occupied the coordinator. The R2 runner made one request and did not enqueue replacement requests:

- R2 reservation: `9ea2001777534e4db9e616e5b651cc1c`
- R2 owner: `LEON2_RDT\guideX`, QAC run PID 2292; owner start `2026-11-21T16:03:12.1017734Z`
- QAC `requestedAtUtc`: `2026-11-21T16:03:12.2252294Z`
- Configured admission window: 180 seconds.
- The request remained `Waiting` through the 180-second window. Deadline cancellation and authoritative reconciliation completed at approximately `2026-11-21T16:06:12.527Z`, 180.43 seconds after runner start; the final cancellation/reconciliation work returned about 0.43 seconds after the configured deadline.
- Final exact-ID inspection: `Cancelled`, reason `Cancelled by client`, no `grantedAtUtc`, no child PID, and no child executable identity. Guest input authorization was false.

At final census, the other reservation remained `Running`: reservation `0e67363c4453403ea844834b0c0b176b`, owner PID 21756, QEMU child PID 18436. R2 did not issue reservation-specific inspect/cancel calls, shutdown, or input against it. QAC reported one managed session, zero queued reservations after R2 cancellation, and the R2 QAC owner process had exited.

R2's planned QMP and display ports had no listeners after cleanup (ports 62897 and 41597). Since no R2 child was admitted, the authentic candidate did not boot. There are no R2 guest serial, QMP interaction, framebuffer, cursor-settling, or Fonts-control observations to report. The historical R6 geometry and Roboto results remain preserved evidence; they are not counted as R2 minimum-size interaction acceptance.

## 5. Acceptance status

| Acceptance area | R2 result |
|---|---|
| Bounded QAC integration and runner tests | Passed, 110/110 host tests |
| Exclusive QAC ownership | Not obtained within the configured window; exact request cancelled and reconciled |
| Authentic AMD64 boot and guest pointer preflight | Not run |
| Fonts at 560×360 / 560×336 client area | Not exercised in R2 |
| Roboto render and family selection | Not exercised in R2 |
| Font-size controls and preview scrolling | Not exercised in R2 |
| Back/navigation, Apply, Cancel, bitmap fallback, Restore Defaults | Not exercised in R2 |
| Guest-side hit-testing and final stability check | Not exercised in R2 |
| R2 QAC owner and endpoints | Owner exited; both planned endpoints have no listeners; other managed guest left running |

`TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open. R2 confirms no production defect; it also does not qualify the remaining minimum-size guest interactions. Resume those interactions only after a future bounded request obtains exclusive QAC ownership.

## 6. Preserved evidence and changed files

R2 host and coordinator evidence is retained under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2\guest`, including the launch plan, candidate manifest, host gate results, runner JSONL events, QAC stdout/stderr, prelaunch and final QAC status/list/queue snapshots, exact-ID terminal inspection, process cleanup census, and endpoint census. No R6 or R6R1 evidence was overwritten.

Implementation commit `198764d19fc5d5b130d0afd9503413a4f9887aa7` contains the reviewed runner, controller/QMP support, and tests. This report is committed separately. Changed repository paths:

- `tools/fonts/font15r4r6r2_acceptance.py`
- `out/font15r1-validation/guest/font15r1_qac_admission.py`
- `out/font15r1-validation/guest/font15r1_qmp.py`
- `tests/test_font15r1_qac_admission.py`
- `tests/test_font15r4r6r2_acceptance_runner.py`
- `docs/fonts/FONT15R4R6R2_BOUNDED_QAC_AND_MINIMUM_SIZE_ACCEPTANCE.md`

## Gate 2 Retry 1 — Production Guest Acceptance

### Attempt and repository preservation

- Attempt: `retry1`, started `2026-11-21 17:19:12 UTC` (`09:19:12 PST`); report closeout followed at `2026-11-21 17:21 UTC`.
- Checkout: `D:\dev\guideXOSServer_FONT_SUPPORT`, branch `FONT_SUPPORT`, starting HEAD `28cd2f3b0cfa138d9e557871208a1838a0126781`.
- The checkout was clean before execution. The mandatory host suites were run with `python -B`, and the checkout remained clean before this report update. No reset, stash, branch switch, worktree, fetch, pull, or push was used.
- The report is the only intended repository change for this retry. Guest, candidate, and test artifacts are isolated under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2\retry1`; previous R2 evidence was not overwritten.

### Gate 1 — Host qualification

The documented suites were rerun and passed **110/110**: QMP safety 46/46, pointer settling 18/18, QAC admission 15/15, and R2 runner integration 31/31. The captured output is `host-tests\font15r4r6r2-gate1-110-tests.txt`. No host-runner or production changes were made. Historical typography suites FONT6/FONT10/FONT11/FONT14 (1,052/1,052 combined) are referenced from prior qualification and were not rerun for this retry.

### Production candidate

- Candidate source identity: production-source HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae`; source execution HEAD `5d4449e92e1e895cca1d60535a3857340af3f156`.
- The retry used a separate writable candidate copy at `retry1\candidate\candidate`; read-only OVMF and the qualified AMD64 production kernel remained the previously verified artifacts.
- The prelaunch manifest contained 24 files. All 24 matched with no missing, extra, or changed files. The post-run manifest also contained 24 files with no changes. Kernel SHA-256: `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`; bootloader SHA-256: `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0`; OVMF SHA-256: `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`.
- QEMU was `11.0.0`, SHA-256 `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`. QAC CLI SHA-256: `268dfe0a8a315aa2912c67665df77061f51609b65a94a0596024c12b114800f0`.
- The six tracked Roboto package files present in the current checkout after the candidate source revision were compared byte-for-byte with the candidate ESP copies; all six matched. No kernel, Settings, PS/2, compositor, or window-management source change was found, so no OS rebuild was made.
- The candidate ESP was the disposable writable medium; networking was disabled. Planned endpoints were QMP `127.0.0.1:62897` and VNC `127.0.0.1:35697` (TCP 41597).

### QAC admission, launch, and first blocker

The final pre-admission census at `2026-11-21T17:19:00.1695592Z` showed the coordinator online with zero QEMU processes, zero managed sessions, zero queued or blocked requests, and no listeners on either planned port. One bounded 180-second admission attempt was submitted through the qualified R2 runner.

- Reservation `63dbd3db57034956877c4c569ddbfb1c` was granted immediately at `2026-11-21T17:19:12.4382476Z` to verified owner PID 28616 (`LEON2_RDT\guideX`). The authorized direct child was PID 22608, `C:\Program Files\qemu\qemu-system-x86_64.exe`, started at `2026-11-21T17:19:12.5568776Z`.
- The runner failed closed while constructing its QMP session: at `2026-11-21T17:19:12.841700Z`, no listener was present on port 62897 for the expected child PID 22608. The exact error is preserved in `qmp-transcript.jsonl` and `runner-start-outcome.json`. No guest input was authorized.
- The QAC child exited after a reported eight-second lifetime; the serial log is empty (0 bytes), and no screenshots were captured. The runner's QMP connection attempt found no listener; the endpoint was closed in the final census. The desktop, framebuffer, pointer, and kernel-loaded state therefore are **not confirmed**. Do not count this as a successful guest boot.

### Guest acceptance results

No pointer movement or click was sent. No screenshot or cursor measurement was obtained, so no minimum-size geometry, Roboto rendering, family selection, font-size change, preview scrolling, Back, Apply, Cancel, bitmap fallback, Restore Defaults, or final stability result is claimed. The previous minimum-size geometry and Roboto evidence remains historical evidence only and was not repeated or promoted by this retry.

| Requirement | Retry result |
|---|---|
| Authentic kernel and desktop boot | Unconfirmed; QMP endpoint unavailable, serial log empty |
| 560×360 outer / 560×336 client Fonts view | Not exercised |
| Roboto and family/provider selection | Not exercised |
| Font-size increase/decrease and preview update | Not exercised |
| Preview scrolling and bounds | Not exercised |
| Back/navigation, Apply, and Cancel | Not exercised |
| Explicit bitmap fallback and Restore Defaults | Not exercised |
| Pointer settling and visual hit-test agreement | Not run; no input authorized |

### Cleanup, regressions, and outcome

Exact-ID QAC inspection reported the owned reservation `Released` because the registered child exited; QAC reported the owner exited. The QEMU child PID 22608 was absent at cleanup. The owner PID was later reused by an unrelated process, which was left untouched. Final QAC status showed zero managed or foreign QEMU processes, zero managed sessions, zero queued or blocked requests, and no reservations. Both planned endpoints were closed. No QMP quit or process termination was sent by this retry.

No host-runner or production code changed, so the 110-test Gate 1 result remains the only newly executed regression suite; the 1,052 typography result remains historical. No production defect is established. The exact blocking operation is runner QMP ownership verification immediately after timely admission. The retry outcome is **`GUEST_MINIMUM_CONTROLS_INCOMPLETE`**, not QAC unavailable and not minimum-size acceptance. `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open.

Retry evidence includes the launch plan, candidate preflight and post-run manifests, source-font hash comparison, host test log, QAC pre-admission and final census, runner JSONL events, QAC stdout/stderr, serial log, QMP transcript, endpoint census, and start-failure summary, all under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2\retry1\guest` (host tests under `retry1\host-tests`).

### Closeout

- Changed repository file: this report only.
- Commit: documentation-only update on `FONT_SUPPORT`; no source, candidate, or generated artifact was committed.
- Top-level Server release build, PacMan audio-link prerequisite, combined integration, persistence/storage, security, public preview, and publication authorization remain separate open release gates.

## Gate 2 Retry 2 — Production Guest Acceptance

### Candidate, host gate, and admission

- Attempt: `retry2`, started `2026-11-21T17:37:59Z`; checkout branch `FONT_SUPPORT`, HEAD `28cd2f3b0cfa138d9e557871208a1838a0126781`.
- The isolated candidate copy matched all 24 authoritative manifest entries before launch. Kernel, bootloader, OVMF, and QEMU hashes matched the qualified R2 artifacts. The candidate ESP was writable and disposable; networking was disabled.
- The listener-start race fix was qualified before guest execution. The focused QMP/R2 runner suites passed 79/79 and the then-current full host gate passed 112/112. Evidence is under `retry2\host-tests`.
- One bounded QAC request, `8a2fa07ff068496095e01709878281c0`, was granted immediately to verified owner PID 29944. QEMU child PID 30380 was registered to that reservation. QMP and VNC listeners were verified as owned by the child before input was authorized.

### Guest observations and resize blocker

The authentic production guest booted to its desktop. A fresh 1280×800 framebuffer was captured and the cursor was measured at `(640,400)`. Using the R2 settled-input path, the guest navigated through Settings → Personalization → Fonts. The Fonts page showed the existing family/provider list, Roboto choices, size display, preview, and Apply/Cancel/Restore Defaults controls at its default 760×500 outer size. No font setting was changed.

The resize grip was safely selected, but the R2 CLI did not yet provide an in-session drag operation. A second QMP client did not receive a QMP greeting; it sent no guest input. The window therefore was not resized to 560×360, and no minimum-size font controls were tested. Movement timeouts were handled only with read-only screenshots and cursor measurements; no movement or click was blindly repeated.

### Cleanup and result

The guest was shut down through the runner. QAC verified the exact reservation as `Released`, the owner and child exited, and the planned QMP/VNC endpoints were closed. A later unrelated managed guest appeared and was left untouched. Retry2 remains **`GUEST_MINIMUM_CONTROLS_INCOMPLETE`**: it confirms real guest boot and Fonts navigation, but does not close `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN`.

Retry2 evidence, including the launch plan, 24-file candidate verification, serial log, screenshots, cursor observations, runner/QMP traces, QAC release inspection, and endpoint census, is under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2\retry2`.

## Gate 2 Retry 3 — Bounded QAC admission

### Preparation and preflight

- Attempt: `retry3`, prepared `2026-11-21T18:09Z`, on `FONT_SUPPORT` at HEAD `28cd2f3b0cfa138d9e557871208a1838a0126781`.
- The candidate was copied to an isolated writable directory. All 24 authoritative manifest entries matched before and after the attempt, with no missing, extra, or changed files. The full kernel SHA-256 remained `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`.
- Preflight at `2026-11-21T18:10:00Z` found QAC online with one unrelated `guideXOSServer_TASKBAR_IMPROVEMENTS` guest running (reservation `c5a032e9fa47441ea330bed507098f32`, owner PID 30184, QEMU PID 24672), no queued work, and no listeners on retry3's planned ports. That guest was not inspected by reservation ID, canceled, shut down, or otherwise touched.
- The former QAC CLI path in the retry2 plan was no longer present. The plan used the available QAC 2.0.0 CLI at `D:\dev\tmp\qac-r2-stage-20261121\bin\GuideXOS.QAC.Cli\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`, SHA-256 `ce5438d2a7b7e74a19397a9273828cc81adb0ad9f1ad4f6ab2c9b9af52d875a7`. Its status, list, and queue commands reported the same online QAC coordinator before admission.

### Single bounded request and reconciliation

The qualified R2 runner submitted exactly one 180-second request. Reservation `ec8859b37c2d4b37b20507ae9b75b3aa` was owned by verified QAC run PID 12240 and remained `Waiting` at queue position 1 behind the unrelated managed guest. At the deadline, the controller canceled only that exact ID and reconciled the terminal state as `Cancelled`; no grant time or child PID was recorded, and guest input authorization remained false. The decision completed at `2026-11-21T18:13:14.478Z`, 180.47 seconds after runner start.

Final inspection confirmed the exact reservation `Cancelled`, the attempt owner exited, zero queued requests, and the unrelated taskbar guest still running. Both planned endpoints (QMP `127.0.0.1:62897` and VNC TCP port 41597) had no listeners. QEMU was not launched; retry3 has no serial, QMP guest-interaction, screenshot, or Fonts-control evidence. The post-run candidate check still matched 24/24 files.

Retry3 host-test, candidate, QAC pre-admission/final census, runner event, and QAC stdout/stderr evidence is under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6R2\retry3`. The retry outcome is **`FONT15R4R6R2_QAC_UNAVAILABLE`**. No production defect is established; minimum-size Fonts acceptance remains open. No later QAC request was submitted.

## Consolidated Gate 2 status

| Requirement | Latest evidence |
|---|---|
| Host runner and safety gate | Passed, 119/119; no skipped tests |
| Production candidate | Retry3 pre/post checks match 24/24 files |
| Real production guest boot | Confirmed in retry2 |
| Settings → Personalization → Fonts navigation | Confirmed in retry2 at default geometry |
| Authentic 560×360 / 560×336 client geometry | Not confirmed; retry2 stopped before resize |
| Font-family, size increase/decrease, and preview scroll/bounds | Not tested at minimum geometry |
| Back, Apply, Cancel, explicit bitmap fallback, Restore Defaults | Not tested at minimum geometry |
| Latest QAC attempt | One retry3 request remained queued, then exact-ID canceled and reconciled |
| `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` | Remains open |

The R2 runner now supports the bounded in-session drag needed to attempt the minimum-size resize. Resume only with a future single bounded QAC admission; do not submit replacement reservations during one attempt. Keep bitmap fallback available. Top-level Server release, PacMan audio-link, combined integration, persistence/storage, security, preview, and publication gates remain separate.

The current repository diff is limited to the acceptance runner and QMP safety support, their focused tests, and this report: `tools/fonts/font15r4r6r2_acceptance.py`, `out/font15r1-validation/guest/font15r1_qmp.py`, `tests/test_font15r2r1_qmp_safety.py`, `tests/test_font15r4r6r2_acceptance_runner.py`, and `docs/fonts/FONT15R4R6R2_BOUNDED_QAC_AND_MINIMUM_SIZE_ACCEPTANCE.md`. No production font, Settings, PS/2, compositor, or window-management code changed.
