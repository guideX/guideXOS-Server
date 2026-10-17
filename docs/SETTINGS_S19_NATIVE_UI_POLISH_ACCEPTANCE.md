# guideXOS Server — S19 Native Settings UI Polish Acceptance

**Outcome B — Partial.** The bounded implementation, hosted regressions, freestanding proof, and production build are complete. Mandatory final production QEMU guest acceptance could not be performed because three QEMU guests owned by other checkouts were active during passive rechecks. None was contacted, altered, or stopped. No S19 guest screenshot or visual pass is claimed.

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
