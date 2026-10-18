# guideXOS Server — S19 Native Settings UI Polish Acceptance

**Current S19 status — Outcome B (Partial).** S19R completed production AMD64 guest review at the normal Settings size, exercised the information and Personalization pages, verified Cancel and Apply persistence, and checked the legacy Control Panel. It also repaired a reproduced stale applied marker in legacy Display Options. S19R2 visibly reconfirmed the legacy applied marker and native Settings reopening after legacy closure. Minimum-size production guest qualification remains unverified; the current status and evidence are recorded in the S19R2 closeout below.

The earlier S19 implementation report is retained as historical evidence. Its initial Outcome B was due to missing production guest review; S19R supplied that review. S19R2 verified the post-legacy Settings reopen, while the current Outcome B reflects the unqualified minimum-size resize behavior.

## Historical S19 implementation report

Sections below preserve the original S19 implementation report and its evidence as written. Read the S19R closeout at the end of this document for the current acceptance status.

## Repository and starting state

- Repository: `D:\dev\guideXOSServer`
- Branch: `main` (unchanged)
- Starting HEAD: `5099c8a8a81e3592730c0155ef248c5b3349779a`
- Starting commit matched the reported S18 checkpoint; no intervening commits were present and no S19 acceptance report existed.
- The starting worktree already contained 4 modified tracked files and 586 untracked paths. It included an uncommitted Settings layout implementation/test, S16R/S17 evidence, and existing output/release candidates. Those pre-existing files and artifacts were preserved; only the S19 files listed below are intended for this phase commit.
- Existing S19-named build/test output directories were inspected and left untouched. New validation output used distinct paths.

## Initial UI audit

The S15–S18 guest screenshots show the production 1280×800 desktop and a Settings window around 760 pixels wide. The earlier pages are functional and provider-backed, but the bitmap text is compact. The System page repeats “Settings” and “System” across the app/sidebar/page/card headings, while the About page mixes release and kernel information in a dense list. Display places framebuffer and topology values in one column. Personalization combines the active background, palette, icon state, tabs, presets, and actions in a dense text-first card, leaving substantial unused space and making current and pending choices harder to scan.

| UI area | Existing behavior | Readability/usability issue | Bounded improvement | Regression risk | Acceptance evidence |
| --- | --- | --- | --- | --- | --- |
| Window and hierarchy | Fixed bitmap-rendered Settings window; repeated Settings/System labels; 12 category identities in the shared core | Duplicate headings use limited vertical space; deferred categories are not clearly distinct | Separate PAGES navigation, page title/subtitle, and card title; retain all category IDs and visibly mute deferred entries | Changed hit geometry or accidentally activating a deferred category | Layout bounds test, all category IDs, guest screenshots and keyboard/mouse navigation |
| System and About | Provider values appear in compact fixed rows; OS release is explicitly unavailable | Low contrast and weak distinction between labels and values; unused card area | Stronger value text, row spacing/dividers, bounded wrapping; keep OS release separate from kernel version | Provider values or availability semantics could be misrepresented | Native provider tests, System/About screenshots, exact “Not defined by this build” reporting |
| Display | Framebuffer geometry and optional topology share one list | Active framebuffer data and unavailable topology are easy to conflate | Group authoritative framebuffer values separately from optional topology; label unavailable values explicitly | Could imply monitor modes or topology that the provider does not report | Provider regression, framebuffer values in the guest, Display screenshot |
| Personalization | Active appearance, color data, icon state, and controls share one compact page | Dense current-state text; controls and action states are harder to scan; pending state can be confused with active state | Reserve a current-state block; organize gradients, wallpapers, and icons into labeled sections; distinguish selected, pending, and active states | Changed geometry could alter clicks or Apply/Cancel behavior | S15–S18 mutation regressions, control bounds/focus tests, pending/apply/cancel screenshots |
| Hover and focus | Existing selected category and Personalization keyboard controls work | Pointer hover and keyboard focus are not consistently visible across controls | Add consistent hover and focus outlines without changing shared category semantics | Focus may be lost or restored to the wrong control | Mouse-to-keyboard transition, Tab/arrow/Enter/Space, lifecycle and guest evidence |
| Window size | Pages use fixed coordinates; compositor had a global minimum resize bound but no per-window maximum | Small sizes could clip fixed controls; oversized Settings has no useful page bound | Centralize page/control geometry, wrap provider values, enforce Settings minimum and maximum resize dimensions | Resize constraints must not change other applications | 1,000 deterministic size transitions plus minimum/normal-size guest review |

The centralized layout and minimum/normal/maximum window constraints are covered by hosted geometry tests. Production guest review at normal, minimum, or smaller supported sizes remains outstanding, so no guest clipping or readability qualification is claimed.

Screenshots reviewed: [S15 gradient Personalization](evidence/settings-s15-20261016/guest/gradient/personalization-gradient.png), [S15 wallpaper Personalization](evidence/settings-s15-20261016/guest/image-wallpaper/personalization-image-wallpaper.png), [S14R System](evidence/settings-s14r-20261015/guest/settings-system.png), [S14R About](evidence/settings-s14r-20261015/guest/settings-about.png), [S14R Display](evidence/settings-s14r-20261015/guest/settings-display.png), [S17 wallpaper choices](evidence/settings-s17-20261017/personalization-wallpapers-initial.png), and [S18 icon controls](evidence/settings-s18-icon-20261017/settings-icons-before.png). Historical images are audit references, not S19 acceptance evidence.

## Presentation changes

The implementation keeps the production bitmap font and existing `drawChar`/`appDrawText` path. Strong text, provider value wrapping, glyph metrics, page geometry, and Personalization control rectangles use bounded helpers. It adds no TrueType dependency, generic GUI toolkit, or font renderer.

- Replaces repeated page chrome with a PAGES sidebar, a clear page title/subtitle, and one card heading. All 12 shared category identities remain in order; only System, Display, Personalization, and About remain available.
- Draws selected, hover, unavailable, and focused states distinctly. Personalization tabs, gradient/wallpaper choices, icon controls, and Apply/Cancel share the layout geometry used by hit testing.
- Separates System and About labels from brighter values, adds row spacing and dividers, and wraps bounded values. About continues to distinguish OS release from kernel version and preserves the provider-backed unavailable release text.
- Groups Display into active framebuffer data and optional display topology. Dimensions, bit depth, pixel format, and scanline pitch are read from the existing provider; missing topology remains “Unavailable.”
- Organizes Personalization into active appearance, built-in gradient/wallpaper choices, desktop icon visibility, Apply/Cancel, and status feedback. Active state remains distinct from pending selection. The eight built-in gradients, wallpaper catalog/pagination, three supported icon visibility controls, and session-only auto-arrange notice remain intact.
- Settings now uses minimum 660×440, preferred 760×560, and maximum 1180×820 window constraints. Resize bounds are applied by the existing compositor and account for the desktop work area.
- No provider, configuration serialization, persistence, authorization, revision, Apply, Cancel, wallpaper, gradient, or icon mutation contract was changed.

## Layout and navigation verification

The deterministic native layout suite passed **1,037/1,037** checks, including 1,000 bounded resize-geometry transitions. It verifies the 12 category targets, available/deferred categories, minimum and normal dimensions, containment, hit-test boundaries, and bounded text wrapping. These are hosted geometry checks, not guest screenshot review. The separate freestanding validation compiled and linked the shared presentation core, native layout helper, and Display refresh helper with production AMD64 freestanding flags; symbol checks found no unresolved or hosted-runtime references.

The native runtime suite passed **1,414/1,414** checks with 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions. Shared presentation core passed **2,650/2,650**; Settings Center passed **89/89**. Compositor pointer-button, VFS hosted read (**4/4**), App Model identity (**25/25**), and file activation (**60/60**) regressions passed.

Personalization retains forward Tab cycling, arrow-key preset/icon navigation, Enter/Space activation, Escape close behavior, focus acquisition/restoration, category shortcuts, mouse selection, and hover updates. The current `KernelApp::onKeyDown(uint32_t key)` interface does not carry modifier state, so Shift+Tab cannot be distinguished from Tab in native Settings; reverse traversal is not claimed. This is a bounded existing input-contract limitation, not a new S19 behavior.

## Production build

Command:

```powershell
mingw32-make -B -C kernel ARCH=amd64 BUILD_DIR=build/s19-native-ui-polish-20261017-final2 -j4
```

The ordinary AMD64 production build passed with **257 compile commands, 0 compiler errors, and 463 warning diagnostic lines**. The S18 reference log had 232 warning lines. Comparing normalized file-plus-warning-text signatures found 224 in each build with zero additions or removals; none are attributable to S19 layout, renderer, or resize changes. The S19 and S18 final ELFs each have 21 undefined-symbol names, with no set difference. The S19 PE import, bound-import, and delay-import directories are all zero-sized. The new layout helper passed a separate freestanding compile/link check.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Kernel ELF | 2,760,592 | `5145DA9BD3053CA88CE3A8A7B63E221E67419FC7FF4F9A8A6E903B8627F53082` |
| Kernel PE | 2,880,556 | `F9741C2F2BF90D0C3448C922516AAA7763CE8329820FA4F28835EFA39033079C` |

The final isolated test-media staging copied that ELF into a new 134,249,984-byte raw image. Raw-image SHA-256 is `2D0D9C2A326551B7055ECA4D8BAEFD70E584AA9B16BD20565E7A406D2C57FB63`; copied OVMF SHA-256 is `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`. The staged kernel ELF SHA matches the final build ELF above. `/desktop.cfg` remained the accepted 237-byte fixture with SHA-256 `B3098D72195A16ED553B12EFF7DB9092A056222783A448ABDBBC092448D1273F`. The staged image was not booted.

## QEMU guest acceptance

The passive QEMU checks found the existing foreign guest from `C:\Users\guideX\.codex\visualizations\…\font11r` (VNC `127.0.0.1:14`, QMP `58203`), a guest from the RX12 native guest checkout, and guests from `guideXOSServerV1.1_DOTNET_SUPPORT`. Their process IDs/owners and command lines were inspected to identify the separate media. All guests were left untouched. Rechecks were passive; no QMP, VNC, socket connection, or termination was attempted. The bounded availability window ended with QEMU still occupied.

S19 validation media was staged as a new isolated raw FAT32 image outside existing release/evidence paths. The source accepted S18 image was not modified; the original 237-byte `/desktop.cfg` fixture was byte-identical during staging. This staging check is not a guest run and is not evidence that S19 UI interactions avoid writes.

No S19 guest screenshot, input exercise, provider persistence check, or legacy Control Panel guest check is claimed. Outstanding evidence is the complete guest sequence in the user brief: boot the final production artifact; review System, About, Display, and complete Personalization at normal and supported minimum sizes; inspect gradient, wallpaper, and icon controls; verify keyboard focus/activation and mouse selection; observe pending/active state; confirm Cancel leaves `/desktop.cfg` unchanged; apply one safe supported setting and verify provider/desktop agreement; revisit and reopen Settings; inspect Control Panel → Display Options; then capture and review a stable desktop and the required page/control screenshots. The production kernel was built, but the isolated staged image was not booted.

## Known limitations and deferred font work

- Display topology remains unavailable when the production backend supplies no authoritative data.
- OS release remains unavailable and is not inferred from the kernel version.
- Auto-arrange remains session-only; in-place FAT writes retain their existing non-crash-atomic contract.
- Text uses the production bitmap font. Future antialiased TrueType integration must retain a configurable bitmap fallback and land through a reviewed renderer interface; S19 does not depend on the separate font-renderer work.
- Shift+Tab is not exposed to `KernelApp` because the current key callback carries no modifier flags.
- The new native page layout has not received final production-guest visual or interaction acceptance; this is the reason S19 remains Outcome B.

## Recommended S20 scope

Add modifier-aware focus traversal across the category list and all currently supported page controls, including reverse Tab, tabs, wallpaper pagination, and action buttons. Preserve the shared S10 category/navigation model and bitmap fallback. Review the separate TrueType renderer's measurement/drawing interface independently before any integration; add minimum-size and focus-contrast guest acceptance for the resulting path.

## S19R closeout — 2026-10-17

### Previous Outcome B and repository state

The previous S19 report had no production guest screenshots because QEMU was occupied during its passive availability checks. S19R booted the candidate in private isolated QEMU instances while leaving the other guests untouched.

- Repository: `D:\dev\guideXOSServer`; branch: `main`; starting HEAD: `57fabe72a32441cfaff6a2ab9b1b5bfcd276948c`.
- The tracked worktree was clean at start. The 582 pre-existing untracked paths were preserved. Work stayed on `main`; no branch, worktree, sibling checkout, or remote operation was used.
- The original S19 raw candidate remained byte-identical at SHA-256 `2D0D9C2A326551B7055ECA4D8BAEFD70E584AA9B16BD20565E7A406D2C57FB63`.
- A private rebuilt test image was made from the candidate with the S19R kernel ELF replacing `/kernel.elf`. It was `3FA1B971CAD4CDD65E73B459625333F14DD2C28222806DE892D540E9FE197D6A` before guest writes and `A89FA4465FD1DF84BC6BC5961D32268855BEB30D37D587F9A808E392B4D99C34` after acceptance. The rebuilt ELF is 2,760,592 bytes with SHA-256 `1D2C119CB947B170B0529D9444F7B8F8BF7FE0904E6EC42CE4916165257FAE3F`.
- The original staged ELF was `5145DA9BD3053CA88CE3A8A7B63E221E67419FC7FF4F9A8A6E903B8627F53082`; the recorded PE identity discrepancy remains documented above. The EFI loader SHA-256 is `E2FDB6AD78E0416AF250BEBBC6541640E3CCDC7DD9A14453C08143B7F4DA48AA`; read-only OVMF SHA-256 is `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.
- Every guest session used QEMU TCG, `-nic none`, private writable raw media, read-only firmware, and the task's own QMP endpoint `127.0.0.1:62455`. The S19 and rebuilt guest processes were closed through that endpoint. Foreign QEMU processes, including PIDs 27876 and 34608, were not contacted or stopped.

Artifact identities, launch parameters, QMP activity, serial output, and configuration snapshots are in [the S19R evidence directory](evidence/settings-s19r-native-ui-20261017/). The original S19 candidate is preserved separately from both writable test copies.

### Production guest visual review

The guest booted to a stable 1280×800 desktop. Start launched the native app as `gxos.builtin.settings`. At the normal 760×560 window size, the System, About, Display, and Personalization pages rendered with visible boundaries, readable labels, distinct current and pending appearance states, and no observed essential-value clipping or overlap.

- System showed guideXOS, kernel version 0.1.0, and AMD64 (x86-64).
- About kept OS release separate from kernel version and displayed “Not defined by this build.”
- Display showed 1280×800, 32 bpp, B8G8R8A8, and 5120-byte pitch. Display topology remained unavailable.
- Personalization exposed all eight gradient swatches, all four wallpaper pages (16 choices), the supported desktop icon controls, the session-only auto-arrange notice, status feedback, and Apply/Cancel. Focus and pending selection were visible in the captured states.
- Keyboard checks covered category shortcuts and arrow navigation, wallpaper page navigation, icon toggling, Tab focus, Space/Enter activation, and Escape close. Mouse selection and hover were also exercised. Shift+Tab remains unsupported because `KernelApp::onKeyDown(uint32_t key)` has no modifier state.

The screenshot set includes System, About, Display, Personalization, gradients, wallpaper pages 1–4, icon controls, pending selections, Cancel and Apply focus/results, Control Panel, Display Options, and the final stable desktop. The rebuilt evidence includes [Settings System](evidence/settings-s19r-native-ui-20261017/rebuilt-settings-initial.png), [Apply result](evidence/settings-s19r-native-ui-20261017/rebuilt-apply-result.png), [legacy Gradients](evidence/settings-s19r-native-ui-20261017/rebuilt-legacy-display-options.png), [legacy Backgrounds after repair](evidence/settings-s19r-native-ui-20261017/rebuilt-legacy-backgrounds-after-fix.png), and [stable desktop after Escape](evidence/settings-s19r-native-ui-20261017/rebuilt-after-legacy-escape.png). The complete QMP and serial logs are retained alongside them.

### Resize qualification

The implemented limits are minimum 660×440, preferred 760×560, and maximum 1180×820. The guest reported resize support. The Settings window opened at the expected normal 760×560 size, but repeated QMP mouse drags at the visible lower-right resize grip did not change its geometry. The minimum-size page review therefore was not completed, and the attempted drag is not evidence that the compositor rejects resizing. The historical deterministic native layout suite passed 1,037/1,037 checks, including bounded size transitions; that hosted result does not qualify the minimum-size guest rendering.

This is the remaining acceptance gate and keeps S19 at Outcome B. Minimum-size guest behavior, control reachability, and hit regions still need a successful interactive resize review.

### Cancel, Apply, and persistence

Before the first guest run, `/desktop.cfg` was 237 bytes with SHA-256 `B3098D72195A16ED553B12EFF7DB9092A056222783A448ABDBBC092448D1273F`. Selecting Ocean and cancelling left the saved file byte-for-byte identical; the before and after-cancel evidence files match. The rebuilt run also logged `gradient.pending cancelled` with no `/desktop.cfg` write before the subsequent Apply.

Applying Ocean in the rebuilt guest produced a successful `/desktop.cfg` write and `[settings-native] background.apply status=0 active=gradient_ocean persisted=gradient_ocean`. The readback file is 234 bytes with SHA-256 `CF310FEA3047F36350F99DB5E2FF458A20E85E6AAE9449F320BFE57F14967249`. Comparing it with the initial fixture shows only `wallpaperId` changed to `gradient_ocean`; scale mode, theme, taskbar position, icon visibility, and other fields remained unchanged. In the first guest run, cold relaunch and reopening native Settings showed Ocean still active.

### Legacy Display Options defect and repair

With Ocean active, the legacy Display Options Backgrounds tab initially drew an applied star beside Blue Flower. The selected outline could remain on Blue Flower, but the applied marker was wrong for the active gradient. The defect and its reproduction were captured in the initial [Backgrounds screenshot](evidence/settings-s19r-native-ui-20261017/legacy-display-options-backgrounds.png).

The bounded repair compares each legacy gallery option ID with the current desktop appearance ID when drawing the applied marker. It changes no selection, persistence, or Apply behavior. Three focused assertions cover missing IDs and active gradient/wallpaper cases. The repaired kernel was rebuilt and booted in a fresh private image; the rebuilt guest showed Ocean starred under Gradients and no applied star under Backgrounds. The post-fix screenshots and serial evidence are retained in the S19R evidence directory.

After the legacy surfaces closed, the guest returned to the desktop. A subsequent Start-menu attempt did not confirm a native Settings relaunch, so the post-legacy return step remains unverified. The first guest had separately confirmed cold relaunch and reopening Settings before the legacy regression check.

### Regression and build results

- Newly run affected native runtime suite: **1,417/1,417 passed**. The previous S19 count of 1,414/1,414 and all other counts in the historical section were not rerun and remain historical evidence.
- The ordinary AMD64 kernel build completed successfully in the isolated output directory `out/validation/settings-s19r-native-ui-20261017/kernel-build`. The rebuilt ELF was staged byte-identically in the private test image. The original candidate was not overwritten.
- The final rebuilt guest repeated Cancel, Apply, and the repaired legacy marker review. No full test matrix was rerun because the source change was limited to the legacy applied marker.

### Current S19 outcome and follow-up

**S19 — Outcome B: Partial.** Actual production guest review and the persistence/legacy checks are complete at normal size, and the reproduced legacy marker defect is repaired. Minimum-size guest resize qualification and the post-legacy Settings relaunch remain unverified. The existing Shift+Tab input limitation and unavailable display topology are recorded above. Do not advance S9, Settings 1.0, or Server v0.3 readiness based on this acceptance run.

S20 should address modifier-aware focus traversal and complete minimum-size production guest qualification after a working resize interaction is established. Preserve the current category/navigation model and bitmap fallback; review any separate font-renderer integration independently.

S19R was committed locally on `main`. The final commit SHA is reported in the task closeout. No push was performed.

## S19R2 closeout — 2026-10-17

### Starting state and scope

S19R2 began at **Outcome B** with two recorded gaps: minimum-size guest rendering and a post-legacy Settings reopen. The repository was `D:\dev\guideXOSServer`, branch `main`, starting HEAD `88a94eb8cba415d69a835eab28cd1a6b681eae95`. The tracked worktree was clean at preflight. The existing untracked evidence, build outputs, and release candidates were inventoried and preserved. Work stayed on `main`; no branch, worktree, reset, clean, stash, fetch, pull, or push was used. No S20 work was started.

### Resize capability and minimum-size result

Settings **does support interactive resizing** in the production source. `SettingsNativeApp` registers `WF_RESIZABLE` and supplies minimum 660×440, preferred 760×560, and maximum 1180×820 in `kernel/core/kernel_apps.cpp` (lines 16625–16640 and 16895–16904); the minimum constants are in `settings_native_layout.h` (lines 13–18). The compositor recognizes the lower-right resize corner when the pointer is within its 8-pixel `RESIZE_MARGIN`, starts a resize drag, clamps width and height, and stores the new geometry in `kernel/core/kernel_compositor.cpp` (lines 540–545, 555–587, and 661–668). `drawWindow` passes the current client width and height into the app draw callback (lines 865–895), and Settings records those dimensions before presenting its layout (`kernel/core/kernel_apps.cpp`, lines 16764–16770). This establishes a real supported feature; it is not classified as fixed-size or non-applicable.

The production guest showed the ordinary 760×560 Settings window on a 1280×800 desktop. QMP/PS/2 and visible GTK/tablet attempts at the lower-right grip did not produce a new geometry. The screenshots `settings-resize-default.png` and `settings-resize-unqualified-attempt.png` show the default bounds and an attempt that remained at that size. The input path did not provide reliable proof that the intended compositor hit region received a complete drag, so this does not establish a compositor defect or rejection of resize. No code repair was justified. Minimum-size guest rendering, reachability, wrapping, clipping, and hit targets therefore remain unqualified.

The historical hosted layout suite result of 1,037/1,037 includes bounded geometry transitions, but it does not substitute for the requested production guest review at 660×440.

### Guest visual and lifecycle results

In the ordered private AMD64 guest run, native Settings opened to System and showed guideXOS, kernel version 0.1.0, and AMD64 (x86-64). Personalization showed `Active background: Gradient / Ocean`. Settings closed with Escape and the desktop returned cleanly. The right-column **Control Panel** menu item opened the legacy Control Panel; its Display icon opened Display Options. The separate left-column `ControlPanel` program shortcut produced a “Not available in bare-metal mode” notice and was not used for the legacy surface.

Display Options showed Ocean with the applied star on Gradients. Backgrounds showed no applied star beside Blue Flower, and the Desktop Icons tab opened and displayed its current controls. Escape closed Display Options and returned to a clean desktop. These S19R2 screenshots are in [the S19R2 evidence directory](evidence/settings-s19r2-native-ui-20261017/README.md). The existing S19R post-fix Backgrounds screenshot remains preserved in the earlier S19R evidence directory.

The initial Settings-to-legacy portion and the post-legacy reopen were captured in separate task-owned boots of the same private writable image, kernel, and firmware. In the second run, after legacy Control Panel and Display Options were closed, native Settings reopened visibly to System, then Personalization showed the same Ocean state; closing Settings returned to a stable desktop. I do not represent the separately captured images as one uninterrupted QEMU process. In the ordered run, pointer input stopped responding after the clean desktop following legacy closure; the second run directly demonstrated the requested post-legacy reopen and clean close.

At the supported normal size, no essential text clipping or overlap was observed in the reviewed System and Personalization states. The final reopen confirms the native application identity in serial output (`gxos.builtin.settings`, snapshot ready) as well as the visible System provider values. Keyboard `P` navigation and Escape close worked; mouse clicks opened the legacy and native surfaces and selected Backgrounds and Desktop Icons. Shift+Tab remains unsupported because the existing `KernelApp::onKeyDown(uint32_t key)` callback has no modifier state.

### Applied marker, configuration, and artifacts

The authoritative guest `/desktop.cfg` was 234 bytes with SHA-256 `CF310FEA3047F36350F99DB5E2FF458A20E85E6AAE9449F320BFE57F14967249` before and after S19R2. It still identified `gradient_ocean`; the new UI review performed no Settings mutation. The before/after byte snapshots and their identity are preserved in the S19R2 evidence directory.

The private guest kernel was 2,760,592 bytes with SHA-256 `1D2C119CB947B170B0529D9444F7B8F8BF7FE0904E6EC42CE4916165257FAE3F`. Read-only OVMF was 3,653,632 bytes with SHA-256 `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`. The private writable raw image was 134,249,984 bytes; its post-run whole-media SHA-256 was `F2367870BE3A86EB290DCBE2A529AFFAF31E58A868A1D91C69347EDEE5BBF926`. The embedded `/desktop.cfg` stayed byte-identical; whole-media changes do not indicate a configuration mutation. The original S19R media and foreign QEMU processes were left untouched. Task-owned guests were stopped through their QMP endpoints.

QMP transcripts, serial logs, screenshots, runtime identity, and per-file SHA-256 values are retained in `docs/evidence/settings-s19r2-native-ui-20261017/`. No production source changed, so no production build or new tests were run. The S19R native runtime 1,417/1,417 and S19 layout 1,037/1,037 remain historical validation results, not newly executed S19R2 results.

### Final outcome and follow-up

**S19 — Outcome B: Partial.** The legacy applied marker is correct, and the native Settings reopen after legacy closure is visually demonstrated. The supported production resize feature still lacks a verified minimum-size guest pass, so S19 cannot be accepted as Outcome A. No source defect was demonstrated or repaired in S19R2.

S20 should retain the existing modifier-aware focus traversal work and continue minimum-size production guest qualification once a reliable pointer/drag route is available. Keep any font-renderer integration separate. Do not advance S9, Settings 1.0, or Server v0.3 readiness from this closeout.
