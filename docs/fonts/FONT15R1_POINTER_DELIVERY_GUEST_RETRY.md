# guideXOS Server — FONT15R1 Pointer Delivery and Guest Retry

**Outcome B — the minimum-size gate remains open.** FONT15R1 found no QEMU device configuration correction or already-qualified Modern Input source fix for the under-delivered pointer. The current PS/2 decoder has a specific candidate packet-drop condition, but existing traces do not contain the raw IRQ12 bytes needed to prove that it caused the earlier movement loss. A QAC-owned AMD64 diagnostic boot confirmed the candidate and framebuffer path; no pointer or keyboard events were sent in that boot because the available Windows UI automation path timed out. The same-window minimum-size sequence therefore was not repeated, and no Fonts, Settings, or compositor defect was demonstrated.

**Date:** 2026-10-22 PDT

**FONT_SUPPORT source:** branch FONT_SUPPORT, HEAD ec221271e1d0c2f918202c56ba899b497cb172ae

**Gate:** TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN remains open. There is no root .phase file.

## 1. Preflight and preservation

The required branch and HEAD matched the baseline. At startup the worktree had eight tracked modifications and 52 untracked status entries, with no staged paths. All were preserved. The new validation evidence adds one untracked out/font15r1-validation/ entry; the pre-existing 52 entries remain. No stash, reset, clean, branch change, history rewrite, or push occurred. No production or test source was edited in FONT_SUPPORT.

The prior [FONT15 report](FONT15_LIVE_GUEST_MINIMUM_SIZE_ACCEPTANCE.md) remains the source for the earlier Roboto rendering, Apply/Cancel, preference reload, bitmap fallback, 760×500 window measurement, and failed pointer measurements. Its acceptance gate was already open.

The independent input owner checkout was inspected read-only:

- D:\dev\guideXOSServer_MODERN_INPUT, branch MODERN_INPUT, HEAD 0a3f141433b37a9e46a8795f3f4cb29145d9efc8. Its pre-existing M ESP/build-identity.txt and ?? out/mbedtls-bootstrap/ were left in place.
- The owner checkout's ps2mouse.cpp, input_manager.cpp, and display_input_mapper.cpp SHA-256 hashes exactly match FONT_SUPPORT. No newer shared input fix is present in that checkout.
- Matching shared-source SHA-256 values are ps2mouse.cpp 3409D25E7AE024EAAC7A584FC10A39CFC7EA4EBE50351D00E6C024ED2D63FF14, input_manager.cpp 8825BCF79BFA7C239D69CFC32E7361DFCF90DBEA1DEA8A570EAB5568621F9EEB, and display_input_mapper.cpp 5C0653B50B5251779F0899C683E4A88545453EC0C4EB5C6D8ACC9BEC50DAAF11.
- D:\dev\guideXOSServer_DiskManagerImprovements, branch DISK_MANAGER_IMPROVEMENTS, HEAD e110046086ff4d06cf681580eb4d2df265a426ef, contains a qualified QMP pacing helper described below. Its worktree already had unrelated staged and unstaged work, including uncommitted DM37 input diagnostics; it was not changed.
- QEMU has no USB tablet or other absolute pointer configured for the FONT15 candidate. The current kernel only polls USB HID under KERNEL_HAS_USB_HID, which is not defined in this source tree. Adding -device usb-tablet therefore is not a usable correction for this candidate.

## 2. Pointer-delivery investigation

The earlier guest captures show the requested movement was not reaching the target control rectangles:

| Evidence | Requested guest movement | Observed guest result |
|---|---|---|
| FONT12R3, same Settings instance | From (465,264) toward resize grip (850,565) using 11 relative steps, each at most 35 px and 250 ms apart | Cursor reached about (501,301); no button-down at the grip; window stayed 760×500. See [FONT12R3 results](../evidence/font12r3/FONT12R3_RESULTS.md#pointer-attempt-and-alternate-path-review). |
| FONT15 V3, VNC RFB | Pointer targets included (1200,700) and the Roboto row at (390,288); resize drags requested (852,568)→(652,428) and (852,568)→(572,368) | The cursor stayed near the middle of the 1280×800 desktop; the row remained unselected and the Settings frame stayed 760×500. See [FONT15 input summary](../../out/font15-validation/guest/input-delivery-summary-v3.json). |
| FONT15 V3, HMP relative | Requested (-124,+139) toward Restore Defaults | Screenshot estimate moved only about (-5,+19), from roughly (620,390) to (615,409). No button activation was attempted. |

The QEMU configuration in the prior guest was pc with the implicit PS/2 mouse. info mice/query-mice identified QEMU PS/2 Mouse as relative; QMP absolute pointer input was rejected with Input handler not found for event type abs. The new R3 launch used the same machine and implicit mouse path. Its serial log reports IntelliMouse wheel negotiation succeeded and PS/2 was the active source.

Code review narrows the candidate failure location without proving the exact cause:

1. [ps2mouse.cpp](../../kernel/core/ps2mouse.cpp#L290-L310) sign-decodes packet X/Y, inverts Y, clamps each decoded axis to ±50, then discards the packet if both clamped axes are at the limit. A large diagonal packet can therefore be dropped. This is consistent with the earlier diagonal undershoots, but no raw packet trace ties those gestures to that branch. The current IRQ log records only the first five IRQ counts and status bytes, not packet bytes, decoded deltas, or drop reasons.
2. [input_manager.cpp](../../kernel/core/input_manager.cpp#L232-L253) derives relative deltas from cumulative PS/2 coordinates. [display_input_mapper.cpp](../../kernel/core/display_input_mapper.cpp#L339-L379) adds those deltas and clamps only to the virtual desktop bounds; this path contains no scale factor or coordinate transform.
3. [desktop.cpp](../../kernel/core/desktop.cpp#L12277-L12317) forwards the same pointer coordinates to the compositor. [kernel_compositor.cpp](../../kernel/core/kernel_compositor.cpp#L505-L563) hit-tests those coordinates, and a resize begins only after pointer-down in the bottom-right resize corner. The drag path uses the captured window and the pointer difference from its start point; the minimum clamp is in [window_geometry.h](../../kernel/core/include/kernel/window_geometry.h#L11-L27).

The measurements establish under-delivery before the target hit rectangle. They do not distinguish QEMU event coalescing/loss from packet decoding/filtering because the relevant raw packet and per-event coordinate logs are absent. The compositor's resize routing is consistent with the observed unchanged frame once the pointer misses the grip; no compositor or Settings defect is demonstrated.

An independently qualified **QMP harness** correction exists in scripts/dm34_qmp_gui_input.py at the DiskManagerImprovements revision above. It sends relative axes separately and paces individual key/button transitions. Its SHA-256 is FD41BE33D4F5C9E0B363B41E6EA4206FA7427E3DA9736EA39691B58505641D02; the DM34 report documents that combined/batched QMP events were acknowledged but unreliable, while paced events reached its guest controls. This is a QMP event-delivery workaround, not a demonstrated physical RFB pointer fix, so it cannot qualify the required pointer acceptance by itself. No helper input was sent to the R1 guest.

## 3. R3 candidate and QAC ownership

The private candidate was copied from the FONT15 V3 candidate; the prior candidate was not used as a writable target. The prelaunch manifest SHA-256 is 7CDB8454B68E02F724048FA779777E6369777794A9D4247EBCEF98F4AFBA5DA6. All 22 listed candidate files had identical prelaunch and postrun hashes; see the [prelaunch](../../out/font15r1-validation/candidate/prelaunch-manifest.json) and [postrun](../../out/font15r1-validation/guest/candidate-postrun-manifest-r3.json) manifests.

| Candidate component | SHA-256 |
|---|---|
| OVMF firmware | 2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792 |
| EFI/BOOT/BOOTX64.EFI | 598552E5A5C154365667737D81336F54325B35D3206A235C3594DDB0522CF5D0 |
| ESP/kernel.elf | F53974791A47AA491C3091DF8E576E2107FF6DD39CDEAF269355972717BDCC04 |
| ESP/ramdisk.img | 0B21C7D4691B4B2CCA9DA530E5A0E5699C85AB5CA0C918F0DFC981CAC646B960 |
| Approved font manifest | 5A5862DCD4110C0E9C9BDDD45D917E195F8804DE8D55D03C65F5529C28EE2FD2 |
| Roboto Regular | 1078DEDD79C55859B102E3EA141D8A74E5FB1F8F28A15D155BF7FC9B5B628346 |
| Roboto Bold | 76A6C0F614D0E3E66F111BA0C9DCF266BFE6B1CD80F67FDAA87B722A3642BBAD |
| Roboto Italic | 3BBF16D61BE1F6B764AA91E74EA29AE9D0EB5E90615D86312DA699FA637FC982 |

The guest used QEMU 11.0.0 (C:\Program Files\qemu\qemu-system-x86_64.exe, SHA-256 A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02), pc,accel=whpx, 2 vCPUs, 1 GiB RAM, standard VGA, network disabled, QMP 127.0.0.1:63801, and loopback VNC 127.0.0.1:5953. Exact argv is in [qemu-launch-r3.json](../../out/font15r1-validation/guest/qemu-launch-r3.json).

QAC's source checkout was branch main, HEAD 8ff500c60393e5e38fa4b8cd4d83f213b17df0f0. The running QAC 2.0.0 CLI and coordinator binaries had SHA-256 F7089D508C1D8307E11DE1B944B6E1087C038C4901333E8228EC117291CA8DF6 and 8B2F42E7DE19E2EDD3702572CAB1C2E5341D325678C2AC0A2E2FC6EEFCFD4B21, respectively. The binaries predate the current source HEAD; no source-to-binary identity is claimed.

Reservation a443492edcdc4d29914f3690e28abf53 was granted in acceptance mode to QEMU PID 14684 (QAC CLI PID 21592). QAC briefly marked its census incomplete at 21:28:54 UTC, then recorded ExclusiveRestored at 21:28:58 UTC. Before any guest query, the reservation inspection reported degraded=false and zero foreign QEMU processes. QEMU exited with code 0 at 21:34:05 UTC; QAC released the reservation. Final status was zero QEMU processes, zero reservations, zero managed sessions, zero queued/blocked work, and no R1 test endpoints. The event ledger and final status/list are [here](../../out/font15r1-validation/guest/qac-reservation-r3.json), [here](../../out/font15r1-validation/guest/qac-final-status-r3.txt), and [here](../../out/font15r1-validation/guest/qac-final-list-r3.txt).

Two earlier R1 launch attempts did not qualify a guest: one exited before boot because its firmware drive argument had an empty path, and the next could not connect while the coordinator was stopped. QAC granted no reservation for the latter. The successful R3 launch used corrected, recorded arguments.

## 4. R3 guest evidence and minimum-size matrix

The real AMD64 candidate reached the kernel main loop and desktop at 1280×800. QMP reported QEMU PS/2 Mouse. A read-only RFB probe negotiated RFB 3.8 and received one full 1280×800 raw 32-bpp framebuffer rectangle (4,096,000 bytes). This confirms the display path only. The QMP screenshot is [boot-r3.png](../../out/font15r1-validation/guest/boot-r3.png); the serial log and summarized markers are [font15r1-r3.serial.log](../../out/font15r1-validation/guest/font15r1-r3.serial.log) and [serial-summary-r3.json](../../out/font15r1-validation/guest/serial-summary-r3.json). The serial log reached the main loop, showed PS/2 active, and recorded no R1 pointer event or Settings mouse-input line.

The Windows computer-use helper timed out during window capture and again through its documented retry/reset sequence. The browser-only viewer displayed the guest framebuffer, but no pointer or keyboard event was sent. Settings was not opened in this R3 boot. No R1 measurement of Settings dimensions or hit testing is claimed.

| Requested qualification step | R1 result |
|---|---|
| Start same Settings window at 760×500 | Not repeated. Prior FONT15 measured 760×500 outer / 760×476 client. |
| Resize that window to 640×400 and 560×360 | Not attempted. |
| Verify 560×336 client area | Not attempted. |
| Test 561×361 | Not attempted. |
| Request 480×300 and verify 560×360 minimum clamp | Not attempted. |
| Verify preview clipping, compact layout, scrolling, and visible-control hit tests | Not attempted. |
| Same-window grow/shrink cycle | Not attempted. |
| Recheck font selection, Apply/Cancel, and bitmap fallback | Not repeated; previous FONT15 results remain prior-run evidence. |

## 5. Hosted regressions

The requested hosted checks passed against the current FONT_SUPPORT worktree:

| Suite | Result | Evidence |
|---|---:|---|
| FONT6 | 420 checks, 0 failures | [FONT6.log](../../out/font15r1-validation/logs/FONT6.log) |
| FONT10 | 88 checks, 0 failures | [FONT10.log](../../out/font15r1-validation/logs/FONT10.log) |
| FONT11 | 48 checks, 0 failures | [FONT11.log](../../out/font15r1-validation/logs/FONT11.log) |
| FONT14 | 496 checks, 0 failures | [FONT14.log](../../out/font15r1-validation/logs/FONT14.log) |
| **Total** | **1,052 checks, 0 failures** | Dedicated executables are under out/font15r1-validation/tests/. |

No production source changed, so no freestanding rebuild was needed for R1. The existing FONT15 build and zero-PE-import evidence remains as recorded in the prior report; R1 does not add a new build claim.

## 6. Disposition and handoff

**Outcome B — external pointer-acceptance blocker.** The real input limitation remains unresolved and no guest pointer acceptance was performed in R1. Keep TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN open. No production implementation was changed, and no Fonts, Settings, or compositor defect was demonstrated.

To resume with physical host pointer input, first confirm QAC is Online. In PowerShell, replay the exact captured QEMU argv through QAC:

    $manifest = Get-Content 'D:\dev\guideXOSServer_FONT_SUPPORT\out\font15r1-validation\guest\qemu-launch-r3.json' -Raw | ConvertFrom-Json
    $qacArgs = @('run','--project','guideXOSServer','--mode','acceptance','--') + @($manifest.args)
    & 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe' @qacArgs

In another local terminal, run:

    python 'D:\dev\guideXOSServer_FONT_SUPPORT\out\font15r1-validation\guest\font15r1_vnc_web.py' --http-port 6081 --vnc-port 5953

Then open http://127.0.0.1:6081/, use the visible VNC canvas to open Settings from the desktop launcher, and move/click with the host mouse. Record requested and observed guest coordinates before starting the same-window minimum sequence. Do not count QMP-generated pointer events as physical acceptance.

If the physical path still undershoots, the next input-owner qualification should record raw IRQ12 packet bytes and packet phase, decoded X/Y, whether the ±50 filter/drop condition fired, cumulative PS/2 coordinates, mapper raw/virtual coordinates, desktop coordinates, and compositor hit/resize result under one event ID. The current uncommitted DM37 diagnostics in the DiskManagerImprovements checkout only record post-decode events and compositor stages; they omit raw bytes and dropped packets and were not used for this candidate. Any shared input change belongs in the Modern Input owner checkout and requires its own qualification before assembling a FONT_SUPPORT candidate.

The independent PacMan audio-link prerequisite and combined Server v0.3 release qualification remain open. This Outcome B makes no claim on those gates.
