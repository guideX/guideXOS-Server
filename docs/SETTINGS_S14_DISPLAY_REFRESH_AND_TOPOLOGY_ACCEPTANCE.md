# guideXOS Server — Phase S14
## Native Display Refresh, Snapshot Consistency, and Topology Qualification

Status: **Outcome B — partial progress**. The bounded refresh implementation, focused and regression tests, clean production build, and S14 QEMU Display-page acceptance passed. The full requested S14 guest sequence through About, close/reopen, and legacy Display Options was not completed in this phase; the missing guest evidence is recorded below.

## Repository preflight

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `db05e3b6f94d2edaa402c1dd34daee3f68d517ed` (`origin/main` at inspection)
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

The S14 QEMU evidence establishes boot, Start-to-Settings navigation, and the real Display snapshot. The requested follow-on guest checks for System/About navigation, close/reopen, and Start-to-Control Panel/Display Options were not completed in this phase. The prior S13 accepted QEMU evidence documents those legacy paths, while S14 lifecycle, navigation, provider, App Model routing, and compositor regressions passed in executable tests. These historical S13 guest results are not presented as new S14 guest observations. This remaining guest sequence is why the result is Outcome B rather than Outcome A.

### Topology qualification, readiness, and next phase

The ordinary production backend still does not provide a logical desktop topology or primary output mode. S14 fixes the unavailable-topology revision fallback so a valid BackendUnavailable response retains the service generation, but it does not invent outputs or infer logical dimensions from the framebuffer or window sizes. No mode switching, polling, or recurring fake production event was added.

S9, Settings 1.0, and Server v0.3 readiness were not advanced. Recommended S15 scope: implement or enable a production-owned topology query and notification at a genuine backend commit boundary, then repeat the full ordinary QEMU Settings and legacy Control Panel sequence with source-matched evidence. Keep mode changes out of scope unless separately authorized.

### Repository and commit status

- Starting branch and HEAD: main at db05e3b6f94d2edaa402c1dd34daee3f68d517ed.
- The S13 acceptance-document manifest's startingHead mismatch is retained as a historical note above; S14 used the verified repository HEAD.
- Preexisting untracked v0.3 candidate artifacts and the S13 evidence/output directories were preserved.
- This acceptance report and its evidence are included in the local S14 commit on main.
