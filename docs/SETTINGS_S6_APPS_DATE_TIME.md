# Settings S6: Apps and Date & Time

## Result

**Outcome A.** Settings now has an Apps inventory and a Date & Time page backed
by the existing guideXOS App Model and the existing hosted clock/time-zone
owners. The pages expose only information those owners provide. No App Model
package lifecycle or privileged time mutation was added.

The full hosted `build.bat` build completed and produced
`guideXOSServer.exe`. The S6 model suite passed 42/42. Runtime checks enumerated
the current AppRegistry, launched Notepad by its registered ID through the
normal desktop launcher, and compared the hosted clock provider with
`std::chrono::system_clock`.

The hosted application was exercised through its CLI/compositor runtime, but
the Settings window was not visually inspected. The page renderer and input
translation unit passed direct syntax compilation; S6 model tests cover the
inventory, selection identity, routes, clock formatting, and refresh policy.

## Apps ownership and behavior

`DesktopService::s_appRegistry` is the authoritative App Model registry. It
combines the manifest sources with synthesized built-in metadata. On the
tested hosted runtime, initialization reported 14 manifest registrations and
33 total desktop registrations (19 built-in metadata entries plus the
manifest registrations after duplicate resolution).

Settings calls `DesktopService::GetSettingsAppModelInventory()` for a
value-owned snapshot; it does not maintain a separate app list. The page is
labelled **Registered apps**, since registry presence does not imply a
package-install lifecycle. Each row shows registry display name, source and
kind. The details view shows ID, type, registration source, launch identity,
and version only when the registry supplies a non-synthetic version. Built-in
synthetic versions are omitted. Running state, publisher, installation date,
package size, app data size, uninstall, repair, and reset are not represented
by the current App Model and are not shown.

The inventory has a fixed capacity of 64. It orders entries by
case-insensitive display name, then app ID, then temporary-registration
generation. Entries without an ID or display name are excluded. If the source
ever exceeds capacity, the page reports both the represented and total counts
and labels the inventory as truncated. The tested runtime's 33 registrations
fit without truncation.

An **Open** action appears only for built-ins with an existing normal
`DesktopService::LaunchApp` handler. Selecting it calls that launcher with the
registered App Model ID; Settings does not spawn apps through a second path.
Manifest/package registrations and built-ins without a normal hosted handler
remain visible but do not receive an Open action. Temporary development
registrations carry their registration generation, so a removed selection
cannot silently bind to a later registration reusing the same ID. The page
refreshes its snapshot every two seconds while visible and focused, and only
redraws when the inventory or selected-registration state changes.

## Date & Time ownership and behavior

Hosted wall-clock time comes from `std::chrono::system_clock`, converted to
calendar text by the shared `clock_time_settings.h` formatting functions.
Settings refreshes the displayed text once per second only while the Date &
Time page is visible, the window is focused, and global search is closed. The
timer updates the clock text region rather than repainting the full Settings
window.

The configured guideXOS display time zone and 12/24-hour preference are read
from the existing Display Options store, with the existing desktop config
fallback. Supported zones are Pacific, Mountain, Central, Eastern, and UTC.
The four U.S. zones use the existing hand-coded U.S. daylight-saving
transition rules. The UI names the selected zone, calculated UTC offset, and
standard/daylight state, and states that there is no IANA time-zone database.
Changing the zone remains owned by Display Options; the Settings action opens
that existing tool.

The hosted clock backend uses the host runtime wall clock. Settings does not
read the Windows host time-zone setting and does not claim that the hosted
clock proves bare-metal clock behavior. The kernel separately reads CMOS/RTC
time, but this branch has no Settings-facing kernel time snapshot, no
time-zone capability in the kernel, and no RTC or system-clock setter exposed
to Settings. The page therefore says that automatic time, manual time
changes, and RTC writes are unavailable. No NTP service or COM2 time mutation
was added; the COM2 system-service protocol is unchanged in S6.

## Routes, search, and interaction

The bounded Settings router accepts `settings://apps`,
`settings://apps/list`, `settings://date-time`,
`settings://date-time/time`, and `settings://date-time/timezone`. It rejects
arbitrary per-app URI targets. Global search includes Apps/application terms
and Date/Time, clock, time-zone, UTC, RTC, calendar, and automatic-time terms.

Apps uses the existing inventory viewport, mouse-wheel scrolling, arrow-key
navigation, visible-row hit testing, hover/focus styling, and compact layout.
Details preserve selection by ID plus registration generation and show a
removed-registration message if that identity disappears. Date & Time has no
mutating controls; its only action opens the existing time-zone settings.
No changes were made to S1–S5 category owners or the Device/Storage bridge.

## Verification

### S6 model/provider tests

`scripts/run-settings-s6-model-test.ps1`: **42/42 passed**.

- Apps inventory and selection: **18/18** (empty/single/multiple entries,
  kind/source/version, long identities, launch support, deterministic order,
  capacity/truncation, and stale-generation handling).
- Date, time, zone, refresh, and hosted provider: **17/17** (formatting,
  midnight/month/year/leap-day boundaries, UTC and U.S. DST offsets,
  unavailable/unknown settings, visibility/focus/search refresh policy,
  hosted wall-clock comparison, and advancing repeated reads).
- Routes and search: **7/7** (Apps and Date & Time routes, invalid arbitrary
  app route rejection, and global search coverage).

Direct MinGW syntax compilation passed for `settings_center.cpp` and
`desktop_service.cpp`; the compiler emitted only existing warnings from
`gui_protocol.h` and `desktop_config.h`.

### Runtime and regressions

- Hosted `build.bat` through `scripts/smoke-clock-time-settings.ps1`: **PASS**;
  the script's seven shared-clock/configuration source checks passed.
- Hosted runtime AppRegistry startup: manifest scan reported 14 registrations
  and AppRegistry reported 33 total desktop registrations. Runtime inventory
  output included built-in IDs such as `gxos.builtin.notepad`, plus the
  manifest/package registrations present in this checkout.
- Hosted normal-launch proof: `desktop.launch gxos.builtin.notepad` resolved
  the registered ID as a built-in, reported a successful launch, and created
  the Notepad window. Settings calls this same `DesktopService::LaunchApp`
  entry point for eligible rows.
- Startup App Model regression smoke: **PASS**; canonical Notepad, Calculator,
  Display Options, Settings, and Control Panel identities remained registered
  and explicitly launchable, with empty startup state preserved.
- User Background phase 1 smoke: **PASS**; manifest bounds, import/removal
  safety, parser/hash/PNG tests, shared File Explorer/Image Viewer dispatch,
  Display Options ownership, and compositor ID resolution passed.
- Display configuration control-plane and persistence source smokes: **PASS**.
- Targeted Disk Manager launch: **PASS** through the existing App Model
  dispatch. Startup regression also covered Settings, Control Panel, and
  Display Options launches.
- Previously run inherited suites remain at their full recorded counts:
  Settings Center model 89/89; Device model 21/21; Storage model 24/24;
  block-device generation 5/5; system-service bridge 78/78; Network Settings
  contract 45/45; and Network configuration transaction 21/21.

The separate phase 5B App Model closeout script did **not** pass its first
status assertion: the current server reports `appModelV1StatusReady=false`
and `overall: WARN`, with launch-storage preview reporting 12 unresolved and
12 high-risk cases. The script expects the App Model summary to be ready and
later hard-codes 18 built-ins, while the current registry metadata contains
19. The targeted runtime launch and startup regression passed. These existing
App Model diagnostics are outside S6; the S6 snapshot does not change the
launch-storage preview checks or the built-in registry count.

QEMU/kernel time proof and physical-hardware testing were not applicable to
this implementation: S6 adds no kernel time snapshot, time mutation, or
system-service request. No live Settings screenshot was captured.

## Known limits

- App inventory is a runtime registry view, not proof of package installation.
- App running state and package/data storage information are unavailable.
- Only existing normal hosted built-in launch handlers get an Open action.
- Time zone support is limited to five existing choices and hand-coded U.S.
  DST rules; there is no general time-zone database.
- Hosted `system_clock` does not establish the bare-metal kernel's RTC state.
- The existing phase 5B App Model status closeout remains not-ready for the
  unrelated launch-storage preview warnings and stale expected count noted
  above.
