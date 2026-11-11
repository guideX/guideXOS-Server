# guideXOS Server — FONT15R4R3 Minimum-Size Interaction Closure

**Outcome B — `GUEST_MINIMUM_INTERACTION_INCOMPLETE`.** The authentic AMD64 guest reached the production Settings Fonts page at 560×360 outer / 560×336 client dimensions. The measured 561×361 boundary resize passed, a delivered 480×300 undersize drag was clamped at 560×360, and Roboto Regular TrueType preview rendered at the minimum. The run stopped at the next pointer move: six QMP movement events were acknowledged, but the following screenshot showed only the first 35-pixel X step and no Y movement. Ownership was freshly verified before and after that action. Per the stop rule, no further guest input was sent. Font-size interaction, preview scrolling, navigation/actions, explicit bitmap-fallback selection, and grow/shrink stability remain unverified. No production defect was demonstrated; `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open.

**Run date:** 2026-11-10 PST (QAC and guest evidence crosses into 2026-11-11 UTC) · **Machine:** `LEON2_RDT` · **Repository:** `D:\dev\guideXOSServer_FONT_SUPPORT` · **Branch:** `FONT_SUPPORT`

## 1. Repository state and preservation

Execution HEAD was `48059d502ca6fb873165bc5e8596ef04d59e59ed` at start and end of the guest run. The index was empty before documentation. The only pre-existing tracked change was the unstaged listener-row adjustment in `out/font15r1-validation/guest/font15r1_qmp.py`; it was preserved byte-for-byte and not staged. The pre-existing untracked `docs/fonts/FONT15R4R2_LIVE_POINTER_AND_MINIMUM_SIZE_ACCEPTANCE.md` was read and preserved unchanged. The post-guest tests generated `tests/__pycache__`; that generated cache was removed.

No production font, kernel, bootloader, UEFI, test, QAC, or remote configuration was changed. No reset, clean, stash, branch change, worktree, merge, rebase, cherry-pick, fetch, pull, or push occurred. No root `.phase` marker was present. The final docs commit, if any, is recorded at the end of this report.

The R4R2 report remains a useful historical record and is retained as an explicitly reviewed companion document. Its evidence and outcome apply to that earlier run; the measurements below are from R4R3.

## 2. Historical R4R2 evidence and warning review

R4R2 established the same live Settings window at 760×500, then 640×400, then 560×360 outer dimensions, with a 560×336 client at minimum. It reached the production Fonts page and observed Roboto preview rendering. Its 561×361 drag request and QMP events were acknowledged, but its resulting raw screenshot was byte-identical to the settled minimum screenshot. The R4R2 starting pointer was not measured. Pixel inspection of its preserved minimum PPM places the cursor near the resize grip, but that is not an exact recorded start coordinate. R4R3 therefore remeasured pointer and window geometry before retrying the boundary.

The R4R2 QAC event ledger recorded one `ExclusiveDegraded` event at `2026-11-09T04:51:27.4627268Z` and `ExclusiveRestored` at `04:51:38.6563656Z`, about 11.19 seconds later. The ledger records only the count of unmanaged QEMU processes, not their PID or executable, so it does not establish which process or lifecycle transition caused the warning. No guest input occurred during that interval; the next input followed restoration. The underlying cause remains undetermined.

At the start of this attempt, QAC showed another project's verified managed reservation and QEMU process. No new reservation was requested and no guest input was sent while it was active. After that session completed, a fresh pre-request check showed QAC clean; the new run used the supported exclusive acceptance request. The procedure required a fresh QAC inspection and status check before and after each critical guest input. Thus the unresolved historical warning did not authorize bypassing ownership checks.

## 3. Candidate and QAC run

The disposable candidate was copied from the previously staged FONT15R4R2 candidate at source HEAD `ec221271e1d0c2f918202c56ba899b497cb172ae`. Its preflight manifest matched all 24 files. A 37-byte write probe on the disposable ESP succeeded and was removed. OVMF was attached read-only. The postrun manifest again found 24 expected files, 24 actual files, no changed files, no extras, and no missing files. Production paths `kernel`, `bootloader`, and `uefi` were unchanged from the candidate source checkpoint to execution HEAD.

| Component | SHA-256 |
|---|---|
| AMD64 `kernel.elf` | `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04` |
| `ramdisk.img` | `0b21c7d4691b4b2cca9da530e5a0e5699c85ab5ca0c918f0dfc981cac646b960` |
| `BOOTX64.EFI` | `598552e5a5c154365667737d81336f54325b35d3206a235c3594ddb0522cf5d0` |
| `OVMF.fd` | `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d347333ab2792` |
| Roboto v2.138 Regular | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` |
| Roboto v2.138 Bold | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` |
| Roboto v2.138 Italic | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` |

QEMU 11.0.0 was `C:\Program Files\QEMU\qemu-system-x86_64.exe`, SHA-256 `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`. Networking was disabled. QAC granted acceptance reservation `6c9456eb9f9540019b2007da433cbaf5`; verified owner PID was 14752 and the managed QEMU child was PID 21688 at `C:\Program Files\qemu\qemu-system-x86_64.exe`. QAC reported `degraded=false`. QMP used loopback port 62391; VNC display 35432 used TCP port 41332. The guest booted to a 1280×800 framebuffer and reached the kernel main loop.

## 4. Minimum-size window and resize results

The same Settings window remained positioned at `(95, 70)`. Geometry was measured from guest screenshots, including inclusive frame bounds; the client area subtracts the 24-pixel title bar.

| Step | Requested outer size | Observed outer / client | Evidence |
|---|---:|---:|---|
| Initial Settings window | 760×500 | 760×500 / 760×476 | Guest screenshot and geometry JSON |
| Intermediate resize | 640×400 | 640×400 / 640×376 | Guest screenshot and geometry JSON |
| Reach minimum | 560×360 | 560×360 / 560×336 | Settled guest screenshot and geometry JSON |
| Grip calibration | 568×368 | 568×368 / 568×344 | Actual drag; frame and pointer observed |
| Return to minimum | 560×360 | 560×360 / 560×336 | Actual drag; frame and pointer observed |
| Exact boundary | 561×361 | **561×361 / 561×337** | Actual `+1,+1` corner drag; passed |
| Return after boundary | 560×360 | 560×360 / 560×336 | Actual `−1,−1` corner drag |
| Undersize clamp | 480×300 | **560×360 / 560×336** | Delivered `−80,−60` drag; clamped |

Before the exact boundary drag, screenshot analysis placed the cursor tip candidate at `(650, 425)` on the lower-right resize grip. Starting outer geometry was 560×360. The helper sent button-down, separate X and Y `+1` events, then button-up; QMP acknowledged each. The next screenshot measured a 561×361 frame at the same position and a cursor displacement of exactly `(+1,+1)` pixels. The settled screenshot independently showed inclusive frame bounds `(95,70)` through `(655,430)`, equivalent to 561×361 outer dimensions. The client measured 561×337. This is direct evidence of production resize hit testing and resolves the prior R4R2 boundary failure for this run.

For the undersize test, the measured minimum window and grip were the starting state. A real drag requested `480×300` outer dimensions (`−80,−60`). The pointer's screenshot-measured tip moved from `(650,425)` to `(570,365)`, confirming delivery of the drag movement. The Settings frame remained at `(95,70)` with inclusive bounds through `(654,429)`, i.e. 560×360 outer / 560×336 client. Its position did not change. This supports a confirmed minimum-size clamp rather than an undelivered request.

## 5. Minimum-size Fonts page and stop boundary

At 560×360, a guest pointer click selected `Roboto-v2.138-Regular.ttf`. The screen showed the two-column validated-font choices, the exact selected filename, a TrueType preview at 16 px, a visible `8–48` size range, and the still-active `guideXOS Bitmap | Regular 7 px` setting. The sample was visible inside its preview viewport and clipped at the viewport edges. The screenshot showed readable sample glyphs, with no obvious glyph corruption or control overlap. No Apply action was taken, so no font preference was saved.

After selection, the cursor tip was measured at `(545,260)`. A pointer move toward the font-size `+` control requested `dx=-138, dy=+39`. The helper split this into four X events (`−35, −35, −35, −33`) and two Y events (`+35, +4`), all QMP-acknowledged. The operation was a move and sent no mouse button events. The fresh screenshot placed the cursor at `(510,260)`: only one `−35` X step was visible, with no Y displacement and none of the remaining requested movement. QAC inspection and status were clean immediately before and after the operation: the same verified QAC owner and QEMU child were present, and `degraded=false`.

The pointer discrepancy occurred after a QMP acknowledgment and after one guest-visible movement, but guest consumption of the remaining acknowledged events was not established. The cause could not be distinguished from the available observations, so it is not attributed to a production defect or compositor behavior. There was no button state to clean up for this move. Per the stop rule, no further guest input occurred after that screenshot. QMP `quit` was used later for cleanup only.

The following minimum-size requirements therefore remain unverified in R4R3: font-size adjustment; preview scrolling and scroll bounds after size changes; Back/navigation; Apply; Cancel; Restore Defaults; explicit bitmap-fallback selection and hit testing; and one bounded grow/shrink stability sequence. The selected TrueType preview is evidence of rendering and family selection only, not of saved preference or persistence. Existing FONT15 persistence evidence was not repeated in this guest run.

## 6. QAC warning, final ownership, and cleanup

During this run, the QAC event ledger recorded one unmanaged-QEMU warning: `ExclusiveDegraded` at `2026-11-11T04:50:11.1880095Z`, restored at `04:50:11.3713609Z`, about 0.183 seconds later. As in R4R2, the ledger gives a count but no PID or executable. It therefore cannot identify whether the observation represented a transient registration/lifecycle interval, stale state, or a genuine unmanaged process. The final run log also contained an unmanaged-process warning. The last pointer input was at `04:48:41.225Z`; no guest input occurred during or after this warning. The helper's per-action gate continued to require verified QAC ownership before input; no action was sent while ownership was degraded or ambiguous.

The verified guest was shut down through QMP `quit`, which produced a `SHUTDOWN` event; QAC then recorded the managed child complete and the reservation `Released`. The final QAC snapshot at shutdown reported zero QEMU processes, reservations, sessions, queued requests, or blocked requests. A later read-only follow-up at `2026-11-11T05:05:57.7079899Z` confirmed that QEMU PID 21688 and listeners on the run's QMP/VNC ports 62391 and 41332 were absent. No guest input or QAC changes occurred during that follow-up, and no foreign guest process was terminated.

## 7. Host-side qualification and regressions

The existing QMP safety suite passed before admission (**43/43**) and again after the guest run (**43/43**). This run did not modify the helper. The previously staged Windows endpoint-table adjustment was preserved and exercised by the suite.

| Suite | Current post-guest result |
|---|---:|
| FONT6 | 420/420 |
| FONT10 | 88/88 |
| FONT11 | 48/48 |
| FONT14 | 496/496 |
| **Typography total** | **1,052/1,052** |
| FONT15R2R1 QMP safety | 43/43 |

The production source paths are unchanged from the candidate checkpoint, so the tested candidate hashes continue to identify the qualified freestanding AMD64 build. These regression results do not cover the minimum-size guest interactions left open above.

## 8. Outcome and next qualification step

This run is **`GUEST_MINIMUM_INTERACTION_INCOMPLETE`**. The 561×361 boundary and the delivered 480×300 clamp passed, but incomplete cursor movement stopped the remaining real guest interactions. This is an input-delivery/observation failure boundary, not evidence of a production typography defect. Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` open.

The next qualification should use a fresh QAC admission and the same verified production candidate and QMP helper. It should first reproduce and diagnose the incomplete multi-event movement with fresh pointer measurements and ownership checks. Continue with size and preview interactions only after reliable movement is established. Do not alter production font code based on this run alone.

Independent v0.3 gates remain separate: PacMan/top-level build prerequisites, combined Server v0.3 integration, public technical-preview security requirements, final release qualification, and publication approval.

Record for later integration, without implementing it in FONT15R4R3: TrueType should eventually be the preferred font system across the guideXOS Server desktop and supported applications, while bitmap rendering remains a dependable fallback. Broader desktop surfaces, application typography APIs, consistent measurement/clipping/fallback, more font families, and rich-text applications remain future integration work.

## 9. Evidence index

All R4R3 run artifacts are under `E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R3\run1`. Raw P6 PPM files are preserved; PNG files are display conversions verified pixel-exact against the PPM. Full QMP traces have paired before/after QAC snapshots.

| Evidence | Artifact |
|---|---|
| R4R2 historical report | [`FONT15R4R2 report`](FONT15R4R2_LIVE_POINTER_AND_MINIMUM_SIZE_ACCEPTANCE.md) |
| Preserved R4R2 settled minimum PNG / raw PPM | [`PNG`](file:///D:/dev/guideXOSServer_FONT_SUPPORT/out/font15r4r2-validation/retry1/guest/settings-minimum-settled-font15r4r2.png) · [`PPM`](file:///D:/dev/guideXOSServer_FONT_SUPPORT/out/font15r4r2-validation/retry1/guest/settings-minimum-settled-font15r4r2.ppm) |
| R4R3 minimum-size Roboto PNG / raw PPM | [`PNG`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/settings-roboto-regular-minimum-font15r4r3.png) · [`PPM`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/settings-roboto-regular-minimum-font15r4r3.ppm) |
| 561×361 settled boundary PNG / raw PPM | [`PNG`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/settings-561x361-boundary-settled-font15r4r3.png) · [`PPM`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/settings-561x361-boundary-settled-font15r4r3.ppm) |
| Boundary pointer measurement and geometry | [`Pointer JSON`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/boundary-561x361-pointer-observation-font15r4r3.json) · [`Geometry JSON`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/window-geometry-561x361-settled-font15r4r3.json) · [`QMP trace`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qmp-boundary-560x360-to-561x361-font15r4r3.jsonl) |
| Undersize clamp PNG / raw PPM / measurement | [`PNG`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/settings-undersize-clamp-font15r4r3.png) · [`PPM`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/settings-undersize-clamp-font15r4r3.ppm) · [`Clamp JSON`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/undersize-clamp-observation-font15r4r3.json) · [`QMP trace`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qmp-undersize-request-480x300-from-minimum-font15r4r3.jsonl) |
| Incomplete pointer movement PNG / raw PPM and measurement | [`PNG`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/before-font-size-plus-minimum-font15r4r3.png) · [`PPM`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/before-font-size-plus-minimum-font15r4r3.ppm) · [`Movement JSON`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/measure-roboto-to-size-plus-font15r4r3.json) · [`QMP trace`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qmp-move-to-font-size-plus-minimum-font15r4r3.jsonl) |
| Candidate manifests and QEMU launch plan | [`Preflight`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/candidate-preflight-font15r4r3.json) · [`Postrun`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/candidate-postrun-font15r4r3.json) · [`Launch plan`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qemu-launch-plan-font15r4r3.json) |
| QAC ledger and final reservation | [`Event ledger`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qac-final-event-ledger-font15r4r3.json) · [`Reservation`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qac-final-inspect-font15r4r3.json) · [`Status`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qac-final-status-font15r4r3.txt) · [`QMP quit trace`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/qmp-quit-font15r4r3.jsonl) · [`Read-only PID/port follow-up`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/postrun-closure-followup-font15r4r3.json) |
| Boot and screenshot integrity | [`Serial log`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/font15r4r3-run1.serial.log) · [`Screenshot integrity`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/guest/screenshot-integrity-font15r4r3.json) |
| Post-guest regression summary and logs | [`Summary JSON`](file:///E:/CodexArtifacts/guideXOSServer_FONT_SUPPORT/FONT15R4R3/run1/logs/postguest-regression-summary.json) · Logs: `FONT6-postguest.log`, `FONT10-postguest.log`, `FONT11-postguest.log`, `FONT14-postguest.log`, and `font15r2r1-qmp-safety-postguest.log` in the same `logs` directory |

**Documentation commit:** this report and the explicitly reviewed R4R2 companion were introduced in `182fdda7d25cbb87f2b3cc5993c080114a38138c`.
