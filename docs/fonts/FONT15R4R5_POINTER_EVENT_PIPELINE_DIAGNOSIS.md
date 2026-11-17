# FONT15R4R5 — Pointer Event Pipeline Diagnosis

**Outcome:** **POINTER_EVENT_DIAGNOSIS_PARTIAL**

**QAC status:** Deferred as **FONT15R4R5_QAC_UNAVAILABLE** because the exclusive acceptance session was occupied at final preflight.

**Owner recommendation:** Hand the next instrumented guest run to the Modern Input workstream. No production defect or repair is established.

## Summary

The FONT15R4R3 and FONT15R4R4 failures are reproducible at the framebuffer level: each run sent six separate relative X/Y events, received a matching successful QMP response for each, and later captured a fresh complete framebuffer showing exactly the first X step. The events were correctly ordered and paced about 200 ms apart. The movement was not near a screen edge, and no mouse button events were sent during either move.

FONT15R4R4 also showed the same helper, QMP schema, and 200 ms cadence move the cursor by the exact requested X and Y deltas in the desktop state. That evidence makes a deterministic host helper sequencing or pacing defect unlikely. It does not establish what QEMU or the guest did with the later events in the failing Fonts state.

The first unobserved transition is after QMP command completion and before the final pointer position is rendered in the guest framebuffer. Neither historical run recorded PS/2 bytes or packets, mapped pointer state, desktop dispatch, or cursor drawing. Therefore this phase cannot assign the failure to QEMU, the PS/2 driver, input mapping, desktop routing, or rendering.

The bounded diagnostic kernel was built and staged outside both source checkouts. It has not been launched. QAC is online, but a different acceptance reservation was active at the last read-only preflight, so no reservation was requested and no guest was started.

## Baseline and preservation

- Fonts checkout: D:\dev\guideXOSServer_FONT_SUPPORT, branch FONT_SUPPORT, HEAD 9570fdbd41d6a9ed8cf322cd2249d003a18ab37d (commit message: ...).
- The checkout was clean at inspection. This phase changes only the host safety test and this report; no production kernel, Settings, or helper implementation file was changed. No commit or push was made.
- Modern Input checkout: D:\dev\guideXOSServer_MODERN_INPUT, branch MODERN_INPUT, HEAD a4c5d5ad58b9b0848992802438e5a58f186c110b, clean. It was inspected read-only.
- No existing FONT15R4R5 report or phase marker was present at initial inspection. The Modern Input checkout's .phase marker was absent.
- A pre-existing Git warning reported an unrecognized temporary object at .git/objects/d1/tmp_obj_GUxf2J; it was left untouched.

## Reconstructed failure evidence

All timestamps below are UTC from the preserved QMP JSONL traces. “Request / ack” is the helper's logged command timestamp and the response timestamp with the matching QMP request ID. Each row is a separate input-send-event request.

| Step | R4R3 movement | R4R3 request / ack | R4R4 movement | R4R4 request / ack |
|---:|---|---|---|---|
| 1 | X −35 | 04:48:40.204030 / 04:48:40.204030 | X −35 | 16:18:34.965112 / 16:18:34.966116 |
| 2 | X −35 | 04:48:40.412372 / 04:48:40.415140 | X −35 | 16:18:35.167155 / 16:18:35.169608 |
| 3 | X −35 | 04:48:40.615769 / 04:48:40.617775 | X −35 | 16:18:35.371151 / 16:18:35.372153 |
| 4 | X −33 | 04:48:40.818994 / 04:48:40.818994 | X −18 | 16:18:35.574067 / 16:18:35.575066 |
| 5 | Y +35 | 04:48:41.022161 / 04:48:41.023909 | Y −35 | 16:18:35.776484 / 16:18:35.778492 |
| 6 | Y +4 | 04:48:41.225223 / 04:48:41.225223 | Y −6 | 16:18:35.979573 / 16:18:35.981936 |

### FONT15R4R3

- Start: cursor (545,260) in a 1280×800 framebuffer, inside the Settings Fonts window and away from pointer bounds.
- Requested total: (−138,+39). Six separate QMP requests above; all six received a matching response containing return:{}.
- No button events were sent during the movement. The preceding font-selection click had acknowledged both down and up.
- Fresh complete P6 screenshot was verified at 2026-11-11T04:48:42.749404Z, about 1.52 seconds after the last movement acknowledgment. Measured cursor: (510,260), exactly the first X step (−35,0).
- Screenshot: E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R3\run1\guest\before-font-size-plus-minimum-font15r4r3.ppm.
- QMP endpoint 127.0.0.1:62391 was verified to QEMU PID 21688 before and after. QAC ownership remained verified and degraded=false.

### FONT15R4R4

- Start: cursor (500,340) in a 1280×800 framebuffer, away from screen bounds.
- Requested total: (−123,−41). Six separate QMP requests above; all six received a matching response containing return:{}.
- No button events were sent during the movement.
- Fresh complete P6 screenshot was verified at 2026-11-14T16:18:37.664427Z, about 1.68 seconds after the last movement acknowledgment. Measured cursor: (465,340), again exactly the first X step (−35,0).
- Screenshot: E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R4\run1\guest\before-font-size-after-scroll-font15r4r4.ppm.
- QMP endpoint 127.0.0.1:62691 was verified to QEMU PID 22476 before and after. QAC ownership remained verified and degraded=false.

The screenshots are fresh, complete framebuffer captures, so they are not stale or partial files. They show the pointer at the first-step location at capture time. They do not reveal whether later guest input was absent, delayed, or applied to internal state without a corresponding cursor draw.

### Successful control in R4R4

Before the Fonts-page failure, the same guest and helper accepted separate X-only and Y-only ±8 movements from the desktop with exact measured displacements. R4R4 also replayed the R3 deltas at the helper's 200 ms cadence from the desktop: X −138 ended at (364,439) and Y +39 ended at (364,478), with screenshots confirming each total. The same batched sequence later failed in the Fonts page. This is evidence of state-dependent or intermittent behavior; it does not isolate which state or guest stage is responsible.

R4R4 separately confirmed font-size changes at 16, 17, and 19 px and preview scrolling with a stable endpoint at the selected 19 px size. These are preserved historical results, not rerun in this phase.

## QMP shape and host-side qualification

The helper at out/font15r1-validation/guest/font15r1_qmp.py sends separate X-then-Y events, splitting each axis into chunks no larger than 35. For example, each event has this shape:

    {"type":"rel","data":{"axis":"x","value":-35}}

R4R3 and R4R4 traces show one event per request, the intended X-then-Y order, roughly 200 ms between requests, and matching response IDs. The official [QEMU QMP Reference Manual](https://qemu-project.gitlab.io/qemu/interop/qemu-qmp-ref.html) defines relative pointer axes as X/Y; wheel operations use InputButton values such as wheel-up and wheel-down, not a relative Z axis. A successful QMP response confirms completion of the QMP command, not receipt or processing by the emulated device or guest.

Added test_six_event_relative_replay_preserves_order_pacing_and_screenshot_state to tests/test_font15r2r1_qmp_safety.py. It checks the six exact chunks and order for relative(-123,-41,0.2), six separately acknowledged requests, deterministic 0.2-second fixture-clock gaps, total 1.2-second pacing, one verified connection/PID, no button state, and an unchanged input-event list across a fresh complete P6 screenshot. The focused test passed 1/1.

The complete requested host safety suite passed **46/46**:

    py -3 -B tests\test_font15r2r1_qmp_safety.py

This includes the existing endpoint ownership, foreign/ambiguous owner rejection, bounded operation, response matching, button cleanup, and screenshot checks. No host helper implementation correction was justified by the evidence. The helper hash at inspection was 5C80B3B8A3471A78E4FF1B5C4302518D05725D073C083EEEF2E6A69BF2E44561.

## Production pipeline map

The observed machine used QEMU's pc machine, standard VGA, and the QEMU PS/2 mouse; the guest log identifies QEMU PS/2 Mouse. No USB tablet was attached. QEMU's implementation is external to this repository, so its internal input-to-PS/2 handling was not instrumented here.

| Boundary | Source and behavior | Limits, errors, and visibility |
|---|---|---|
| Host request | out/font15r1-validation/guest/font15r1_qmp.py, QMP.relative (around line 685), sends type=rel X then Y events and waits for each command response. | Max step 35, bounded event count/deadline, exact QEMU PID and endpoint verification. Trace establishes request and QMP response only. |
| QMP to emulated device | QEMU input-send-event on the external QEMU pc/PS/2 path. | QMP response has no guest packet or pointer-consumption acknowledgment. No per-event QEMU device trace was captured. |
| PS/2 controller and IRQ | kernel/core/main.cpp registers kernel::ps2mouse::irq_handler on IRQ12 (around line 1350). kernel/core/ps2mouse.cpp, irq_handler (around line 223), checks output-buffer-full and auxiliary-data status, then reads one byte. | Non-auxiliary data is read/discarded. Only the first five IRQ12 entries are logged; there are no runtime byte or packet counters in the production build. |
| Packet assembly and decode | kernel/core/ps2mouse.cpp, irq_handler: phase 0 synchronizes on byte-0 bit 3; IntelliMouse mode assembles four bytes; sign bits decode X/Y and screen Y is inverted. | Each axis is clamped to ±50; a packet is discarded if both decoded axes reach the ±50 limit; pointer coordinates clamp to [0,width−1] and [0,height−1]. No packet sequence or drop trace is available. The attempted movements are individually below these limits and start away from screen bounds, but raw packet contents were not captured. |
| Input mapping | kernel/core/input_manager.cpp, poll_ps2_mouse (around line 232), reads the driver's cumulative X/Y, derives a delta from the previous sample, maps it as a PS/2 relative event, and updates shared mouse state. | This path has no event queue; it samples accumulated position. It sets a dirty flag when mapped coordinates/buttons/wheel differ. Mapping diagnostics exist, but were not enabled for these classic PC/VGA guest runs. |
| Main loop and desktop routing | kernel/core/main.cpp polls input, consumes mouse_dirty, then calls desktop::handle_mouse (around lines 1469–1486). kernel/core/desktop.cpp, handle_mouse (around line 12202), updates desktop pointer coordinates and routes movement/button edges to the window compositor. | No production trace linked a particular poll to a desktop call. The runs sent no button events; historical logs do not expose compositor press/capture state at each movement. |
| Window/compositor handling | kernel/core/desktop.cpp calls KernelCompositor::handleMouseMove; kernel/core/kernel_compositor.cpp, KernelCompositor::handleMouseMove (around line 569), updates drag/resize or hover/hit-test/window callbacks. | This function routes window behavior; it is not the software cursor renderer. No movement-level compositor trace was present in the failed runs. |
| Visible cursor | kernel/core/desktop.cpp, draw_cursor (around line 12175), restores the previous cursor area, saves pixels at the new position, and writes cursor pixels directly to the framebuffer front buffer. | The final P6 captures establish the displayed cursor location only. They do not prove whether internal pointer state or hit testing changed between captures. |

Because the PS/2 driver accumulates absolute coordinates and the input manager samples their difference, multiple decoded packets can collapse into a later sampled delta without an event queue. A dirty boolean is also not a per-event record. These designs make bounded counters at each boundary important; source inspection alone does not show which stage lost or failed to render movement.

## Diagnostic candidate and QAC decision

A disposable source copy was created at E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5\diag-source from the Fonts checkout at HEAD 9570fdbd41d6a9ed8cf322cd2249d003a18ab37d. Only this external copy received the compile-time diagnostic flag GXOS_FONT15R4R5_POINTER_DIAGNOSTIC; neither checkout was edited for instrumentation.

The diagnostic build adds bounded PS/2 byte/packet counters and a 16-entry packet ring with raw packet bytes, decoded deltas, resulting coordinates, buttons, and wheel state. F9 arms collection; F12 stops and dumps from main context. It also enables at most 32 mapper records and 32 desktop/cursor records, including compositor press state. It adds no unbounded IRQ logging, allocation, or normal input-semantic change.

The isolated AMD64 build completed with MBEDTLS_PROFILE_VERIFY=PASS; the output is an ELF64 x86-64 kernel, 2,825,512 bytes, SHA-256 BE49CBB39791D70921BAC7E14201C0851D8F5A858E6FAECA96B32078DA5A6420.

A 24-file diagnostic candidate was staged at E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5\run1\candidate\candidate. Its preflight manifest, E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5\run1\guest\candidate-preflight-font15r4r5.json, records source commit, all file hashes, expected/actual file count 24, no missing/extra files, and that only ESP/kernel.elf differs from the R4R4 base candidate. The manifest records guestLaunched:false.

At the final read-only QAC snapshot:

- Coordinator was online, version 2.0.0.
- One managed acceptance session was running: project guideXOSUEFI-FONTS8R2, reservation 2ce27cb50c5d4078a8c1ae2b9f463778, QEMU child PID 30884, owner verified.
- There were no queued reservations and no foreign QEMU process.
- Snapshot files are E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5\run1\guest\qac-status-at-defer-font15r4r5.txt, qac-list-at-defer-font15r4r5.txt, and qac-queue-at-defer-font15r4r5.txt.

The acceptance slot was occupied. Per the task's exclusive ownership rule, this phase did not request a reservation, launch or terminate QEMU, send guest input, or perform cleanup on the other project. The prepared candidate remains unlaunched.

## Modern Input handoff

No failing transition is proven and no repair is proposed. The recommended next owner is Modern Input because it owns the shared production input path. The inspected Modern Input and Fonts sources for kernel/core/ps2mouse.cpp are byte-identical (SHA-256 3409D25E7AE024EAAC7A584FC10A39CFC7EA4EBE50351D00E6C024ED2D63FF14); kernel/core/input_manager.cpp is also byte-identical (SHA-256 8825BCF79BFA7C239D69CFC32E7361DFCF90DBEA1DEA8A570EAB5568621F9EEB). Modern Input's desktop.cpp differs, so it should be checked before using the diagnostic candidate as a later baseline.

Handoff facts:

- **Reproduction:** From a measured in-bounds pointer, send the two preserved six-event sequences above at 200 ms pacing in desktop and Fonts contexts. Capture a fresh P6 framebuffer after the last response.
- **Host observation:** Six of six requests and matching QMP return:{} responses; no movement button events.
- **Guest observation:** Not available for R4R3/R4R4. The framebuffer shows one visible X step. PS/2 bytes, complete packets, mapped coordinates, desktop calls, and cursor draws were not counted.
- **First failing transition:** Not localized. First unsupported boundary is after QMP command acknowledgment and before the final pointer update visible in the framebuffer.
- **Prepared evidence build:** Source commit 9570fdbd41d6a9ed8cf322cd2249d003a18ab37d; diagnostic kernel SHA-256 above; staged candidate and preflight manifest above; not launched.
- **Minimal next measurement:** Under an admitted QAC reservation, arm with F9, send one bounded X event and one bounded Y event with fresh screenshots, then replay the six-event case in desktop and Fonts contexts; stop collection with F12. Correlate raw IRQ bytes and completed packet count with decoded deltas, mapped pointer values, desktop dispatch, compositor press state, and cursor draw records.
- **Decision after measurement:** If packet counts stop first, inspect QEMU PS/2 packet production, controller status, and packet assembly. If packets decode but mapped state does not move, inspect driver accumulation and mapping. If mapped state changes but desktop/cursor records do not, inspect polling/dirty-state delivery. If desktop coordinates change but draw records or framebuffer do not, inspect cursor restoration/drawing and presentation. Make a production repair only after one boundary is demonstrated.
- **Focused regressions after a demonstrated fix:** deterministic X/Y packet decode and accumulation, repeated polling with multiple packets, event ordering and button state, then the bounded desktop-versus-Fonts guest replay. Re-run the relevant host safety suite and freestanding AMD64 build/link qualification after any production change.

The Modern Input checkout was not edited. The diagnostic flag and instrumentation exist only in the disposable Fonts source copy and staged candidate.

## Font Support and independent gates

Previously accepted geometry remains valid and was not repeated: 560×360 outer Settings window, 560×336 client area, a 561×361 boundary resize, and a delivered 480×300 undersize request clamped to the minimum. Preserve the R4R4 font-size and 19 px preview-scroll endpoint evidence as well. The historical **1,052/1,052 hosted typography checks** were not rerun because production Fonts/Settings code was unchanged.

Keep TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN open. Remaining control acceptance includes reliable pointer hit testing and the outstanding navigation/actions, including Back, Apply/Cancel, Restore Default where supported, bitmap fallback selection, and drawing/hit-test agreement. After reliable multi-event input is established, run the small Fonts acceptance set from the task; do not repeat the resize matrix.

Independent PacMan audio, top-level Server v0.3 build, combined integration, public preview qualification, and publication authorization remain separate open gates. This diagnostic does not close or invalidate them.

## Changed workspace files

- tests/test_font15r2r1_qmp_safety.py — deterministic six-event replay regression.
- docs/fonts/FONT15R4R5_POINTER_EVENT_PIPELINE_DIAGNOSIS.md — this report.

All QMP traces, screenshots, QAC snapshots, diagnostic sources, build output, and candidate manifest remain under E:\CodexArtifacts\guideXOSServer_FONT_SUPPORT\FONT15R4R5 or their cited R4R3/R4R4 artifact directories. Nothing was committed or pushed.
