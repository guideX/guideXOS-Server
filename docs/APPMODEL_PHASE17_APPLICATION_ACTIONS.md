# App Model Phase 17: Declarative Application Actions

Status: Outcome B. The action model is generic and one real production action executes end to end. One production action is currently grounded, and Start/All Programs has no generic action presentation menu; action delivery to non-built-in execution kinds remains deferred.

## Audit and selected production capability

The repository has no existing generic app-action context menu. Start, pinned/recent entries, and ordinary desktop launch routes resolve an application and launch it; File Explorer's Open With surface selects a document handler; file and folder context operations carry selection/path context; Settings/Control Panel links open their existing pages. Those paths do not expose parameterless app-owned commands through a shared registry contract. The hosted server's Console command loop is a suitable small generic consumer and now offers generic enumerate/invoke commands.

| Application / surface | Existing operation | Current caller/path | Real implementation? | Action candidate? |
|---|---|---|---|---|
| Navigator | Home toolbar command | Navigator toolbar → `handleToolbarAction(kWidgetIdHome)` → `navigateTo("about:navigator")` | Yes; existing UI command resets the current tab to the built-in Home page | **Yes:** `open-home` / `Home` |
| Navigator | Back, Forward, Reload, URL navigation | Toolbar/URL field and URI/document activation | Yes, but stateful navigation or typed activation | No; not a parameterless independent action |
| File Explorer | Home/root and child navigation | Existing navigation model; external folders use `DesktopService::OpenFolder` | Yes; internal navigation is window/current-directory state, external path activation is already typed Folder activation | No; no new open-at-fixed-path command is needed |
| File Explorer | Open, Open With, copy/move/delete/rename | Selection and filesystem-entry context routes | Yes, but document activation or selection-sensitive operations | No; context-sensitive operations are out of scope |
| Developer Studio | Open/New Project and document commands | Package manifest is present, but its NativeElf implementation source is not in this checkout; document activation is its only current App Model capability | Existing NativeElf document backend only; no inspectable parameterless action consumer in this checkout | No |
| Notepad | New/Open/Save/Save As | Editor/document UI | Yes, but document and dirty-buffer state affect behavior | No |
| ImageViewer | Open, zoom, pan, rotate, save | Viewer UI | Yes, but depends on loaded image/window state | No |
| Settings / Control Panel | Pages and default-app changes | Start links and Settings Center | Yes, but navigation or configuration mutation | No; actions are not settings/defaults |
| Task Manager / Disk Manager | Select process/disk and operate on it | Context and current selection | Yes, selection-dependent | No |
| Desktop / Start / recent / shortcuts | Launch target | DesktopService and shell launch routes | Yes, generic Application or typed activation | No separate action is needed for ordinary launch |
| Hosted Console | `desktop.app.actions` / `desktop.app.action` | Generic server command loop | Yes; delegates to the generic DesktopService APIs | Generic consumer; it does not special-case the selected app's action |

This audit found one grounded, parameterless command that can be invoked without file/URL/selection context: Navigator Home. The action is distinct from URI activation: it calls the same pre-existing Home toolbar handler. Notepad, File Explorer, ImageViewer, and Developer Studio correctly expose zero actions in production. No fake manager, speculative command, setting, or document action was added.

## Identity, bounds, and ownership

An action declaration is owned by its `AppManifest`/`RegisteredApp`; there is no second shell-owned declaration database. JSON manifests may declare an `actions` array of `{ "id": ..., "label": ... }`. Built-in Navigator metadata declares one `open-home` / `Home` action. Action IDs are case-sensitive canonical ASCII: 1–48 bytes, lowercase `a-z`, digits `0-9`, and single interior hyphens; no leading, trailing, or consecutive hyphens. Labels are required, at most 64 bytes, and reject control characters. Duplicate IDs invalidate the manifest; duplicate labels are allowed because IDs carry identity. Enumeration sorts by canonical action ID for stable order.

Each manifest supports at most 12 actions. The registry supports at most 512 applications, so no more than 6,144 declarations can be retained across all applications. Enumeration returns a fixed 12-slot value-owned snapshot per app (including the registry-assigned registration generation and availability flags); it exposes no pointers into mutable AppRegistry storage. There is no description or argument/property bag because no current UI or grounded action requires either.

AppRegistry gives every durable or temporary registration a monotonic registration generation. Replacements and action-backend availability changes get a new generation; temporary unregister removes the registration, and a later slot or canonical-ID reuse cannot satisfy an old snapshot. Enumeration, resolution, and dispatch use the existing AppRegistry mutex in DesktopService. Invocation checks canonical app ID, action grammar/declaration, current generation (when invoking an enumerated snapshot), display label snapshot, and backend availability before dispatch. The generic action dispatcher checks built-in ownership and revalidates immediately before invoking the registered app handler. Failures have bounded statuses: success, app unavailable, action unavailable, stale registration, and dispatch failure.

The public generic API is `DesktopService::GetAppActions(canonicalAppId)` and `DesktopService::InvokeAppAction(canonicalAppId, actionId)`. A snapshot overload preserves generation/label validation for menu consumers. Actions do not create a new `AppActivationKind`: they are parameterless command requests, not app launches with document/URI/folder payloads. A request to an already-running Navigator is sent to its own process mailbox with the action ID and generation; Navigator revalidates on its UI thread and calls the existing Home toolbar handler. With no running process, the action follows Navigator's existing ordinary Home launch path.

The application owns action-ID-to-behavior mapping (`Navigator::InvokeAppAction` and its `open-home` mapping); AppRegistry owns declaration, identity, generation, enumeration, and revalidation. DesktopService binds the built-in application's generic handler, as it does for existing built-in typed activation. The Console syntax accepts the target canonical App ID and action ID; it contains no per-action behavior. There is no action menu in Start/All Programs today, so no menu-specific dispatch switch was introduced.

## Other execution models and policy

NativeElf has no App Action ABI or current action consumer; it remains unsupported for invocation. The Developer Studio NativeElf implementation is unavailable in this checkout and its manifest declares no actions. The registry's generic declaration/enumeration shape can represent any registered manifest, but the built-in dispatcher intentionally rejects non-built-in execution kinds until a real safe action transport exists. `GXAppPackage` and `Script` manifest kinds exist, but this checkout has no managed application action receiver or generic action transport for them.

An action that starts Navigator is an ordinary app launch and records Recent Programs exactly once according to the app's existing `recordRecentPrograms` hint. An action delivered to its already-running process does not add a separate Recent Programs item. Failed or stale actions do not launch, mutate Recent Programs, or touch defaults, Startup, document handlers, URI handlers, or folder handlers. Actions are not defaults and are not surfaced in Settings → Default Apps. No persistent action state is created. No Startup, Default Apps, or action enable/disable behavior changed.

## Verification

The focused AppRegistry model suite passed **36/36**. It covers production/zero-action declarations, manifest parsing and validation, ID and label limits, duplicate-ID rejection and duplicate-label allowance, deterministic order, owned values, no-backend filtering, backend-generation mutation, synthetic generic dispatch, unknown/malformed targets, unregister, same-slot/same-ID replacement, stale-generation rejection, and slot reuse. It ran **100 successful resolution/revalidation/generic-dispatch cycles** and **100 stale or unknown-action failures** without invoking the app callback.

The production hosted runtime smoke passed **13/13** checks. It made **one launch-on-miss action invocation plus 10 real active-instance invocations**. It verified generic discovery, empty action sets, unknown/malformed/overlong rejection without launch or window, unchanged Recent Programs/default-handler persistence on failure, ordinary Navigator process/window ownership, Home URL state, consumed requests through the UI-thread Home handler, no extra Recent Programs entry for active-process delivery, and clean close. Runtime evidence is process/window ownership, URL state, and dispatch logs; native window screenshot capture was unavailable, so no visual appearance claim is made.

### Build and regression matrix

| Area | Result |
|---|---|
| Standard hosted build | PASS; 177 warning diagnostics, 0 errors. This equals the Phase 16 standard-build warning baseline. AppRegistry, manifest validation/loading, DesktopService, and server action additions emitted no warnings. Navigator warnings are existing unrelated layout helpers/sign comparisons. |
| Experimental hosted build | PASS; 176 warning diagnostics, 0 errors. No warning originates in the AppRegistry, manifest, DesktopService, server, or new Navigator action code. |
| Phase 16 folder activation model/runtime | PASS; 26/26 model assertions and 53/53 runtime checks, including folder/document routing, canonical File Explorer ownership, internal navigation, Recent Programs, and close. |
| Phase 15 URI/protocol model/runtime | PASS; 41/41 model checks, 100 persistence reload cycles, 100 activation cycles, 100 no-handler cycles, and runtime 12/12 plus 20 unknown-protocol attempts. |
| Phase 14 legacy association retirement | PASS; 27/27 runtime checks and 106 repeated no-handler opens; BMP/GIF remain unsupported. |
| Phase 13 JPEG | PASS; 20/20 runtime checks, 3 normal activations, 1 Open With activation, 7 decoder failure probes, and 2 Default Apps checks. |
| Phase 12 Navigator HTML | PASS; 11 runtime checks across local load, relative link/history, reload, Open With, and failure behavior. |
| Phase 11 ImageViewer PNG | PASS; 21 runtime checks, 3 normal activations, 1 Open With activation, 8 decode failure probes, and Settings discovery. |
| Phase 10 Developer Studio | PASS on the experimental hosted binary; 10/10 runtime checks and 4 real Developer Studio process/window launch-close cycles, shared `.txt` Open With competition, canonical Notepad one-time choice, and AppRegistry-selected `.CPP` default. |
| Phase 9 Settings Default Apps | PASS; 39/39 model checks, 9/9 interaction checks, 100 lifecycle cycles. |
| Phase 8 default-handler persistence | PASS; 54/54 checks, 100 persistence reload cycles, 100 ordinary dispatches, 100 one-time Open With dispatches. |
| Phase 7 Open With | PASS; 17/17 model checks, 100 menu lifecycle cycles, 20 explicit runtime activations, and unchanged ordinary Notepad default. |
| Phase 6 document activation | PASS; 60/60 model checks and 39 runtime checks (7 File Explorer, 29 Notepad, 3 negative), including 20 repeated activations. |
| Phase 5B readiness | PASS; `appModelV1StatusReady=true`, `unresolved=0`, `highRisk=0`; read-only regression-closeout smoke reports `result=PASS`. |
| Startup / Recent Programs | Phase 4D Recent Programs regression PASS; isolated Startup regression PASS for no-launch registration, five canonical explicit launches, clean ownership, and persistence across two starts with zero restored app windows. |

Action declarations and dispatch are hosted AppRegistry/DesktopService/Navigator code. `gui_protocol.h` gained one appended directed-mailbox message; `rg` found no bare-metal `kernel/` include of this header and no kernel/core source was changed. The hosted change is outside the QEMU build/source path, so QEMU is not applicable. The Phase 16 4096-byte folder/document ownership, generic folder opening, internal Explorer navigation, and no-folder-default policy remain covered by the regression above; this phase changes no folder routing or store code.

Actions are not persisted, are not default handlers, and do not mutate Startup. Failed runtime requests were checked against window ownership, Recent Programs, and the default-handler file bytes; the generic failure path does not call any registration or activation-store mutator. The existing Settings Default Apps and default-handler model suites passed with the new action capability present and did not add action rows.
