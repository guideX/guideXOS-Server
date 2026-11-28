# TASKBAR6 — Native Qualification, Application Integration & Final Polish

**Outcome:** **TASKBAR6_HOSTED_VERIFIED_GUEST_PENDING**
**Branch:** **TASKBAR_IMPROVEMENTS**
**Baseline:** **687a10b7c98c98648861c9c20e24747f5d3365f8** (TASKBAR5)

## Outcome and baseline

The hosted and native AMD64 builds and focused regressions pass. An AMD64 UEFI candidate booted to the guideXOS desktop and exposed a real GOP framebuffer. The final native auto-hide wiring was corrected and rebuilt after that guest run. Native taskbar interaction and guest restart persistence remain pending because CUA could capture the QEMU window but could not activate it to send input. Outcome B applies: the graphical guest was available, but the required interaction qualification could not be completed.

At preflight the repository was clean on TASKBAR_IMPROVEMENTS, at the baseline commit above. Local refs showed the branch four commits ahead of origin/main and one commit ahead of origin/TASKBAR_IMPROVEMENTS. The fetch attempt failed because GitHub rejected the configured SSH key. The counts are local-ref snapshots; current remote movement could not be verified. No merge, rebase, push, or v0.3 release-candidate change was made.

## TASKBAR1–TASKBAR6 evidence matrix

Historical counts below come from the milestone reports and were not rerun unless listed in the TASKBAR6 verification section.

| Milestone | Deterministic / build evidence | Hosted graphical evidence | Native build | Actual native guest | Cold-restart persistence |
| --- | --- | --- | --- | --- | --- |
| TASKBAR1 | 20 taskbar model assertions; hosted build passed. | No screenshot artifact recorded in its report. | AMD64 build passed; taskbar changes remained hosted-only. | Pending; TASKBAR1 report explicitly deferred native integration. | Not tested. |
| TASKBAR2 | 37 model assertions, 25 identity checks, 55 App Model action assertions; hosted and AMD64 builds passed. | No hosted screenshot artifact recorded. | Pass. | Pending; coordinator was unavailable at that milestone. | Pending. |
| TASKBAR3 | 71 model assertions, the same identity/action suites, pointer/window/resize regressions, hosted and AMD64 builds passed. | Three real hosted screenshots: running taskbar, grouped preview, and preview selection under out/taskbar/TASKBAR3/. Built-in widget apps had no safe per-window frame surface; their cards used the explicit fallback. | Pass. | Pending. | Pending. |
| TASKBAR4 | No TASKBAR4 report or local TASKBAR4 changes were present at preflight. | No evidence found. | No milestone evidence found. | No milestone evidence found. | No milestone evidence found. |
| TASKBAR5 | 91 taskbar assertions; pointer, actions, window UI, resize, hosted, AMD64, and x86 checks passed. | Real layout/settings screenshots under out/taskbar/TASKBAR5/; the auto-hide reveal image predates the final hit-test change. | AMD64 and x86 pass. | QAC admitted an x86 run, but the guest reported no framebuffer and 0×0 dimensions. out/taskbar/TASKBAR5/qac-guest-text-mode.png is a real text-mode capture, not taskbar evidence. | Not tested. |
| TASKBAR6 | 104 taskbar model assertions; current regressions and builds listed below pass. | No fresh hosted image after the auto-hide correction: the CUA inventory had no running guideXOS hosted compositor window. | AMD64 pass; the bootloader was also built for the guest candidate. | AMD64 UEFI desktop boot and GOP framebuffer observed. CUA input activation failed before a guest taskbar action could be verified. | Not tested. |

The current state keeps deterministic tests, hosted graphics, native build, guest interaction, and restart persistence as separate evidence categories.

## TASKBAR6 implementation

### Auto-hide hit testing

The hosted compositor and native desktop now call the same tested pointer helper used by the taskbar regression. While hidden, pointer presence inside the taskbar rectangle no longer counts as already being over a visible taskbar; the reveal strip starts the configured dwell instead. Once shown, the bar and owned popups hold visibility. The native path preserves its existing dock rectangle, and the shared helper computes the reveal strip from that edge.

The regression exercises the production helper used by both renderers for top, bottom, left, and right docks at a negative virtual-desktop origin. It asserts both the intermediate WaitingToReveal state and the completed reveal after the dwell. It also covers unknown/stale pointer coordinates, a viewport edge mapped to that origin, preview ownership, and context-menu ownership. This closes the TASKBAR5 deterministic regression gap. Native pointer interaction was not qualified.

### Real application progress producer

File Explorer paste/copy/move now publishes its actual existing FileOperationProgress snapshots into the taskbar activity store. FileExplorerApp::pasteClipboardTo begins tracking against its live window ID; the taskbar resolves that token through the registered window and its canonical App Model identity. The desktop callback maps real byte counts to determinate or indeterminate status and maps actual completion/failure states. The owner token is cleared after the operation and when its window unregisters. Percent calculation avoids multiplication overflow for large byte counts.

The existing producer has no cancellation state, so no cancellation event or percentage is fabricated. Automated coverage verifies byte-count mapping, unknown totals, completion/failure, bounded percentages, live-window group resolution, and status removal after the owner closes. The producer has not yet been exercised visually in the native guest.

### Thumbnails and layout

No thumbnail capture path was added. The hosted built-in Notepad, Calculator, and File Explorer use widget rendering without per-window XRGB frame surfaces; the native compositor likewise has no per-window content surface. Capturing a region of the shared framebuffer would not establish safe window ownership. Existing title/icon/state fallback remains in place.

The existing TASKBAR3/TASKBAR5 screenshots were reviewed. No concrete hosted layout defect requiring broader visual changes was identified. The post-fix hosted auto-hide screenshot could not be retaken because the hosted compositor window was absent from the CUA app inventory. No synthetic screenshots were created.

## QAC investigation and native guest result

The prior TASKBAR5 image and report establish that the old candidate was an x86 text-mode guest with no framebuffer. Its qemu-system-i386 path and Multiboot header did not request a framebuffer/video handoff; -vga std alone did not create one. That was a guest boot-mode/configuration problem, not evidence that the screenshot interface itself was unavailable. The saved QEMU capture transport did produce a real image of the text-mode guest.

QAC CLI and coordinator were accessible. Before the corrected run, QAC reported online, with no reservations, no active sessions, and no foreign QEMU processes. One initial invocation had a PowerShell argument-list construction error: QEMU rejected an empty OVMF file= argument before firmware startup, and QAC released that child. The corrected invocation then received an exclusive guideXOSServer / acceptance reservation.

The exact first guest candidate was built before the final shared auto-hide wiring change:

- QEMU: **C:\Program Files\qemu\qemu-system-x86_64.exe**.
- Firmware: repository **OVMF.fd**, attached read-only.
- Boot disk: writable directory-backed FAT IDE drive at **out/taskbar/TASKBAR6/guest-esp/**. This isolated copy contains the UEFI bootloader at **EFI/BOOT/BOOTX64.EFI** and the candidate AMD64 kernel at **kernel.elf**.
- Guest-kernel SHA-256: **BA84034A5427E832391942E9AC14541BA101F2C90C05BCA43F1CDEF2D583F4AF**. This identifies the pre-final-wiring kernel used in both boots.
- Display: **-vga std -display gtk**; machine **pc,usb=off**; 1024 MiB RAM; serial on stdio; QAC direct-child launch.
- QAC reservation **fe9f53fede4f4370ac570823e97449fb**; QEMU child PID 18164.

The serial log records UEFI boot through BOOTX64.EFI, kernel entry, two GOP handles with one unique primary framebuffer, and an active 1280×800, 32-bit B8G8R8A8 render target. A genuine CUA screenshot displayed the guideXOS desktop and bottom taskbar. This proves the framebuffer and capture path were available.

The first CUA screenshot displayed the desktop. CUA could open QEMU's host-side View menu, but a subsequent attempt to activate the QEMU window for guest input failed with “failed to activate captured window.” Opening a host-side QEMU menu is not guest input evidence, and no taskbar action is claimed. That reservation ran for 218 seconds and QAC recorded the child exit and Released.

One additional QAC run tested whether immediate window targeting would work before any other UI action. The same activation error occurred before a guest click. This run used reservation **d2ea6b5408f84f7da4a6a8a16db52fe8**, QEMU PID 26644, ran for 33 seconds, and also reached Released. Its boot and serial log are **out/taskbar/TASKBAR6/qac-amd64-uefi-guest-interaction.log**. No third guest launch was attempted.

The screenshot was returned inline by the CUA capture tool but that tool did not provide a PNG export path. Therefore no T6 screenshot file is claimed. The real serial evidence is **out/taskbar/TASKBAR6/qac-amd64-uefi-guest-run.log** and **out/taskbar/TASKBAR6/qac-amd64-uefi-guest-interaction.log**; both QAC reservations were released and the coordinator returned to zero sessions. The QEMU guest_errors debug logs are empty.

## Persistence and storage safety

Native taskbar preferences use the existing /desktop.cfg path, and pins use the separate /desktop.taskbar.pins store. The candidate used a writable FAT copy under the ignored TASKBAR6 output directory. The source ESP tree was copied byte-for-byte before adding the bootloader and kernel; the repository ESP still has neither kernel.elf nor EFI/BOOT/BOOTX64.EFI. Its desktop.cfg remained byte-identical to the staged copy after boot.

UEFI created an NvVars file only in the staged guest disk. The taskbar preference file was not changed. Since CUA could not send guest input, no pin or preference change was committed and no restart-based persistence claim is made. Deterministic serialization and normalization remain covered by the taskbar model test.

## Verification

| Check | Result | Evidence |
| --- | --- | --- |
| scripts/run-taskbar-application-model-test.ps1 | PASS — 104 assertions. | out/taskbar/TASKBAR6/taskbar-model-test.log |
| scripts/run-compositor-pointer-buttons-test.ps1 | PASS. | out/taskbar/TASKBAR6/pointer-test.log |
| scripts/run-app-model-identity-test.ps1 | PASS — 25/25. | out/taskbar/TASKBAR6/app-model-identity-test.log |
| scripts/run-appmodel-application-actions-test.ps1 | PASS — 55/55. | out/taskbar/TASKBAR6/appmodel-actions-test.log |
| tests/window_ui_regression_test.cpp | PASS. | out/taskbar/TASKBAR6/window-ui-test-run.log |
| tests/compositor_resize_notification_test.cpp | PASS. | out/taskbar/TASKBAR6/resize-notification-test-run.log |
| build.bat | PASS — hosted executable linked. | out/taskbar/TASKBAR6/hosted-build.log |
| mingw32-make -C kernel ARCH=amd64 all | PASS — native kernel built. | out/taskbar/TASKBAR6/native-amd64-build.log |
| UEFI bootloader Release x64 | PASS — candidate bootloader produced. | out/taskbar/TASKBAR6/bootloader-build.log |
| git diff --check | PASS after source and report edits. | Staged and unstaged diff check. |

No separate taskbar desktop-configuration test runner exists in this checkout. The focused taskbar model suite covers preference round-trip, safe defaults, invalid values, pins independent of Start Menu pins, and geometry. The AMD64 build includes the production native taskbar code. No x86 build was repeated in TASKBAR6.

## Remaining issues and integration recommendation

- Native guest launch, grouping, pin/unpin, context menus, previews, window actions, overflow, auto-hide, and settings changes still need real input qualification.
- A cold restart with a changed pin or taskbar preference has not been performed.
- There is no safe per-window thumbnail surface for the built-in applications in the current compositor architecture.
- File Explorer progress is integrated but has not been seen in a native guest taskbar.
- A fresh hosted auto-hide screenshot remains unavailable.
- TASKBAR4 has no report or local milestone evidence.
- The remote mainline refs could not be refreshed because SSH authentication failed.

The source and regression work is ready for review, but the feature branch is not yet ready for mainline integration. Next work should repeat only the missing interaction and persistence checks after the CUA input target can activate the QEMU guest, retake the hosted auto-hide capture from the running guideXOS window, and refresh remote refs before deciding on integration. Optional native thumbnails should remain deferred until the compositor exposes a bounded, owned per-window surface.
