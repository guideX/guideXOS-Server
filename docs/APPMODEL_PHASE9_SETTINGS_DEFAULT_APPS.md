# App Model Phase 9: Settings Default Apps

Date: 2026-10-02

## Result

**Outcome B — the Settings integration and model behavior are implemented and
validated; visual acceptance is blocked because this environment exposes no
native application windows to inspect.**

Settings now presents machine-global file-handler policy under **Apps →
Default apps**. It reads and mutates policy through `DesktopService` and the
authoritative `AppRegistry` APIs. It does not parse or write
`appmodel-default-handlers.cfg`, maintain a second association table, or
describe machine-wide settings as per-user preferences.

The existing **Installed apps** inventory and its Open action remain in the
Apps category. The stable route `settings://apps/default` selects Apps, opens
Default apps, and starts focus at its tab control; `settings://apps/defaults`
is also accepted. Search terms for default apps/applications, file types,
associations, Open With, and `.txt`, `.log`, `.ini`, and `.cfg` route to the
same page. With a populated App Model snapshot, the deep link focuses the first
file-type row; with an empty or unavailable snapshot, it focuses the Default
apps tab.

## Starting state and review

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `4fb0495940307ce35d041a92afc5ba482f952428`
- Upstream at start: `origin/main`; 1 commit ahead, 0 behind.
- Pre-existing user state preserved: modified `desktop.json` and six
  untracked `tmp/startup-appmodel-regression-*` directories, including the
  2026-10-02 fixture.
- Reviewed App Model Phase 5B, 6, 7, and 8 reports and Settings S4–S9 reports.
  There is no separate S1 report; S1 behavior is covered by Settings Center
  source and its model suite. S7/S8/S9 reports cover the inherited navigation,
  search, focus, and compact-window conventions.

The S6 Apps page was a bounded AppRegistry inventory (64 entries), ordered by
display name and canonical ID. It shows registry metadata and details, keeps
temporary registration generation with selection identity, supports keyboard
and mouse selection plus scrolling, and exposes Open only for built-ins with
an existing launcher. Search and the `settings://apps` routes were already
bounded. Phase 9 retains this inventory and Open behavior as **Installed
apps** rather than replacing it.

## Data ownership and presentation

`DesktopDefaultAppsBackend` is a Settings adapter for the service methods:

```text
Settings DefaultAppsModel
    → DesktopService
    → AppRegistry
    → machine-global default-handler policy
```

The adapter queries known extensions, policy state, and capable handlers;
uses the App Model inventory for labels and durable-registration checks; and
calls the service set/clear methods with canonical IDs. The backend remains
responsible for current capability, registration durability, persistence,
and verified reread. Settings refreshes after selection and reports the
authoritative result.

The page holds at most **32 extension rows**, sorted by normalized lexical
extension and deduplicated. An overflow is reported as truncation. Each row
preserves built-in, configured, and effective policy data separately. It
identifies the effective application as Current and labels the policy
`System default` or `Configured default`. An unavailable configured handler
is shown alongside the effective fallback (for example, `Configured: Text
Editor (unavailable)` and `Current: Notepad`). If the registered application
has disappeared and has no trustworthy label, Settings says `Configured app
unavailable` without inventing a name.

The picker enumerates only currently capable durable handlers and is bounded
by the service result. Visible labels are presentation; selection carries the
canonical App ID. Duplicate labels receive a secondary ID suffix so they can
be distinguished, but the suffix is never used as identity. A stale picker
choice is submitted to the authoritative API and revalidated there. Choosing
an unconfigured built-in default is a no-op. Restore default clears the
configured record, rereads the policy, and returns focus to the extension
row. Empty extension, unavailable service, no-handler, and truncation states
remain explicit. Default-policy data is refreshed on entry/action; this page
does not poll.

## Interaction and compact layout

The Settings page uses the existing Apps tabs and row controls. Mouse and
Enter open a picker for the selected extension; Cancel returns focus to that
row. Keyboard movement covers extension rows, handlers, Restore, and Cancel.
Mouse hit-testing is restricted to visible rows, and scrolling keeps the
selected extension and picker choice stable.

The 640 × 480 layout model keeps the tabs and status in bounds, presents four
visible extension rows, and makes five picker rows plus Restore and Cancel
reachable through scrolling. The model's geometry and hit-test checks passed.
This is model evidence, not screenshot-backed visual acceptance.

## Verification

### Phase 9 and neighboring App Model suites

- Default Apps model: **36/36**.
- Default Apps interaction: **9/9**.
- Default Apps open/select/refresh/restore/close lifecycle: **100/100**.
- Phase 8 persistence/resolution: **44/44**; owner reload **100/100**;
  ordinary Open dispatch **100/100**; one-time Open With dispatch **100/100**.
- Phase 7 Open With menu model: **16/16**; menu lifecycle **100/100**.
  Hosted one-handler runtime: **4/4** checks, with **20/20** menu selections
  and explicit activations; ordinary Open still used the unchanged default.
- Phase 6 file activation: **57/57** model checks and **39/39** hosted runtime
  checks, including 20 exact-path document activations and fail-closed cases.
- App Model identity: **25/25**.
- Phase 5B closeout: **PASS**, with **0 unresolved** and **0 high-risk**
  stored targets. Current status and inventory agreed; the
  product-default dispatch, fallback, recents, risky-target, and out-of-scope
  checks passed. This supersedes the older S6 note that recorded a failed
  Phase 5B closeout on an earlier checkout.

The synthetic multi-handler fixture registers two same-label editors in an
isolated `AppRegistry`. The Settings model selects Editor B by canonical ID;
ordinary association resolution selects B; a one-time explicit Open With
activation can select A without changing the configured B policy. Recreating
the registry owner reloads B, and Restore removes the override and returns
ordinary resolution to the built-in fallback. A reused registration position
cannot redirect a stale ID. No synthetic handler is registered in the normal
production registry. This is isolated model/registry integration evidence;
the hosted GUI runtime remains the production one-handler case.

### Settings regressions

- Settings Center: **89/89**.
- Devices: **21/21**; Storage: **24/24**.
- S6 Apps/Date & Time: **42/42**.
- S7 Accessibility: **21/21**; Developer: **23/23**; navigation/search:
  **14/14**.
- Users: model **11/11**, navigation **8/8**, interaction/layout **13/13**.
- Network Settings contract: **45/45**; transaction: **21/21**.
- Recent Programs hosted smoke: **PASS**; persisted desktop-state writes
  remained false and temporary state was restored.
- Startup App Model hosted smoke: **PASS**; registration, explicit launch,
  empty startup window state, and repeated startup persistence all passed.

The file-activation and identity test runners now link the Phase 8 store and
filesystem implementation required by their current AppRegistry sources;
both suites passed at their full counts.

### Hosted build and runtime

`build.bat` completed successfully and produced `guideXOSServer.exe`. The
captured build log reported **184 warning lines and 0 error lines**. No warning
was emitted from `settings_center.cpp` or `desktop_service.cpp`; the warnings
are the existing shared/unrelated diagnostics recorded by the build.

The hosted Settings runtime was run from an isolated copy of the executable
and desktop state, using `gui.start` followed by
`desktop.launch gxos.builtin.settings`. The App Model reported a successful
canonical Settings launch, the Settings Center started, and the compositor
reported one owned window after startup settled. The same runtime's file
association diagnostic confirmed that `.txt`, `.log`, `.ini`, and `.cfg`
resolve to `gxos.builtin.notepad` with current document capability and an
available backend. The root `appmodel-default-handlers.cfg` was absent before
and after the run; the isolated runtime did not create one either.

Computer Use returned `apps=[]` and only the Codex in-app browser, with no
native windows. Therefore the Default apps window could not be captured or
visually inspected. The hosted App Model launch and compositor ownership were
verified, and the route/search/focus, mouse/keyboard, scrolling, and compact
layout procedures were exercised in model tests. No claim is made about
runtime clipping, hover, or focus-ring appearance.

QEMU and physical hardware were not used: Phase 9 changes are in the hosted
Settings renderer/model and hosted `DesktopService` API surface; no
bare-metal Settings path or kernel service contract changed.

## Closeout and limits

In the current production AppRegistry, Notepad is the only real capable
handler for the four known text extensions. The picker truthfully offers no
alternate editor until another capable durable application is registered.
With such an app present, normal users can inspect and change the
machine-global default through Settings without Settings understanding the
persistence format. If an override becomes unavailable, Settings retains and
shows that configured state while showing the effective fallback; File
Explorer ordinary Open uses the effective fallback (currently Notepad).

The next app-ecosystem capability supported by this evidence is **more
document-capable applications**. The default-policy and Open With paths
already support competing handlers, while the production text-handler set
currently contains only Notepad. A package/install lifecycle would help
deliver those applications, but it is not required to keep policy ownership
and resolution correct.

This report records the implementation and tests, but the environment's lack
of a visible native Settings window leaves layout/focus/hover/clipping
inspection outstanding. Runtime fixtures and logs were not staged.
