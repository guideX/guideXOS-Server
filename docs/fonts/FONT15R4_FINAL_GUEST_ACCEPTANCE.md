# guideXOS Server — FONT15R4 Final Guest Acceptance

**Outcome B — `GUEST_ACCEPTANCE_INCOMPLETE`.** QAC granted exclusive acceptance ownership and the production AMD64 candidate booted to the real desktop at 1280×800. The serial log confirms that the guest opened the approved Roboto faces. The initial desktop screenshot captured the pointer, and QMP status and screenshot commands succeeded. The first paced X-only movement was rejected by the helper's accepted-connection ownership check before it sent an input event. Following the required stop condition, no more guest input was sent and Settings was not opened. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. No production defect was demonstrated.

**Date:** 2026-11-07 PST (guest and QAC logs use 2026-11-08 UTC) · **Repository:** `D:\dev\guideXOSServer_FONT_SUPPORT` · **Branch:** `FONT_SUPPORT`

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
