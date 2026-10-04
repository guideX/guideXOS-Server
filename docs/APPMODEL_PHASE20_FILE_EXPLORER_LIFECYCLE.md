# App Model Phase 20 — File Explorer Overlapping Activation & Final-Window Lifecycle

Status: **Outcome B — the File Explorer orphan is fixed and multi-instance ownership is verified through eight simultaneous windows; native visual inspection and two launch/close fault boundaries remain unverified.**

## Phase gate and checkout

- Starting HEAD: `e9d42915b25b60579cc301c4d0bdc5976bf931d5` (`Document App Model instance lifecycle authority`).
- Branch: `main`.
- Cached upstream divergence before this work: `origin/main...HEAD` was `0 1` (zero behind, one ahead).
- No authoritative `.phase` marker was present. `APP_MODEL_CURRENT_STATE.md` and the Phase 19 report made Phase 20 the next expected phase.
- Existing `desktop.json` changes, generated images, and temporary artifacts were preserved. No reset, clean, stash, history rewrite, remote change, or credential change was used.

## Phase 19 reproduction and pre-change timeline

Phase 19 recorded overlapping File Explorer PIDs **15** and **16**, windows **1004** and **1005**. After both windows closed, PID 16 exited and PID 15 remained `running=true` without a compositor window.

The Phase 20 pre-change hosted reproduction used two fixture folders under `tmp/phase20-prechange-repro-phase20-prechange-repro-adc48b8f2c394095b25459355a9e0e33/`:

| Step | Observation |
|---|---|
| T0 | Zero File Explorer windows and live processes. |
| T1–T2 | Open A created PID 11/window 1000 with A's exact path. Closing it removed the window and PID 11 reached `running=false`. |
| T3–T4 | Sequential open B created PID 12/window 1001. Closing it also reached zero windows and no live Explorer process. |
| T5 | Overlapping open A created PID 13/window 1002, owned by PID 13. |
| T6 | Open B created PID 14/window 1003, owned by PID 14. Both activation-consumption logs and titles showed their respective exact paths. |
| T7 | Closing A removed window 1002. PID 13 stayed `running=true`; B/window 1003 remained intact. |
| T8 | Closing B removed window 1003 and PID 14 exited normally. No window remained, but PID 13 stayed `running=true`. |

The retained ProcessTable rows include completed-process history; lifecycle counts above use the `running` state and compositor ownership, not historical row count. The detailed pre-change log is preserved at `tmp/phase20-prechange-repro-phase20-prechange-repro-adc48b8f2c394095b25459355a9e0e33.log`.

## Root cause and ownership trace

The external route remains `DesktopService::OpenFolder` → folder-capable handler resolution in AppRegistry → owned `AppActivationKind::Folder` context → `FileExplorer::LaunchWithActivation` → `ProcessTable::spawn` → File Explorer window creation. The compositor stores each window's `ownerPid`; a close removes that window from compositor state and sends `MT_Close` to that PID.

Each `ProcessTable::spawn` already created a distinct process record, worker thread, and copied activation context. The alias was inside hosted File Explorer: all navigation, window, filesystem-provider, selection, history, prompt, context-menu, and other mutable state lived in `static` class fields. Entry double-click tracking also used namespace-level mutable statics. The hosted server runs these workers in one address space, so those values were shared across otherwise distinct PIDs.

On B's `MT_Create`, the shared `s_windowId` changed from A's window 1002 to B's 1003. The compositor still delivered A's close to PID 13 correctly. PID 13 compared the close ID 1002 with the shared value 1003, so its local loop condition stayed true and it continued polling `gui.output` after its window had been removed. `ProcessTable` marks a process stopped only after its entry function returns; no cleanup request or generic final-window terminator existed to end that loop. The surviving worker also retained its thread-local current activation pointer until return, while the aliased Explorer state reflected the shared instance data.

This is a **shared mutable application-state alias followed by a missed app-local close**, not a wrong compositor owner PID, AppRegistry selection error, stale window registration, or omission in the ProcessTable worker cleanup. There was no distinct `FileExplorer` state object per process before the repair.

Other relevant shared state is intentional: `FileOperations` keeps a mutex-protected application-wide clipboard plus operation-active/generation values. Those do not own Explorer windows, current paths, navigation models, or close state. Provider helpers and launch entry points remain static because they are stateless or must serve as process entry points.

## Repair and intended concurrency model

The hosted launcher thunk now constructs one stack-owned `FileExplorer` per worker and calls its instance `run`. Mutable Explorer methods and fields are instance members, including the window ID, filesystem provider, current path and entries, roots, back/forward history, selection and scroll state, keyboard/mouse state, prompt/delete/context-menu state, operation-generation observation, and click tracking. The existing `MT_Close` comparison now reads that worker's own window ID. A matching close lets the app entry return; the existing ProcessTable worker wrapper then records normal exit and sets `running=false`/`finished=true`.

The intended and verified hosted model is **one independent File Explorer process and window per external folder activation**. Same-folder opens are not deduplicated. Different folders retain independent paths. `DesktopService::OpenFolder`, AppRegistry handler selection, the owned and bounded folder activation contract, generic shell entry points, and Recent Programs behavior were not redesigned. No generic lifecycle manifest policy, generic IPC, Navigator-style reuse route, or second cleanup owner was introduced.

There is no shared canonical “close final window, then terminate any app” primitive in the hosted lifecycle: `MT_Close` is delivered to the owning app, and the app returns from its entry point. File Explorer now follows that existing app-owned contract. Navigator and ImageViewer remain useful contrasts: they reject a second active launch; Settings has separately demonstrated independent process/window instances. The File Explorer fix does not change the Phase 19 conclusion that there is no current evidence for a universal App Model instance policy.

## Phase 20 runtime results

The lifecycle smoke uses the real `DesktopService::OpenFolder` and AppRegistry path, then compares live ProcessTable rows with `desktop.windows.owners` output. Every settled live Explorer PID had exactly one window, each window pointed back to that PID, and its title retained the requested path.

- Healthy single launch/close: passed; final window close leaves no live Explorer process.
- Sequential A then B launch/close: passed.
- Two different folders, close A then B: passed; A's close left B's owner pair intact.
- Two different folders, close B then A: passed; B's close left A's owner pair intact.
- Same folder opened twice: passed as two independent PID/window pairs; both closed cleanly.
- Three folders overlapped and closed B, A, C: passed with three distinct PIDs, windows, and exact path titles.
- Practical overlap of eight unique folders: passed with eight distinct PID/window pairs and exact path mapping. A shuffled close order returned to zero windows and zero live Explorer processes; each intermediate state preserved the remaining owner pairs.
- Repeated overlap: **100/100 cycles** passed, alternating same-folder/different-folder launches and alternating close order. Every cycle returned to the zero-window/zero-live-process baseline, with no path ownership mismatch or unexpected launch failure.
- Invalid external activation: file used as folder, missing path, malformed/control-character path, and 4,097-byte overlong path each created no Explorer process or window. The overlong command uses the Phase 16 unquoted console form because quoting a command longer than the console line bound splits it at the command-reader layer.
- Phase 16 regression: 53 checks passed, including root, nested, empty, spaces, dotted names, file-as-folder, missing/malformed/overlong rejection, internal navigation, Back history, and Recent Programs behavior.

The final Phase 20 lifecycle log is `tmp/phase20-file-explorer-lifecycle-phase20-file-explorer-lifecycle-54097334cc204b4da6611a4e34de4832.log`; the full console capture is `tmp/phase20-lifecycle-smoke-final-8way.log`. The script is `scripts/smoke-appmodel-phase20-file-explorer-lifecycle.ps1`.

## Regression and build results

- Phase 19 authority conclusion: preserved; no universal instance policy was added.
- Phase 18 zero-action Start/Recent behavior: passed.
- Phase 17 App Actions runtime and model checks: passed.
- Phase 16 folder runtime and model checks: passed.
- Phase 15 URI runtime and model checks: passed.
- Phase 14 canonical document routing: passed.
- Phase 13 JPEG activation: passed.
- Phase 12 Navigator local HTML: all 11 checks passed on rerun with a 30-second timeout. One earlier run reached the second Navigator `goto` wait timeout at the script's default 15 seconds; the clean rerun passed.
- Phase 11 PNG activation: passed.
- Phase 10 Developer Studio activation: passed.
- Phase 9 Settings Default Apps model: passed.
- Phase 8 default-handler persistence model: passed.
- Phase 7 Open With runtime and model checks: passed.
- Phase 6 document activation: passed.
- Phase 5B closeout: `appModelV1StatusReady=true`, `unresolved=0`, and `highRisk=0`; closeout result was PASS.
- Recent Programs and Startup App Model regressions: passed. Settings center, inventory, network, S6, S7, and users model checks: all passed.
- The additional bare-metal `smoke-desktop-startup-sync.ps1` regression did not pass: UEFI reached the bootloader, which reported `ELF: AllocatePages failed: EFI_OUT_OF_RESOURCES` for an ELF virtual range ending at `0x22b5aa60` (about 580 MiB) under its fixed 512 MiB QEMU configuration. The kernel did not start and emitted none of the expected startup-scan markers. This is outside the hosted File Explorer repair; the serial log is `logs/desktop-startup-sync-20261003-200701.serial.log`.
- Standard hosted `build.bat`: passed with **177 warnings, 0 errors**, matching the Phase 19 177-warning baseline. No warning came from `file_explorer.cpp`/`file_explorer.h`; warning delta is zero.
- Experimental hosted `build-native-experimental.bat`: passed with **176 warnings, 0 errors**, matching the Phase 19 176-warning baseline. No warning came from File Explorer; warning delta is zero.

The changed source is hosted `file_explorer.cpp`/`file_explorer.h`, compiled by both hosted builds. Bare-metal File Explorer has a separate `FileExplorerApp` implementation in `kernel/core/kernel_apps.cpp`; no shared process/window lifecycle code changed, so a Phase 20 QEMU run was not required. The separate startup-sync regression above was attempted as regression coverage and failed before kernel startup.

No native window screenshot/capture was available. The evidence is from process IDs/states, window IDs and owner PIDs, exact path titles/activation logs, close events, and final ProcessTable/compositor state; no visual proof is claimed.

## Outcome and remaining boundary

**Outcome B.** The reproduced orphan is fixed, and the multi-instance lifecycle is deterministic through eight concurrent windows plus 100 two-instance cycles. Pixel-level inspection remains unavailable. The test suite did not inject window-creation failure or force a deterministic close-during-launch / activation-during-close schedule, so those transition races are not claimed as covered.

Phase 21 candidate: harden the one existing-instance action delivery route, Navigator's `open-home`. `Navigator::InvokeAppAction` finds a running PID and queues a generation-tagged message, but there is no app-consumption acknowledgement or atomic closing-state handshake between the running check and queue delivery. That is a narrow app-action lifecycle edge and does not establish a need for generic instance resolution or a broader App Model policy.

## Closeout answers

1. The orphan occurred because A and B shared one static `s_windowId` and other mutable Explorer state. B overwrote the ID, so A ignored its own directed close and never returned to ProcessTable cleanup.
2. File Explorer is now verified for independent simultaneous processes/windows through an eight-instance overlap. The evidence does not require another instance policy.
3. After all File Explorer windows close and shutdown settles, the tested hosted runtime has zero live File Explorer processes; all 100 cycles restored that invariant.
4. No. The fix is local to hosted File Explorer state ownership; Phase 19's “no universal lifecycle policy is currently justified” conclusion remains valid.
5. The concrete Phase 21 candidate is Navigator `open-home` queue-versus-close acknowledgement/closing-state handling, as described above.
