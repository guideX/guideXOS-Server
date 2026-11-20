# FONT15R4R6R1 — Pointer Settling and QAC Admission Hardening

**Outcome A — `POINTER_SETTLING_AND_QAC_HOST_QUALIFIED`.** Host-side pointer settling, screenshot freshness checks, QMP ownership/deadline handling, and a bounded QAC admission boundary are implemented and passed deterministic tests. The evidence does not identify the cause of the R6 intermediate cursor image, does not qualify a live QAC run, and does not close the Fonts minimum-size acceptance gate.

## 1. Repository and preservation

- Repository: `D:\dev\guideXOSServer_FONT_SUPPORT`; branch: `FONT_SUPPORT`.
- The task began at `d7c8b990eac0f9bf7e51e5adb197ce45db112e0b`, with a clean worktree. The pasted request’s last-reported SHA `f891c7ab8c6d520fec0e2de6c86dd696e7d9e839` was historical; the actual branch had advanced.
- Implementation commits: `1777ba3e91ee240da12285f4a752a61f91ed65dd` and `0d29a65753dc1d7e48019729167f5ca66bb79361`.
- The closeout report is being committed separately; its commit is reported in the task closeout.
- R6 evidence under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6\run1` was read only and preserved. No QAC request, guest boot, guest input, or live contention was initiated in R1.
- No production font, input, compositor, or Settings source was changed, and no production build was run.
- The separate `D:\dev\guideXOS_QAC` checkout was inspected read-only at `1ea193258690f9e7b39390b101a117166ef8bbdd`; it was not modified.

## 2. R6 movement and observation timeline

The R6 run began this movement with cursor `(545,260)` in a `1280×800` framebuffer and requested `dx=-137, dy=0`, targeting `(408,260)`. The existing helper split the X movement into four acknowledged QMP events:

| Event | Delta | QMP request UTC | Matching response UTC |
|---:|---:|---|---|
| 1 | −35 | 05:19:35.832954 | 05:19:35.833953 |
| 2 | −35 | 05:19:36.035474 | 05:19:36.037485 |
| 3 | −35 | 05:19:36.240020 | 05:19:36.242022 |
| 4 | −32 | 05:19:36.443546 | 05:19:36.445546 |

All four responses contained `return: {}`. The first screenshot command was requested at 05:19:39.155078; its QMP response arrived at 05:19:39.170076, and the new P6 file was verified at 05:19:39.193079. Its image capture timestamp is 05:19:39.169077. That is about 2.72 seconds after the last movement response. The unique framebuffer-wide cursor match was `(510,260)`, exactly −35 px from the initial position, with 108/108 cursor pixels matching.

No input was sent after that incomplete first observation. A later read-only screenshot was requested at 05:22:17.244422, acknowledged at 05:22:17.260421, and verified at 05:22:17.268421. Its capture timestamp is 05:22:17.259423, about 158.09 seconds after the first frame. The cursor was `(408,260)`, the full requested −137 px, again with 108/108 pixels matching. The preserved evidence does not timestamp when the remaining −102 px became visible.

The production R6 run had no guest-side PS/2, input-map, desktop-dispatch, or cursor-draw diagnostics. The earlier R5R1 diagnostic observed six acknowledged QMP events, six decoded PS/2 packets, two desktop pointer updates, and full net `(−123,−41)` movement. That makes coalescing or asynchronous updates plausible, but it does not demonstrate the cause of the R6 frame. Neither a driver defect nor a QMP-helper defect is inferred.

## 3. Screenshot and cursor measurement findings

The existing QMP helper sends screenshots through QMP `human-monitor-command` with HMP `screendump`. It uses a new destination, waits for the matching QMP response, and validates the P6 header and exact expected raster byte count before accepting the file. The R6 traces record fresh 1280×800 P6 files; old output reuse or a partial file does not explain the measured first frame.

QMP command success and screenshot completion are separate from guest input consumption. QEMU documents `input-send-event` as sending input events to the guest; `screendump` captures screen contents to a file. The QMP response and completed file therefore establish command/capture completion, not that the guest processed and rendered every preceding input event before that frame. See the [QEMU QMP Reference Manual](https://qemu-project.gitlab.io/qemu/interop/qemu-qmp-ref.html). This last distinction is an inference from the command boundaries, not a claim that the QMP response reports guest processing.

The old cursor matcher used the 19×12 `s_cursorBitmap` and its 108 opaque black/white pixels. R6 frames have blue UI outlines, so matching was checked against the actual PPM images. The new matcher scans the framebuffer using the source cursor template, requires a unique exact opaque-pixel match, rejects ambiguity and invalid/oversized P6 images, and handles clipped edges conservatively. It uses image dimensions rather than assumed screen geometry. The actual first and later R6 PPMs each produced the expected unique 108/108 match.

## 4. Settling implementation and safety contract

`out/font15r1-validation/guest/font15r1_pointer_settling.py` adds an opt-in `settle` action to the existing host helper. Existing movement behavior remains the default. The settling path requires the caller’s measured start position, relative delta, cursor template, output directory, target region, monotonic deadline, observation cap, interval, and tolerance.

It verifies the QMP endpoint, issues one logical relative movement through the existing bounded/chunked `QMP.relative()` path, and then performs only fresh read-only captures. It never retries movement, clicks, presses buttons, scrolls, or drags. Each observation records monotonic and UTC timing, screenshot freshness/size/dimensions, measured cursor, and error details. Limits include at most 32 screenshots, an interval no longer than 5 seconds, a total window no longer than 30 seconds, a 5-million-pixel scan cap, and a bounded candidate count. Screenshot and ownership operations share the absolute deadline.

Results distinguish expected/clamped destination, partial or no displacement, screenshot-limit outcomes, unavailable or ambiguous measurement, stale or invalid screenshot, ownership loss, QMP failure, deadline overrun, overshoot, and unexpected position. `click_authorized` is true only when the unique measured cursor reaches the expected destination and lies inside the caller-supplied target rectangle; the settling helper itself does not click. A QMP acknowledgment alone cannot authorize interaction.

The QMP helper now accepts optional absolute deadlines for screenshots and ownership rechecks. A follow-up fix clamps connection-ownership rechecks to the caller’s original absolute deadline, so repeated ownership probes cannot extend the overall operation. The new mode is opt-in; existing `move` callers retain their behavior.

## 5. Deterministic pointer tests

`tests/test_font15r1_pointer_settling.py` adds 18 fake-session tests, including immediate and delayed completion, partial/no movement, ambiguous and unavailable cursor, stale/invalid capture, ownership loss, bounded timeout/count/interval, both axes, clamped screen edges, overshoot/corridor mismatch, target miss, and interrupted capture. The delayed case verifies one logical movement and read-only later observations.

## 6. QAC admission audit and bounded boundary

R6 recorded a configured 180-second window but a grant after 239.613 seconds, 59.613 seconds late. The blocking-order defect is in the R6-local PowerShell runner retained in the external artifact bundle: after the monotonic deadline it synchronously waited on the long-lived QAC `run` process’s redirected stdout before cancellation or reconciliation. After the managed guest exited, it reported `NO_ADMISSION` with an empty request ID even though direct inspection showed a verified grant followed by `Released`. That final classification is incorrect. The QAC coordinator itself has a separate configurable wait timeout (default 1800 seconds); the 180-second client admission window belonged to the wrapper.

The new `out/font15r1-validation/guest/font15r1_qac_admission.py` supplies a bounded caller-side adapter and controller for future Fonts runs. It polls one exact request ID with monotonic timing and bounded per-call timeouts, reconciles at deadline, cancels only that caller’s ID, and confirms the resulting state. It tracks versions and late returns; stale or duplicate replies cannot erase a grant. A late grant never authorizes guest input. If a grant may have occurred or ownership cannot be reconciled, the result requires reconciliation and the owner process must remain alive. An on-time grant also requires verified running-child ownership before guest input is authorized.

The controller is not wired into the historical R6 PowerShell runner. Do not reuse that runner for another acceptance. The shared QAC repository remains unchanged; use the new controller boundary from a future owner-side runner and verify the exact reservation and child before guest input. No live QAC reservation was requested for this host qualification.

`tests/test_font15r1_qac_admission.py` adds 15 fake-coordinator/fake-clock tests covering immediate and delayed grants, queued-at-deadline cancellation, grant/cancel races, late inspect return/grant, accepted cancellation, ambiguous or unavailable coordinator state, foreign request/reservation protection, polling overrun, stale/duplicate responses, and ownership verification.

## 7. Test results

Command run after the final QMP deadline change:

```text
python -B -m unittest tests.test_font15r2r1_qmp_safety tests.test_font15r1_pointer_settling tests.test_font15r1_qac_admission -v
```

Result: **79/79 passed** — the existing QMP safety suite 46/46, pointer settling 18/18, and QAC admission 15/15. No live QEMU guest or QAC contention was needed for these tests.

## 8. Minimum-size acceptance and next guest phase

Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. R6 had already demonstrated the 560×360 window, 560×336 client, 561×361 boundary resize, delivered 480×300 clamp, Roboto rendering, and real Roboto Regular selection. R1 preserves those results without repeating the geometry qualification.

Still unverified at minimum size: font-size adjustment, preview scrolling, Back/navigation, Apply/Cancel, Restore Defaults, and explicit bitmap fallback. Visibility of these controls does not count as acceptance. Independent PacMan audio/build prerequisites, combined integration, security, persistent user data, and public technical-preview gates remain unchanged.

Recommended follow-up: **FONT15R4R6R2 — Settled Pointer Minimum-Size Controls Acceptance**. Obtain exclusive QAC ownership with a bounded caller-side controller; boot the authentic production AMD64 candidate; verify a fresh initial cursor measurement; opt into one-movement/read-only-settling for each intended target; click only after the measured cursor is in the target region; then perform only the outstanding controls above. Preserve timing, QMP responses, fresh screenshots, measured coordinates, and any partial or delayed movement. Do not repeat the historical geometry matrix. Close the minimum-size gate only after all required interactions pass.

## 9. Changed files and implementation commits

The intentional implementation changes are:

- `out/font15r1-validation/guest/font15r1_pointer_settling.py` — bounded observer and opt-in CLI action.
- `out/font15r1-validation/guest/font15r1_qac_admission.py` — bounded admission/reconciliation controller and CLI adapter.
- `out/font15r1-validation/guest/font15r1_qmp.py` — optional absolute deadlines and screenshot freshness metadata; ownership checks remain bounded by the caller deadline.
- `tests/test_font15r1_pointer_settling.py` — 18 deterministic tests.
- `tests/test_font15r1_qac_admission.py` — 15 deterministic tests.
- `docs/fonts/FONT15R4R6R1_POINTER_SETTLING_AND_QAC_HARDENING.md` — this closeout.

Implementation commit `1777ba3e91ee240da12285f4a752a61f91ed65dd` adds the observer, admission boundary, QMP extensions, and tests. Commit `0d29a65753dc1d7e48019729167f5ca66bb79361` tightens QMP ownership rechecks to the absolute deadline. The report commit is given in the task closeout.
