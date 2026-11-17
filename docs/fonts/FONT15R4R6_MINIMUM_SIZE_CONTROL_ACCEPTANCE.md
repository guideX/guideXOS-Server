# guideXOS Server — FONT15R4R6 Minimum-Size Fonts Control Acceptance

**Outcome B — `GUEST_MINIMUM_CONTROLS_INCOMPLETE`.** The production AMD64 guest reached the Fonts page at the accepted 560×360 minimum and rendered/selectively previewed Roboto Regular. The first fresh frame after a measured X-only pointer move showed only one of four requested movement segments. A later read-only frame showed the full displacement, but the time when the remaining movement became visible is unknown. Input stopped after the incomplete first observation. The remaining minimum-size controls are unverified, so `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open. No production font or input defect is inferred.

## Repository and closeout

- Repository: `D:\dev\guideXOSServer_FONT_SUPPORT`; branch: `FONT_SUPPORT`.
- Starting HEAD: `9570fdbd41d6a9ed8cf322cd2249d003a18ab37d`.
- The completed [R1 diagnostic report](FONT15R4R5R1_INSTRUMENTED_GUEST_POINTER_PIPELINE.md) was reviewed and committed alone as `207f41f2e347a5ffdf340fdac08d56aa7a7e8dcf` (`docs: close out FONT15R4R5R1 report`).
- At task start, `tests/test_font15r2r1_qmp_safety.py` had a pre-existing modification and `docs/fonts/FONT15R4R5_POINTER_EVENT_PIPELINE_DIAGNOSIS.md` was untracked. Both were preserved. The test run modified its previously clean `__pycache__` file; that cache is restored to the committed bytes before finalizing this report. No production source files were changed.
- No phase marker was closed or advanced. The minimum-size acceptance gate remains open.

## Candidate and guest setup

The guest used a copied, previously qualified FONT15R4R4 production candidate, not the diagnostic R1 kernel. Candidate provenance points to source execution HEAD `5d4449e92e1e895cca1d60535a3857340af3f156` and production source HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae`.

The preflight and post-run manifests each contain 24 files. All 24 hashes match; there are zero changed, missing, or extra files. `ESP/kernel.elf` SHA-256 is `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`; `ESP/EFI/BOOT/BOOTX64.EFI` is `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0`; read-only `OVMF.fd` is `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`. QEMU was version 11.0.0 (`a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`); the QAC CLI SHA-256 was `268dfe0a8a315aa2912c67665df77061f51609b65a94a0596024c12b114800f0`.

QEMU used read-only pflash, an isolated writable ESP copy, disabled networking, serial capture, QMP at `127.0.0.1:62896`, and VNC at `127.0.0.1:35696` (TCP 41596). The guest booted to the desktop and the initial complete framebuffer was 1280×800. The serial log recorded the Settings launch and font-file opens.

## QAC admission and cleanup

One QAC acceptance request was submitted: `0189d791aa3b4d40902143b020ce12a7`, owner CLI PID 34632. QAC recorded the request at `2026-11-17T04:59:04.0556529Z` and granted it at `2026-11-17T05:03:03.6681873Z`: a 239.613-second wait, 59.613 seconds beyond the configured 180-second admission window. At poll 33 the request was still `Waiting` at 180.451 seconds.

The admission controller had a blocking-order bug: after its deadline loop, it synchronously awaited the QAC `run` process's redirected stdout before performing deadline reconciliation or cancellation. That process remained attached to the guest for its lifetime. After guest shutdown, the controller printed `NO_ADMISSION` with an empty reservation ID. That terminal classification is incorrect: direct QAC inspection records a grant, verified running ownership, and then `Released` with reason `Managed child exited.` The request was not cancelled at the deadline. This deadline overrun is recorded as a controller deviation; the reservation was obtained through QAC, and no guest input was sent before ownership was verified.

Before guest shutdown, cleanup rechecked the exact reservation, QEMU PID 30288, executable, and loopback QMP listener. The approved QMP helper sent `quit`; QEMU PID 30288 and owner CLI PID 34632 exited. Final QAC status showed zero QEMU processes, zero managed sessions, zero queued or blocked requests, zero reservations, and no listeners on the owned QMP/VNC ports. No other project's guest was terminated.

## Pointer preflight and minimum-size observations

The short pre-navigation pointer check passed with 108/108 cursor pixels at each measured position:

| Action | Expected and observed position |
|---|---:|
| Initial | (640, 400) |
| X-only +8 | (648, 400) |
| Y-only +8 | (648, 408) |
| Separately paced (+12, +10) | (660, 418) |

Settings was opened through the desktop and navigated to Personalization → Fonts. The window was resized once to the previously established **560×360 outer / 560×336 client** size at screen origin (96, 70). At this size, the production layout places the preview inset at (316, 326), size 302×29; the family rows are compact two-column choices. Visible control rectangles included Back at (314, 162)–(402, 186), the Roboto Regular row at (471, 252)–(620, 268), size decrease at (366, 288)–(390, 310), size increase at (396, 288)–(420, 310), Apply at (314, 378)–(374, 400), Cancel at (380, 378)–(440, 400), and Restore Defaults at (446, 378)–(548, 400). These bounds agree with `kernel/core/settings_fonts_layout.cpp`.

The minimum-size screenshot showed a readable bitmap baseline and a clipped preview viewport without visible overlap among the displayed controls. A measured pointer click on the Roboto Regular row changed the selected state to `Roboto-v2.138-Regular.ttf`; the page showed a 16 px TrueType preview while the committed current font remained bitmap pending Apply. The click's press and release were acknowledged, the cursor was measured at (545, 260), and the state change was visible. This is real minimum-size family-selection hit-test evidence.

The next X-only move requested −137 pixels from (545, 260) toward the size controls. QMP acknowledged four relative events `−35, −35, −35, −32`. The first fresh framebuffer measured the cursor at (510, 260), only −35 pixels from the start. After no further guest input, a later read-only frame measured (408, 260), the requested X coordinate; its capture was 158 seconds after the first frame, so the time at which the remaining movement completed is unknown. The first frame's cursor measurement was the stop condition. No Y-only move to the plus button, size click, wheel event, Back, Apply, Cancel, Restore Default, explicit bitmap-fallback selection, or additional guest input followed.

This delayed observation resembles the partial first-frame movement recorded in [FONT15R4R4](FONT15R4R4_POINTER_EVENT_DELIVERY_AND_CONTROL_ACCEPTANCE.md), while the [R5R1 diagnostic replay](FONT15R4R5R1_INSTRUMENTED_GUEST_POINTER_PIPELINE.md) later observed full movement. The intermittent behavior remains unexplained; this acceptance neither repairs nor declares it fixed. No Settings termination was observed before input stopped.

## Minimum-size acceptance matrix

| Requirement | FONT15R4R6 result |
|---|---|
| Authentic production guest and accepted minimum geometry | Observed; geometry matrix not repeated |
| Roboto TrueType rendering and preview clipping | Observed at minimum size; preview remained inside its clipped viewport |
| Compact family list and visible controls | Observed visually; no visible control overlap |
| Family selection by pointer | **Passed** for Roboto Regular; selection state changed and was visible |
| Font-size adjustment and post-change layout | Not tested; input stopped before activation |
| Preview scrolling and minimum-size bounds | Not tested at minimum size; prior 19 px bound evidence remains historical |
| Back/navigation at minimum size | Not tested after the stop condition |
| Apply and Cancel at minimum size | Not tested after the stop condition; prior larger-window guest evidence remains historical |
| Restore Default and explicit bitmap fallback at minimum size | Not tested after the stop condition; initial bitmap state is not an explicit fallback interaction |
| Hit-test agreement | Family row passed; size and action controls remain unverified |
| No unexpected Settings termination | No termination observed before the stop condition |

Accordingly this is **Outcome B, `GUEST_MINIMUM_CONTROLS_INCOMPLETE`**, not acceptance and not a confirmed production defect. Earlier geometry, larger-window Apply/Cancel, bitmap fallback, preference reload, and preview-bound evidence remains available in [FONT15](FONT15_LIVE_GUEST_MINIMUM_SIZE_ACCEPTANCE.md) and [FONT14](FONT14_MINIMUM_SIZE_TYPOGRAPHY_POLISH.md), but does not substitute for the unobserved minimum-size controls.

## Regression results

The QMP helper safety gate passed 46/46 before QAC admission. After guest cleanup, all requested regressions passed again:

| Suite | Result |
|---|---:|
| FONT6 | 420/420 |
| FONT10 | 88/88 |
| FONT11 | 48/48 |
| FONT14 | 496/496 |
| **Typography total** | **1,052/1,052** |
| QMP helper safety | 46/46 |

The post-guest builds and logs are retained under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6\run1\logs`. No guest binaries, screenshots, or other phase artifacts were added to the repository.

## Evidence index and independent release gates

The full evidence bundle is under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R6\run1`:

- Candidate: `guest/candidate-preflight-font15r4r6.json`, `guest/candidate-postrun-font15r4r6.json`, and `guest/qemu-launch-plan-font15r4r6.json`.
- Boot and UI: `guest/initial-desktop-font15r4r6.ppm`, `guest/minimum-size-font15r4r6.ppm`, `guest/minimum-roboto-regular-selected-font15r4r6.ppm`, and `guest/font15r4r6-run1.serial.log`.
- Pointer: preflight measurement JSON files, `guest/regular-row-to-size-plus-x-move-font15r4r6.jsonl`, the first and settled PPM screenshots, and `guest/pointer-delayed-observation-font15r4r6.json`.
- QAC deviation and cleanup: `guest/qac-admission-polls.jsonl`, `guest/qac-admission-controller-correction-font15r4r6.json`, `guest/qac-ownership-verified-font15r4r6.json`, `guest/qac-cleanup-prequit-font15r4r6.json`, `guest/qac-owned-qmp-quit-font15r4r6.jsonl`, and `guest/qac-cleanup-postquit-font15r4r6.json`.
- Regressions: `logs/FONT6-postguest.log`, `logs/FONT10-postguest.log`, `logs/FONT11-postguest.log`, `logs/FONT14-postguest.log`, and `logs/font15r2r1-qmp-safety-postguest.log`.

Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. The PacMan audio-link prerequisite, top-level guideXOS Server v0.3 release build, combined subsystem integration, public technical preview security qualification, and final publication authorization remain independent gates. The future OS-wide TrueType direction remains outside FONT15R4R6.
