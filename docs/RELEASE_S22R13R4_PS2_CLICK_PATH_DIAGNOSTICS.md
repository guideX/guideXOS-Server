# guideXOS Server v0.3 — S22R13R4 PS/2 Click-Path Diagnostics

**Disposition: `INPUT_PATH_QUALIFIED` (Outcome A) for the diagnostic build.** A bounded QMP control method produced repeatable Start-menu open/close/reopen transitions, and the v2 guest trace tied a Start click from PS/2 bytes through widget activation. Settings navigation, PacMan launch/gameplay/exit, and VFS/PacMan registration also completed on that diagnostic image. The earlier R3 missed toggle was not reproduced, so its original failing stage remains unknown.

This report does not qualify the unchanged ISO in an R4 guest. It records the diagnostic image and its results separately from the unchanged candidate and prior R1 evidence. No publication readiness is claimed.

## 1. Repository and candidate provenance

- Repository: `D:\dev\guideXOSServer`; branch `main`; starting commit `7b120dfdfa067ce8c343a3f9801ef1bedf2ab9bd`.
- The tracked worktree was clean before this work. The initial status reported 9,384 untracked paths. No existing paths were removed, and no unrelated paths were staged. New R4 evidence and concurrent artifacts remain in the workspace; the evidence directory is `docs/evidence/s22r13r4-ps2-click-diagnostics-20261023/`.
- The unchanged candidate remains at `out/release-profile/0eed0ed2-df00-403a-9646-0dffb22be3bb/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`, SHA-256 `b6b7b0faeef779a4ac06abb96ad749d2ce906df2e7751c1b9c329dd0a23fc2af`. The same hash was verified after the diagnostic guest runs.
- The diagnostic overlay was built from the starting commit with `GXOS_PS2_CLICK_TRACE`. It adds bounded mouse tracing and the `_WIN32 && !GXOS_BARE_METAL` guard around bitmap-font HDC helpers so the freestanding MinGW build does not import Windows-only declarations. It does not change mouse thresholds or filtering behavior.
- The diagnostic v2 artifacts are in `docs/evidence/s22r13r4-ps2-click-diagnostics-20261023/diagnostic-build-v2/`:

  | Artifact | Identity |
  |---|---|
  | Build ID | `s22r13r4-ps2-click-trace-v2-20261023` |
  | Kernel SHA-256 | `31ff03bd151be9f1f1c409059e968e3a00e57712bec96721506423250c366fb3` |
  | Symbol audit SHA-256 | `810691897cea832fb1be05a9d89acb2aea4ddea7bb598d6aa8b24fc61a79be3c` |
  | FAT ESP image SHA-256 | `8f125c6a353c9859d0d13f91aa60b7d7ef3b72c01d8892b79024bee29f542883` |
  | Diagnostic ISO SHA-256 | `0ac0c662775e20f23a12e5b7377cb54d4bb27b7e1b9c5727699196c55a78eb96` |

  `build-identity.txt` labels this `diagnostic-only` and `diagnostic-overlay-not-release-candidate`. Independent ISO verification reports a bootable UEFI El Torito image. The v1 diagnostic build is retained separately; it exposed that trace records were not flushed during normal desktop input processing, so it was not used for click assertions.

- Freestanding build and symbol audit passed. The audit found no relocations, dynamic imports, dynamic symbols, or dynamic segment. The diagnostic ELF/PE audit is retained with the v2 ESP.
- No branch or worktree operation, `dist` change, readiness-marker change, release publication change, or QAC-repository change was made.

## 2. R3 sequence reconstruction

R3 ran QEMU 11.0 with a 1280×800 standard VGA guest and the relative PS/2 mouse; no tablet or absolute pointer was added. Its helper sent QMP `input-send-event` relative-axis events. The transcript records this movement sequence while positioning the cursor:

```text
80,0
0,40
-40,40  (three commands)
-40,13  (two commands)
-40,0   (three commands)
-25,0
```

Each movement was an `input-send-event` relative event: X and Y were separate events in a single call for diagonal steps. The action timestamps show `80,0` at 01:54:32 UTC, with its pointer screenshot about 0.75 seconds later; `0,40` at 01:56:16; the three `-40,40` steps at 01:57:45–47; two `-40,13` steps and three `-40,0` steps at 01:58:03–07; and `-25,0` at 01:58:09. This was a screenshot-guided sequence with pauses between observations, not a continuous movement stream. The transcript is retained at `docs/evidence/s22r13r3-guest-input-acceptance-20261023/boot1/qmp-transcript.jsonl`.

The R3 click helper sent a left-button `input-send-event` down, waited 500 ms, revalidated ownership, sent the explicit left-button up, then waited 500 ms before the next observation. The first click opened Start, the next closed it, and the next reopened it. A later paired click left Start closed; the final screenshot showed no toggle. The helper's recorded down/up intervals varied from about 0.33 to 0.69 seconds. R3 also tried QMP keyboard events and HMP `sendkey meta_l`; keyboard toggles were not a reliable fallback.

The screenshot showed the cursor on the Start target, but R3 did not record numeric guest cursor coordinates. QMP accepted the button commands, but that does not show the guest received them.

The later missed toggle was a verified-target attempt, but R3 had no per-click guest trace. Its QMP acknowledgments and button-up command do not establish whether that event was lost before the guest, during PS/2 processing, or after desktop dispatch. R3 therefore did not classify that miss as QEMU, QAC tooling, kernel, or widget failure.

## 3. PS/2 and desktop source audit

- QEMU exposes one relative `QEMU PS/2 Mouse`. The boot2 launch manifest records the `pc,accel=whpx` machine, two vCPUs, 2048 MiB RAM, standard VGA, networking disabled, read-only candidate CD-ROM, read-only OVMF, and a private writable 16-file ESP copy.
- The PS/2 handler assembles three-byte packets (four when wheel mode is enabled). It validates the byte-0 always-one bit and returns packet phase to zero before applying motion filtering. It decodes the new button mask before the rejected-motion return. If both clamped axes reach ±50, it records a rejected packet and returns without applying movement. The packet phase remains synchronized, and the decoded button mask remains updated. That return does not set `s_dirty`; the input manager reads the current button mask during polling.
- Accepted packet deltas are added to the PS/2 position and clamped to screen bounds. The input manager maps relative deltas into the virtual desktop, which clamps again. Static inspection and the v2 trace showed matching coordinates in tested ranges; no pointer/dispatcher divergence was observed.
- Desktop dispatch uses the mapped pointer position, tests the compositor window, then evaluates Start-menu or app widgets. The trace records the resolved window target, Start-button/menu target, row index, and click result. Settings Display navigation was routed to compositor window `0x3E8`.
- There is no existing host-test seam for the port-I/O-bound IRQ packet assembler. The guest run used only accepted movement steps, so this phase did not inject an oversized packet or claim an end-to-end rejection/resynchronization test. The source audit establishes the phase reset and button decode order; hosted validation below covers the mapper and compositor button state.

## 4. Diagnostic design and input-control contract

The diagnostic adds stable event IDs for PS/2 bytes, complete accepted/rejected packets, button transitions, pointer mapping, desktop dispatch, window hit-test, widget hit-test, and click result. It uses separate fixed 256-record buffers for interrupt and main-loop producers. IRQ handling only appends records; serial output occurs in cooperative context. A scope guard flushes traces on every `handle_mouse` return, including early returns.

The v2 trace buffer overflowed while PacMan was running (`irq-overflow` and main-buffer drop counters increased). Start and Settings click traces were complete before that overflow. Later PacMan window-close evidence is visual and serial lifecycle output, not a complete per-stage trace. The trace buffer limitation is recorded as a diagnostic limitation; it does not change the mouse driver.

The bounded helper in `boot2/qmp-input.ps1` uses this contract:

1. Before every input action, verify the QAC reservation and QEMU child PID still belong to this run.
2. Send relative X and Y movement in individual steps where each axis is within `-49..49`; confirm the guest cursor using a screenshot and the guest pointer-map trace.
3. Pair every left-button down with an explicit up, rechecking QAC ownership before release. The helper's error path attempts a safe release only while the same reservation and child remain verified.
4. Assert a guest UI state change from the trace and screenshot. A QMP reply alone is not a click assertion.

The diagnostic guest showed an observed scale near two screen pixels per input unit in the tested range. This is a measurement for this QEMU configuration, not a coordinate conversion rule.

## 5. Hosted validation and freestanding build

Hosted results are recorded in `docs/evidence/s22r13r4-ps2-click-diagnostics-20261023/hosted-validation.txt`:

- `display_input_mapper_probe.exe`: relative motion, paired button edges, and 1280×800 pointer bounds — **PASS**.
- `compositor_pointer_buttons_test.exe`: compositor pointer-button state and cleanup invariants — **PASS**.
- The freestanding AMD64 kernel build with `EXTRA_CFLAGS=-DGXOS_PS2_CLICK_TRACE` and the symbol audit — **PASS**.

These hosted checks do not exercise PS/2 port reads or prove end-to-end guest behavior. No change to filtering thresholds was made.

## 6. QAC ownership and guest evidence

Two bounded QAC sessions were needed. The first used diagnostic v1 for pointer calibration; its missing normal-path trace flush prevented a meaningful click assertion, so no click was attempted. It was shut down through its owned QMP endpoint. After adding the flush-on-return guard, the second session used v2 for the controlled interaction and acceptance checks.

| Run | Reservation / QEMU PID | Result |
|---|---|---|
| boot1, diagnostic v1 | `39011666cc5b4fd1b9ac511a94cf7915` / `16696` | Pointer calibration only; clean QMP shutdown and QAC release. |
| boot2, diagnostic v2 | `60aae845d5c1463cb9454239d72a4a81` / `17160` | Start, Settings, PacMan, gameplay input, and visual app exit. |

The v2 owner was verified before every input action. QAC reported no foreign QEMU process. Shutdown used the owned helper's `hmp quit`; immediately afterward `qac list`, `qac status`, and `qac queue` reported zero reservations, QEMU processes, managed sessions, queued reservations, and blocked requests. A later read-only check briefly observed a distinct QAC reservation (`0203a68771fa4c9ea89a5da16d7814cc`, QEMU PID `19228`, created 02:55:09 UTC). No input or shutdown was sent to it. At 02:55:50 UTC, a subsequent read-only check again reported zero reservations, QEMU processes, sessions, and queued reservations. The final snapshot is `boot2/qac-final-observation-with-concurrent-session.txt`. Captures are in `boot1/` and `boot2/`; the v2 launch vector is `boot2/qemu-launch.json`.

## 7. Click-path result and acceptance on diagnostic v2

The boot2 serial trace and screenshots show:

| Interaction | Trace / visible result |
|---|---|
| Pointer calibration and Start target | `pointer-map` coordinates matched desktop-dispatch coordinates. Screenshot `pointer-on-start-target.png` shows the arrow on Start at `(54,780)`. |
| First Start click | Event `0x0F`: PS/2 byte `0x09`, button transition `0→1`, accepted zero-motion packet at `(54,780)`, pointer map, desktop dispatch, Start widget hit, click result `open`. Event `0x10` released the button. `start-menu-after-first-click.png` shows the menu. |
| Close and reopen Start | Event `0x11` hit the Start widget and returned the menu to closed; event `0x13` hit it again and returned `open`. Screenshots `start-menu-after-close.png` and `start-menu-after-reopen.png` show both states. Release edges `0x12` and `0x14` recorded buttons up. No stuck-button state was observed. |
| Settings launch and navigation | After Start refresh, the Settings row was clicked. Serial reported `target=Settings resolvedType=BuiltInApp`; `settings-after-start-click.png` shows Settings. A click on Display hit window `0x3E8`; serial reported the Display input and repaint, and `settings-display-page.png` shows the selected page. Clicking the visible close control returned to the desktop; see `desktop-after-settings-close.png`. |
| VFS and PacMan registration | The post-refresh serial checkpoint lists `/`, `/system`, `/certs`, `/config`, and `/config/navigator` (count 5), `/Apps` present, a complete 956-byte PacMan manifest, `registeredPacMan=yes`, and `menuPacManAvailable=yes`. `start-menu-pacman-refresh.png` shows the entry remained in Start. |
| PacMan launch and gameplay | The PacMan Start row was clicked at `(150,627)`. The trace hit menu row index 10 and recorded button down/up. Serial records `LaunchDispatch`, successful ELF validation, and PacMan resource reads. `pacman-after-launch-wait.png` shows the game window. A paired QMP `left` key event, the same direction used in accepted R1 gameplay, was followed by score changing from 0 to 70 and a Game Over board in `pacman-after-left.png`. |
| PacMan exit and desktop | The cursor was visibly placed over the game's close button. A paired left click closed the game window; `desktop-after-pacman-exit.png` shows the responsive desktop. The trace buffer had overflowed during the app run, so this close is asserted by the before/after screenshots rather than a complete trace of its dispatch. R4 did not capture a `PACBM RUNTIME_CLEANUP` marker; the internal cleanup path is therefore not independently asserted in this boot. |

This establishes a reliable safe-step, paired-button interaction method on diagnostic v2. It does not identify why the earlier R3 no-motion click failed.

## 8. Prior R1 evidence reused

Navigator was not repeated during this input-focused boot. R1 Boot 2 already exercised Navigator after PacMan gameplay and exit: the address bar loaded `file:///Apps/PacMan/app.json`, rendered the package manifest, and retained all five VFS mounts and `/Apps`. See [R1 Boot 2 Navigator screenshot](evidence/s22r13r1-pacman-guest-acceptance-20261022/boot2/screenshots/navigator-file-url-result.png), [R1 report](RELEASE_S22R13R1_PACMAN_GUEST_ACCEPTANCE.md), and its serial log. This is prior evidence on the unchanged candidate, not an R4 v2 result.

## 9. Candidate acceptance boundary

The unchanged ISO hash is intact, but R4 did not boot that ISO. The click-path, Settings page navigation, and PacMan gameplay results above belong to diagnostic ISO `0ac0c662775e20f23a12e5b7377cb54d4bb27b7e1b9c5727699196c55a78eb96`. Prior R1 evidence remains the direct unchanged-candidate evidence for PacMan, gameplay, exit, and Navigator. R4 closes the missing Settings Display navigation on the diagnostic build only.

For unchanged-candidate acceptance, Main Integration should use the demonstrated bounded input contract in one explicitly identified run if release policy requires fresh evidence on that exact ISO. Do not mix such results with the v2 trace results. The original R3 missed-click cause remains an open attribution question; another diagnostic run with the same setup is not indicated unless improved trace retention is specifically needed.

## 10. Separate release limitations and security decisions

- `/Documents` remains a confirmed product limitation. FAT short-name creation rejects `Documents` because its base name is nine characters; R3 records `FAT_CREATE_INVALID_NAME` and failed standard-folder enumeration. The release owner must decide whether this is acceptable for v0.3 or needs a dedicated filesystem repair. This input phase did not change it.
- `/.startmenu_recent` persistence remains unsupported by the FAT filename path, which rejects the dotted name. This is separate from input attribution and was not repaired.
- Native ELF applications execute with kernel privilege; application isolation is absent.
- Writable application packages on the ESP can be discovered and executed at runtime.
- Runtime ABI negotiation remains insufficient for safe compatibility with incompatible host-table layouts.
- Release-owner disposition remains required for trusted package composition, writable-storage execution policy, third-party Native ELF execution, and ABI compatibility guarantees.

These results do not close those gates or authorize publication/readiness changes.

## 11. Main Integration continuation

Record `INPUT_PATH_QUALIFIED` for the diagnostic v2 method and keep the R3 click's original cause unassigned. Preserve the unchanged candidate byte-for-byte. If exact-ISO acceptance is required, run one bounded acceptance on the unchanged candidate using safe relative steps, screenshot-confirmed positions, paired button edges, and QAC ownership checks. Carry forward the `/Documents`, `/.startmenu_recent`, and security/ABI release-owner decisions as separate gates.
