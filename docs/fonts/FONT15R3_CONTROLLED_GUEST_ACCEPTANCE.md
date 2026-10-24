# guideXOS Server — FONT15R3 Controlled Guest Acceptance

**Outcome B — `GUEST_MINIMUM_SIZE_ACCEPTANCE_INCOMPLETE`.** The candidate, helper, test suites, and isolated launch plan were prepared, but QAC did not grant exclusive ownership during the bounded wait. The single request was canceled before launch. No guest, guest input, or production Settings window was exercised in FONT15R3. This is an admission/evidence limitation, not a production defect. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open.

**Date:** 2026-10-24 PDT

**Branch:** `FONT_SUPPORT`

**Starting HEAD:** `510e1b7107c1007e2200f6c6a041dcfc1a391d9a`

**Ending execution HEAD before the report-only commit:** `510e1b7107c1007e2200f6c6a041dcfc1a391d9a`

## 1. Checkout preservation and prior evidence

The actual branch and requested baseline matched. The index was empty at startup. The eight tracked modifications were `build.ps1`, `docs/fonts/FONT12_V03_RELEASE_QUALIFICATION.md`, `scripts/generate-wallpaper-pack.ps1`, `scripts/run-native-filesystem-contract-test.ps1`, `tests/font_raster_test.cpp`, `tests/font_truetype_test.cpp`, `tests/native_abi_layout_test.cpp`, and `tests/system_font_selection_test.cpp`. The worktree also had 8,192 untracked status entries; the original 8,200 status lines (tracked plus untracked) are preserved in [`git-status-start-r3.txt`](../../out/font15r3-validation/guest/git-status-start-r3.txt). A comparison at the end, excluding only the new FONT15R3 report and `out/font15r3-validation/` evidence, matched the starting status exactly. No production source or pre-existing artifact was edited. No reset, clean, stash, branch switch, worktree, merge, rebase, cherry-pick, history rewrite, or push was performed.

There is no `.phase` marker in the checkout and no earlier FONT15R3 report or evidence directory. Only the primary agent was active in this checkout. The five required FONT14/FONT15/FONT15R1/FONT15R2/FONT15R2R1 reports were read and their SHA-256 hashes were rechecked after the run; they remain unchanged per [`prior-report-hashes-r3.json`](../../out/font15r3-validation/guest/prior-report-hashes-r3.json). FONT15R3 evidence and the current-source regression logs are under [`out/font15r3-validation`](../../out/font15r3-validation/).

## 2. Helper safety and paced-QMP method

The existing [`font15r1_qmp.py`](../../out/font15r1-validation/guest/font15r1_qmp.py) and [`test_font15r2r1_qmp_safety.py`](../../tests/test_font15r2r1_qmp_safety.py) were used; no second QMP/VNC framework was created. The focused suite passed **18/18** before admission and again afterward. The final run log is [`font15r2r1-helper-safety-r3.log`](../../out/font15r3-validation/logs/font15r2r1-helper-safety-r3.log).

The audited helper retains bounded command/action/cleanup timeouts, verifies the loopback QMP listener and accepted connection against the expected QAC-owned QEMU PID and process identity, tracks button state conservatively, attempts bounded button-up cleanup after failed drags, and reports `unknown` release state while refusing further input when release cannot be verified. It does not reconnect to a foreign or unverified endpoint. Guest event consumption remains separate from QMP acknowledgment. Movement keeps X and Y as separate relative-axis events.

The actual DM34R1 helper at `D:\dev\guideXOSServer_DiskManagerImprovements\scripts\dm34_qmp_gui_input.py` was checked; SHA-256 is `FD41BE33D4F5C9E0B363B41E6EA4206FA7427E3DA9736EA39691B58505641D02`. Its event format is `{"type":"rel","data":{"axis":"x"|"y","value":step}}`, sent one event at a time, split to at most 100 pixels, with 200 ms pacing. Button transitions are separate `{"type":"btn","data":{"button":"left","down":true|false}}` events paced 150 ms apart. This documents the accepted DM34 method only; FONT15R3 sent no input and did not establish delivery on the Fonts candidate.

## 3. Candidate and launch preflight

A fresh disposable copy of the previously accepted FONT15 candidate was prepared under `out/font15r3-validation/candidate/`. The candidate's build identity records source HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae` and build time `2026-10-21T04:12:54Z`. The only commits between that production source and the starting HEAD add documentation, the safety-qualified QMP helper, and its tests; no production source changed. All 23 members in the prior accepted post-run manifest matched, and all 24 copied files (including the candidate's own manifest) remained byte-identical through the canceled request. See [`candidate-prelaunch-r3.json`](../../out/font15r3-validation/guest/candidate-prelaunch-r3.json), [`candidate-source-match-r3.json`](../../out/font15r3-validation/guest/candidate-source-match-r3.json), and [`candidate-postwait-manifest-r3.json`](../../out/font15r3-validation/guest/candidate-postwait-manifest-r3.json).

| Candidate component | SHA-256 |
|---|---|
| Read-only OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |
| UEFI `BOOTX64.EFI` | `598552E5A5C154365667737D81336F54325B35D3206A235C3594DDB0522CF5D0` |
| AMD64 `kernel.elf` | `F53974791A47AA491C3091DF8E576E2107FF6DD39CDEAF269355972717BDCC04` |
| Disposable runtime `ramdisk.img` | `0B21C7D4691B4B2CCA9DA530E5A0E5699C85AB5CA0C918F0DFC981CAC646B960` |
| Approved font manifest | `5A5862DCD4110C0E9C9BDDD45D917E195F8804DE8D55D03C65F5529C28EE2FD2` |
| Roboto Regular / Bold / Italic | `1078DEDD79C55859B102E3EA141D8A74E5FB1F8F28A15D155BF7FC9B5B628346` / `76A6C0F614D0E3E66F111BA0C9DCF266BFE6B1CD80F67FDAA87B722A3642BBAD` / `3BBF16D61BE1F6B764AA91E74EA29AE9D0EB5E90615D86312DA699FA637FC982` |

QEMU 11.0.0 (`C:\Program Files\qemu\qemu-system-x86_64.exe`, SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`) was selected. The copied firmware exists at a nonempty path (3,653,632 bytes); the plan uses it read-only and gives QEMU only the disposable folder-backed ESP as writable media. Networking is disabled. Planned isolated endpoints were QMP `127.0.0.1:61383`, VNC `127.0.0.1:41239` (QEMU display `35339`), viewer `127.0.0.1:41240`, and a new R3 serial/evidence path. All planned ports were free. Exact arguments and hashes are in [`qemu-launch-plan-r3.json`](../../out/font15r3-validation/guest/qemu-launch-plan-r3.json).

No QEMU launch occurred. Consequently there is no R3 QEMU PID, verified QMP connection, framebuffer startup, booted-image FONT14/FONT15 marker, serial log, or authentic R3 desktop screenshot. The prior FONT15 guest evidence remains unchanged and is not presented as an R3 boot.

## 4. QAC admission and disposition

Immediately before submission, QAC 2.0.0 was online. Its census showed one verified managed acceptance reservation and no earlier queued or blocked requests. The active owner was the DM37 run: reservation `8b84465adf184cc6a1f416744168127c`, owner PID `31704`, QEMU child PID `24052`; the process command line and project path were captured in [`qemu-process-census-prelaunch-r3.json`](../../out/font15r3-validation/guest/qemu-process-census-prelaunch-r3.json). The full snapshot is in [`qac-prelaunch-status-r3.txt`](../../out/font15r3-validation/guest/qac-prelaunch-status-r3.txt), [`qac-prelaunch-list-r3.txt`](../../out/font15r3-validation/guest/qac-prelaunch-list-r3.txt), and [`endpoint-census-prelaunch-r3.json`](../../out/font15r3-validation/guest/endpoint-census-prelaunch-r3.json). No FONT15R3 reservation or process existed. The selected endpoints were free.

Exactly one FONT15R3 request was submitted through `qac run`: reservation `c33b377be9cc4ee1a23e2c2d4613bf18`, owner/client PID `27860`. QAC queued it at FIFO position 1 behind the active DM37 owner. It remained `Waiting`, with no grant time and no child PID. It was canceled through QAC; final state is `Cancelled`, reason `Cancelled by client`, `processId=null`. The QAC run client exited before launch (exit code 1, recorded in [`qac-run-r3.exit-code.txt`](../../out/font15r3-validation/guest/qac-run-r3.exit-code.txt)). Final QAC census still showed only the existing DM37 guest, zero queued requests, and zero blocked requests. It was not stopped or otherwise touched. Logs and snapshots are [`qac-run-r3.log`](../../out/font15r3-validation/guest/qac-run-r3.log), [`qac-final-inspect-r3.json`](../../out/font15r3-validation/guest/qac-final-inspect-r3.json), [`qac-final-status-r3.txt`](../../out/font15r3-validation/guest/qac-final-status-r3.txt), and [`qac-final-list-r3.txt`](../../out/font15r3-validation/guest/qac-final-list-r3.txt). The post-cancel process census is [`qemu-process-census-postcancel-r3.json`](../../out/font15r3-validation/guest/qemu-process-census-postcancel-r3.json).

**Timing deviation:** the request was submitted at approximately 12:01:54 PDT and canceled at approximately 12:05:11 PDT, about 197 seconds later. This was roughly 17 seconds beyond the requested three-minute maximum due to polling/tool-call overhead. Cancellation succeeded and no child process existed, but the wait did exceed the stated cap. No replacement request was submitted.

## 5. Guest input and Settings acceptance

No QMP input command was sent. Therefore there are no FONT15R3 requested movements, QMP acknowledgments, observed pointer displacements, activations, button-down/up acknowledgments, or post-release behavior to report. No drag was attempted and no button state was left uncertain.

The required same-live-window measurements were not made. Earlier FONT15 evidence observed a 760×500 window on a separate guest run; it does not satisfy the R3 sequence. In R3, the initial 760×500 size, 640×400 intermediate size, 560×360 outer size, 560×336 client size, 561×361 boundary, 480×300 undersize clamp, and same-window grow/shrink cycle are all **unverified**. Fonts layout at minimum, Roboto rendering in that window, clipping and scrolling, two-column choices, font/size controls, Back, Apply, Cancel, bitmap fallback, production draw/hit-test agreement, and stability are likewise **unverified in R3**. There is no R3 Settings screenshot or serial log. No production defect was reproduced.

## 6. Regression and build qualification

The four hosted suites were rebuilt from the current sources into the new R3 output directory and passed:

| Suite | Result |
|---|---:|
| FONT6 | 420/420 |
| FONT10 | 88/88 |
| FONT11 | 48/48 |
| FONT14 | 496/496 |
| **Total** | **1,052/1,052** |

Logs, binaries, and [`hosted-regression-summary-r3.json`](../../out/font15r3-validation/logs/hosted-regression-summary-r3.json) are under `out/font15r3-validation/`. The FONT15R2R1 helper suite passed 18/18 as recorded above.

No production source changed. Candidate ELF, bootloader, runtime, and font-package hashes match the prior accepted FONT15 candidate. The existing FONT14/FONT15 reports provide the prior freestanding AMD64 PE/ELF build and zero-PE-import-directory qualification; no rebuild was needed or performed. The candidate's prior kernel PE hash is `5EBAA6ADB41985194B3ABDC0ECC962DDBA873D36C8F63789AB4938E15F9F2662`.

## 7. Remaining acceptance gates

Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open. A future attempt still needs QAC exclusive ownership, authentic boot and framebuffer evidence, verified QMP endpoint ownership, guest-visible separate-axis movement, target activation and a bounded drag with confirmed button-up, then the complete same-window resize sequence and all minimum-size Fonts interaction checks with original screenshots and serial evidence. Hosted regressions and helper safety are complete for this phase; no guest acceptance gate is closed.

The independent PacMan audio-link prerequisite, top-level guideXOS Server v0.3 release build, combined subsystem integration qualification, and final release-owner publication authorization remain open.

**Commit SHA:** the report-only documentation commit is recorded in the delivery summary. No production code, test source, prior evidence, or unrelated worktree change was committed or pushed.
