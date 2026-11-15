# TASKBAR3 — Previews and Visual Enhancements

**Outcome:** `TASKBAR3_HOSTED_VERIFIED_GUEST_PENDING` (Outcome B)
**Starting commit:** `e1072e00879ae375f7370eff430fd9eea8e92dd7`
**Branch:** `TASKBAR_IMPROVEMENTS`

## Implementation summary

TASKBAR3 extends the existing shared application grouping model and both production taskbar renderers. Taskbar buttons now expose clearer hover, focus, running, minimized, and grouped-window states. Preview cards can activate an individual window. Compact layouts add a paged overflow menu while preserving the taskbar's system controls.

The shared activity contract is in `taskbar_activity_core.h`. Its fixed 32-record store represents application-scoped or live-window-scoped attention and progress with `None`, `Indeterminate`, `Determinate`, `Completed`, and `Failed` states. Percentages are bounded to 0–100; invalid or ambiguous ownership tokens are rejected. Hosted window-scoped updates require a live, non-tombstoned window, and the native manager requires a registered window. Focus or activation acknowledges attention, closing a window removes its window-scoped record, and producers explicitly clear completed or terminated operation state.

No production application currently publishes taskbar progress. The reusable contract and presentation are implemented and tested; application-specific producer integration remains pending. The UI does not invent progress values.

## Preview rendering

### Hosted

The hosted compositor samples only an existing per-window XRGB8888 frame surface. It validates pixel format, dimensions, stride, and buffer length before reading. Thumbnails preserve aspect ratio within 176×100 pixels, use nearest-neighbor sampling, and refresh only when the frame generation changes. A four-entry cache uses about 275 KiB of fixed pixel storage; surface replacement and window closure invalidate its entries.

The built-in Notepad, Calculator, and File Explorer windows used for the visual run draw through the widget path and do not expose a per-window XRGB frame. Their cards therefore show the real title, icon, and active/running/minimized state with an explicit “Preview unavailable” message. The compositor does not substitute an unrelated image.

### Native

The native compositor does not expose a separate per-window content surface. It renders bounded title/icon/state cards for up to four windows in a group, with active and minimized presentation. Both hosted and native paths use the same activity states, grouping, popup placement policy, and thumbnail sizing rules.

## Taskbar status and interaction

- Hovered buttons receive a stronger theme-based fill and border. Focused windows have a distinct accent indicator; other running windows retain a running marker. Minimized windows are visually muted, and grouped entries show their member count.
- Attention uses a bounded `!` indicator. It clears when the owning application or window is activated/focused. Progress uses a bounded taskbar strip; indeterminate status uses a short activity segment, completed status uses a success color, and failed status uses an error color.
- Preview cards are placed against the actual display bounds and dock edge. The pointer can move from a taskbar button into its popup; Escape dismisses it. Clicking a card activates that exact window. The hosted run confirmed this by selecting one of two Notepad cards and observing that window move to the foreground.
- Horizontal and vertical layouts share the same rectangles for drawing and hit testing. Overflow rows are paged in groups of eight. A live hidden window can be activated; a pinned launchable entry can be launched; stale entries are disabled.

## Visual evidence

The screenshots below were captured from the running hosted executable at its 1024×768 virtual desktop size. They are local, uncommitted artifacts under `out/taskbar/TASKBAR3/`.

| Artifact | What it shows |
| --- | --- |
| `out/taskbar/TASKBAR3/hosted-running-taskbar.png` | Two Notepad windows grouped together, plus Calculator and File Explorer; File Explorer is focused and the other groups are unfocused. |
| `out/taskbar/TASKBAR3/hosted-grouped-preview.png` | Hover preview for the two-window Notepad group, with an individual title/state card for each window. The cards accurately report that these widget-rendered windows have no image surface. |
| `out/taskbar/TASKBAR3/hosted-preview-selection.png` | The selected Notepad preview window raised above File Explorer. |

A single-window Calculator preview was also observed live and showed the same explicit fallback card. Its screenshot could not be saved: the Windows capture helper returned `unknown screenshotId screenshot-0` on the first attempt and again after refreshing the state. The compact hosted screenshot and live progress/attention producer views were not captured. Compact layout, activity state, and popup bounds are covered by the focused model tests instead; no simulated screenshot was created.

## Tests and builds

| Check | Result |
| --- | --- |
| `scripts/run-taskbar-application-model-test.ps1` | PASS — 71 assertions, including grouping, stale-window removal, activity lifecycle and bounds, popup clipping, aspect ratio, hover transitions, compact layout, overflow selection rectangles, and vertical docks. Log: `out/taskbar/TASKBAR3/focused-taskbar-test-final.log`. |
| `scripts/run-compositor-pointer-buttons-test.ps1` | PASS. |
| `tests/window_ui_regression_test.cpp` (compiled with `g++ -std=c++17 -Wall -Wextra -O2 -iquote .`) | PASS. |
| `tests/compositor_resize_notification_test.cpp` | PASS. |
| `scripts/run-app-model-identity-test.ps1` | PASS — 25/25 checks. |
| `scripts/run-appmodel-application-actions-test.ps1` | PASS — 55/55 assertions. |
| `mingw32-make -C kernel ARCH=amd64 all` | PASS — produced `kernel/build/amd64/bin/kernel.elf`. Log: `out/taskbar/TASKBAR3/native-build-final.log`. |
| `build.bat` | Final full hosted build and link: PASS. Log: `out/taskbar/TASKBAR3/hosted-build-final.log`. |
| `git diff --check` | PASS; only Git line-ending normalization notices were emitted. |

## Guest qualification

`qac status` returned that `qac` is not recognized on PATH. No guest was launched. Native guest interactions and TASKBAR2's restart-based pin-persistence qualification remain pending; this is a qualification limitation, not a native build failure.

## Performance and remaining limitations

Preview work is bounded: at most four cached hosted thumbnails of 176×100 pixels, validated source reads, and no refresh for an unchanged frame generation. Native cards use fixed arrays and do not allocate image buffers. No periodic per-window polling or animation loop was added.

The hosted and native progress APIs currently have no production producer, and the native compositor has no live window-content surface. These limitations are explicitly reflected in the UI and evidence above.

## Recommended TASKBAR4 scope

1. Add one real progress producer through the bounded taskbar activity contract, including explicit completion, failure, termination, and clear handling.
2. Run QAC guest acceptance for grouping, pinned launch, activation, hover previews, preview selection, overflow, and dismissal; repeat the TASKBAR2 pin persistence check after restart.
3. If the native compositor gains a safe per-window surface snapshot API, evaluate bounded native thumbnails without copying unrelated framebuffer regions.
