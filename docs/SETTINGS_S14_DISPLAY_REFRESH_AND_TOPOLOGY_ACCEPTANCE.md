# guideXOS Server — Phase S14
## Native Display Refresh, Snapshot Consistency, and Topology Qualification

Status: **S14R Outcome A — Native Display Refresh Accepted**. S14 originally exited with Outcome B because the complete ordinary QEMU guest sequence had not been recorded. S14R completed that sequence, reviewed the bounded refresh contract, reran the deterministic and regression suites, and verified the final production artifact identities. The historical S14 result is retained below; the S14R closeout records the completed acceptance.

## Repository preflight

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Original S14 pre-implementation base: `db05e3b6f94d2edaa402c1dd34daee3f68d517ed`. S14 implementation commit and S14R starting HEAD: `d7c0f83aceeca722a45b5ec84a9c74bad2f27b94` on `main`.
- Worktree: the tracked tree was clean. The following pre-existing untracked `out/` artifacts were present and are being preserved: `out/desktop_control_theme_test.exe`, `out/v03-amd64-qemu-candidate/`, `out/v03-amd64-scifi-final-build.log`, `out/v03-amd64-scifi-final-candidate/`, `out/v03-amd64-scifi-repro-build.log`, `out/v03-scifi-hosted-experimental.log`, and `out/v03-scifi-hosted-standard.log`.
- No S14 acceptance document or equivalent Settings refresh implementation was present in the starting tree or reachable commit history. The existing VirtIO GPU topology observer is a separate pending-topology diagnostic and does not notify native Settings.
- The S13 acceptance document and its artifact manifest record `de63af809e60504565db97b93998b4d30a0b6e2e` as their starting HEAD, while this repository's S13 accepted commit is `db05e3b6f94d2edaa402c1dd34daee3f68d517ed`. This report uses the verified repository HEAD above and treats the S13 test/build values as historical until rerun.

## Pre-implementation ownership findings

### Framebuffer source and update paths

The production framebuffer metadata is owned by `kernel::framebuffer` in `kernel/core/framebuffer.cpp`. On the accepted AMD64 UEFI path, `main.cpp` calls `framebuffer::init_from_bootinfo`; that function copies the selected BootInfo framebuffer base, dimensions, pitch, depth, and format into kernel-owned static state. Settings reads only the metadata getters. Its provider does not return the framebuffer address or a pointer.

The repository has other framebuffer initialization entry points, but the production call sites found during this investigation initialize at boot. No post-boot framebuffer mode setter, framebuffer state generation, or framebuffer-change notification exists. Reinitializing the framebuffer is not a production live-change operation and will not be used to manufacture a Settings event.

### Display configuration and compositor state

`gxos::display::DisplayConfigurationService` owns an active-configuration generation, initially 1. A validated successful `ApplyConfiguration` or successful pending-topology reconciliation advances it. Query responses return the active generation with the active configuration. The Settings provider currently derives its snapshot revision from this generation when a valid active configuration exists, but falls back to revision 1 when topology is unavailable; this loses generation information in the unavailable-topology case and will be corrected.

The kernel VirtIO GPU backend can produce detected and active output snapshots only when the explicit `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE` and `GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE` build gates are enabled. The ordinary production AMD64 build does not enable those gates, so `get_display_configuration_backend_snapshots` returns unavailable there. The QEMU-only display-event observer is also gated separately; it reports a bounded pending topology change and deliberately does not apply it automatically.

`KernelCompositor` owns its screen/work-area dimensions and has a topology reconciliation operation used after a successful active configuration apply. A compositor window resize is a change to one application window's bounds, not evidence of a desktop-resolution change. No existing compositor display-change callback reaches Settings.

### Native Settings, app dispatch, and execution context

The native `SettingsNativeApp` samples System/About and Display snapshots during initialization and refreshes Display when the user selects that category. It does not currently observe later state changes. The S10 `SettingsPresentationCore` owns category navigation, focus, and capability revisions; the Settings adapter and app-owned snapshot remain the presentation/runtime boundary.

The existing `KernelApp`/`AppManager` lifecycle and app event hooks provide the smallest integration surface. `DisplayConfigurationService` documents that its synchronous submit path is invoked on the desktop owner path. `desktop::tick()` runs `AppManager::update()` on that same path. This permits a source-change event to set a fixed-size pending marker on currently running apps; Settings can consume it in its normal update and read the provider there. No callback pointer needs to outlive a Settings instance, and no provider read needs to run in a device interrupt or compositor resize callback.

### Topology qualification at the starting revision

The ordinary production kernel's topology result is unavailable because the active configuration query depends on a gated-off QEMU VirtIO GPU compositor backend. It is not caused by a missing Settings presentation field or by using the window size as the desktop size. Consequently, the accepted production UI correctly leaves logical desktop size, active layout, and primary output mode unavailable. The compositor's boot dimensions do not independently qualify as a logical multi-output desktop topology.

## Selected integration surface

After recording the findings above, the implementation extended the existing app event dispatch with one display-configuration-changed event. It is emitted only after a successful active-configuration commit advances the authoritative service generation. `AppManager` fans out the generation synchronously to its bounded running-app table; the native Settings instance coalesces notifications into one pending generation marker and consumes it on its normal update path.

The Settings refresh path requests a fresh authorized provider snapshot, validates its structure/version/size/revision and field invariants, and commits it through a bounded snapshot-state helper. Duplicate snapshots do not repaint; older or internally inconsistent snapshots are rejected; unavailable or malformed reads present unavailable data rather than silently labeling old values current. The shared presentation core retains its navigation and focus state. When Display is hidden or the window is unfocused, a valid refresh updates cached state without invalidating the visible page; selecting Display or restoring focus causes the normal repaint.

The provider revision continues to identify the active-configuration generation, including a successful query that explicitly reports no active topology. Generation ordering uses a documented 32-bit serial-number rule, skips zero when the source counter wraps, and rejects ambiguous/backward resets within a live instance. A new Settings instance starts with a fresh provider snapshot and a fresh ordering baseline.

## Validation results

### Implementation and ownership

- Added one value-only display-change hook to the existing KernelApp/AppManager event path. AppManager visits its fixed running-app table synchronously; it stores neither subscriber pointers nor callbacks.
- DisplayConfigurationService increments a nonzero active-configuration generation and emits the hook only after a successful active apply or topology reconciliation. Preview, failed apply, dismissal, window resize, and the QEMU-only pending-topology observer do not emit this event.
- SettingsNativeApp coalesces events into one generation marker and consumes it during its normal desktop-owner update. It reads the existing authorized Display provider through the presentation adapter, validates the whole snapshot, and updates the S10 capability state without resetting navigation or focus. A visible focused Display page is invalidated only when accepted values change or become unavailable; hidden/unfocused pages retain the cache until the normal page repaint.
- A closing Settings instance clears its snapshot and pending signal. The app receives notifications only while it remains in the running-app table, and its handler checks running/window state. The provider callback runs after DisplayConfigurationService commits the response and releases its busy state.
- The provider revision encodes the nonzero 32-bit active-configuration generation plus one. Revision 1 is reserved for the zero generation; the 64-bit value can represent generation UINT32_MAX as revision 2^32. A half-range serial-number comparison accepts forward wrap and rejects duplicates with different data, old generations, and ambiguous/backward resets. A failed provider read clears values but retains the revision watermark; recovery at that same generation is accepted without labeling old data current.
- Provider authorization remains bound to the runtime-owned identity gxos.builtin.settings. Existing request version, structure size, capacity, field invariants, and no-mutation checks remain enforced. The shared presentation core receives only a validated capability snapshot and never reads framebuffer memory or hardware registers.

### Focused and regression tests

| Suite | Result |
| --- | ---: |
| Native Settings provider/runtime, including 100 create/close/reopen cycles, 250 deterministic refresh cycles, and 1,000 bounded notification/state transitions | 828/828 passed |
| S10 Settings presentation core, including Display revision wrap and navigation/focus preservation | 2,647/2,647 passed |
| Hosted Settings/model regressions: Settings Center | 89/89 passed |
| Hosted Settings/model regressions: Inventory and Storage | 45/45 passed |
| Hosted Settings/model regressions: Default Apps | 48/48 passed |
| Hosted Settings/model regressions: S6 | 42/42 passed |
| Hosted Settings/model regressions: S7 | 58/58 passed |
| Hosted Settings/model regressions: Users | 32/32 passed |
| Hosted Settings/model regressions: Network | 45/45 passed |
| Hosted Settings/model regressions total | 359/359 passed |
| App Model identity | 25/25 passed |
| App Model application actions | 55/55 passed |
| App Model URI protocol | 41/41 checks passed; persistence, activation, and no-handler cycle groups each 100/100 |
| App Model folder activation | 26/26 passed |
| App Model file activation | 60/60 passed |
| Compositor pointer-button regression | Passed |
| Compositor resize notification direct compile/run | Passed |
| Display/framebuffer, configuration, VirtIO GPU source/runtime smoke checks | Passed |
| Freestanding display-refresh helper compile/link and unresolved-reference checks | Passed |

The focused test runner output and compact command/count index are in [test-results.md](evidence/settings-s14-20261015/test-results.md). Test-only source-state injection remains isolated to the native runtime harness.

### Production build and artifacts

The clean AMD64 production build used a new output tree, kernel/build/s14-clean-final-20261015, and completed with 155 compile commands, one link, zero compiler/linker errors, and 233 warning lines. The S13 historical baseline was 154 compile commands, zero errors, and 409 warning lines. The S14 implementation files produced no warning diagnostics; removing unused names from existing no-op KernelApp event parameters eliminated repeated header warning lines. No warnings were globally suppressed.

The final kernel ELF is 2,694,528 bytes with SHA-256 465F7C86957B4E41E1FC8A3392177875167F9850C048348F9BB03BEADB842102. The PE image SHA-256 is A429049F16ECAA5E70739C0F5416954E11BAAA7D599D4078EBC33CB92CECCFA5.

The ELF undefined-name inventory contains 20 unique names versus 19 in S13 because the added KernelApp::onDisplayConfigurationChanged method appears in the reference inventory. That method is also defined in the final image. The two names absent from the defined-symbol inventory, __dso_handle and mainCRTStartup, are the same two present at S13; the link succeeds and there is no new externally unresolved symbol. The complete clean build log is committed under the evidence directory.

Boot artifacts used by the accepted guest run:

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | 465F7C86957B4E41E1FC8A3392177875167F9850C048348F9BB03BEADB842102 |
| UEFI bootloader | E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA |
| OVMF firmware | 2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792 |

### QEMU guest acceptance

The final S14 kernel booted from an isolated writable ESP copy with the matching OVMF copy in QEMU using machine pc,accel=whpx, two vCPUs, 1536 MiB, virtio-vga,id=video0, GTK display, UTC RTC, and networking disabled. The QMP endpoint was 127.0.0.1:58416. The guest reached the desktop, produced serial heartbeats, opened native Settings, and selected Display. The captured [Display page](evidence/settings-s14-20261015/guest/settings-display.png) matches the serial framebuffer values: 1280 x 800, 32 BPP, B8G8R8A8, and 5120 bytes per line. Logical desktop, layout, and primary output mode remain explicitly Unavailable.

The saved [QEMU launch manifest](evidence/settings-s14-20261015/guest/qemu-launch.json), [QMP transcript](evidence/settings-s14-20261015/guest/qmp-transcript.log), [serial log](evidence/settings-s14-20261015/guest/guest-com1.serial.log), [initial desktop](evidence/settings-s14-20261015/guest/desktop-initial.png), [Settings launch page](evidence/settings-s14-20261015/guest/settings-launch.png), and [kernel identity](evidence/settings-s14-20261015/guest/ESP-build-identity.txt) bind that run to the production artifacts above. The owned QEMU process was shut down through its QMP endpoint. A preexisting foreign QEMU process observed during that run was left untouched. QEMU emitted the same benign “Ignoring request for interrupt vector 0” warning recorded in the S13 run.

No legitimate live production topology-change path exists in the ordinary AMD64 guest: framebuffer metadata is initialized from UEFI GOP at boot, and live VirtIO GPU topology queries are behind explicit QEMU-only build gates. The S14 deterministic runtime fixture therefore drives a genuine service-generation notification through the production provider adapter and S10 presentation path. It does not claim an in-guest live mode transition.

At the original S14 exit, the S14 QEMU evidence established boot, Start-to-Settings navigation, and the real Display snapshot, but System/About navigation, close/reopen, and Start-to-Control Panel/Display Options had not been completed. The prior S13 accepted QEMU evidence and S14 executable regressions provided context but did not close that guest gap. S14R completes and records the missing ordinary guest sequence below; the historical S14 classification remains Outcome B.

### Topology qualification, readiness, and next phase

The ordinary production backend still does not provide a logical desktop topology or primary output mode. S14 fixes the unavailable-topology revision fallback so a valid BackendUnavailable response retains the service generation, but it does not invent outputs or infer logical dimensions from the framebuffer or window sizes. No mode switching, polling, or recurring fake production event was added.

S9, Settings 1.0, and Server v0.3 readiness were not advanced. Recommended S15 scope: implement or enable a production-owned topology query and notification at a genuine backend commit boundary, then repeat the full ordinary QEMU Settings and legacy Control Panel sequence with source-matched evidence. Keep mode changes out of scope unless separately authorized.

### Repository and commit status

- Original S14 pre-implementation base: `db05e3b6f94d2edaa402c1dd34daee3f68d517ed`. S14 implementation and S14R starting HEAD: `d7c0f83aceeca722a45b5ec84a9c74bad2f27b94` on `main`.
- The S13 acceptance-document manifest's startingHead mismatch is retained as a historical note above; S14 used the verified repository HEAD.
- Preexisting untracked v0.3 candidate artifacts and the S13 evidence/output directories were preserved.
- The original S14 report and evidence were committed locally on `main`; this S14R closeout and distinct evidence directory are committed locally on `main` as documentation-only acceptance work.

## S14R closeout — complete guest acceptance and contract review

### Starting state and acceptance gap matrix

S14R started on `main` at `d7c0f83aceeca722a45b5ec84a9c74bad2f27b94`, the S14 implementation commit. The tracked worktree was clean. The preexisting untracked `out/` artifacts listed in Repository preflight were preserved. No accepted S14R closeout was present. No source implementation change was needed.

| Requirement | Evidence before S14R | Gap at original S14 exit | S14R action and result |
| --- | --- | --- | --- |
| Authoritative display generation | DisplayConfigurationService owns active configuration generation; successful apply/reconciliation advances it | No live ordinary-guest transition was available to observe | Inspected service and event boundary; deterministic provider-backed cycles exercised the changed-state path |
| Change notification | Bounded value-only KernelApp/AppManager display-change event | End-to-end behavior had only deterministic coverage, not the full ordinary UI flow | Confirmed event is emitted only after successful authoritative commit; reran 1,000 bounded notification/state transitions |
| Snapshot revision | Provider revision maps nonzero source generation to a 64-bit revision (`generation + 1`; revision 1 represents source generation zero) | Prior S14 QEMU showed only initial revision | Verified wrap/order handling in the presentation tests and fresh Settings launch/reopen snapshots at revision 1 |
| Coalescing and bounded delivery | One pending generation marker per Settings instance; fixed running-app table | No guest changed-mode event was possible | Reran 250 deterministic provider refresh cycles and 1,000 transitions; duplicate, stale, and late notification/lifecycle cases remain bounded |
| Snapshot validation | Whole-snapshot structure/version/size/revision and field-invariant validation | Guest did not exercise refresh navigation and reopen | Reran native runtime and presentation suites; verified only validated complete snapshots are committed |
| Repaint and presentation state | Shared presentation core retains category/focus; repaint is requested only for accepted visible Display changes or normal page/focus restoration | No full guest category/focus sequence | Ordinary guest completed Display → System → About → Display with mouse and keyboard input and preserved navigation/focus |
| Hidden page and lifecycle | Hidden/unfocused refresh updates cached state; close clears pending state; new instance reads provider anew | No S14 guest close/reopen evidence | Fresh close/reopen succeeded; second instance reported `snapshot=ready revision=1`; 100 lifecycle repetitions passed |
| Topology availability | Ordinary production backend query was already classified unavailable when gated backend support is absent | Need to confirm UI does not fabricate missing topology | Confirmed unavailable fields in Settings and legacy Display Options; no inference or mode change was made |
| Ordinary production UI | S14 guest had booted and opened Display | System/About, reopen, Control Panel and Display Options were missing | Completed the full requested ordinary QEMU sequence and captured the screenshots, serial log, QMP transcript, and artifact manifest below |

### Refresh architecture and correctness review

The active display configuration remains owned by `gxos::display::DisplayConfigurationService`. A successful `ApplyConfiguration` or successful pending-topology reconciliation advances its nonzero 32-bit generation. The notification is published only after that commit. Preview, failed apply, dismissal, compositor window resize, and the diagnostic pending-topology observer do not publish a change. `AppManager` synchronously fans the value-only event across its fixed running-app table; it retains no subscriber pointers, callback registrations, heap queue, or unbounded event list.

`SettingsNativeApp` coalesces incoming generations into one pending marker and consumes it during its normal desktop-owner update. It requests the existing authorized provider snapshot, validates the complete response, then commits it through the S10 presentation state. Provider revisions encode source generation plus one, reserving revision 1 for source generation zero and allowing `UINT32_MAX` to map to `2^32`. The serial-number ordering rule accepts forward wrap, rejects duplicate revisions with different data, stale generations, and ambiguous/backward resets, and permits recovery at the same revision after an unavailable read without relabeling old values current.

The shared presentation core retains category and focus during refresh. A changed accepted snapshot invalidates the visible, focused Display page; hidden or unfocused pages update their cache and repaint on ordinary page selection or focus restoration. Closing Settings clears its cached snapshot and pending notification marker. A new instance takes a fresh provider snapshot. The provider remains authorized for `gxos.builtin.settings`; snapshot validation does not broaden mutation rights or read framebuffer memory/hardware registers from the presentation core.

The fresh deterministic runtime evidence covers the end-to-end notification → provider snapshot retrieval → validated shared presentation update → repaint-request path, including changed source state, duplicate/stale/late notifications, close/reopen safety, and navigation/focus preservation. It passed with 828/828 native runtime checks, 100 lifecycle repetitions, 250 refresh cycles, and 1,000 bounded transitions. The shared presentation suite passed 2,647/2,647. These are controlled harness transitions through the production provider seam, not a fabricated production guest mode change.

### Ordinary QEMU guest acceptance

The final S14 production kernel was booted from an isolated writable ESP copy with a matching isolated OVMF copy. The launch used `pc,accel=whpx`, two vCPUs, 1536 MiB, `virtio-vga,id=video0`, GTK display, UTC RTC, and networking disabled. QMP was bound to `127.0.0.1:58426`. The guest booted normally to the desktop.

The actual Settings sequence passed: Start → Settings; mouse selection of Display; keyboard navigation to System and About; return to Display; close; then Start → Settings again. System showed product `guideXOS`, kernel `0.1.0`, architecture `AMD64 (x86-64)`. About showed the same product/kernel/architecture and stated the OS release version is not defined. The reopened Settings instance started on System with `snapshot=ready revision=1` and rendered the current System snapshot.

Settings Display and the canonical COM1 boot evidence agree on width 1280, height 800, 32 BPP, B8G8R8A8, and pitch 5120 bytes per line. Logical desktop dimensions, active layout, primary output, stable output identity, and output mode remain unavailable. Settings did not infer them from framebuffer or window dimensions.

Start → Control Panel opened the legacy embedded Control Panel. The launch diagnostic records `target=ControlPanel` as `blocked-unknown-fallback`; the existing desktop fallback still opened the embedded panel, and no routing change was made. Its Display tile launched Display Options through the existing typed dispatch. Backgrounds and Displays tabs were exercised; the Displays tab truthfully reported unavailable topology and `Display query failed`. No wallpaper or display mode was applied. The Control Panel is a desktop modal/overlay, so this was verified sequentially with Settings; in the overlapping attempt, closing the Control Panel modal left Settings active (captured in [the post-modal Settings screenshot](evidence/settings-s14r-20261015/guest/settings-after-control-panel-closed.png)), and Settings was then closed separately. The Control Panel was also opened by itself and captured, confirming it remained independently usable. Display Options closed cleanly.

After all surfaces were closed, the guest remained at the clean desktop without stale windows, crash, or hang. A second isolated boot used independent writable ESP and OVMF copies with the same verified hashes; it reached the desktop, showed the normal “System started successfully” notification, and reached the kernel main loop. The second boot logs and screenshot are in `evidence/settings-s14r-20261015/guest/repro-boot/`. Both task-owned QEMU processes were shut down through their own QMP endpoints. The foreign QEMU process observed during the run was left untouched. QMP pointer calibration used visible guest screenshots and small relative events; an initial host pointer-capture click was not counted as a guest action or production defect.

Fresh guest evidence is in [the S14R guest directory](evidence/settings-s14r-20261015/guest/): [Display](evidence/settings-s14r-20261015/guest/settings-display.png), [System](evidence/settings-s14r-20261015/guest/settings-system.png), [About](evidence/settings-s14r-20261015/guest/settings-about.png), [reopened Settings](evidence/settings-s14r-20261015/guest/settings-reopened-system.png), [legacy Control Panel](evidence/settings-s14r-20261015/guest/control-panel.png), [Display Options Backgrounds](evidence/settings-s14r-20261015/guest/display-options-backgrounds.png), [Display Options Displays](evidence/settings-s14r-20261015/guest/display-options-displays.png), and [final desktop](evidence/settings-s14r-20261015/guest/desktop-final.png). The same directory contains the full [serial log](evidence/settings-s14r-20261015/guest/guest-com1.serial.log), [QMP transcript](evidence/settings-s14r-20261015/guest/qmp-transcript.log), [launch manifest](evidence/settings-s14r-20261015/guest/qemu-launch.json), [ESP manifest](evidence/settings-s14r-20261015/guest/ESP-file-manifest.sha256), and [build identity](evidence/settings-s14r-20261015/guest/ESP-build-identity.txt), all covered by [the evidence SHA-256 manifest](evidence/settings-s14r-20261015/evidence-manifest.sha256).

### Artifact identity, regressions, repairs, and limitations

The isolated guest media matched the final S14 artifacts:

| Artifact | SHA-256 |
| --- | --- |
| Kernel ELF | `465F7C86957B4E41E1FC8A3392177875167F9850C048348F9BB03BEADB842102` |
| Kernel PE | `A429049F16ECAA5E70739C0F5416954E11BAAA7D599D4078EBC33CB92CECCFA5` |
| UEFI bootloader | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |
| OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |

The original S14 clean AMD64 build remains the final production build: 155 compile commands, one link, zero compiler/linker errors, and 233 warning lines. No production source changed during S14R, so no rebuild was necessary; the isolated guest image hashes match the committed S14 artifact records. The ELF undefined-symbol inventory has 20 unique names; the added display callback is defined in the final image, and the two names absent from the defined-symbol inventory match the S13 baseline. The full build log remains under the S14 evidence directory.

Fresh S14R regression results are indexed in [test-results.md](evidence/settings-s14r-20261015/test-results.md), with raw runner output under its `tests/` subdirectory. In addition to the native runtime and presentation suites above, hosted Settings/model regressions passed 359/359; App Model identity 25/25; application actions 55/55; URI protocol 41/41 plus 100/100 persistence, activation, and no-handler cycles; folders 26/26; and files 60/60. Compositor, display/provider, and freestanding checks remained passing. No defect was discovered or repaired.

There is no legitimate live display-mode transition in the ordinary production guest. The UEFI framebuffer is initialized at boot, while production topology snapshots and live VirtIO GPU notification are behind explicit QEMU-only build gates. S14R therefore does not claim an observed live production generation change, changed framebuffer values, or mode transition. It verifies the bounded source notification seam and controlled deterministic provider transitions instead. Stable output identifiers, primary output, active layout, logical desktop size, and output mode remain unavailable.

### Final S14 acceptance classification

**S14 — Outcome A: Native Display Refresh Accepted**

The S14 implementation meets its bounded notification, revision, snapshot, repaint, and lifecycle contract under deterministic tests; ordinary production guest navigation and the complete legacy Display Options sequence passed; topology availability is accurately qualified; artifact identity is verified; and all required fresh regression suites passed. The only remaining limitation is the documented absence of a genuine production live-mode/topology change path in this build. S9, Settings 1.0, and Server v0.3 readiness were not advanced. S15 was not started. Recommended S15 scope remains a production-owned topology query and genuine commit-boundary notification followed by a new ordinary guest acceptance; display-mode management is out of scope absent separate authorization.
