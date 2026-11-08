# guideXOS Server — FONT15R4 Final Guest Acceptance

**Outcome B — `GUEST_ACCEPTANCE_INCOMPLETE` (latest controlled attempt: retry3).** QAC granted exclusive acceptance ownership and the production AMD64 candidate booted to the real desktop at 1280×800. Retry3 demonstrated guest-visible separate-axis movement for small X and Y requests. After a bounded X-only navigation movement, the helper could not verify a new QMP connection for the follow-up screenshot, so the observed displacement could not be measured. Input stopped at that point; no control activation, drag, or Settings interaction followed. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. No production defect was demonstrated. Retry2 details remain below; retry3 evidence is in section 8.

**Date:** Retry2 2026-11-07 PST; retry3 2026-11-08 PST (guest and QAC logs use 2026-11-08 UTC) · **Repository:** `D:\dev\guideXOSServer_FONT_SUPPORT` · **Branch:** `FONT_SUPPORT`

## 1. Repository preservation and QAC admission

FONT15R4 started and finished guest execution and hosted regression runs at HEAD `7e7adafcea4beda6f7322dafef03a027f56356d4`. This is later than the FONT15R3 checkpoint; HEAD was not reset. The index was empty. One pre-existing tracked modification was present and preserved: `out/font15r1-validation/guest/font15r1_qmp.py`. No production source was changed. The end-of-execution status is recorded in [`git-status-end-before-report-r4-retry2.txt`](../../out/font15r4-validation/retry2/guest/git-status-end-before-report-r4-retry2.txt).

Existing FONT15R4 staging records were inspected and preserved. They show an earlier request canceled while queued and a later preflight that found another guideXOS Server acceptance guest active. For this guest attempt, the fresh QAC 2.0.0 status/list/queue snapshot showed zero QEMU processes, reservations, managed sessions, queued requests, and blocked requests. QAC granted the new request immediately; no queue wait was used.

| QAC evidence | Result |
|---|---|
| Coordinator | Online, PID 8204 |
| Reservation | `d9194285fab94b01b9aaac07430d5770`, `guideXOSServer`, `acceptance` |
| Grant | `2026-11-08T02:54:22.3385998Z` |
| QAC owner | PID 10724; owner identity was verified while the child ran |
| Registered child | QEMU PID 11916, `C:\Program Files\qemu\qemu-system-x86_64.exe` |
| Final reservation inspection | `Released`; reason `Registered child process exited.` |
| Final QAC census | 0 QEMU, 0 reservations, 0 sessions, 0 queued, 0 blocked |

During cleanup, interrupting the QAC client ended its owner process before the QEMU child exited. QAC correctly marked the reservation `Recovery Required` while PID 11916 remained live. A single verified-helper QMP `quit` then produced a `SHUTDOWN` event; QAC inspected the exited child and recorded the reservation as `Released`. The intermediate recovery state and final cleanup are preserved in [`qac-recovery-cleanup-observation-r4-retry2.txt`](../../out/font15r4-validation/retry2/guest/qac-recovery-cleanup-observation-r4-retry2.txt), [`qmp-quit-recovery-r4-retry2.jsonl`](../../out/font15r4-validation/retry2/guest/qmp-quit-recovery-r4-retry2.jsonl), and [`qac-final-inspect-r4-retry2.json`](../../out/font15r4-validation/retry2/guest/qac-final-inspect-r4-retry2.json). The final process census and endpoint check were empty.

## 2. Candidate and launch provenance

The guest used a fresh writable copy under `out/font15r4-validation/retry2/candidate`. It was copied from the previously verified FONT15R4 staging and matched all 23 prelaunch manifest entries. The ESP passed a write probe. Firmware was mounted read-only, networking was disabled, and QAC launched QEMU as the reservation's direct child. No production source changed under `kernel`, `bootloader`, or `uefi` between the candidate source HEAD and the execution HEAD. The booted copy's 23-file postrun manifest matched the prelaunch hashes.

| Component | Bytes | SHA-256 |
|---|---:|---|
| OVMF firmware | 3,653,632 | `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792` |
| UEFI `BOOTX64.EFI` | 56,832 | `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0` |
| AMD64 `kernel.elf` | 2,819,544 | `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04` |
| `ramdisk.img` | 67,108,864 | `0b21c7d4691b4b2cca9da530e5a0e5699c85ab5ca0c918f0dfc981cac646b960` |
| Roboto Regular | 88,780 | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` |
| Roboto Bold | 90,556 | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` |
| Roboto Italic | 94,736 | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` |
| QEMU 11.0.0 executable | — | `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02` |

The existing FONT14/FONT15 freestanding AMD64 build and zero PE import-directory results remain the applicable build evidence. Since production source and candidate hashes matched, no freestanding rebuild was needed. See [`candidate-prelaunch-r4-retry2.json`](../../out/font15r4-validation/retry2/guest/candidate-prelaunch-r4-retry2.json) and [`candidate-postrun-manifest-r4-retry2.json`](../../out/font15r4-validation/retry2/guest/candidate-postrun-manifest-r4-retry2.json).

## 3. Real guest boot and framebuffer

QEMU 11.0.0 booted the AMD64 candidate. QAC reported the registered QEMU PID and executable identity. The verified loopback QMP query returned guest status `running`; `info mice` reported the QEMU PS/2 mouse. The raw QMP framebuffer capture is 1280×800 (3,072,016-byte PPM). It shows the real guideXOS desktop and the pointer near the framebuffer center, approximately `(640, 408)` by visual estimate.

The serial log reached `[KERNEL] Entering main loop (waiting for input)...` and recorded successful opens of `/system/fonts/Roboto-v2.138-Bold.ttf`, `Roboto-v2.138-Italic.ttf`, and `Roboto-v2.138-Regular.ttf`, followed by `[FONT6] system selection init=Ok active-font fallback-available=yes`. The desktop used its gradient wallpaper fallback after a filesystem mount error; this did not prevent desktop boot or loading the font files. QEMU also printed `Ignoring request for interrupt vector 0` during startup.

Evidence: [initial desktop PNG](../../out/font15r4-validation/retry2/guest/initial-desktop-r4-retry2.png), [raw PPM](../../out/font15r4-validation/retry2/guest/initial-desktop-r4-retry2.ppm), [serial log](../../out/font15r4-validation/retry2/guest/font15r4-retry2.serial.log), [initial QMP query trace](../../out/font15r4-validation/retry2/guest/qmp-initial-query-r4-retry2.jsonl), and [screenshot hashes](../../out/font15r4-validation/retry2/guest/screenshot-hashes-r4-retry2.json).

## 4. Paced-QMP input result

The initial QMP query and screenshot were acknowledged. The first input action requested a bounded X-only movement of `dx=24`, `dy=0`, with 200 ms pacing. The helper exited 1 with `established QMP connection owner set [] does not match the expected QAC-owned PID 11916`. The helper rejected the connection before sending `input-send-event`; it created no movement trace. Therefore, there is no QMP movement acknowledgment and no guest-observed displacement result. The exact request and disposition are recorded in [`qmp-input-failure-r4-retry2.json`](../../out/font15r4-validation/retry2/guest/qmp-input-failure-r4-retry2.json).

| Check | Requested | QMP acknowledgment | Guest-observed result |
|---|---|---|---|
| Initial pointer | Capture framebuffer | Screenshot command succeeded | Pointer visible near `(640, 408)`, approximate |
| X-only movement | `dx=24`, `dy=0`, 200 ms | Rejected before input command | Not measured; no event sent |
| Y-only movement | Separate-axis method | Not attempted | Unverified |
| Desktop control activation | Navigate and click | Not attempted | Unverified |
| Bounded drag / release | One bounded drag | Not attempted; no button-down sent | No button was held; release state was not pending |

Per the stop condition, no retry or alternate input path was used. QMP acknowledgments are not treated as proof of guest event consumption.

## 5. Settings and minimum-size checks

Pointer interaction was not established, so the production Settings application was not opened. No same-window resize or minimum-size typography item was attempted.

| Requirement | Result |
|---|---|
| Settings initial outer size 760×500 | Unverified; Settings not opened |
| Intermediate 640×400 and minimum 560×360 | Unverified |
| Minimum client size 560×336 | Unverified |
| 561×361 boundary and 480×300 clamp | Unverified |
| Same-instance grow/shrink cycle | Unverified |
| Roboto rendering and preview clipping at minimum | Unverified in Settings |
| Compact two-column choices, font controls, and bounded preview scroll | Unverified |
| Back, Apply, Cancel, and bitmap fallback | Unverified |
| Production draw/hit-test agreement, responsiveness, and no overlap | Unverified |

The boot log confirms that the guest opened the Roboto files, but it does not qualify their appearance or interaction in the minimum-size Settings window.

## 6. Post-guest regressions

All required hosted regressions passed after the guest attempt. The helper suite also passed before admission and again after guest cleanup.

| Suite | Result |
|---|---:|
| FONT6 | 420/420 |
| FONT10 | 88/88 |
| FONT11 | 48/48 |
| FONT14 | 496/496 |
| **Hosted typography total** | **1,052/1,052** |
| FONT15R2R1 QMP safety, prelaunch | 18/18 |
| FONT15R2R1 QMP safety, post-guest | 18/18 |

Logs and hosted executables are under [`out/font15r4-validation/retry2`](../../out/font15r4-validation/retry2); the compact results summary is [`hosted-regression-summary-r4-retry2.json`](../../out/font15r4-validation/retry2/logs/hosted-regression-summary-r4-retry2.json). No production source or tests were edited.

## 7. Outcome and remaining gates

The exact outcome is **`GUEST_ACCEPTANCE_INCOMPLETE`**, caused by the helper's inability to verify the accepted QMP connection owner for the first input attempt. No pointer event was delivered, so this is not evidence of a PS/2 driver failure or a typography defect. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. The candidate boot, initial framebuffer, font-file opens, QAC grant, helper failure, and cleanup evidence are preserved for a later controlled attempt.

FONT15R4 does not close the independent PacMan audio-link prerequisite, the top-level guideXOS Server v0.3 release build, combined subsystem integration, final release qualification, or publication authorization. No typography feature, driver change, QEMU configuration change, or production source fix was made.

The execution HEAD was `7e7adafcea4beda6f7322dafef03a027f56356d4`; the report-only local commit SHA, if committed, is stated in the Codex completion message.

## 8. Retry3 controlled guest attempt — 2026-11-08 PST

Retry3 ran on branch `FONT_SUPPORT` at starting and ending execution HEAD `e9d3b86e86b504d806552f4f6e421335eb38f6bb`. The index was empty. The actual starting worktree had one pre-existing tracked modification, `out/font15r1-validation/guest/font15r1_qmp.py`; it was left unchanged and unstaged. The eight tracked modifications listed in earlier phase reports were no longer present at this checkpoint. Start and end status are in [`git-status-start-r4-retry3.txt`](../../out/font15r4-validation/retry3/guest/git-status-start-r4-retry3.txt) and [`git-status-end-before-report-r4-retry3.txt`](../../out/font15r4-validation/retry3/guest/git-status-end-before-report-r4-retry3.txt).

The existing incomplete report and retry2 evidence were preserved. Retry3 uses a fresh writable copy under [`retry3/candidate`](../../out/font15r4-validation/retry3/candidate), with a 24-entry [prelaunch manifest](../../out/font15r4-validation/retry3/guest/candidate-prelaunch-r4-retry3.json) and [postrun manifest](../../out/font15r4-validation/retry3/guest/candidate-postrun-manifest-r4-retry3.json). Every postrun hash matched. The read-only `OVMF.fd` hash is `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`; `BOOTX64.EFI` is `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0`; `kernel.elf` is `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`; and `ramdisk.img` is `0b21c7d4691b4b2cca9da530e5a0e5699c85ab5ca0c918f0dfc981cac646b960`. The three approved Roboto face hashes match the retry2 table in section 2. The QEMU 11.0.0 executable hash is `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`. No `kernel`, `bootloader`, or `uefi` production path changed since candidate source HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae`; the existing freestanding AMD64 build and zero PE import-directory evidence remains applicable. The writable ESP passed a 29-byte write probe. Networking was disabled.

Immediately before admission, QAC 2.0.0 (coordinator PID 8204) reported zero QEMU processes, reservations, managed sessions, queued requests, and blocked requests. The list showed no active owner or project priority. Reservation `826d11c596e04e33bb15d5082982d38a` was granted immediately in acceptance mode at `2026-11-08T18:56:22.4224386Z` to `guideXOSServer`; the client owner was PID 11080 and the registered direct QEMU child was PID 27568 at `C:\Program Files\qemu\qemu-system-x86_64.exe`. The reservation was not queued or degraded. QAC start snapshots are in [`qac-immediate-pre-request-status-r4-retry3.txt`](../../out/font15r4-validation/retry3/guest/qac-immediate-pre-request-status-r4-retry3.txt), [`qac-immediate-pre-request-list-r4-retry3.txt`](../../out/font15r4-validation/retry3/guest/qac-immediate-pre-request-list-r4-retry3.txt), and [`qac-immediate-pre-request-queue-r4-retry3.txt`](../../out/font15r4-validation/retry3/guest/qac-immediate-pre-request-queue-r4-retry3.txt). The QAC launch log is [`qac-run-r4-retry3.log`](../../out/font15r4-validation/retry3/guest/qac-run-r4-retry3.log).

The guest reached the real desktop at 1280×800. The initial raw QMP framebuffer is [`initial-desktop-r4-retry3.ppm`](../../out/font15r4-validation/retry3/guest/initial-desktop-r4-retry3.ppm), with a pixel-preserving PNG conversion at [`initial-desktop-r4-retry3.png`](../../out/font15r4-validation/retry3/guest/initial-desktop-r4-retry3.png). The serial log reached the kernel main loop and opened Roboto Bold, Italic, and Regular from `/system/fonts`; it also recorded `[FONT6] system selection init=Ok active-font fallback-available=yes`. The full serial log is [`font15r4-retry3.serial.log`](../../out/font15r4-validation/retry3/guest/font15r4-retry3.serial.log).

| Guest input check | Requested movement | QMP result | Guest-observed result |
|---|---|---|---|
| Initial pointer | Capture framebuffer | Screenshot command acknowledged | Pointer approximately `(640,408)` |
| X-only | `dx=24`, `dy=0`, 200 ms pacing | One X event acknowledged | Pointer moved approximately `(640,408)` → `(664,409)` |
| Y-only | `dx=0`, `dy=24`, 200 ms pacing | One Y event acknowledged | Pointer moved approximately `(664,409)` → `(664,433)` |
| Navigate toward Start control | `dx=-610`, `dy=0`, 200 ms pacing; 18 bounded X events | All 18 `input-send-event` commands acknowledged | Not measured; the next screenshot helper connection failed before sending a screenshot command |
| Control activation | No click sent | Not attempted | Unverified |
| Bounded drag / release | No button-down sent | Not attempted | No button was held; no release was pending |

The follow-up screenshot failure is preserved in [`qmp-post-navigation-screenshot-failure-r4-retry3.json`](../../out/font15r4-validation/retry3/guest/qmp-post-navigation-screenshot-failure-r4-retry3.json). The helper error was `established QMP connection owner set [] does not match the expected QAC-owned PID 27568`. The long movement trace is [`qmp-navigate-start-x-r4-retry3.jsonl`](../../out/font15r4-validation/retry3/guest/qmp-navigate-start-x-r4-retry3.jsonl). QMP acknowledgments do not establish consumption or displacement for that long movement. Per the stop condition, no further input was sent. The initial separate-axis traces and screenshots are [`qmp-x-only-move-r4-retry3.jsonl`](../../out/font15r4-validation/retry3/guest/qmp-x-only-move-r4-retry3.jsonl), [`desktop-after-x-only-r4-retry3.png`](../../out/font15r4-validation/retry3/guest/desktop-after-x-only-r4-retry3.png), [`qmp-y-only-move-r4-retry3.jsonl`](../../out/font15r4-validation/retry3/guest/qmp-y-only-move-r4-retry3.jsonl), and [`desktop-after-y-only-r4-retry3.png`](../../out/font15r4-validation/retry3/guest/desktop-after-y-only-r4-retry3.png). Screenshot hashes are in [`screenshot-hashes-r4-retry3.json`](../../out/font15r4-validation/retry3/guest/screenshot-hashes-r4-retry3.json).

Settings was not opened. The same-window 760×500 initial size, 640×400 intermediate size, 560×360 minimum, 560×336 client area, 561×361 boundary, 480×300 clamp, and grow/shrink cycle remain unverified. Minimum-size TrueType rendering, clipping, compact choice layout, font controls, preview scrolling, Back/navigation, Apply, Cancel, bitmap fallback, hit-test alignment, and responsiveness remain unverified in retry3. `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` stays open.

Cleanup used the same expected-PID-checked helper to send QMP `quit`. QEMU exited with code 0 at `2026-11-08T19:01:51.7063421Z`; QAC recorded the reservation `Released` with reason `Managed child exited.` Final QAC status/list/queue showed zero QEMU processes, reservations, managed sessions, queued requests, and blocked requests. The final [reservation inspection](../../out/font15r4-validation/retry3/guest/qac-final-inspect-r4-retry3.json) and [endpoint census](../../out/font15r4-validation/retry3/guest/endpoint-census-final-r4-retry3.json) show release and no remaining QEMU process or endpoint.

Post-guest regressions passed: FONT6 **420/420**, FONT10 **88/88**, FONT11 **48/48**, FONT14 **496/496** (**1,052/1,052 total**), and the FONT15R2R1 helper safety suite **18/18**. Logs and the results summary are under [`retry3/logs`](../../out/font15r4-validation/retry3/logs). The pre-admission helper safety run also passed 18/18. No production source, test source, driver, or typography behavior was changed in retry3. Retry3 did not close the v0.3 release boundaries listed in section 7.

The report update is the only intended tracked change for retry3. Disposable guest screenshots, logs, and manifests remain under `out/font15r4-validation/retry3`; nothing is pushed. The report-update commit SHA, if committed, is stated in the Codex completion message.
