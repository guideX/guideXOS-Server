# Settings S9 acceptance recovery

Date: 2026-10-11

Result: **Outcome B — implementation advanced; full acceptance remains incomplete.**

## S9R continuation status (2026-10-11)

The subsequent native integration assessment records **Outcome C** for S9R:
the hosted Settings Center has no compatible production-kernel entry point or
provider/identity bridge, and integrating it would require a wider shared
runtime milestone. See
[`SETTINGS_S9R_NATIVE_INTEGRATION_ASSESSMENT_20261011.md`](SETTINGS_S9R_NATIVE_INTEGRATION_ASSESSMENT_20261011.md)
for the architecture, validation, and exact stop boundary. This does not
change the earlier hosted results or accept S9 / Settings 1.0.

The original recovery snapshot updates the acceptance status in
`SETTINGS_S9_1_0_ACCEPTANCE.md` as of this date. It does not accept S9 or start
S10; the later S9R investigation is recorded above and in the linked
assessment.

## Repository and phase state

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `c6354904ca2287af2c38de0bc0f7aba2063158e2`
- Starting tracked worktree: clean
- `.phase`: absent at start and remains absent
- Existing untracked build outputs were preserved and excluded from the commit.

## Defects fixed

1. On mouse resize commit, the compositor updated its window bounds but did not
   send the owning app an `MT_Resize`. Settings therefore kept drawing at its
   previous dimensions. The compositor now sends the final clamped dimensions
   after a real size change.
2. On app creation, the compositor restored saved bounds but acknowledged only
   the window ID and title. A reopened Settings instance could draw using its
   default dimensions inside the restored, smaller frame. The create path now
   sends the actual restored dimensions when they differ from the requested
   size. Settings redraws from those dimensions.

Both paths share the resize payload formatter in
`compositor_resize_notification.h`. The focused protocol test passes.

## Hosted acceptance

The supported isolated hosted build completed successfully with zero compiler
errors. The rebuilt executable is
`out\validation\settings-s9-recovery-20261011-a\hosted-runtime\guideXOSServer.exe`
(8,804,453 bytes). The compiler still reports existing warning-only
diagnostics in shared and unrelated code.

The normal desktop Start menu launched the canonical Settings App Model entry.
The hosted visual pass inspected all twelve pages: System, Display, Network &
Internet, Personalization, Devices, Storage, Apps, Users, Date & Time,
Accessibility, Developer, and About. The Default Apps chooser opened and
cancelled. Search for “network” returned network fields and navigated to the
Network page. User and network pages accurately show capability limits.

After the final rebuild, Settings reopened at its saved compact bounds. The
System page fit inside the restored frame, and Accessibility showed Enhanced
focus **On** after a full hosted server restart. The preference was stored as
`desktop.accessibility.enhancedFocusIndicator: true` in the isolated runtime's
`desktop.json`. This is hosted persistence evidence; guest persistence was not
verified.

### Existing hosted and integration suites

All recorded suites passed without reducing coverage (463 checks):

| Area | Checks |
|---|---:|
| Settings Center; S6 Apps/Date & Time | 89/89; 42/42 |
| S7 Accessibility; Developer; navigation/search | 21/21; 23/23; 14/14 |
| S8 Users; navigation; interaction/layout | 11/11; 8/8; 13/13 |
| Devices; Storage inventories | 21/21; 24/24 |
| Network Settings contract; transaction | 45/45; 21/21 |
| System-service bridge; block-device generation | 78/78; 5/5 |
| Default Apps model and interaction | 39/39; 9/9 |

The new resize payload test passes 1/1 (464 total checks including it).
Additional checks passed: 100 Default Apps lifecycle cycles, 7 clock/time
smoke checks, display control-plane and persistence smokes, user-background
Phase 1 smoke, and the existing startup/App Model and advanced-launch
regressions. The system-service bridge and network transaction tests include
unavailable-provider, invalid-input, authorization, and rollback coverage.

## QEMU acceptance

Three independent isolated ordinary boots completed. Each used a fresh ESP and
firmware copy, booted the same kernel image
(`SHA-256 1C0FA4325B1DA9D8220F08E16E404B70C6D8F6CA414ACD1E053D53747388D086`),
reached the kernel main loop, and passed the Navigator and system-service
bridge smoke checks. The production bridge client queried network, device, and
storage snapshots on all three boots. Its unauthenticated static-network
mutation returned `Unauthorized`, and a follow-up snapshot was unchanged.
Logs are under
`out\validation\settings-s9-recovery-20261011-a\qemu\boot1` through `boot3`.

A separate graphical QEMU boot reached the guest desktop using the repository's
`pc,usb=off` input setup. The available computer-use input did not produce a
confirmed guest Start-menu activation or Settings launch, so there is no guest
Settings page, interaction, close/reopen, or guest-persistence evidence. This
is the remaining required acceptance gap; hosted success is not counted as a
substitute. A QEMU process from the sibling
`guideXOSServer_DiskManagerImprovements` checkout was observed and left
untouched.

## Network status and safe operations

Settings reports the provider's adapter/link/IP/gateway/DNS snapshot and
distinguishes DHCP, static, unavailable, and unverified internet state. It
exposes no unauthenticated network Apply action. The existing taskbar network
widget draws provider-backed link/configuration bars; it does not claim
internet connectivity from adapter presence. No network mutation was applied
from the Settings UI. Transaction tests and the three live bridge probes
covered rejected writes and unchanged state.

## Server v0.3 integration readiness

| Capability | Status | Boundary |
|---|---|---|
| Hosted Settings App Model launch, close/reopen, saved window sizing | **Ready** | Normal launcher route, lifecycle regressions, and the fixed restored-size render passed. |
| System, About, Apps inventory/default handlers | **Limited/Alpha** | Inventory and chooser work; package install/uninstall and broader handler capabilities remain bounded. |
| Display and Personalization | **Limited/Alpha** | Existing control-plane/import transactions and persistence smokes pass; full guest visual/apply review remains open. |
| Network | **Limited/Alpha** | Truthful read-only state and safe rejection/rollback coverage; authenticated production configuration is unavailable. |
| Devices and Storage | **Limited/Alpha** | Bounded kernel snapshots; GPT enumeration and reliable volume capacity/free-space data are not provided. |
| Date & Time | **Limited/Alpha** | Hosted clock/time-zone display; NTP, manual clock setting, and RTC writes are unsupported. |
| Accessibility and Developer | **Limited/Alpha** | Enhanced focus persists; other accessibility features are unavailable. Developer diagnostics inherit the App Model Phase 5B not-ready state. |
| Users | **Blocked for account administration** | The page accurately explains that human accounts, authentication, profiles, permissions, and ownership are unsupported. |
| Full Settings S9 / Settings 1.0 acceptance | **Blocked pending guest evidence** | The required Settings interaction inside a QEMU guest has not been demonstrated. |

The active App Model Phase 5B diagnostic remains not-ready with 12 unresolved
and 12 high-risk preview records; its stale built-in-count fixture remains
independent integration work. Do not treat Settings diagnostics as a substitute
for that closeout.

## Remaining S9 work

Use a QEMU guest with working input to launch Settings from the desktop menu,
visit the pages, exercise representative supported controls and failure
states, then close and relaunch it. Verify only persistence with real durable
guest backing. Revisit S9 after that evidence; do not create a `.phase` marker,
advance to S10, or merge as part of this recovery.
