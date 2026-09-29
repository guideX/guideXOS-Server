# Settings S7: Accessibility and Developer

## Accessibility audit

The hosted guideXOS UI does not currently have authoritative global owners for
UI/text scale, a high-contrast palette, pointer size/visibility, reduced motion,
screen-reader speech, magnification, color filters, or keyboard input timing.
The desktop themes are Classic and SciFi; neither is a high-contrast mode.
Keyboard focus is available, and `FocusIndicator` is a shared renderer used by
desktop icons and Start-menu rows. Other applications still have their own
focus drawing paths, so the S7 preference is documented as applying to the
shared renderer and Settings controls rather than every application control.

The built-in on-screen keyboard is registered as
`gxos.builtin.onscreenkeyboard` and is exposed as a normal App Model launch
action when present.

## Implemented accessibility behavior

Accessibility exposes one writable preference: **Enhanced focus indicator**.
It defaults to off and is stored as the strict JSON boolean
`desktop.accessibility.enhancedFocusIndicator` in `desktop.json`, alongside the
other desktop configuration. Malformed values fall back to off. Settings writes
the desktop configuration owner, applies the value to the shared
`FocusIndicator` renderer, requests a compositor config reload, then rereads the
persisted owner before updating its displayed state. Failed writes, application,
or rereads restore the previous value where possible.

When enabled, the shared renderer draws a solid white outer ring and a black
inner ring around focused controls. Settings uses the same preference for its
own focused rows and buttons. The setting applies immediately and survives
closing and reopening Settings. It does not change the cursor, UI scale, or
application-specific focus renderers.

The Accessibility page also offers the registered on-screen keyboard. The
page names unsupported settings as unavailable and does not present them as
switches. In particular, global text/UI size, high contrast, pointer controls,
reduced motion, screen reader/speech, magnification, color filtering, and input
timing remain deferred because there is no suitable existing global owner and
renderer path.

## Developer tools and diagnostics

Developer Studio (`com.guidexos.developerstudio`) and Console
(`gxos.builtin.console`) are shown from the live AppRegistry snapshot. Their
actions call the ordinary `DesktopService::LaunchApp(appId)` route and are
disabled when the registered entry does not advertise open support. Settings
does not invoke executable paths directly.

The App Model page reports the registry total, bounded Settings snapshot count
and capacity, truncation state, Native ELF registration count, and the live
Studio/Console registration status. It explicitly does not infer launch
readiness from registration.

The Services page makes one read-only Network, Device, and Storage query when
opened; Refresh repeats those bounded reads. Availability reflects the actual
client response. COM2 peer authentication is reported unavailable because the
TCP peer is unauthenticated, and network configuration mutation is reported as
rejected by the service bridge. There are no privileged mutation controls.

The Diagnostics page reads the live App Model V1 summary and the storage launch
preview diagnostic. The preview is labeled nonfatal/read-only and exposes its
total, unresolved, and high-risk counts. It does not initiate preview writes.
Build identity, detected architecture, and runtime/backend are also shown.

The inherited App Model Phase 5B closeout state remains `not-ready`, with 12
unresolved and 12 high-risk launch-storage preview cases. A later stale
expected-built-in-count assertion expects 18 while the current AppRegistry
contains 19. S7 does not alter those assertions or attempt Phase 5B remediation.

## Navigation and compact layout

Implemented deep links include:

- `settings://accessibility` and `settings://accessibility/focus`
- `settings://accessibility/keyboard`
- `settings://developer`
- `settings://developer/apps`
- `settings://developer/services`
- `settings://developer/diagnostics`

Search terms map to implemented Accessibility actions and Developer pages.
Both pages use the Settings row viewport, wheel scrolling, keyboard focus
visibility adjustment, and visible-row-only hit testing. Focus moves to the
selected category when a wheel scroll would hide the focused row.

## Test results

- S7 Accessibility model: 21/21; Developer model: 23/23; navigation/search: 14/14.
- Settings Center model: 89/89; S6 model/provider/routes/search: 42/42.
- Device and Storage inventory: 21/21 and 24/24.
- System-service bridge: 78/78, including Device 13/13 and Storage 12/12.
- Network Settings contract: 45/45; Network transaction: 21/21.
- Block-device generation: 5/5.
- Display control-plane, display persistence, synthetic display, and wallpaper/
  background smokes passed.
- Startup App Model regression passed, including canonical launch checks for
  Control Panel and Display Options. Disk Manager, Display Options, and Control
  Panel also opened through their App Model IDs in the hosted runtime.
- The existing Phase 5B closeout smoke remains red at its `ready=true` assertion;
  see the inherited condition below.

## Runtime evidence and limitations

The normal `build.bat` hosted build passed. It emitted existing warnings about
misleading indentation, an unused `trim` lambda, and an unused local in Notepad.

At 640 × 480, the hosted Settings app was searched by keyboard for “enhanced
focus.” The preference changed from `false` to `true` in
`desktop.accessibility.enhancedFocusIndicator`; closing and reopening Settings
retained `true`. The runtime harness restored the user's original
`desktop.json`, `desktop.state`, and `display-options.cfg` bytes afterward.
Settings and the Developer page reported the shared renderer as **Enhanced**.

With the preference enabled, a hosted compositor capture shows the actual Start
menu keyboard selection surrounded by the shared renderer's white outer and
black inner rings:
`out/validation/settings-s7-enhanced-startmenu-focus.png`. This exercises the
same `FocusIndicator` path used by selected desktop icons and Start-menu rows.
The hosted Settings capture `out/validation/settings-s7-accessibility-enhanced.png`
shows the preference as **On**. The captures are ignored validation artifacts,
not repository deliverables.

The Settings page was exercised by keyboard search and activation, category
navigation, wheel scrolling, and visible-row hit testing at 640 × 480. The
Developer root, Services, App Model, and Diagnostics pages were opened; Console
was launched through the Settings action and appeared as
`gxos.builtin.console`. Separate normal App Model launch checks opened
`gxos.builtin.diskmanager`, `gxos.builtin.displayoptions`, and
`gxos.builtin.controlpanel`. Developer Studio is registered as
`com.guidexos.developerstudio`, but its live AppRegistry entry sets
`openSupported=false`; Settings accurately displays **Open unavailable** and
does not try to launch it.

The live Settings runtime reported 33 registered apps, a complete 33-entry
Settings snapshot with capacity 64, and 12 Native ELF registrations. The
Services page reported 0 of 3 available (Network, Device, Storage). It reports
COM2 peer authentication unavailable because the peer is unauthenticated, and
network configuration mutation rejected by the service bridge. The diagnostic
page reported App Model V1 `not-ready` and a read-only storage preview with 94
records, 12 unresolved, and 12 high-risk cases (`writesStorage=false`).

The inherited Phase 5B closeout smoke still fails at its existing assertion
that requires `appModelV1StatusReady=true`; live diagnostics say
`appModelV1Status=false` and `not-ready`. The later stale fixture expects 18
built-in registrations while the live registry has 19. S7 leaves both
assertions and the unresolved/high-risk cases intact.

QEMU was not run: this preference is consumed by the hosted Windows compositor,
and no S7 kernel UI path was changed. Physical hardware was not used.
