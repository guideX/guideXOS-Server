# App Model Phase 18 — Start / All Programs Action Presentation

Status: **Outcome B.** The existing hosted Start context menu now presents actions through generic AppRegistry data for All Programs and Recent Programs. The model, identity, invocation, and hosted regressions pass. Native-window screenshots were unavailable, so visual acceptance and real mouse interaction with the new rows remain unverified. Desktop icons and the separate bare-metal Start menu remain deferred.

## Phase gate and repository state

The prompt expected Phase 17 at `b3245f4b932e2dfc9440a5ef501f7d8232f6df50`. That was the actual starting HEAD on `main`. No authoritative `.phase` marker was present; the Phase 17 report and current-state map identified Phase 18 as next, with no existing Phase 18 completion report. The prompt was current, not stale or duplicated.

The initial worktree already contained a modified `desktop.json`, generated Navigator images, Phase 11/16/17 logs and fixtures, and other unrelated state. Those files were not staged. The existing Phase 17 AppRegistry declarations and invocation API were reused without adding declarations or changing persistence.

## Start, All Programs, and menu audit

The hosted Start button and panel are drawn and routed by `compositor.cpp`; the panel is compositor state, not a separate app window. The pinned/recent and All Programs lists are built dynamically from `DesktopService::GetRegisteredApps()`. Their visible labels and canonical app IDs are held in parallel vectors, so a selected row supplies its stored canonical ID without matching a display name.

The primary row click continues through the existing `Compositor::openStartMenuApp()` launch path. Right-clicking a program row already opened `RightClickMenu`, whose row model held Open, pin/unpin, and Recent Programs removal commands. It already draws a cursor hover highlight, handles mouse clicks, and dismisses on outside click. It has no keyboard menu-key or Shift+F10 navigation, no keyboard row movement, and no touch long-press path. This phase adds no new input-routing or menu-stack mechanism.

Recent Programs uses the same selected-row identity vectors and calls the same Start app context-menu entry point, so it inherits the action rows. Desktop icons use a separate `ShowForDesktopItem` menu path and are deferred. Bare-metal Start is a separate static implementation in `kernel/core/desktop.cpp`; it does not consume hosted `DesktopService` or AppRegistry action snapshots and is deferred.

## Presentation and dispatch

`start_menu_action_model.h` turns an owned `AppActionList` snapshot into menu rows. It clamps iteration to the fixed 12-action array and accepts only available, current, nonzero-generation entries whose canonical app ID matches the selected snapshot and whose label fits the existing 64-byte bound. Action order is the AppRegistry enumeration order. The menu row owns a copy of the declaration label and `AppActionInfo`.

`RightClickMenu::buildItems()` queries `DesktopService::GetAppActions(canonicalAppId)` and builds the existing menu shape. All Programs gets Open, pin/unpin, then a divider and declared action labels when any are available. Recent Programs adds the existing remove command after its divider. A zero-action or unknown app gets no empty action group; its ordinary Open, pin/unpin, and Recent Programs commands remain.

Clicking an action row calls `DesktopService::InvokeAppAction(snapshot)`. The snapshot overload retains the registration generation and label for revalidation. Success closes Start through the compositor's existing close path. Failure closes the popup, reports the failure, and does not fall back to an ordinary app launch. The Start and menu production changes contain no Navigator, `open-home`, or `Home` special case; they render the values returned by DesktopService.

The current production inventory remains one action: `guidexos.navigator` declares `open-home` with label `Home`. Notepad, File Explorer, ImageViewer, and Developer Studio declare zero actions. The 12-action synthetic fixture builds 15 All Programs rows and 17 Recent Programs rows. At 28 pixels per row, the largest Recent Programs menu is 476 pixels high and fits a 640×480 viewport at a clamped 4-pixel top origin. A 64-byte label remains owned by the row, and GDI clipping confines its drawing to that row.

Duplicate labels remain distinct because selection carries the action ID and full snapshot, not the rendered text. The focused test repeatedly selects the `zeta` action from two identically labelled rows, dispatches that ID, and clears the menu model.

## Shell-surface boundary

| Surface | Phase 18 status | Reason |
|---|---|---|
| Hosted Start app list | Supported | Uses the existing Start app context menu and canonical row identity. |
| Hosted All Programs | Supported | Dynamically generated and uses the generic context menu path. |
| Hosted Recent Programs | Naturally inherited | Reuses the same row identity and app context-menu path. |
| Desktop application icons | Deferred | Separate context-menu target/model; expanding it is outside this phase. |
| Bare-metal Start / All Programs | Deferred | Separate static kernel implementation without the hosted AppRegistry action API. |
| Hosted Console | Remains supported | Generic `desktop.app.actions` and `desktop.app.action` commands remain unchanged. |
| Settings → Default Apps | Not applicable | Actions are not defaults and remain absent from its model and persistence. |

## Safety and behavior evidence

The menu model tests production Navigator data, zero-action and unknown applications, unavailable backends, deterministic ordering, duplicate labels, all 12 actions, long labels, malformed counts, generation ownership, removed actions, unregister, same-slot and same-ID replacement, and backend changes. Old menu snapshots fail generic invocation after the registration is replaced or its action is removed. The test also runs 100 valid generic dispatches and 100 stale/unknown failures without redirecting a callback.

The Phase 17 hosted runtime smoke passed 13/13 checks: it queried the declaration, checked zero-action apps, rejected unknown/malformed actions without launch or persistence changes, invoked Home once on a stopped Navigator and ten times on its active instance, verified the current Home URL and UI-thread consumption, verified Recent Programs semantics, and closed the owned window cleanly. The existing Navigator message handler maps `open-home` to `handleToolbarAction(kWidgetIdHome)`, the same function used by the toolbar. Ordinary Start row launch code remains unchanged; native Start-click testing was unavailable with the current UI tooling.

The Phase 17 Console command consumer remains generic and passed in that same runtime smoke. No action declarations, default handlers, Startup entries, protocol defaults, folder defaults, or action state are persisted by menu construction or invocation.

## Regression and build results

| Area | Result |
|---|---|
| Phase 18 action/menu model | **55/55**; includes 50 menu-model build/select/dispatch/clear cycles, 100 valid dispatch cycles, and 100 stale/unknown failures. |
| Phase 17 production action runtime | **13/13**; one launch-on-miss plus ten active-instance Home invocations; clean close and unchanged active-instance Recent Programs. |
| Phase 16 folder activation | Model **26/26**; hosted runtime **53/53**. |
| Phase 15 URI/protocol | Model **41/41**, 100 persistence reloads, 100 activations, 100 no-handler cycles; runtime **12/12** and 20 unknown-protocol attempts. |
| Phase 14 association retirement | Hosted runtime **27/27** and 106 repeated no-handler opens. |
| Phase 13 JPEG | Hosted runtime **20/20**; three ordinary opens, one Open With open, seven decoder-failure probes, and two Default Apps checks. |
| Phase 12 Navigator HTML | Hosted runtime **11/11**. |
| Phase 11 ImageViewer PNG | Hosted runtime **21/21**; three ordinary opens, one Open With open, and eight decoder-failure probes. |
| Phase 10 Developer Studio | Experimental hosted runtime **10/10**, including four real process/window launch-close cycles. |
| Phase 9 Default Apps | Model **39/39**, interaction **9/9**, and 100 lifecycle cycles. |
| Phase 8 handler persistence | **54/54**, with 100 persistence reloads, 100 ordinary dispatches, and 100 one-time Open With dispatches. |
| Phase 7 Open With | Model **17/17**, 100 menu-model cycles; runtime **4/4** with 20 explicit activations and unchanged ordinary default. |
| Phase 6 file/document activation | Model **60/60**; runtime **39/39** (7 File Explorer, 29 Notepad, 3 negative checks), including 20 repeated document activations. |
| Phase 5B readiness | PASS; `appModelV1StatusReady=true`, `unresolved=0`, `highRisk=0`, and regression `result=PASS`. |
| Settings | Center **89/89**; inventory **24/24**; S6 **42/42**; S7 **14/14**; Users **13/13**; Network contract **45/45**; Clock/Time smoke PASS. |
| Startup / Recent Programs | Startup regression **16/16**; Phase 4D Recent Programs report `result=PASS` with temporary state restored. |
| Standard hosted build | PASS; **177 warnings, 0 errors**, matching the Phase 17 warning baseline. No warning was emitted by `right_click_menu.cpp` or the menu-model header. |
| Experimental hosted build | PASS; **176 warnings, 0 errors**, matching the Phase 17 warning baseline. No warning was emitted by `right_click_menu.cpp` or the menu-model header. |

The built-in document-dispatcher regression initially exposed four stale expected error strings left behind by the earlier shared activation API. The test expectations now match that API's generic errors; the dispatcher suite passes **21/21**, including 100 PNG, 100 Navigator HTML/HTM, and 100 JPEG lifecycle cycles. No production dispatcher code changed for that test correction.

## Platform and visual limits

The hosted `build.bat` and `build-native-experimental.bat` source lists include the root `compositor.cpp` and `right_click_menu.cpp`. The bare-metal `kernel/Makefile` instead compiles `kernel/core/*.cpp`, including its separate `kernel/core/desktop.cpp`; no kernel source changed. The Phase 18 UI code is therefore hosted-only and QEMU was not applicable.

The computer-use surface returned no native app windows, so no Start-menu screenshot or native cursor interaction was available. The menu model, row bounds, clipping code, identity snapshots, dispatch result, compositor close path, and runtime action lifecycle were checked, but visual placement, rendered label appearance, hover, outside dismissal, and clicking a Start action were not visually accepted. This is why the phase closes as Outcome B.

## Closeout

The work is committed separately from the pre-existing modified configuration, generated images, and temporary Phase 11/16/17 files. See the task closeout for the final commit, final worktree, upstream divergence, and push result.
