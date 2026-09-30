# Settings S9: Settings 1.0 Acceptance and Polish

Date: 2026-09-30

## Result

**Outcome B — implementation and hosted App Model launch are strong; full live UI acceptance remains blocked by this execution surface.**

The supported hosted build passed. The normal App Model launched Settings as
gxos.builtin.settings and the compositor registered a visible Settings window.
Settings model, provider, bridge, and inherited regression suites passed at
their full recorded counts. The existing hosted compositor CLI procedure also
exercised Settings at 640 × 480: keyboard search and activation, category and
row clicks, scrolling, Enhanced focus indicator off-to-on persistence, and
Developer actions including Console launch. Ten Settings close/reopen cycles
passed across five isolated hosted runs.

The environment exposed no native app windows to Computer Use, and its
computer app-launch API was absent. Therefore this report does not claim a
visual inspection, screenshot-backed assessment, or complete keyboard/mouse
session. In particular, the live all-twelve-category traversal, full search
matrix, all deep-link launches, 640 × 480 review of every page, and normal-size
visual review remain unverified. No Settings defect was reproduced, so no
source change or new regression test was warranted.

## Starting state and documentation review

- Repository: D:\dev\guideXOSServer
- Branch: main
- Starting HEAD: 53032008008e6d654b1a2626804c31074602dea1
- Upstream at start: origin/main; 1 commit ahead, 0 behind.
- Tracked worktree state at start: clean.
- Existing untracked generated directories preserved:
  - tmp/startup-appmodel-regression-20260929-060500/
  - tmp/startup-appmodel-regression-20260930-060517/

Reviewed the S2, S2R, S3, S4, S5, S6, S7, and S8 phase reports and current
Settings source/models. There is no standalone S1 Settings report in docs;
the initial Settings Center behavior is represented by the current source and
the 89-check Settings Center model suite.

The ownership boundaries remain intact: Display uses its display
configuration owner; Network uses the kernel snapshot service and exposes no
production mutation action; Personalization uses the existing shared
background/theme owners; Devices and Storage use bounded kernel snapshots;
Apps uses AppRegistry; Date & Time reports hosted clock and existing shared
time-zone settings; Accessibility persists the shared enhanced-focus
preference; Developer reads existing diagnostics; Users truthfully reports no
human-account model; System and About report hosted/product state.

## Supported hosted build

- Command: build.bat
- Result: PASS; exit code 0.
- Artifact: D:\dev\guideXOSServer\guideXOSServer.exe, 8,233,941 bytes.
- Build log: out/validation/settings-s9-hosted-build.log
- Diagnostics: 184 compiler warning lines and 0 error lines. Warnings include
  misleading indentation in shared code, unused functions/variables in
  unrelated components, GCC array-bounds warnings in Trash::Launch, and an
  ignored MSVC-specific pragma. The build completed successfully; no warning
  was emitted from Settings-specific implementation code beyond included
  shared headers.

The alternate Visual Studio/MSBuild hosted project path was not run. Earlier
phase reports document unrelated CRT include-shadowing, duplicate-output,
and GCC-specific source issues on that path; those are not evidence of a
failure in the supported build.bat path.

## Launch path and environment limitation

The production hosted path is:

1. build.bat produces guideXOSServer.exe.
2. run-server.bat starts that normal hosted server and provides its runtime
   library path.
3. The hosted desktop starts through gui.start.
4. Settings launches through desktop.launch gxos.builtin.settings.
5. DesktopService resolves the canonical built-in App Model ID and
   SettingsCenter creates the Settings window under compositor ownership.

The Startup App Model regression used the normal hosted executable and
run-server.bat in an isolated runtime copy. It verified that
gxos.builtin.settings is registered, starts through the explicit App Model
launch path, and owns a Settings window. The S7 hosted-runtime procedure used
a byte-for-byte copy of the same normal hosted artifact; it did not use a
Settings-only executable or bypass the App Model/compositor.

Computer Use returned apps=[] and no native windows. Its optional
computer.launch_app entry was unavailable (cua.computer was undefined), and
there were no browser tabs. A fresh inventory after the hosted tests remained
empty. The hosted application itself launched and registered compositor
windows through the production runtime CLI, but those windows were not
exposed as native windows for screenshot capture or direct desktop
inspection. This is an environment/UI-exposure limitation, not a Settings
launch failure.

## Whole-product category and routing audit

The Settings source has a content renderer for each of the twelve real
CategoryId values. The generic placeholder renderer is only selected for the
internal Count sentinel. S8 navigation tests cover all twelve top-level route
slugs. This is source/model evidence, not a claim that all twelve pages were
visually inspected in S9.

| Category | Existing page contract reviewed | S9 live visual traversal |
|---|---|---|
| System | Hosted computer name, processor, logical processors, installed memory, architecture, firmware, active display, and major destination links. Hostname remains read-only. | Not performed. |
| Display | Current display configuration comes from DisplayConfigurationService; existing supported modes, Apply/Cancel behavior, and Display Options launch are retained. Control-plane, persistence, and synthetic-layout checks passed. | App Model launch passed; page and controls not visually reviewed. |
| Network & Internet | Authoritative guideXOS service snapshot, adapter/link/IP/gateway/DNS fields, truthful unavailable state, and no Apply path through unauthenticated COM2. | Not visually reviewed. Existing S7 diagnostics reported 0 of 3 services available in the hosted runtime. |
| Personalization | Shared wallpaper/theme owner and existing PNG import transaction; rejection preserves the active background. Prior S4 hosted logs show a valid PNG import and a non-PNG validation failure. | Not rerun in S9; cancellation and current picker visuals remain unverified. |
| Devices | Bounded kernel inventory, conservative status, details, and explicit unavailable state; no Windows Device Manager substitution. | Not visually reviewed. |
| Storage | Bounded disks/volumes, known MBR and mount data, unsupported fields omitted, and existing Disk Manager action. | Not visually reviewed. |
| Apps | Deterministic AppRegistry snapshot, selection/detail behavior, and Open only for registered launchable built-ins. | App Model launch path passed; Apps page and its Open row were not visually reviewed. |
| Users | Read-only explanation that human identity, accounts, authentication, profiles, permissions, and ownership are unsupported; App Model identity is not a person. | Not visually reviewed. |
| Date & Time | Hosted wall clock and existing time-zone configuration; no claim of NTP, manual time, or RTC-write support. | Not visually reviewed. |
| Accessibility | Enhanced focus is the implemented preference; unsupported capabilities are not fake switches. | Preference was changed and persisted in the hosted runtime; no S9 screenshot of the focus ring. |
| Developer | AppRegistry, service, security, and Phase 5B diagnostics come from existing providers. Console action uses the normal App Model. | Services, Apps, and Diagnostics views were opened through compositor-routed actions; content was not screenshot-inspected. Console launch passed. |
| About | Product identity, development build identity, architecture, and available hosted hardware details; no invented Server release version. | Not visually reviewed. |

### Sidebar, search, and deep links

The category order and labels in the navigation model match the twelve
categories above. Model suites passed the inherited navigation/search
coverage. The S9 hosted compositor procedure searched for “enhanced focus”
with keyboard input and activated the result. It did not exercise every
requested representative term or mouse selection of a search result.

The model route parser covers the twelve top-level slugs: system, display,
network, personalization, devices, storage, apps, users, date-time,
accessibility, developer, and about. Supported nested routes include System
device; Display resolution/display-mode; Network ipv4/dns/gateway;
Personalization background; Devices category filters; Storage disks/volumes;
Apps list; Date & Time time/timezone; Accessibility focus/keyboard;
Developer apps/services/diagnostics; Users session/security; and About
version. Existing route-model tests passed. S9 did not interactively launch
each deep link from a visible Settings window.

### Keyboard, mouse, layout, and focus

The screenshot-free hosted procedure exercised compositor-routed keyboard
and mouse input at 640 × 480. It entered and selected the enhanced-focus
search result by keyboard, moved focus with Tab/Enter, clicked the
Accessibility and Developer navigation/actions, and scrolled. The same
procedure started with enhancedFocusIndicator=false, enabled it, verified
the stored value became true, closed/reopened Settings, and verified
persistence. Five successful runs each performed two close/reopen sequences,
for ten cycles total. Captured run logs are
out/validation/settings-s9-lifecycle-run-3.log through
out/validation/settings-s9-lifecycle-run-7.log.

This establishes runtime input delivery and preference persistence, not
visual focus bounds. The S9 execution surface could not inspect the selected
row, hover region, focus ring, overlap, clipping, scrollbar hit testing, or
repaint behavior. The previous S7 screenshot
out/validation/settings-s7-enhanced-startmenu-focus.png shows the shared
renderer’s white outer and black inner focus rings on a Start-menu selection;
it is inherited evidence, not a new S9 capture. There are no S9 screenshots.

The procedure resized Settings to 640 × 480 and exercised Accessibility and
Developer interactions only. It did not visit all twelve pages at that size.
The Startup App Model test also launched Settings at its default size, but
did not provide visual layout evidence. Normal-size and all-page compact
layout acceptance therefore remain open. No conclusion about cursor flicker,
tearing, hover oscillation, clipping, or visual sparsity is claimed.

## App and advanced-tool launch results

| App Model ID | Result | Evidence scope |
|---|---|---|
| gxos.builtin.settings | PASS | Canonical registration and Settings compositor window ownership. |
| gxos.builtin.notepad | PASS | Startup App Model explicit launch; not launched from the Settings Apps row in S9. |
| gxos.builtin.console | PASS | Launched by the Settings Developer action in the hosted compositor procedure. |
| gxos.builtin.diskmanager | PASS | Hosted desktop App Model route. |
| gxos.builtin.displayoptions | PASS | Startup regression and hosted desktop App Model route. |
| gxos.builtin.controlpanel | PASS | Startup regression and hosted desktop App Model route. |
| gxos.builtin.onscreenkeyboard | PASS | Registered app launched through the hosted desktop App Model route. |
| com.guidexos.developerstudio | Expected unavailable | Registry reports openSupported=false; Settings correctly keeps Open unavailable and does not bypass App Model. |

An eligible app launch succeeded from a Settings action (Console). The
specific Apps inventory Open-row action was not visually exercised in S9.

## Provider, performance, errors, and wording

Source and model review confirms refresh work is page-scoped: Network and
Devices/Storage periodic policies activate only while their page is selected
and the window is focused; Apps refreshes while visible/focused and not
searching; Date & Time updates its text region once per second only while
visible/focused and not searching; Personalization refreshes from its
existing callback; Developer service/diagnostic reads are manual or on page
entry. No static page has an accumulating timer in the reviewed lifecycle.

No visual latency or repaint measurement was possible. No microbenchmark was
added. Model/provider checks covered unavailable Network, Devices, and
Storage; empty Apps inventory; unsupported Open; Users unsupported state;
and time-provider unavailability. The existing wallpaper regressions include
valid PNG and invalid-file handling. These error states were not all
reproduced as visible S9 UI states.

The bounded wording review found the intended distinction between unavailable
state, unsupported capability, undefined identity, and implemented-but-off
preference in the models/pages. No wording repair was indicated. The visual
consistency review is source-level only: shared row/focus helpers and page
renderers remain in use; title alignment, row fit, hover, clipping, and
normal-size spacing were not visually compared in S9.

## Automated tests and inherited regressions

All requested Settings model and bridge suites passed without reducing
coverage:

| Suite | Result |
|---|---:|
| Settings Center | 89/89 |
| S6 Apps/Date & Time | 42/42 |
| S7 Accessibility | 21/21 |
| S7 Developer | 23/23 |
| S7 navigation/search | 14/14 |
| S8 Users identity | 11/11 |
| S8 navigation | 8/8 |
| S8 interaction/layout | 13/13 |
| Devices inventory | 21/21 |
| Storage inventory | 24/24 |
| System-service bridge | 78/78, including Device 13/13 and Storage 12/12 |
| Network Settings contract | 45/45 |
| Network transaction | 21/21 |
| Block-device generation | 5/5 |

Additional inherited checks:

- Display control-plane source smoke: PASS.
- Display persistence source smoke: PASS.
- Synthetic Display/layout smoke: 77/77 checks.
- Clock/time Settings source smoke: 7/7 checks.
- User-background phase 1 smoke: PASS (manifest bounds, import transaction,
  rejection/removal safety, PNG/parser, shared dispatch, and syntax checks).
- Startup App Model regression: PASS (canonical registrations, no
  unexpected startup windows, explicit launch ownership, and persisted
  empty-window state).
- Advanced launch routes: PASS for Disk Manager, Display Options, Control
  Panel, and On-Screen Keyboard.

The S7 hosted runtime procedure was repeated five times with screenshots
disabled; each run passed preference persistence and Settings/Console window
ownership checks. One first orchestration wrapper failed to capture
PowerShell Write-Host output even though its hosted procedure printed the
passing persistence marker; the capture redirection was corrected and the
five retained repetitions passed.

## Phase 5B, QEMU, hardware, and evidence

The live hosted diagnostic remains not-ready: App Model V1 ready=false,
overall WARN, 94 launch-storage preview records, 12 unresolved, and 12
high-risk; writesStorage=false. The stale later fixture still expects 18
built-ins while the current registry has 19. S9 did not weaken or repair
those assertions.

QEMU was not run. S9 changed no kernel-backed Settings provider or service
protocol, and the system-service bridge/model suites passed. Existing S2R,
S3, and S5 QEMU evidence remains the inherited production-path proof; S9
claims no new QEMU result. Physical hardware was not tested.

There are no fresh S9 screenshots because the execution surface exposed no
native windows. Existing ignored S7 screenshots remain under
out/validation/ and are not committed.

## Repository closeout

The acceptance report is the only tracked S9 change. The two pre-existing
startup regression directories remain untouched, and the S9 Startup App Model
run created one additional untracked directory:
tmp/startup-appmodel-regression-20260930-065239/. It is also preserved and is
not committed. Test executables, logs, and inherited screenshots remain under
ignored validation/output directories. The runtime procedure's temporary
window-bounds change was restored; desktop.json, desktop.state, and
display-options.cfg were restored by the procedure.

## Defects, limitations, and readiness

- Settings defects discovered/fixed: none.
- Product/runtime launch defect: none reproduced; canonical App Model
  launches and compositor ownership passed.
- Validation-environment limitation: no native app/window exposure, app
  launch API, or screenshot surface. This prevented the required full visual
  and keyboard/mouse acceptance.
- Underlying subsystem limits accurately surfaced by Settings remain:
  no human-account/authentication/profile model; no authenticated COM2
  service peer or Network mutation; hosted Network/Devices/Storage service
  availability is limited; storage does not provide GPT enumeration or
  reliable volume capacity/free-space data; Apps do not have package
  uninstall/lifecycle support; and time does not have NTP/manual/RTC-write
  support.

Settings is a strong hosted implementation and its canonical runtime launch
works. It is **not formally accepted as Settings 1.0** until a visible hosted
window can complete the all-page, full-search, deep-link, focus-rendering,
wallpaper-picker, and normal/640 × 480 visual acceptance gates. This is
Outcome B, not Outcome A or C.

The first post-Settings subsystem project should be the App Model Phase 5B
launch-target/lifecycle closeout: resolve the 12 unresolved/high-risk preview
records and its stale built-in-count fixture while keeping the independent
not-ready assertion intact.
