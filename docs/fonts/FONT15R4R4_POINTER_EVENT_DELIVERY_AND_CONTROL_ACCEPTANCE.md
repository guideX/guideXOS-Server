# FONT15R4R4 Pointer Event Delivery and Control Acceptance

**Outcome: `GUEST_MINIMUM_INTERACTION_INCOMPLETE`**

**Execution HEAD:** `5d4449e92e1e895cca1d60535a3857340af3f156` (`FONT_SUPPORT`)

**Acceptance gate:** Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open.

The R4R3 multi-event pointer failure reproduced on the authentic Server guest after some minimum-size Fonts interactions had succeeded. The new run also confirmed that the unchanged 200 ms movement cadence can succeed in one desktop state, so delivery is intermittent in the observed environment. QMP acknowledgments and one fresh screenshot do not identify which downstream stage lost the remaining movement. No production font, input-driver, or compositor defect was established.

## Repository preservation

At start, the branch was `FONT_SUPPORT` at the execution HEAD above, the index was empty, and the only working-tree change was the pre-existing unstaged edit in `out/font15r1-validation/guest/font15r1_qmp.py`. That edit removes a TCP listener-state filter because the Windows listener table can return `dwState=0`. It remains present and unstaged. No existing FONT15R4R4 report or artifact directory existed. No local instructions or phase marker applied.

The work added a focused wheel-event fix in the validation helper, two tests in `tests/test_font15r2r1_qmp_safety.py`, and this report. No production source under `kernel`, `bootloader`, or `uefi` changed. The read-only `MODERN_INPUT` checkout was clean at its existing HEAD and was not modified. Existing reports and prior-run artifacts remain intact.

At finish, HEAD remains `5d4449e92e1e895cca1d60535a3857340af3f156`; no commit was created. The staged diff contains only this report, the two focused tests, and the wheel-action helper fix. The pre-existing endpoint-verifier hunk remains unstaged. Running the tests created `tests/__pycache__/test_font15r2r1_qmp_safety.cpython-312.pyc`; removal was rejected by the filesystem policy, so that generated file remains untracked and was not staged.

## R4R3 failure reconstruction

R4R3 began the failing movement at cursor `(545,260)` and requested `dx=-138, dy=+39`. The pointer was inside the 1280×800 framebuffer and 560×360 Settings window, with no boundary clamp expected. There were no button events during the move. The prior selection click had acknowledged both button-down and button-up, and QAC reported the same running owner before and after with `degraded=false`.

The helper sent these six separate relative events, with about 200 ms between commands; every matching QMP response was successful:

| Order | Axis | Value |
|---:|---|---:|
| 1–4 | X | −35, −35, −35, −33 |
| 5–6 | Y | +35, +4 |

A fresh P6 screenshot, verified about 1.5 seconds after the final command, measured the cursor at `(510,260)`: only the first X event was guest-visible. QEMU reported `QEMU PS/2 Mouse`; the launch used the `pc` machine and no USB tablet. The screenshot was fresh and showed the first movement, but no per-event PS/2 byte or completed-packet counters were available.

The first unsupported transition is between the acknowledged QMP commands and complete guest-visible pointer-state change. QMP replies establish command completion; they do not establish device exposure, IRQ delivery, packet assembly, guest pointer update, or compositor rendering for every event. The available trace does not distinguish those stages and does not prove PS/2 packet loss.

## Host helper findings and focused correction

Before QAC admission, the existing safety suite passed **43/43**. The helper already sends X and Y separately, bounds each relative step to 35 pixels, paces events, matches QMP response IDs, verifies endpoint ownership, propagates failures, and requires fresh complete P6 screenshots. Its button path tracks release attempts and does not retry uncertain input.

During the required preview-scroll check, the helper's wheel action attempted a relative event on axis `z`. The admitted QEMU 11.0.0 guest rejected it with `Parameter 'axis' does not accept value 'z'`. QEMU's QMP reference defines pointer position axes as `x`/`y` and provides `wheel-up`/`wheel-down` button values for pointer events ([QEMU QMP reference](https://qemu-project.gitlab.io/qemu/interop/qemu-qmp-ref.html#enum-InputButton)). The focused helper correction maps positive deltas to `wheel-down` and negative deltas to `wheel-up`, using the existing bounded button press/release cleanup path. It adds no retries. Two focused tests cover both directions, release acknowledgment, pacing, and zero-delta rejection.

The wheel-action gap is separate from the six-event relative-motion failure. After the helper correction, wheel-down events changed the Fonts preview in the guest. The post-run safety suite passed **45/45** (the original 43 plus the two focused tests).

## Input pipeline inspection

Read-only inspection covered both `FONT_SUPPORT` and `MODERN_INPUT`:

- `kernel/core/ps2mouse.cpp` services IRQ12, reads bytes, assembles four-byte IntelliMouse packets, decodes and clamps movement, and limits IRQ12 logging to the first five entries. It has no full byte/packet counters.
- `kernel/core/input_manager.cpp` polls PS/2 position deltas, maps relative movement, and updates `MouseState`; this path does not use an event queue.
- `main.cpp` polls input and routes pointer/button/wheel state to the desktop. `desktop.cpp` updates the software cursor and handles mouse interactions; the compositor routes wheel input and manages window interactions.
- `settings_fonts_layout.cpp` shares layout rectangles for Fonts drawing and hit testing. No source evidence established a layout mismatch.
- The inspected Modern Input checkout has additional diagnostic/capture paths, but the production PS/2 driver and input manager remain the same. No qualified repair or handoff is justified by this run.

The guest log confirmed IntelliMouse initialization, IRQ12 registration, Settings-native mouse navigation, and the Fonts route with three faces. It contains only the first five IRQ12 log entries, not a complete event/packet trail. The observed pointer remained far from framebuffer edges, no button was held during the reproduced move, and the fresh screenshot showed the first cursor update. Thus clipping, held-button capture, and a wholly stale screenshot do not explain the complete trace; QEMU-to-device delivery, guest interrupt/packet processing, later state polling, and the remaining downstream steps remain unresolved. Targeted future diagnostics should correlate IRQ12 byte counts and completed packets, decoded deltas, input-manager mapped/clamped state, and compositor pointer/capture state per action.

## QAC admission, candidate, and cleanup

The pre-request census was online and empty: no QEMU process, reservation, managed session, queued request, or blocked request. After the host safety gate and diagnostic plan, exclusive QAC acceptance was granted:

| Item | Evidence |
|---|---|
| Reservation | `d80b41f5f811438191dbefaab5698bc6`, `guideXOSServer`, `acceptance`, `Running`, `degraded=false` |
| Owner / child | PowerShell owner PID 25536; direct QEMU child PID 22476, `C:\Program Files\QEMU\qemu-system-x86_64.exe` |
| Endpoints | QMP `127.0.0.1:62691`; VNC `127.0.0.1:35691` (TCP 41591); listeners verified against PID 22476 before and after each action |
| QEMU build | 11.0.0, SHA-256 `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02` |
| Candidate | Isolated copy, 24 files; preflight exactly matched the approved R4R3 24-file manifest |
| Guest isolation | Read-only OVMF pflash, disposable writable ESP copy, network disabled |

The post-run candidate manifest remained **24/24**, with **0 changed, 0 missing, and 0 extra files**. Its `kernel.elf` hash is `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`, matching the already-qualified freestanding AMD64 artifact. The existing build evidence is retained in the [FONT14 report](FONT14_MINIMUM_SIZE_TYPOGRAPHY_POLISH.md); no new production build was claimed.

After the stop condition, QMP `quit` shut down only the QAC-owned child. The QMP wrapper's immediate live-owner assertion observed the expected exit transition; a separate final QAC census confirmed **0 QEMU processes, 0 reservations, 0 sessions, 0 queued requests, and 0 blocked requests**. PID 22476 and listeners on ports 62691, 35691, and 41591 were absent. No foreign QEMU process was stopped.

## Guest pointer and minimum-size results

The initial fresh screenshot measured cursor `(640,400)`. Separate X-only `+8` and Y-only `+8` actions each produced the exact measured displacement; separate `−8` actions returned to center. The R3 movement deltas were replayed individually with a fresh screenshot after each event and each was visible. That slower sequence ended at `(502,439)`.

The same batched X/then-Y R3 deltas were next sent at the helper's 200 ms cadence from the desktop. X `−138` split into `−35,−35,−35,−33` ended at `(364,439)`; Y `+39` split into `+35,+4` ended at `(364,478)`. Both screenshots showed the exact displacement. This demonstrates success in that desktop state, not reliable delivery across the guest session.

The production Settings flow opened through Start and Personalization. The first screenshot immediately after the Fonts tile click remained visually unchanged even though serial logged `page=personalization/fonts faces=3`; a settled screenshot about 750 ms later showed the Fonts page. This is consistent with delayed repaint for that transition and was not treated as input failure.

The prior accepted 560×360 outer / 560×336 client minimum geometry was reused. The compact two-column font choices were visible. A pointer click selected `Roboto-v2.138-Regular.ttf`; the settled screen showed TrueType preview text at 16 px, and serial logged opens of `/system/fonts/Roboto-v2.138-Regular.ttf`. The preview was clipped within its viewport as expected. At measured control positions, plus changed the pending size from 16 to 17 px, minus returned it to 16 px, and further plus clicks produced a settled 19 px display. These pending changes were not applied.

At 19 px, pointer position `(500,340)` was measured within the preview. After the wheel-helper correction, wheel-down visibly scrolled the samples. Further wheel-down actions reached an endpoint; the fresh screenshot after one additional wheel-down was byte-identical to the preceding screenshot. This establishes visible scrolling and a stable bound at the selected 19 px size. A bound recomputation after changing size while already scrolled was not completed.

The required next move to the minus control requested `dx=-123, dy=-41` from `(500,340)`. The helper sent separate X events `−35,−35,−35,−18` and Y events `−35,−6`, each about 200 ms apart, with all six matching QMP responses acknowledged. The no-button movement ended with a fresh, complete P6 screenshot verified 1.68 seconds after the final movement command. Cursor matching found `(465,340)`, exactly one X step (`−35,0`) from the starting position; the other events and all Y displacement were not visible. QAC ownership remained verified and `degraded=false` around the action. The pointer was not at a screen boundary.

Per the stop rule, no more guest input followed this reproduced partial movement. Results:

| Required interaction | R4R4 result |
|---|---|
| Reuse 560×360 minimum and compact two-column choices | Observed; earlier geometry acceptance retained |
| Roboto family selection and TrueType rendering | Passed at 16 px |
| Font-size changes | Plus/minus and larger size changes observed (16, 17, 19 px) |
| Preview visibility, clipping, and scrolling | Observed; stable lower bound at 19 px |
| Scroll-bound recomputation after a post-scroll size change | Not verified |
| Back/navigation, Apply, and Cancel | Not reached after stop condition |
| Restore Default and explicit bitmap fallback | Not reached after stop condition |
| Full control hit-test, no-overlap, and application-stability sweep | Incomplete |
| Preference persistence | Not tested; no Apply action was taken |

No unexpected application termination was observed before the stop condition. Because movement was not consistently observable, the remaining control suite is incomplete and the minimum-size gate cannot close.

## Post-guest regressions

| Suite | Current result |
|---|---:|
| FONT6 | 420/420 |
| FONT10 | 88/88 |
| FONT11 | 48/48 |
| FONT14 | 496/496 |
| **Typography total** | **1,052/1,052** |
| QMP helper safety, including focused wheel tests | 45/45 |

Per-suite logs are under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R4\run1\logs`.

## Evidence bundle and next step

Raw QMP P6 screenshots, PNG display conversions, per-action traces, paired QAC snapshots, candidate manifests, the QEMU launch plan, serial log, cursor detections, and final QAC census are preserved under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R4\run1`. Key files include `guest/font15r4r4-run1.serial.log`, `guest/candidate-preflight-font15r4r4.json`, `guest/candidate-postrun-font15r4r4.json`, `guest/qac-final-census-font15r4r4.json`, `guest/move-from-preview-to-font-minus-font15r4r4.jsonl`, and `guest/before-font-size-after-scroll-font15r4r4.ppm`.

Continue the acceptance only after a targeted diagnostic run can observe a complete multi-event move and correlate guest IRQ12 bytes/packets with decoded and compositor pointer state. Do not infer a driver defect from the current evidence. The independent PacMan audio prerequisite, top-level Server v0.3 build, combined integration, public preview qualification, and publication authorization remain separate gates.

TrueType should eventually become the preferred typography provider throughout guideXOS Server, with bitmap rendering retained as a reliable fallback. Broader provider integration remains future work and was not implemented here.
