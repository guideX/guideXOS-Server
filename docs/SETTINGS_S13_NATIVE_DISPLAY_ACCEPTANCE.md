# guideXOS Server — Phase S13 Acceptance
## Native Display Settings and Authoritative Framebuffer Information

**Result: Outcome A — S13 accepted.** Native Settings now reports a trusted, read-only snapshot of the active framebuffer through the shared Settings presentation core. The legacy Control Panel and Display Options still launch and operate independently.

Starting HEAD: `de63af809e60504565db97b93998b4d30a0b6e2e`

Branch: `main`

Build date: 2026-10-14

## Display sources and field meaning

The active framebuffer API is authoritative for the framebuffer values. In the production AMD64 guest, kernel boot information selected the canonical UEFI GOP framebuffer from `BootInfo`; duplicate descriptor aliases were not treated as separate displays. The serial log records 1280×800 pixels, 5120 bytes per scanline, 32 bits per pixel, and `B8G8R8A8` at boot and during kernel framebuffer initialization.

The provider reads these fields from the kernel framebuffer API:

| Settings field | Authoritative source | Production guest result |
| --- | --- | --- |
| Framebuffer size | Active kernel framebuffer width and height | 1280 × 800 |
| Color depth | Active framebuffer bits per pixel | 32 BPP |
| Pixel format | Exact UEFI GOP format retained by framebuffer initialization | B8G8R8A8 |
| Framebuffer pitch | Active framebuffer bytes per scanline | 5120 bytes / line |
| Logical desktop | Read-only active `DisplayConfigurationService` snapshot | Unavailable |
| Display layout | Read-only active `DisplayConfigurationService` snapshot | Unavailable |
| Primary output mode | Read-only active `DisplayConfigurationService` snapshot | Unavailable |

Framebuffer dimensions describe the active render target. They do not describe physical-monitor maximum or native capabilities, refresh rate, or a list of supported modes. The production display-configuration query did not return an active topology in this QEMU guest, so the provider leaves logical desktop, layout, and primary mode unavailable. It does not infer them from the QEMU command line. The compositor's logical dimensions and individual application-window dimensions are separate concepts and are not substituted for framebuffer values.

## Provider contract and authorization

S13 extends the existing Settings provider rather than adding a second provider path. `DisplaySnapshotRequestV1` and `DisplaySnapshotV1` have explicit versions and structure sizes. The returned value-only structure uses availability flags, fixed-width units, and a bounded 32-byte primary-mode identifier. It exposes no framebuffer pointer, physical address, or writable mapping.

The provider reads framebuffer state through the kernel framebuffer API and asks `DisplayConfigurationService` only for `QueryActiveConfiguration`. Active configuration fields are accepted only after version, size, output-count, dimension, layout, and terminated-text validation. Invalid or unsupported optional values remain unavailable. Pitch is exposed only when it is at least the storage required for one scanline; padded scanlines are accepted. Supported pixel depths are 16, 24, and 32 BPP. Unknown pixel formats are represented as `Unknown` without discarding valid geometry.

Requests require the runtime-owned identity `gxos.builtin.settings`, a valid request version and size, caller-owned output storage of the required capacity, and non-null result metadata. Unauthorized callers, malformed requests, unsupported versions, undersized buffers, and stale nonzero revisions are rejected. A nonzero active display-configuration generation supplies the snapshot revision; when that service is unavailable the provider uses the documented fallback revision 1. The Settings app obtains a fresh snapshot on launch. S13 does not subscribe to display-change events or refresh an open window automatically; close and reopen Settings to sample the current state again.

The native Settings adapter continues to reject mutations. Tests also record that the Display provider submits no display-configuration mutation. No Settings resolution, wallpaper, or other display-write operation was added. The legacy Display Options app retains its own preexisting controls and persistence path.

## Native Settings integration

Display is a real category in the existing S10 presentation core and production adapter. The core owns selection for System, Display, and About. Keyboard shortcuts navigate between those categories, and mouse selection uses the same category state. The native renderer displays grouped framebuffer and active-mode values, uses explicit “Unavailable” values, and adapts to narrower window sizes without clipping. Display capability remains read-only.

Display navigation, repaint, focus, close, and reopen use the existing AppManager and KernelCompositor lifecycle. Closing clears both System/About and Display snapshots. Reopening creates a new `gxos.builtin.settings` instance and obtains a new ready snapshot. System and About remain available; unimplemented categories remain deferred.

## Production guest acceptance

The final kernel booted in an isolated visible QEMU guest with machine `pc,accel=whpx`, two vCPUs, 1536 MiB, `virtio-vga,id=video0`, and networking disabled. The run used a copied OVMF image and isolated writable ESP. Its QMP endpoint was `127.0.0.1:58413`; the owned QEMU process exited through that QMP session after acceptance. Other QEMU processes were left untouched.

| Guest step | Evidence |
| --- | --- |
| Start menu presents Settings and Control Panel | [Start menu](evidence/settings-s13-20261014/start-menu-open.png) |
| Settings Display selected with mouse and real values | [Display page](evidence/settings-s13-20261014/settings-display-mouse.png) |
| System → Display → About → Display navigation and repaint | [System](evidence/settings-s13-20261014/settings-system.png), [About](evidence/settings-s13-20261014/settings-about.png), [return to Display](evidence/settings-s13-20261014/settings-display-return.png) |
| Close and reopen Settings; Display snapshot remains valid | [Reopened Display page](evidence/settings-s13-20261014/settings-display-reopened.png) |
| Start → Control Panel opens the legacy panel | [Control Panel](evidence/settings-s13-20261014/control-panel-open.png) |
| Launch legacy Display Options and exercise existing tabs | [Display Options](evidence/settings-s13-20261014/display-options-open.png), [Backgrounds tab](evidence/settings-s13-20261014/display-options-backgrounds-tab.png), [Displays tab](evidence/settings-s13-20261014/display-options-displays-tab.png) |
| Reopen and close Control Panel independently; return to desktop | [Reopened Control Panel](evidence/settings-s13-20261014/control-panel-reopened.png), [clean desktop](evidence/settings-s13-20261014/desktop-clean.png) |

The serial log independently confirms the boot-time framebuffer geometry and format, Settings snapshot readiness, keyboard navigation through Display/System/About, mouse selection of Display, close/reopen, and the accepted Control Panel close shortcut. The Display Options Displays tab showed its existing backend-unavailable state. No mode was applied. Display Options closed through its cancel path; Control Panel was reopened separately and then closed with Alt+F4.

The configured QEMU PS/2 mouse accepts relative events, not absolute events. A direct absolute QMP event returned “Input handler not found for event type abs”; calibrated relative events then successfully selected the Settings Display category, the Control Panel, and Display Options tabs. Serial logs confirm those guest-side interactions, so no kernel input change was needed.

## Tests and regressions

| Suite | Result |
| --- | ---: |
| S13 native Settings runtime/provider suite, including 100 lifecycle repetitions and two Display reads per cycle | 796/796 passed |
| S10 shared Settings presentation core | 2,645/2,645 passed |
| Hosted Settings/model suites: Settings Center, Inventory, Default Apps, S6, S7, Users, Network | 359/359 passed (89, 45, 48, 42, 58, 32, 45) |
| App Model Phase 5B launch/routing regression | Passed; Control Panel and Display Options routes verified |
| Freestanding Settings presentation-core link and hosted-symbol check | Passed |
| `smoke-virtio-gpu-display-options.ps1` | Passed |
| `smoke-qemu-display-options-runtime.ps1 -SkipRuntime` | Passed |

The provider tests cover valid and unavailable framebuffer data, zero dimensions, unknown formats, unsupported depths, padded and too-short pitch, active-mode values, stale revisions, request/version/size failures, caller authorization, repeated refresh, and no mutation submissions. Shared-core tests cover System/Display/About transitions and read-only capability state. The 100-cycle lifecycle harness performs repeated Display snapshots across instance boundaries.

## Production build and artifacts

The normal AMD64 kernel build completed and linked successfully from `kernel/build/s13-final-20261014`. It ran 154 compiler commands, emitted 409 warnings, and emitted zero compiler errors. S12 also emitted 409 warnings on the comparable build. Review of the final log found no warning on an added S13 line. The final kernel and S12 kernel each have 19 unresolved symbols, with no symbol-set difference.

| Artifact | SHA-256 |
| --- | --- |
| S13 kernel, also copied into the guest ESP | `48D41C2BC86BCCDAF3821698E79E27EC13D42527A07F489A62F1B36AE6B8DC21` |
| Bootloader | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |
| OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |

The [artifact manifest](evidence/settings-s13-20261014/artifact-manifest.json) records source and committed evidence hashes, build counts, boot-artifact identity, QEMU settings, and screenshots. The committed [build log](evidence/settings-s13-20261014/amd64-kernel-build.log), [guest serial log](evidence/settings-s13-20261014/guest-com1.serial.log), [QMP transcript](evidence/settings-s13-20261014/qmp-transcript.log), and [QEMU launch manifest](evidence/settings-s13-20261014/qemu-launch.json) retain the run evidence. Serial ANSI controls were removed, line endings normalized, and trailing padding trimmed. Build-log line endings were normalized and trailing padding trimmed. The QMP transcript was normalized to LF line endings. Raw run artifacts remain under `out/validation/settings-s13-20261014`.

## Scope, limitations, and next phase

S13 adds read-only native Display information only. It does not change S9, Settings 1.0, or Server v0.3 readiness. No framebuffer initialization or allocation design, canonical `/desktop.cfg` write path, wallpaper/gradient persistence, display mode, or legacy Control Panel implementation was replaced.

Recommended S14 scope: add an established refresh notification when the active display-configuration generation changes, then expose logical desktop and output-mode identity only when a production backend supplies validated values. Keep any native display-write contract separate and reviewed before enabling changes in Settings.
