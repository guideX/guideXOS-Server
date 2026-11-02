# guideXOS Server — FONT15R2 Paced QMP Guest Acceptance

**Outcome B — `GUEST_INPUT_ACCEPTANCE_INCOMPLETE`.** QAC did not grant this candidate an exclusive acceptance reservation. One managed guest remained active, an earlier acceptance request was ahead in the FIFO queue, and FONT15R2's reservation stayed queued for about eight minutes. The request was canceled through QAC before admission. No FONT15R2 QEMU process started, so no pointer input or production Settings acceptance was performed. This is an ownership/evidence limitation; no font or compositor defect was demonstrated.

**Date:** 2026-10-22 PDT (raw QAC timestamps are 2026-10-23 UTC)  
**Branch:** `FONT_SUPPORT`  
**Starting and ending HEAD:** `4d67c76be214d6e8799dd482c554605208cdc29c`  
**Gate:** `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open.

## 1. Preservation and prior evidence

The starting branch was `FONT_SUPPORT`, one commit ahead of `origin/FONT_SUPPORT`, at the requested baseline HEAD. The worktree already contained tracked edits to `build.ps1`, `docs/fonts/FONT12_V03_RELEASE_QUALIFICATION.md`, two scripts, and four hosted test sources, plus existing untracked assets, reports, and validation artifacts. They were preserved. No reset, clean, stash, branch change, history rewrite, or push occurred. The root `.phase` file was absent. No other agent was active in this task tree.

The prior FONT15 and FONT15R1 reports remain the records for their earlier results. FONT15R1's accepted Outcome B is not repeated as a new result here. The only commit between the accepted candidate source HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae` and this phase's starting HEAD adds the R1 report; there is no kernel source delta between those HEADs.

## 2. Existing helper recovery and input method

The existing scripts were present under `out/font15r1-validation/guest/`: `font15r1_qmp.py`, `font15r1_vnc_web.py`, `font15r1_vnc_frame_probe.py`, and `ppm_to_png.py`. Their help/startup paths were checked. No helper source was changed and no second automation framework was created.

`font15r1_qmp.py` sends each relative axis as its own QMP command, splits each requested axis into steps no larger than 35 pixels, and waits 200 ms after each step. Button down/up transitions are separate and paced 150 ms apart; the drag action keeps the button held across its movement. This is a more conservative step size than DM34R1's 100-pixel maximum while retaining the independently delivered axes and pacing.

The actual DM34R1 implementation, `D:\dev\guideXOSServer_DiskManagerImprovements\scripts\dm34_qmp_gui_input.py` (SHA-256 `FD41BE33D4F5C9E0B363B41E6EA4206FA7427E3DA9736EA39691B58505641D02`), was inspected alongside `docs/DISK_MANAGER_PHASE_DM34_WIPE_AND_ERASE.md`. Its successful retry sent relative X and Y separately in steps up to 100 with 200 ms spacing, and separate button transitions with 150 ms spacing. Screenshots and guest behavior showed movement, widget hover/selection, and control activation. The same report records that combined X/Y requests were acknowledged by QMP but produced no cursor movement. No Disk Manager file was changed. These results validate the technique on DM34; they do not establish that it works on this FONT15 candidate.

The existing helper failure paths were exercised without QEMU:

- The QMP helper returned an error against a closed loopback port. A loopback stub then sent a valid QMP greeting, received `qmp_capabilities`, and withheld its reply; the helper exited with `TimeoutError` after 5.191 seconds, matching its five-second socket timeout. Evidence is in [the timeout record](../../out/font15r2-validation/guest/qmp-silent-response-timeout-r2.json) and [captured error](../../out/font15r2-validation/guest/qmp-silent-response-timeout-r2.txt).
- The VNC web helper served `/` with HTTP 200 in 66 ms. A WebSocket upgrade with no VNC server returned HTTP 502 in about 2.039 seconds. Ctrl+C stopped only the helper process; its loopback port was free afterward. The QMP closed-port result is in [the helper summary](../../out/font15r2-validation/guest/helper-validation-r2.json).
- The QMP helper's socket timeout bounds each connection/read, not the total duration of an arbitrary multi-step action. Its `drag` path does not send button-up in a `finally` if a mid-drag QMP request fails; a future live attempt must treat that as a helper limitation and shut down only its QAC-owned guest if release cannot be confirmed.

The last reported empty-firmware launch failure was addressed in the new launch plan by resolving and checking the nonempty copied `OVMF.fd` path. QEMU itself was not invoked in R2, so that preflight was not confirmed by a boot.

## 3. Candidate and QAC ownership

A disposable copy was made at `out/font15r2-validation/candidate/` from the existing accepted FONT15 candidate. All 23 listed files matched the prior R1 prelaunch and postrun hashes when copied. The [R2 prelaunch manifest](../../out/font15r2-validation/guest/candidate-prelaunch-r2.json) has SHA-256 `65FBDFBF2703346692FB5F7FC427C9FC5AF01CB0FB2745B23D9A883C3E2D4011`; [post-queue verification](../../out/font15r2-validation/guest/candidate-preservation-after-queue-r2.json) confirms every file stayed unchanged because no guest used it.

| Candidate component | SHA-256 |
|---|---|
| OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |
| UEFI `BOOTX64.EFI` | `598552E5A5C154365667737D81336F54325B35D3206A235C3594DDB0522CF5D0` |
| AMD64 `kernel.elf` | `F53974791A47AA491C3091DF8E576E2107FF6DD39CDEAF269355972717BDCC04` |
| `ramdisk.img` | `0B21C7D4691B4B2CCA9DA530E5A0E5699C85AB5CA0C918F0DFC981CAC646B960` |
| Approved font manifest | `5A5862DCD4110C0E9C9BDDD45D917E195F8804DE8D55D03C65F5529C28EE2FD2` |
| Roboto Regular | `1078DEDD79C55859B102E3EA141D8A74E5FB1F8F28A15D155BF7FC9B5B628346` |
| Roboto Bold | `76A6C0F614D0E3E66F111BA0C9DCF266BFE6B1CD80F67FDAA87B722A3642BBAD` |
| Roboto Italic | `3BBF16D61BE1F6B764AA91E74EA29AE9D0EB5E90615D86312DA699FA637FC982` |

The launch preflight resolved QEMU 11.0.0 at `C:\Program Files\qemu\qemu-system-x86_64.exe` (SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`), the copied read-only firmware, the kernel, bootloader, ramdisk, and font manifest. The only writable QEMU storage was the disposable folder-backed FAT ESP. The isolated plan used QMP `127.0.0.1:61381`, VNC `127.0.0.1:41237`, a loopback viewer on `127.0.0.1:41238`, and a new serial-log path. All three ports were free at preflight and after cancellation. The exact arguments are in [qemu-launch-plan-r2.json](../../out/font15r2-validation/guest/qemu-launch-plan-r2.json), SHA-256 `EDB1A0E12676300FCC702F92A9870ACF21A042A2657CE6DA053A7BB7328997E7`.

QAC was online. At submission it reported one managed acceptance guest, zero foreign QEMU processes, and two requests already ahead; FONT15R2 was admitted to position 3, then advanced to position 2. The documented QAC wait bound is 1,800 seconds. After about eight minutes with the active guest still running and one request still ahead, the FONT15R2 client was interrupted while queued. QAC recorded reservation `42407ed7e08f41d5ac80ee430869c5a5` as `Cancelled`, reason `Requesting client exited before admission`, with no child PID or process identity. The final QAC census still showed the other managed guest and one earlier queued request; they were not touched. See [the request log](../../out/font15r2-validation/guest/qac-run-r2.log), [final reservation record](../../out/font15r2-validation/guest/qac-final-inspect-r2.json), [QAC status](../../out/font15r2-validation/guest/qac-final-status-r2.txt), and [QAC list](../../out/font15r2-validation/guest/qac-final-list-r2.txt).

No candidate guest booted. No serial log, framebuffer, or FONT15R2 screenshot was produced. No QMP input request was sent to a guest. QMP acknowledgement, guest event delivery, cursor motion, and control activation were therefore not measured in R2.

## 4. Pointer and Settings acceptance

| Stage 4 pointer check | R2 result |
|---|---|
| Initial cursor position | Not captured; no guest boot |
| X-only and Y-only displacement | Not sent; requested and observed movements unavailable |
| Move to a known control and activate it | Not attempted |
| Button-held drag | Not attempted |
| Combined-axis route comparison | Not repeated; DM34's result remains specific to its candidate |

The required same-instance Settings sequence was not run. No R2 measurements or screenshots exist for the 760×500 starting window, 640×400, 560×360, 560×336 client area, 561×361, the 480×300 undersize clamp, or a grow/shrink cycle. At minimum size, preview clipping/scrolling, two-column choices, navigation, selection, font-size adjustment, Apply/Cancel, bitmap fallback, hit-test geometry, overlapping controls, and text rendering were not rechecked.

The earlier FONT15 report remains the evidence for its previously demonstrated TrueType/Roboto rendering, font controls, Apply/Cancel, bitmap fallback, preference reload, and 760×500 window. Those results are not R2 observations and do not close the missing minimum-size pointer acceptance. No production Settings or compositor defect was reproduced.

## 5. Hosted and freestanding qualification

The existing hosted validation executables were rerun, with fresh logs under `out/font15r2-validation/logs/`:

| Suite | Result |
|---|---:|
| FONT6 | 420 checks, 0 failures |
| FONT10 | 88 checks, 0 failures |
| FONT11 | 48 checks, 0 failures |
| FONT14 | 496 checks, 0 failures |
| **Total** | **1,052 checks, 0 failures** |

See [FONT6](../../out/font15r2-validation/logs/FONT6.log), [FONT10](../../out/font15r2-validation/logs/FONT10.log), [FONT11](../../out/font15r2-validation/logs/FONT11.log), [FONT14](../../out/font15r2-validation/logs/FONT14.log), and the [summary](../../out/font15r2-validation/logs/hosted-regression-summary-r2.json). These are reruns of the previously built hosted test binaries; no new test source or executable was added in R2.

No production kernel source changed. The actual candidate ELF and bootloader hashes match the previously qualified FONT15 build. The prior FONT15 report records its AMD64 PE/ELF build and zero PE import-directory checks, including kernel PE SHA-256 `5EBAA6ADB41985194B3ABDC0ECC962DDBA873D36C8F63789AB4938E15F9F2662`. No freestanding rebuild was needed or run in R2, and no new boot qualification is claimed.

## 6. Disposition and remaining gates

This is **Outcome B — `GUEST_INPUT_ACCEPTANCE_INCOMPLETE`**, caused by unavailable QAC ownership. It is not evidence of a font, Settings, compositor, or PS/2 driver defect. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. The large-diagonal PS/2 packet-drop condition remains an unproven hypothesis; R2 generated no raw guest input traces.

The typography gate still requires a QAC-owned AMD64 boot, observed separately paced axis movement and control activation, a supported button-held drag, and the complete same-Settings-instance resize/minimum-clamp sequence with authentic screenshots, serial logs, and input traces. The earlier Roboto, Apply/Cancel, bitmap fallback, and preference-reload results should be treated as prior evidence only.

The independent PacMan audio-link prerequisite, top-level Server v0.3 build gate, combined integration qualification, and final release authorization remain open. No FONT15 gate or broader release gate was closed by R2.

**Commit:** none. HEAD remains `4d67c76be214d6e8799dd482c554605208cdc29c`; no push was made.
