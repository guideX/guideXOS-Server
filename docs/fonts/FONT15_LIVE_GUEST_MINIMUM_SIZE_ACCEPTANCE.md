# guideXOS Server — FONT15 Live Guest Minimum-Size Acceptance

**Outcome: B — guest acceptance incomplete, blocked by PS/2 pointer delivery.** The production Settings application rendered the approved Roboto TrueType faces in a real AMD64 QEMU guest at its observed 760×500 default window size. Keyboard-driven font selection, preview sizing, Apply, Cancel, bitmap fallback, and preference reload after a new QEMU boot were demonstrated. VNC and QEMU mouse events did not reach the requested guest coordinates, so the same live window could not be qualified at 560×360, the undersize clamp could not be checked, and pointer hit testing and pointer-only actions remain open. No FONT15 production defect was demonstrated. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open.

**Date:** 2026-10-20 PDT (raw guest/QAC logs are dated 2026-10-21 UTC)  
**Checkout:** `D:\dev\guideXOSServer_FONT_SUPPORT`  
**Branch:** `FONT_SUPPORT`  
**Starting and ending HEAD:** `ec221271e1d0c2f918202c56ba899b497cb172ae` (expected FONT14 baseline)

## 1. Repository state and phase marker

The required branch and FONT14 ending commit were present. Startup and ending status each had zero staged paths, eight tracked unstaged modifications, and 52 untracked status entries. The pre-existing modifications and untracked artifacts were preserved; the report and raw evidence remain untracked. No production kernel or test source was changed, no branch operation occurred, and there was no commit or push. See [`retry-startup-git-status.txt`](../../out/font15-validation/retry-startup-git-status.txt), [`ending-git-status.txt`](../../out/font15-validation/ending-git-status.txt), and [`ending-head.txt`](../../out/font15-validation/ending-head.txt).

The report was checked before resuming. FONT14 already required the marker to remain open because its dimensions and pointer rectangles were hosted measurements. FONT15 did not close it. The full startup inventory remains in [`startup-git-status.txt`](../../out/font15-validation/startup-git-status.txt).

## 2. Candidate and provenance

The candidate was staged in the private writable folder-backed FAT tree [`candidate/ESP`](../../out/font15-validation/candidate/ESP), with firmware [`OVMF.fd`](../../out/font15-validation/candidate/OVMF.fd). It used the current HEAD AMD64 kernel build and the copied bootloader; the original repository ESP and approved package were not modified. There is no single raw disk-image hash because QEMU used a folder-backed FAT drive. The per-file post-run manifest and critical component hashes are in [`candidate-postrun-v3.json`](../../out/font15-validation/candidate/candidate-postrun-v3.json) and [`postrun-esp-files-v3.json`](../../out/font15-validation/candidate/postrun-esp-files-v3.json).

| Candidate component | SHA-256 |
|---|---|
| Firmware `OVMF.fd` | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |
| Bootloader `BOOTX64.EFI` | `598552E5A5C154365667737D81336F54325B35D3206A235C3594DDB0522CF5D0` |
| AMD64 kernel ELF | `F53974791A47AA491C3091DF8E576E2107FF6DD39CDEAF269355972717BDCC04` |
| AMD64 kernel PE | `5EBAA6ADB41985194B3ABDC0ECC962DDBA873D36C8F63789AB4938E15F9F2662` |
| `ramdisk.img` (64 MiB) | `0B21C7D4691B4B2CCA9DA530E5A0E5699C85AB5CA0C918F0DFC981CAC646B960` |
| Approved font manifest | `5A5862DCD4110C0E9C9BDDD45D917E195F8804DE8D55D03C65F5529C28EE2FD2` |
| Roboto Regular | `1078DEDD79C55859B102E3EA141D8A74E5FB1F8F28A15D155BF7FC9B5B628346` |
| Roboto Bold | `76A6C0F614D0E3E66F111BA0C9DCF266BFE6B1CD80F67FDAA87B722A3642BBAD` |
| Roboto Italic | `3BBF16D61BE1F6B764AA91E74EA29AE9D0EB5E90615D86312DA699FA637FC982` |

These hashes match both the disposable candidate files and the approved package files; the post-run manifest provides the archived candidate verification.

The first private boot exposed the generated package at `/fonts` inside `ramdisk.img`, but the guest reported that the wallpaper/runtime image failed to mount at `/system`; consequently that first run showed bitmap fallback only. After inspection of the guest VFS path and the existing FONT6 proof setup, the same approved package was also staged at the root-filesystem path `ESP/system/fonts`. V2/V3 serial logs then showed the guest opening all three `/system/fonts/Roboto-v2.138-*.ttf` files. This was a private candidate staging correction, not a production source or asset change. The candidate staging records are [`candidate-staging-manifest.json`](../../out/font15-validation/candidate/candidate-staging-manifest.json), [`candidate-runtime-v2-manifest.json`](../../out/font15-validation/candidate/candidate-runtime-v2-manifest.json), and [`candidate-postrun-v3.json`](../../out/font15-validation/candidate/candidate-postrun-v3.json).

## 3. QEMU ownership and isolation

All three guest launches ran as direct QAC-managed QEMU children under separate `acceptance` reservations. The first request waited in QAC's exclusive FIFO until admitted. Later prelaunch snapshots showed the managed lane empty and zero foreign QEMU processes; QAC granted each request before the guest started. The QAC reservation IDs and child PIDs were:

| Reservation | QEMU PID | Result |
|---|---:|---|
| `8d12af52583546b9a52be83d67b3bd7b` | 14892 | Booted initial candidate; exited through QMP, QAC released |
| `1719f32b022f47389c6bf7254c4ed077` | 28116 | Booted V2 candidate; exited through QMP, QAC released |
| `c405e92f4b9c4f32865d0f3e7cad53eb` | 24124 | Booted V3 candidate; exited through QMP, QAC released |

The launch records are [`qemu-launch-planned.json`](../../out/font15-validation/guest/qemu-launch-planned.json), [`qemu-launch-v2.json`](../../out/font15-validation/guest/qemu-launch-v2.json), and [`qemu-launch-v3.json`](../../out/font15-validation/guest/qemu-launch-v3.json); QAC child and release output is in [`qac-run.log`](../../out/font15-validation/guest/qac-run.log), [`qac-run-v2.log`](../../out/font15-validation/guest/qac-run-v2.log), and [`qac-run-v3.log`](../../out/font15-validation/guest/qac-run-v3.log). QMP and VNC were bound to loopback ports 63781–63783 and 5950–5952, with separate serial paths and a private candidate. Networking was disabled. No foreign guest was contacted or controlled.

After the final guest exited, QAC reported zero QEMU processes, reservations, managed sessions, queued work, and blocked work. The final QEMU process census and endpoint census were empty: [`qac-final-status.txt`](../../out/font15-validation/guest/qac-final-status.txt), [`qac-final-list.txt`](../../out/font15-validation/guest/qac-final-list.txt), [`qemu-final-process-census.json`](../../out/font15-validation/guest/qemu-final-process-census.json), [`qemu-final-endpoints.json`](../../out/font15-validation/guest/qemu-final-endpoints.json). This establishes an exclusive QAC acceptance reservation during each run; it is not a claim of machine-wide isolation beyond QAC's census and coordination scope.

## 4. Guest boot and real rendering

QEMU 11.0.0 booted the real AMD64 kernel and production Settings implementation at 1280×800. V2/V3 serial logs reached `[KERNEL] Entering main loop (waiting for input)...`; they showed `[FONT6] system selection init=Ok active-font fallback-available=yes` and opens for all three approved TTF files. No kernel-fault, native-ELF-fault, panic, hang, or unexpected QEMU exit was observed during the UI run. The guest displayed the actual guideXOS desktop and Settings window, with the correct `Settings` title and real Fonts page.

Raw QMP screendumps, converted losslessly from PPM to PNG, include:

- [Desktop boot](../../out/font15-validation/guest/desktop-v2-start.png)
- [Fonts page with Roboto choices and bitmap active](../../out/font15-validation/guest/fonts-page-v2.png)
- [Roboto Regular pending, 16 px TrueType preview](../../out/font15-validation/guest/font-select-regular-pending-v2.png)
- [Preview after increasing sample size to 17 px](../../out/font15-validation/guest/font-size-plus-v2.png)
- [Roboto applied and active](../../out/font15-validation/guest/font-roboto-applied-confirmed-v2.png)
- [Roboto still active after a new QEMU process boot](../../out/font15-validation/guest/fonts-after-restart-v3.png)
- [Bitmap fallback selected, applied, and saved](../../out/font15-validation/guest/bitmap-applied-v3.png)
- [Fonts page after the unsuccessful minimum-size drag](../../out/font15-validation/guest/resize-to-min-result-v3.png)
- [Fonts page after the unsuccessful 480×300 drag](../../out/font15-validation/guest/resize-480x300-result-v3.png)

The guest also printed `[VFS] ERROR: Filesystem mount failed` while trying to mount the wallpaper/runtime ramdisk at `/system`; the desktop used its gradient fallback. The persistent root FAT volume mounted at `/` and supplied `/system/fonts`, so this did not prevent Settings or TrueType rendering. The same wallpaper-pack mount failure appears in older guest evidence; it is not evidence of a FONT15 typography defect.

Serial evidence is preserved in [`font15-boot.serial.log`](../../out/font15-validation/guest/font15-boot.serial.log), [`font15-boot-v2.serial.log`](../../out/font15-validation/guest/font15-boot-v2.serial.log), and [`font15-boot-v3.serial.log`](../../out/font15-validation/guest/font15-boot-v3.serial.log).

## 5. Window dimensions and layout

The live Settings window was pixel-measured from the guest screenshots at its observed default outer bounds, approximately x=95..855 and y=70..570: **760×500 outer**. The 24 px title bar leaves an approximately **760×476 client area**. At this size, the Fonts page, font list, preview, size controls, Apply, Cancel, and Restore Defaults controls were visible without unexpected overlap or corruption. This provides real guest evidence at 760×500.

The requested resize events and observed dimensions were:

| Requested action | Requested guest gesture | Actual observed result |
|---|---|---|
| Resize to 560×360 | VNC drag from (852,568) to (652,428) | Window remained 760×500; 560×360 not reached |
| Request 480×300 | VNC drag from (852,568) to (572,368) | Window remained 760×500; minimum clamp not exercised |
| 561×361 | Not attempted after pointer delivery failed | Unverified |
| 640×400 | Not attempted after pointer delivery failed | Unverified |
| 760×500 | Initial live size, pixel-measured | Observed at 760×500 |

The screenshots distinguish the requested gestures from the actual window size. Because the window never reached minimum size, FONT15 did **not** verify the 560×336 client area, 33 px Fonts preview, 29 px inner clip, compact two-column choice layout, minimum-size scrolling, bounded scroll positions, or the clamp from 480×300. The same-window grow/shrink cycle is also open.

## 6. Input delivery and hit testing

The QEMU command used the default PC PS/2 relative mouse and did not add a USB tablet. QEMU's `info mice` response identified `QEMU PS/2 Mouse`. A VNC RFB client successfully sent pointer and drag messages to the loopback VNC server, but the visible cursor did not reach the requested row, Restore Defaults button, or resize grip. A QMP absolute pointer event was rejected with `Input handler not found for event type abs`, consistent with the absence of an absolute input handler.

The best quantified guest observation was a QEMU HMP relative move requesting **−124,+139** pixels; the visible cursor moved only about **−5,+19** pixels (approximate screenshot measurement). The Settings-native serial log contained no `input source=mouse` line. The PS/2 driver only logs its first five IRQ12 events, so the serial trace cannot prove the IRQ count for every later VNC event. The available evidence places the blocker in the QEMU PS/2/Modern Input delivery path, probably before a pointer event reaches the intended compositor hit rectangle; it does not establish a compositor rectangle defect. Full observations and traces are in [`input-delivery-summary-v3.json`](../../out/font15-validation/guest/input-delivery-summary-v3.json), [`pointer-hit-test-v3.jsonl`](../../out/font15-validation/guest/pointer-hit-test-v3.jsonl), [`restore-default-pointer-v3.jsonl`](../../out/font15-validation/guest/restore-default-pointer-v3.jsonl), [`resize-to-min-attempt-v3.jsonl`](../../out/font15-validation/guest/resize-to-min-attempt-v3.jsonl), and [`resize-480x300-attempt-v3.jsonl`](../../out/font15-validation/guest/resize-480x300-attempt-v3.jsonl).

Keyboard navigation and controls were exercised through QEMU-injected guest keyboard events as supplementary evidence: the Settings shell launch route, Personalization/Fonts shortcuts, font selection, size adjustment, Apply, and Cancel all produced visible guest results. This does not count as successful physical pointer delivery. No Modern Input driver or production input code was changed, and no proof-build instrumentation was added.

## 7. Font selection, actions, persistence, and fallback

The Fonts page enumerated Built-in Bitmap plus Roboto Bold, Italic, and Regular, with the selected TTF path shown as `Roboto-v2.138-Regular.ttf`. The TrueType preview visibly changed from bitmap text to smooth 16 px Roboto; increasing the pending size changed the preview to 17 px. Apply changed the active face to `Roboto | Regular 16 px` and the guest displayed “Font applied and saved.” The corresponding preference record was written to the disposable candidate.

The first TrueType preference was then loaded by a **new QAC-managed QEMU process** using the same candidate: reopening Settings showed `Current: Roboto | Regular 16 px`. This verifies preference reload across a VM restart. An orderly in-OS reboot was not achieved: keyboard input did not reach the shell's `reboot` command, and QMP `system_reset` combined with `-no-reboot` stopped the VM in QEMU's `shutdown` state. QAC then exited that private process and a new one booted the same candidate. This is not claimed as a clean in-OS shutdown or power-loss test.

Cancel discarded a pending TTF selection and displayed “Pending changes discarded. The active font was not changed.” Explicit bitmap selection and Apply restored `guideXOS Bitmap | Regular 7 px`; the guest displayed “Font applied and saved.” Restore Defaults could not be activated because the pointer did not reach its control. The preference records remained on the private candidate only. The two-record persistence format is not treated as proof of power-loss atomicity.

## 8. Hosted regression results

The required hosted typography/Settings suites passed with **1,052 checks and 0 failures**:

| Suite | Result |
|---|---:|
| FONT6 system font selection | 420 checks, 0 failures |
| FONT10 shell typography | 88 checks, 0 failures |
| FONT11 cache workload | 48 checks, 0 failures |
| FONT14 Settings layout and package typography | 496 checks, 0 failures |

Per-suite logs are [`FONT6.log`](../../out/font15-validation/logs/FONT6.log), [`FONT10.log`](../../out/font15-validation/logs/FONT10.log), [`FONT11.log`](../../out/font15-validation/logs/FONT11.log), and [`FONT14.log`](../../out/font15-validation/logs/FONT14.log); executables are in [`out/font15-validation/tests`](../../out/font15-validation/tests). The prior FONT2–FONT11 total of 6,681 hosted checks and FONT6 guest guarantees remain historical evidence; the broader FONT2–FONT11 set was not rerun in FONT15.

## 9. Freestanding and separate release-build validation

The AMD64 freestanding kernel PE/ELF build passed from HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae` using `C:\mingw64\bin\mingw32-make.exe -j2 ARCH=amd64 BUILD_DIR=build/font15-amd64` in `kernel/`. Build logs are [`kernel-build.log`](../../out/font15-validation/candidate/kernel-build.log) and [`kernel-build.stderr.log`](../../out/font15-validation/candidate/kernel-build.stderr.log); hashes are in [`kernel-build-manifest.json`](../../out/font15-validation/candidate/kernel-build-manifest.json). PE Import Directory, Import Address Table, and Delay Import Directory are all zero in [`pe-import-directories.txt`](../../out/font15-validation/candidate/pe-import-directories.txt). No kernel source changed during FONT15; no new static-memory delta was introduced, so the FONT14 review remains applicable.

The separate top-level Server release build remains blocked by the previously reported PacMan audio link inputs. FONT15 did not repair, rerun, or relabel that prerequisite as resolved.

## 10. Acceptance gate status

| Gate | FONT15 status |
|---|---|
| Candidate provenance and approved Roboto package | **Pass** for the disposable guest candidate |
| QAC-managed exclusive acceptance and isolated endpoints | **Pass** for the three managed runs; all released |
| Real guest boot and Settings/Fonts rendering | **Pass** at observed 760×500 |
| 560×360, 561×361, and 640×400 live layouts | **Open** — pointer could not resize the window |
| 480×300 minimum clamp | **Open** — undersize request did not reach the window manager |
| Minimum preview clipping, compact columns, and scroll bounds | **Open** — minimum client size was not reached |
| Pointer delivery and displayed-rectangle hit testing | **Blocked** — PS/2 pointer under-delivery; no target hit proven |
| Keyboard-driven font selection, Apply, and Cancel | **Pass** as guest-visible QMP keyboard evidence |
| TrueType preference reload on a new QEMU boot | **Pass**; clean in-OS shutdown remains unverified |
| Bitmap fallback selection and Apply | **Pass** |
| Restore Defaults pointer action | **Open** — pointer did not reach the control |
| Hosted FONT6/FONT10/FONT11/FONT14 regressions | **Pass** — 1,052 checks |
| Freestanding AMD64 PE/ELF build and zero-import checks | **Pass** |
| `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` | **Open** |

## 11. Defects and blockers

No confirmed Fonts, Settings, or compositor defect was demonstrated. TrueType rendering, preview-size changes, Apply, Cancel, saved selection reload, and bitmap fallback worked in the guest. The release gate remains open because guest pointer delivery did not reach the requested coordinates, leaving minimum-size interaction and hit testing unverified. The ramdisk wallpaper mount failure is separately observed; the root VFS font path worked after private candidate staging correction.

## 12. Remaining FONT15 and v0.3 gates

FONT15 needs a functioning guest pointer path and a repeat of the same-window drag sequence at 560×360, 561×361, 640×400, and 760×500, plus the 480×300 clamp. At minimum size, verify the 560×336 client area, preview clipping and scrolling, compact choice layout, control hit rectangles, Restore Defaults, and one grow/shrink cycle. Preserve the demonstrated TTF selection, persistence, Apply/Cancel, and bitmap fallback results.

This incomplete Outcome B leaves `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. The PacMan/top-level release-build prerequisite, full Server v0.3 integrated candidate, broader desktop/subsystem regression acceptance, and final combined release readiness also remain independently open. FONT15 does not authorize publication.

## 13. FONT15 changes and commit

No production kernel, input, Settings, or test sources were changed. Private QMP/VNC helpers, candidate copies, screenshots, logs, and manifests were written under [`out/font15-validation`](../../out/font15-validation). The report was updated in place. Starting and ending HEAD remain `ec221271e1d0c2f918202c56ba899b497cb172ae`; there is no FONT15 commit and nothing was pushed. The worktree is not clean: the eight pre-existing tracked modifications and 52 untracked status entries remain.
