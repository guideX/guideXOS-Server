# FONT15R4R5R1 — Instrumented Guest Pointer Pipeline

**Outcome: POINTER_PIPELINE_DIAGNOSIS_INCOMPLETE**

The diagnostic candidate booted under exclusive QAC ownership and the exact six-event replay completed. This run moved the guest cursor by the full requested displacement, so the historical one-step-only result was not reproduced. Guest instrumentation shows where six PS/2 packet updates were consolidated into two input-map and desktop updates, but that consolidation preserved the net movement. The first failing transition from the earlier failure remains unknown; no production defect or repair is established.

## Finding

The six QMP commands each received a matching success response. The guest recorded all six replay movements as distinct, valid PS/2 packets. PS/2 counters reported 32 bytes and 8 decoded packets total: two setup packets followed by the six replay packets. There were no synchronization discards, invalid packets, corrupt drops, or ring overflows.

The input mapper and desktop handled the replay in two samples: first X −35, then the remaining X/Y sum of −88,−41. The cursor was drawn at (465,340) and then (377,299). From the verified start at (500,340), the final displacement was exactly (−123,−41). This run therefore shows event aggregation, not lost net movement.

The historical R4R4 capture for the same sequence ended at (465,340), exactly the first X step, about 1.68 seconds after the final acknowledgment. Its trace is E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R4\run1\guest\move-from-preview-to-font-minus-font15r4r4.jsonl. Run2 ended at the full displacement about 1.92 seconds after its final acknowledgment; the different outcomes remain unexplained.

Source inspection explains a plausible aggregation path: the PS/2 interrupt handler updates current pointer coordinates as packets arrive; input::poll_ps2_mouse() later samples those coordinates and computes a delta from the previous sample. Packets arriving between polls can therefore become one input-map update. The probes did not timestamp each poll, so this run cannot establish why the earlier R4R4 screenshot remained at (465,340), nor whether the same poll timing caused that result.

## Checkout and preservation

- Checkout: D:\dev\guideXOSServer_FONT_SUPPORT
- Branch / HEAD: FONT_SUPPORT / 9570fdbd41d6a9ed8cf322cd2249d003a18ab37d
- The index was empty at task start and remains empty. The pre-existing unstaged edit to tests/test_font15r2r1_qmp_safety.py and pre-existing untracked docs/fonts/FONT15R4R5_POINTER_EVENT_PIPELINE_DIAGNOSIS.md remain intact.
- The R1 report already existed as an untracked run1 deferral report. This file updates it with run2 results; it remains untracked.
- No AGENTS.md was present. No other agent was active in this task.
- No production, QMP helper, or diagnostic source was changed. The test run’s generated tracked Python cache was restored. No commit or push was made, and the MODERN_INPUT checkout was not edited.

The earlier run1 evidence remains under E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5R1\run1. That attempt queued reservation a7606ef7850c4e30aee5258f97405f4f, canceled only that still-waiting request, and did not boot a guest or send input. Its artifacts were preserved and not overwritten.

## Candidate and instrumentation

- Diagnostic source revision: 9570fdbd41d6a9ed8cf322cd2249d003a18ab37d, branch FONT_SUPPORT.
- Source copy: E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5\diag-source.
- Diagnostic compile flag: GXOS_FONT15R4R5_POINTER_DIAGNOSTIC.
- Candidate copied and verified: E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5R1\run2\candidate\candidate (24/24 files match the preflight manifest).
- ESP/kernel.elf: ELF64 AMD64, 2,825,512 bytes; SHA-256 be49cbb39791d70921bac7e14201c0851d8f5a858e6faeca96b32078da5a6420.
- OVMF.fd: SHA-256 2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792.
- QEMU executable: QEMU 11.0.0; SHA-256 a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02.
- The candidate is diagnostic-only and differs from the prepared FONT15R4R4 base candidate in the kernel image. It is not a shipping candidate.

Available bounded observations:

- PS/2 byte, framed-packet, decoded-packet, synchronization, invalid, corrupt-drop, and ring-overflow counters; a 16-entry raw/decoded packet ring.
- Up to 32 input-map records.
- Up to 32 desktop mouse-dispatch and cursor-draw records.
- F9 resets and arms capture; F12 stops capture and dumps the records.
- There is no QEMU-internal per-event delivery counter. This PS/2 path has no pointer-event enqueue/dequeue queue to count, so those stages remain unavailable.

## Host safety gate

python -m unittest tests.test_font15r2r1_qmp_safety -v passed **46/46** before QAC admission. The suite covered QAC PID and loopback endpoint ownership, foreign/ambiguous owner rejection, bounded empty-result retries, no reconnect after ownership loss, bounded drag cleanup and unresolved-button failure, complete fresh P6 screenshots, separate-axis movement, wheel-button semantics, and matching QMP responses.

No typography source changed, so the prior 1,052/1,052 hosted font checks were not repeated.

## QAC ownership and guest

QAC granted exclusive acceptance reservation 114ccc93f66a4d8690ea0101b80e6d24 immediately. The verified owner was LEON2_RDT\guideX (CLI PID 34276); its direct QEMU child was PID 22832, C:\Program Files\qemu\qemu-system-x86_64.exe, started at 2026-11-15T05:31:00.6990778Z. QAC reported the owner liveness as Verified and the run as not degraded. At admission, the coordinator showed no competing QEMU process or queued/blocked request.

The launch used QEMU 11.0.0, pc,accel=whpx, 2 CPUs, 1024 MB, standard VGA, read-only OVMF, a writable isolated ESP copy, serial capture, no network, no USB tablet, QMP 127.0.0.1:62792, and VNC 127.0.0.1:35693 (TCP 41593). QMP info mice identified QEMU PS/2 Mouse.

Each QMP helper connection verified that the loopback listener belonged to PID 22832 before sending a command or requesting a screenshot.

The guest booted to the desktop and Settings > Personalization > Fonts. Fresh framebuffer captures were complete P6 images at 1280×800. The Settings window was at its 560×360 minimum outer size (560×336 client). The active face remained guideXOS Bitmap Regular 7 px; Roboto Regular was selected only as a pending preview, and Apply was not pressed. The replay screenshots visibly show a 19 px preview, while earlier setup/replay-plan JSON says 20 px; this metadata mismatch is retained as a setup limitation. It does not affect the measured pointer delta.

The pointer template matched all 108 cursor pixels at the starting position (500,340). Buttons were up; the desktop diagnostic reported compositor-press=0. No drag was active during the measured sequence. F9 capture also included two zero-net setup packets (+35 X, then −35 X), so the six replay packets are PS/2 records 3–8.

A separate, uninstrumented reposition before F9 had requested (+93,+41) from (407,299); QMP acknowledged it, but the immediately captured frame measured only the first +35 X at (442,299). Because probes were not armed and framebuffer processing can lag input, this is not used to locate a boundary. The pointer was then restored to (500,340), confirmed by the 108/108 template match and the diagnostic start frame.

## Exact six-event replay

The sequence matches the saved FONT15R4R4 trace at E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R4\run1\guest\move-from-preview-to-font-minus-font15r4r4.jsonl. Requests were sent about 200 ms apart, with no button events:

| Step | QMP event | Request UTC | Matching response UTC | Guest PS/2 replay packet |
|---:|---|---|---|---|
| 1 | X −35 | 06:00:53.439783 | 06:00:53.441787 | #3: −35,0 → (465,340) |
| 2 | X −35 | 06:00:53.642335 | 06:00:53.644345 | #4: −35,0 → (430,340) |
| 3 | X −35 | 06:00:53.844857 | 06:00:53.846867 | #5: −35,0 → (395,340) |
| 4 | X −18 | 06:00:54.047387 | 06:00:54.049397 | #6: −18,0 → (377,340) |
| 5 | Y −35 | 06:00:54.251056 | 06:00:54.252056 | #7: 0,−35 → (377,305) |
| 6 | Y −6 | 06:00:54.453742 | 06:00:54.455745 | #8: 0,−6 → (377,299) |

All six distinct input-send-event requests received their matching successful QMP response IDs. The requested total was (−123,−41).

Both replay input-map records were valid relative-global mappings with fallback=no, clamped=no, wheel=0, and buttons=00.

The final screenshot command was acknowledged at 2026-11-15T06:00:56.367154Z; the complete P6 artifact was verified at 06:00:56.372154Z, about 1.92 seconds after the sixth QMP response. The cursor-template/framebuffer comparison found the old 108-pixel cursor at (500,340) and the new 108-pixel cursor at (377,299), with 216 changed pixels total and no other large changed component. Observed displacement was exactly (−123,−41). The Fonts page remained visible.

## Pipeline measurements

| Stage | Run2 observation | Evidence limit |
|---|---|---|
| QMP accepted commands | 6/6 matching successful acknowledgments | Does not by itself measure QEMU internals |
| QEMU emulated device delivery | No internal event counter | Guest PS/2 records match the six deltas |
| PS/2 bytes and packets | 32 bytes; 8 framed and 8 decoded packets total (2 setup + 6 replay) | The six replay packet deltas match the requested sequence |
| PS/2 errors | 0 sync discards, 0 invalid, 0 corrupt drops, 0 ring overflow | Capture stayed within the 16-record ring |
| Pointer enqueue/dequeue | Not observable; no queue exists in this PS/2 polling path | No assumed counts |
| Input-map updates | 2 during replay: (−35,0), then (−88,−41) | The latter combines replay steps 2–6 |
| Desktop mouse dispatch | 2 during replay at (465,340), then (377,299) | F12 totals were 4 including the two setup moves |
| Compositor cursor draws | 2 during replay at the same coordinates | F12 totals were 4 including setup |
| Framebuffer movement | Full −123,−41; final (377,299) | Fresh, verified P6 screenshot plus cursor-template comparison |

The packet records preserve each of the six requested deltas and intermediate PS/2 coordinates. The first replay packet was followed by one mapping/desktop update at (465,340); the next mapping update was the sum of the remaining five packets at (377,299). The earliest observed reduction in per-command granularity is therefore between per-packet PS/2 coordinate updates and the later input-manager poll/map sample. The total displacement was preserved, so this is not the failing transition sought by the mission.

## Follow-up ownership and recommendation

The next investigation owner is Modern Input, focused on kernel/core/ps2mouse.cpp, kernel/core/input_manager.cpp (poll_ps2_mouse()), and the kernel/core/main.cpp poll/dirty dispatch into desktop handling. The measured run does not justify a production correction or a claim that these functions caused the earlier one-step result.

The smallest useful next diagnostic is to timestamp PS/2 packet completion and each poll_ps2_mouse() sample, recording sampled coordinates and resulting mapped deltas. Re-run the exact six-event sequence in the state that reproduces the historical (465,340) result and correlate those samples with desktop dispatch/cursor records. A focused regression should cover both final displacement and intermediate pointer/hit-test observations. Do not change production input behavior until the one-step result is reproduced and a failing transition is measured.

The observed guest PS/2 packets mean the host-side QMP command path is not the leading hypothesis for this successful run, but the absent QEMU internal counter and state-dependent historical failure keep the host/device boundary from being ruled out completely.

## Font Support and release boundary

TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN remains open. This diagnostic run did not apply the pending TrueType selection and did not repeat the minimum-size control or pointer hit-testing acceptance. Previously established font, geometry, apply/cancel, bitmap fallback, and preference-reload evidence remains unchanged.

PacMan/top-level build prerequisites, combined Server v0.3 integration, public technical-preview security qualification, and publication approval remain separate gates. This run does not authorize a release or a system-wide TrueType migration.

## QAC cleanup and artifacts

QEMU was stopped through the owner-verified QMP helper with quit; the QAC-managed run exited with code 0 and released reservation 114ccc93f66a4d8690ea0101b80e6d24. Final QAC state was Online with zero reservations, managed sessions, queued requests, or blocked requests. QMP port 62792 and VNC TCP port 41593 had no listeners; QEMU PID 22832 and QAC CLI PID 34276 were absent.

Run2 candidate, QAC, serial, QMP, screenshot, and counter artifacts are preserved under:

E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5R1\run2

Key guest artifacts include:

- guest\qac-ownership-evidence-run2.json
- guest\candidate-preflight-font15r4r5r1-run2.json
- guest\font15r4r5r1-run2.serial.log and guest\guest-diagnostic-extract-run2.txt
- guest\six-event-replay-qmp-run2.jsonl
- guest\six-event-diagnostic-start-run2.ppm / .png
- guest\six-event-final-run2.ppm / .png
- guest\six-event-framebuffer-delta-run2.json
- guest\diagnostics-stop-f12-qmp-run2.jsonl
- guest\qac-status-final-run2.txt, guest\qac-list-final-run2.txt, guest\qac-queue-final-run2.txt, guest\qac-inspect-final-run2.txt, and guest\process-endpoint-final-run2.json

No commit SHA exists because no commit was created.
