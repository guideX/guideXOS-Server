# guideXOS Server — FONT15R4R2 Live Pointer and Minimum-Size Acceptance

**Outcome B — `GUEST_ACCEPTANCE_INCOMPLETE`.** QAC granted exclusive acceptance ownership and the AMD64 guest booted. The corrected QMP helper demonstrated independently measured X/Y pointer movement, opened the production Settings Fonts page, rendered Roboto, and reached a settled 560×360 outer window with a 560×336 client. The required 561×361 boundary drag was acknowledged by QMP but produced no measured cursor displacement and no screenshot change. Per the stop rule, guest input ended there; the 480×300 clamp and remaining Fonts controls were not attempted. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. No production defect was demonstrated.

**Date:** 2026-11-08 PST (QAC/guest log timestamps cross midnight into 2026-11-09 UTC) · **Machine:** `LEON2_RDT` · **Repository:** `D:\dev\guideXOSServer_FONT_SUPPORT` · **Branch:** `FONT_SUPPORT`

## 1. Repository preservation and scope

Starting and ending execution HEAD: `48059d502ca6fb873165bc5e8596ef04d59e59ed`. The branch was `FONT_SUPPORT`; the index was empty. The only pre-existing tracked modification was the unstaged, qualified-helper edit at `out/font15r1-validation/guest/font15r1_qmp.py`. It was left unchanged. No other agent was active in this checkout. The final documentation is the only new non-ignored workspace file; guest evidence and regression outputs are under the ignored `out/font15r4r2-validation/retry1/` tree.

No production, test, helper, or remote configuration was modified. No reset, clean, stash, branch change, worktree, merge, rebase, cherry-pick, fetch, pull, or push occurred. The local `origin/FONT_SUPPORT` reflog retained its pre-existing `update by push` entry at `48059d50`; as required, the actual remote remains unknown and was not investigated. There is no root `.phase` marker. No commit was created; HEAD did not move.

## 2. QMP helper safety gate and candidate

Before admission, `python -m unittest tests.test_font15r2r1_qmp_safety -v` passed **43/43**. The current helper uses the Windows connected-endpoints table, requires exact endpoint tuples and `ESTABLISHED` state, checks the expected QAC QEMU PID and process identity, retries an empty owner snapshot only within its bound on the same socket, does not blindly reconnect, bounds drag cleanup, reports unresolved button state as failure, and verifies a fresh complete P6 PPM screenshot.

The authentic candidate came from the previously accepted FONT15R4 retry3 staging (`candidateSourceHead` `ec221271e1d0c2f918202c56ba899b497cb172ae`). All **24/24** manifest entries were checked before launch and again after exit; no files changed, disappeared, or appeared. The disposable candidate used a separate writable folder-backed ESP, passed and removed a 37-byte write probe, and kept OVMF firmware read-only. Production `kernel`, `bootloader`, and `uefi` paths have no changes since the candidate source checkpoint.

| Candidate component | SHA-256 |
|---|---|
| AMD64 `kernel.elf` | `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04` |
| `ramdisk.img` | `0b21c7d4691b4b2cca9da530e5a0e5699c85ab5ca0c918f0dfc981cac646b960` |
| `BOOTX64.EFI` | `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0` |
| `OVMF.fd` | `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792` |
| Roboto v2.138 Regular | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` |
| Roboto v2.138 Bold | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` |
| Roboto v2.138 Italic | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` |

QEMU 11.0.0 was `C:\Program Files\QEMU\qemu-system-x86_64.exe` (SHA-256 `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`). Networking was disabled. QMP used loopback port 61391 and VNC display 35351 (TCP port 41251); both were closed after cleanup. Prelaunch manifest, launch plan, and postrun manifest are in [`candidate-preflight-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/candidate-preflight-font15r4r2.json), [`qemu-launch-plan-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/qemu-launch-plan-font15r4r2.json), and [`candidate-postrun-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/candidate-postrun-font15r4r2.json).

## 3. QAC admission, guest boot, and cleanup

Immediately before admission, QAC 2.0.0 was online with zero QEMU processes, reservations, sessions, queued requests, or blocked requests. Reservation `2cfb19b75c5c4556859566c88df6d7ed` was granted in `acceptance` mode to `guideXOSServer`; QAC owner PID 27916 launched verified direct child QEMU PID 26568. Live QAC inspection reported `degraded=false` and verified the child identity before guest commands.

The guest reached the real AMD64 desktop. QMP returned `running`; `info mice` reported a QEMU PS/2 mouse. The fresh initial framebuffer was 1280×800. Serial output reached the kernel main loop and opened the three approved `/system/fonts/Roboto-v2.138-*.ttf` files. The serial log is [`font15r4r2-retry1.serial.log`](../../out/font15r4r2-validation/retry1/guest/font15r4r2-retry1.serial.log).

QAC's event ledger records one unmanaged QEMU process from 2026-11-09 04:51:27.462727 UTC until 04:51:38.656366 UTC, then records `ExclusiveRestored`. The process identity was not recorded. No `input-send-event` occurred during that 11.2-second degraded interval; the next input event was at 04:52:11.434 UTC. At the end, the verified helper sent QMP `quit`, received a `SHUTDOWN` event and acknowledgment, and QAC released the reservation with exit code 0. The final census showed zero QEMU processes, reservations, sessions, queues, or listeners on ports 61391 and 41251. The QAC run log emitted a degraded-reservation warning near shutdown despite the earlier restored event; both ledger events and the final census are preserved in [`qac-reservation-events-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/qac-reservation-events-font15r4r2.json), [`qac-run-font15r4r2.log`](../../out/font15r4r2-validation/retry1/guest/qac-run-font15r4r2.log), and [`endpoint-census-final-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/endpoint-census-final-font15r4r2.json).

## 4. Pointer delivery and real control activation

Each listed displacement was measured from fresh guest screenshots, not inferred from the QMP acknowledgment. Movement used separate X/Y events; the helper paced events by 200 ms and bounded each relative step to 35 pixels.

| Request | QMP result | Screenshot-measured pointer tip |
|---|---|---|
| Initial capture | Fresh 1280×800 P6 | `(640, 400)` |
| X-only `+12, 0` | Acknowledged | `(652, 400)` — observed `+12, 0` |
| Y-only `0, +12` | Acknowledged | `(652, 412)` — observed `0, +12` |
| X-only `+280, 0` in 8 bounded events | All acknowledged | `(932, 412)` — observed `+280, 0` |
| Y-only `0, +210` in 6 bounded events | All acknowledged | `(932, 622)` — observed `0, +210` |
| Boundary drag `+1, +1` | Button down, both axis events, and release acknowledged | No cursor or frame displacement; image hash unchanged |

The pointer reached the desktop Start control and its click opened the Start menu. A click on the Settings launcher opened the production Settings window; subsequent pointer clicks navigated to Personalization and Fonts. Selecting Roboto Regular loaded `Roboto-v2.138-Regular.ttf` and changed the preview to the TrueType face at 16 px. The helper acknowledged button-down and button-up for clicks and resize drags. Start menu activation and resized window geometry were visible guest results; later independent pointer movement also followed acknowledged releases. The complete traces and coordinate measurements are under [`pointer-observations-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/pointer-observations-font15r4r2.json).

## 5. Same-window resize and Fonts observations

The same Settings window stayed at `(95, 70)` while the compositor resized it. Geometry below comes from measured guest screenshot edges; client height subtracts the 24-pixel title bar.

| Stage | Observed outer | Observed client | Result |
|---|---:|---:|---|
| Initial Settings | 760×500 | 760×476 | Pass |
| Intermediate frame | 640×400 | 640×376 | Pass; screenshot measured the frame at those dimensions |
| Settled minimum | 560×360 | 560×336 | Pass |
| Boundary request | Requested 561×361 | Remained 560×360 | **Fail / stop boundary** |
| Undersize request 480×300 | Not attempted | — | Unverified |
| Grow/shrink cycle | Shrink sequence observed; no subsequent growth | — | Unverified |

Resize repaint lag affected the immediate captures: the first screenshot taken just after a resize still showed the prior 760×500 frame, and the next captured frame showed 640×400. A delayed, independent fresh capture confirmed the settled 560×360 minimum. The measured dimensions and byte-preserved raw PPM copies are indexed in [`window-geometry-results-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/window-geometry-results-font15r4r2.json).

At the settled minimum, authentic guest screenshots show Roboto TrueType preview rendering, the compact two-column font selector, the preview clipped within its viewport, and the bitmap default before Roboto was selected. No unexpected overlap, glyph corruption, or Settings termination was visible. The one-pixel boundary drag was QMP-acknowledged but did not change any screenshot pixel: the boundary PPM SHA-256 exactly matches the settled-minimum PPM. Per the mandatory stop condition, no more guest input was sent. Font-size buttons, selection of an additional TrueType style, preview scrolling/bounds, Back, Apply, Cancel, the undersize clamp, and the grow-after-shrink cycle therefore remain unverified. No preference was applied or written.

The helper verified **21/21** fresh, complete P6 screenshots at 1280×800; each PPM size matched its declared dimensions. [`screenshot-integrity-font15r4r2.json`](../../out/font15r4r2-validation/retry1/guest/screenshot-integrity-font15r4r2.json) records hashes and the corresponding QMP verification traces. PNG files are display conversions of the preserved raw PPM screenshots.

## 6. Post-guest regressions

All requested post-guest suites passed. Current results are distinct from the historical baselines, even though the totals match them.

| Suite | Current result |
|---|---:|
| FONT6 | 420/420 |
| FONT10 | 88/88 |
| FONT11 | 48/48 |
| FONT14 | 496/496 |
| **Hosted typography total** | **1,052/1,052** |
| FONT15R2R1 QMP safety | 43/43 |

Logs and rebuilt test executables are under [`logs`](../../out/font15r4r2-validation/retry1/logs) and [`tests`](../../out/font15r4r2-validation/retry1/tests). No production source changed, so the prior verified freestanding AMD64 build remains applicable; its kernel ELF hash is the candidate hash above.

## 7. Outcome and next action

This attempt is **`GUEST_ACCEPTANCE_INCOMPLETE`**, not a production defect. The first mandatory failure boundary was the 561×361 resize test: QMP acknowledged a one-pixel X/Y drag, but the fresh screenshot proved no cursor movement and the Settings window remained 560×360. The accepted gate stays `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN`.

The smallest next action is a new controlled guest retry that first establishes an on-screen pointer position inside the live resize grip from a fresh screenshot, then repeats the 561×361 boundary while capturing only after the frame settles. If one-pixel displacement remains unobservable, stop there again. The 480×300 clamp and remaining Fonts controls should only follow after that boundary passes.

The independent PacMan audio-link prerequisite, top-level Server v0.3 build gate, combined subsystem qualification, public technical preview security gates, and final publication approval remain open. No commit or push was made.
