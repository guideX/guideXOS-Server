# guideXOS Server — Phase S12 Acceptance

**Result: Outcome A — S12 accepted.** Start → Settings opens the native Settings Center, and Start → Control Panel continues to open the legacy Control Panel. Both paths were exercised in the final production AMD64 guest.

Starting HEAD: `ac40af27ffa566d075d370ee86c8323cd3fc4d45`

Branch: `main`

Build date: 2026-10-14

## Launch routing

Before S12, the visible Start-menu Settings label was classified as a shell action and deliberately redirected to the embedded Control Panel. The canonical `gxos.builtin.settings` identity could launch the native app separately. This made the menu label disagree with the native app identity.

S12 removes Settings from the shell-label resolver path. The built-in metadata now resolves the label to a `BuiltInApp` with app ID `gxos.builtin.settings` and dispatch name `Settings`. The Start-menu hit handler uses the existing typed launch path. A failed launch reports failure and does not fall back to Control Panel. Control Panel remains a `ShellAction` handled by the existing `activate_start_menu_control_panel` path; it is not registered as a second native Settings app.

The S12 native runtime suite verifies the distinct resolver targets and launch failure reporting. Final-guest serial output confirms the Settings menu selection reached `BuiltInApp`, `gxos.builtin.settings`, and the Settings dispatch.

## System and About data

The native Settings window continues to use the S10 presentation core, the production Settings adapter, runtime-owned provider snapshots, and `KernelCompositor` rendering and input. S12 advances the read-only System/About snapshot to version 2 and gives each field explicit availability flags and bounded storage.

| Displayed field | Production source | S12 result |
| --- | --- | --- |
| Product | `KERNEL_NAME` | `guideXOS` |
| OS release version | No authoritative OS release source exists in this build | Unavailable; About says “Not defined by this build” |
| Kernel version | `KERNEL_VERSION_STRING` in `kernel/core/include/kernel/version.h` | `0.1.0` |
| Architecture | `arch::get_arch_name()` | `AMD64 (x86-64)` in the AMD64 guest |

The existing `0.1.0` value is the kernel version because it comes from the kernel version header. It is not an OS release version. S12 does not infer a product release from the development plan or hard-code `0.3`. The separate build identity marker is documented in `kernel/core/include/kernel/build_identity.h` as a temporary file-paste verification marker, so it is not presented as release metadata. Physical memory and runtime-environment fields were also omitted because no authoritative source was established for this slice.

The provider copies values into caller-owned bounded buffers and exposes no kernel pointers. Product, kernel version, and architecture are required fields; OS release is optional and is absent with its availability flag clear. Oversized optional release text is rejected rather than truncated. AppManager-owned caller identity, snapshot revision checks, and the read-only contract remain in place; the S10 adapter mutation callback still rejects writes.

System shows Product, Kernel version, and Architecture. About shows Product, OS release version, Kernel version, and Architecture. The remaining Settings categories are intentionally deferred.

## Final production guest acceptance

QEMU booted the final kernel from an S12-owned writable ESP and a copied OVMF image. The S12 ESP was isolated from the S11R writable guest; only its kernel image was replaced. QEMU used a visible GTK window, machine `pc,accel=whpx`, 2 vCPUs, 1536 MiB, no network, and QMP on `127.0.0.1:58313`. The guest was shut down through its QMP session after acceptance.

| Guest step | Evidence and result |
| --- | --- |
| Start menu exposes both entries | [Start menu screenshot](evidence/settings-s12-20261014/start-menu-open.png) |
| Start → Settings opens native Settings | [System screenshot](evidence/settings-s12-20261014/settings-click-down-up.png); serial records the typed BuiltInApp route and Settings identity |
| Navigate to About using guest input | [About screenshot](evidence/settings-s12-20261014/settings-about.png); it shows the unavailable OS release explicitly |
| Close and reopen Settings | [Reopened System screenshot](evidence/settings-s12-20261014/settings-reopened.png); a new instance reports a ready provider snapshot at revision 1 |
| Start → Control Panel preserves the legacy surface | [Control Panel screenshot](evidence/settings-s12-20261014/control-panel-open.png) |
| Exercise the legacy Display action | [Display Options screenshot](evidence/settings-s12-20261014/control-panel-display-options.png); the final guest serial records the DisplayOptions built-in dispatch |
| Open Control Panel while Settings is running | [Settings above Control Panel](evidence/settings-s12-20261014/concurrent-settings-and-control-panel.png). Settings remains topmost; the legacy surface stays alive underneath |
| Close Settings while Control Panel remains open | [Control Panel revealed after Settings closes](evidence/settings-s12-20261014/concurrent-after-settings-close.png) |
| Close Control Panel independently | [Clean desktop after both close](evidence/settings-s12-20261014/desktop-clean-concurrent-close.png) |

The serial log records Settings focus acquisition, snapshot readiness, About input through the shared core, shutdown, focus loss, and the repeated Start → Settings launch. Reopening after close receives a fresh app instance and provider snapshot. The native runtime tests also verify that a duplicate launch focuses the existing instance without creating another, and that duplicate launch arguments cannot retarget its identity. The guest log confirms Settings acquired focus when launched.

The Control Panel overlap check passed: Start → Control Panel opened while Settings was running, the Settings window remained topmost, and closing Settings revealed the still-open Control Panel. Closing the Control Panel then returned to the desktop. Display Options was opened and closed separately.

## Regression tests

| Suite | Result |
| --- | ---: |
| S12 native runtime and provider suite, including identity, authorization, no-write behavior, launch routing, failure reporting, optional metadata, and 100 lifecycle repetitions | 564/564 |
| S10 shared Settings presentation core | 2,645/2,645 |
| Freestanding presentation-core link and hosted-symbol check | Passed; no hosted runtime symbols |
| Settings Center model | 89/89 |
| Inventory models: Device and Storage | 45/45 |
| Default Apps model and interactions | 48/48 |
| S6 model | 42/42 |
| S7 Accessibility, Developer, and navigation/search models | 58/58 |
| Users models | 32/32 |
| Network Settings contract | 45/45 |
| Hosted Settings/model checks | 359/359 |

The current S12 native-runtime result extends the S11 identity, authorization, provider, and lifecycle coverage. These are fresh S12 runs; historical S11 counts are not substituted for the results above.

## Production build and artifact identity

The full production AMD64 kernel build completed and linked successfully in the isolated directory `kernel/build/s12-20261014`. It ran 154 compile commands, reported 409 warning diagnostics and zero compiler errors. The warnings are existing unused-helper/unused-parameter diagnostics; none were reported at the changed provider or Settings rendering lines.

| Artifact | SHA-256 |
| --- | --- |
| Final S12 kernel, also copied into the isolated guest ESP | `7EFD368AE4B08E0B443ED8E8B52A35D2921957D1A2D2F0DBB292620A04C852BC` |
| Bootloader | `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA` |
| OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |

The kernel is 2,682,184 bytes. [The artifact manifest](evidence/settings-s12-20261014/artifact-manifest.json) identifies the built kernel, guest ESP copy, bootloader, firmware, isolated QEMU configuration, and evidence files. The committed [production build log](evidence/settings-s12-20261014/amd64-kernel-build.log), [guest serial log](evidence/settings-s12-20261014/guest-com1.serial.log), and [QMP transcript](evidence/settings-s12-20261014/qmp-transcript.log) retain the run evidence. For readable diffs, the committed serial copy drops terminal ANSI control sequences and normalizes line endings; the committed build log normalizes line endings and trims trailing line padding; the QMP transcript only normalizes line endings. The original raw logs remain under `out/validation/settings-s12-20261014`, and the artifact manifest records source and committed-copy hashes.

## Repairs, limitations, and next phase

S12 repaired the visible Start → Settings route so it now agrees with the native app identity. The provider expansion adds authoritative Product, kernel version, and architecture values while representing the missing OS release version explicitly. No fake version, Settings write operation, second Settings implementation, unrelated build-script change, or legacy Control Panel replacement was introduced.

The build still has no authoritative OS release version or release identity. Settings categories beyond System and About remain deferred. S12 does not change S9 or Settings 1.0 acceptance, and it does not publish or advance v0.3 readiness.

Recommended S13 scope: add one read-only Display category backed by a kernel-owned snapshot of current framebuffer dimensions and active display mode where those values are authoritative. Keep display changes in the legacy Control Panel until Settings has a separately reviewed transactional write contract; add focused provider, shared-core, hosted, and production-QEMU acceptance for that category.
