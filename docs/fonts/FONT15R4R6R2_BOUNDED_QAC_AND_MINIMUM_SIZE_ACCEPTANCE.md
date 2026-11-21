# FONT15R4R6R2 — Bounded QAC and Minimum-Size Fonts Acceptance

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
