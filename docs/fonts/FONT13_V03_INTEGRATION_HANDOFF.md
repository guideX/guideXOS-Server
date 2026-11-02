# FONT13 — v0.3 Integration Handoff

- **Phase:** integration preparation only
- **Branch snapshot:** `FONT_SUPPORT` at `89a58151c19da3d6532d838ae3a29a2c0ab4711a`
- **Recommendation:** keep bitmap as the clean-install graphical default for v0.3; package a validated, license-cleared TrueType set before advertising user selection.
- **Current outcome:** **B — Partial.** FONT13R2 reverified the approved v2.138 package and independent FAT image staging. The normal top-level build still stops at the unrelated PacMan audio link prerequisite, and real guest Settings acceptance remains input-blocked. The minimum-size Settings acceptance remains open.

### FONT13R continuation status — 2026-10-18

The historical FONT13 baseline below is preserved. The continuation resolves provenance and implements bounded v2.138 packaging; see [FONT13R_VERIFIED_FONT_PACKAGING.md](FONT13R_VERIFIED_FONT_PACKAGING.md) for the full evidence and manifest. Current outcome remains **B — Partial** because the top-level normal build stopped at the PacMan link step and real guest Settings enumeration/Apply/restart/fallback could not be qualified through the unresolved pointer-delivery limitation. The bitmap default remains unchanged, and `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open.

### FONT13R2 closeout status — 2026-10-18

The closeout confirms the three approved TTF hashes, Apache 2.0 license, Google attribution, pinned upstream archive, reproducible subset outputs, and exact six-file FAT `/fonts/` readback. The same 64 MiB runtime image hash is present in the fresh private ESP image. The package safety runner passed 21/21 checks.

The full top-level build blocker is now isolated to source registration in the PacMan Native ELF packaging script: current PacMan source calls the two audio functions, and `pacman_audio.cpp` defines them, but the six-object link list omits that source. A fresh-source link reproduction and the pre-FONT13 Phase 13 build record are indexed in [FONT13R2_PACMAN_BUILD_HANDOFF.md](FONT13R2_PACMAN_BUILD_HANDOFF.md). No PacMan source or package builder was changed here.

The existing fresh clean-image boot reached the desktop with no font preferences. The guest confirmed PS/2 keyboard and mouse support; USB HID was reported unavailable. Super opened the Start Menu, but no keyboard path selected its Settings entry, and the pointer attempts did not launch Settings. The Modern Input INPUT3D handoff documents that xHCI HID report delivery is not implemented. No further guest interaction was attempted after confirming the unsupported route. Settings enumeration, preview, Apply, shell change, restart restore, Restore Defaults, guest missing-font fallback, and the 560×360 TTF minimum remain unqualified.

See [FONT13R2_PACKAGING_CLOSEOUT.md](FONT13R2_PACKAGING_CLOSEOUT.md) for the complete evidence, retained regression results, acceptance matrix, and integration decision. Outcome remains **B — Partial**; no phase advance, commit, merge, push, or FONT14 work is authorized by this handoff.

This document prepares an owner-executed integration. It does not merge branches, qualify all release gates, or declare guideXOS Server v0.3 released.

## 1. Repository and phase state

- Starting branch and ending branch: `FONT_SUPPORT`.
- Starting HEAD and ending HEAD: `89a58151c19da3d6532d838ae3a29a2c0ab4711a`.
- The starting HEAD matches the requested commit. `origin/FONT_SUPPORT` points to this same commit in the local tracking refs.
- No branch, checkout, worktree, or remote state was changed. Nothing was pushed.
- The existing tracked worktree changes remain untouched: the FONT12 qualification document, native filesystem test runner, and ABI layout test. Their contents correspond to the prior FONT12R qualification corrections and are recorded in the existing FONT12R evidence. They were not committed as FONT13 work.
- Existing untracked evidence and all existing `out/` artifacts were preserved. New validation output was written only below `out/font13-validation/`; this directory did not exist at the start of this phase.
- Existing stray root entries `-OutputDirectory/` and `System.Collections.Hashtable` were preserved and are not FONT13 deliverables.
- The branch commit history from `origin/main` is the FONT1–FONT12 series. Several shared Settings, desktop, application, compositor, and boot files are integration touchpoints. They are not all standalone font modules. Existing bitmap fonts and the FONT5 demo are separately identified below.

No production kernel source was changed for FONT13. The only new tracked deliverable is this handoff. Since Outcome A was not reached, no commit was created; the legitimate FONT12R work remains uncommitted.

## 2. Feature inventory and source manifest

### 2.1 Production font subsystem

| Area | Required source and headers | Role |
|---|---|---|
| Shared contract and bitmap backend | `kernel/core/font_contract.cpp`; `kernel/core/font_contract_internal.h`; `kernel/core/include/kernel/font_contract.h`; `kernel/core/font_bitmap_backend.cpp` | Bounded face, glyph, pixel, and renderer contract; built-in bitmap implementation used as the fallback. |
| TrueType validation and outline decoding | `kernel/core/font_truetype.cpp`; `kernel/core/font_truetype_loader.cpp`; `kernel/core/font_truetype_internal.h`; `kernel/core/include/kernel/font_truetype.h` | Bounded SFNT/TrueType table checks, metadata extraction, glyph lookup, and outline decoding. |
| Grayscale rasterization | `kernel/core/font_truetype_raster.cpp` | Bounded grayscale glyph rasterizer; no shaping, kerning, CFF, variable, or color-font support. |
| Glyph cache and text layout | `kernel/core/font_glyph_cache.cpp`; `kernel/core/font_glyph_cache_internal.h`; `kernel/core/font_text_layout.cpp`; `kernel/core/include/kernel/font_text_layout.h` | Fixed-capacity glyph cache and shared measurement/layout for supported text paths. |
| Discovery, selection, persistence | `kernel/core/system_font_selection.cpp`; `kernel/core/system_font_selection_internal.h`; `kernel/core/include/kernel/system_font_selection.h`; `kernel/core/include/kernel/system_font_selection_types.h` | `/system/fonts/` discovery, face catalog, bounded source loading, active selection, Settings operations, and FONT9 preference storage. |
| Native Settings Fonts page | `kernel/core/system_font_settings_ui.cpp`; `kernel/core/include/kernel/system_font_settings_ui.h` | Native Settings model, preview, Apply, Cancel, Restore Defaults, status, and active-font reflection. |
| Desktop font adapter | `kernel/core/desktop_font.cpp`; `kernel/core/desktop_system_font.cpp`; `kernel/core/include/kernel/desktop_font.h` | Bridges the shared service to native desktop drawing. |

### 2.2 Font integration sites

These are shared production files with FONT1–FONT11 integration changes. Review their full diffs against the chosen integration base rather than treating them as exclusively font-owned:

- `kernel/core/desktop.cpp` and `kernel/core/include/kernel/desktop.h`: shell text routing, Start Menu, decorations, clock/date, and drawing lifecycle.
- `kernel/core/kernel_compositor.cpp`: composition and redraw paths used by text-bearing desktop surfaces.
- `kernel/core/kernel_apps.cpp`; `kernel/core/include/kernel/kernel_app.h`: native Settings Fonts page and related app/window integration.
- `kernel/core/main.cpp`: service initialization and ordering after the filesystem is available.
- `kernel/core/native_elf_baremetal.cpp`; `kernel/core/include/kernel/native_elf_baremetal.h`: native app/demo proof integration. The FONT5 demo is validation material, not the clean-install font package.
- `kernel/core/ata.cpp`: FONT11 proof-only diagnostic hook guarded by `GXOS_FONT11_QEMU_PROOF`; this macro was absent from the forced release-style build.
- `settings_presentation_core.cpp` and `settings_presentation_core.h`: shared Settings presentation test surface and integration.

### 2.3 Build, tests, and validation assets

- `kernel/Makefile`: no font-specific source registration is needed in the current build because the kernel uses `CORE_SOURCES := $(wildcard core/*.cpp)` and `core/include` is on its include path. Confirm this remains true on the integration base.
- `sdk/build-samples.ps1`; `sdk/samples/font5-glyph-cache/app.json`; `sdk/samples/font5-glyph-cache/main.cpp`: build and source for the optional FONT5 sample.
- `scripts/run-font-contract-test.ps1`, `scripts/run-font-truetype-test.ps1`, `scripts/run-font-raster-test.ps1`, `scripts/run-font-glyph-cache-test.ps1`, `scripts/run-system-font-selection-test.ps1`, `scripts/run-font-text-layout-test.ps1`, `scripts/run-font10-shell-typography-test.ps1`, `scripts/run-font11-cache-workload-test.ps1`: subsystem test runners.
- `scripts/run-font5-qemu-demo.ps1` and `scripts/run-font6-qemu-proof.ps1`: development/qualification flows, not normal image packaging.
- `tests/font_contract_test.cpp`; `tests/font_truetype_test.cpp`; `tests/font_raster_test.cpp`; `tests/font_glyph_cache_test.cpp`; `tests/system_font_selection_test.cpp`; `tests/font_text_layout_test.cpp`; `tests/font10_shell_typography_test.cpp`; `tests/font10_systemfont_stubs.cpp`; `tests/font11_glyph_cache_workload_test.cpp`; `tests/settings_presentation_core_test.cpp`.
- `Apps/Font5GlyphCacheDemo/app.json`; `Apps/Font5GlyphCacheDemo/bin/amd64/font5demo.elf`; `Apps/Font5GlyphCacheDemo/resources/Roboto-Regular.ttf`: prebuilt demo app and its private resource. Do not mistake this resource for an installed system font.
- Qualification documentation and evidence are retained in `docs/fonts/FONT1_INFRASTRUCTURE_AUDIT.md` through `FONT12_V03_RELEASE_QUALIFICATION.md`, plus `docs/evidence/font4/` through `font12r3/`. The evidence files are not runtime dependencies.

### 2.4 Existing assets and unrelated-to-TTF resources

Tracked font assets currently include:

- TTFs: `assets/Fonts/roboto/Roboto-Regular.ttf` (146,004 bytes), `Roboto-Bold.ttf` (146,768), `Roboto-Italic.ttf` (152,208), and `Roboto-BoldItalic.ttf` (153,104).
- TTF accompanying files: `assets/Fonts/roboto/OFL.txt` and `NOTICE.txt`.
- Existing bitmap/BTF resources: `assets/Fonts/defaultfont.png`, `assets/Fonts/enludo.btf`, `assets/Fonts/Song.btf`, and the `roboto_9pt_*`, `roboto_12pt_*` PNG atlases under `assets/Fonts/roboto/`.
- Generated bitmap atlas: `kernel/core/include/kernel/generated/system_font_roboto.h`; existing bitmap service: `kernel/core/system_font.cpp` and `kernel/core/include/kernel/system_font.h`; legacy root header: `bitmap_font.h`.

The four TTF binaries are tracked repository inputs but were not added by the FONT1–FONT11 commit series in the audited `origin/main..FONT_SUPPORT` diff; FONT13 did not establish their provenance. The bitmap assets and generated bitmap atlas also predate this TTF integration series. They are not substitutes for files under `/system/fonts/`. Their independent provenance/attribution should remain with the existing bitmap subsystem; the TTF `OFL.txt` and `NOTICE.txt` do not establish licenses for every legacy PNG/BTF asset. No new license claim for those legacy assets is made here.

## 3. Clean-install packaging and font provenance

### 3.1 Current production path

The current kernel looks for direct `.ttf` children of `/system/fonts/`. Discovery is non-recursive, handles the accepted suffix case-insensitively, and uses the VFS/FAT file read path. The raw FAT32 VFS supports the required file reads and directory enumeration. The existing FONT6 QEMU proof manually copied files into a private evidence ESP before boot; it does not prove release packaging.

The normal `build.ps1`/ESP population path and `scripts/create-release-iso.ps1` package the established ESP contents. There is no production rule copying `assets/Fonts/roboto/*.ttf`, `OFL.txt`, or `NOTICE.txt` into `/system/fonts/`. The tracked `ESP/` inventory has no `system/fonts/` records. The App5 demo resource is separately packaged for its demo and is not copied to the system font directory. Therefore a clean image currently has no `/system/fonts/` directory, no selectable TTF faces, and no packaged TTF license/attribution files.

The kernel and desktop remain usable in that state: no selected face or unavailable TTF resolves to the built-in bitmap backend. FONT12 guest evidence covers missing assets and invalid preference recovery. Clean-install TTF availability and license installation remain packaging gaps.

### 3.2 Identity limits and candidate package

FONT6 identifies an installed face using its path, parsed family/style metadata, file length, and a 64-bit FNV content fingerprint/stable identity. Renaming, replacing, or changing bytes changes identity; a saved TTF selection then reports/falls back safely rather than silently selecting another face. Install candidate files as direct children with stable names, for example `/system/fonts/Roboto-Regular.ttf`. Do not install four faces as if all were currently selectable: the service accepts at most three external font sources, each at most 192 KiB (576 KiB total fixed source capacity). The current four TTFs total 598,084 bytes and exceed the three-face capacity. A three-face package such as Regular/Bold/Italic is within the present face count and size bounds, subject to validation and license review.

The repository contains `OFL.txt` (SIL Open Font License 1.1) and a `NOTICE.txt` naming Roboto authors and recording the hashes below. The local notice itself says the exact upstream release is not established. Local hashes match the notice, but a matching hash proves file identity, not the binary's upstream provenance. Do not ship these files as the v0.3 system package until the exact source release and its license/attribution obligations are matched to each binary.

| Current repository file | Bytes | SHA-256 |
|---|---:|---|
| `Roboto-Regular.ttf` | 146,004 | `dbd285b518e398832f6f4a736109c355ce25a49546bfce41bab256c9ef7e56eb` |
| `Roboto-Bold.ttf` | 146,768 | `27467020febce4c0ac8482ea8e8d6f32bf8c5721fb9c301344d0e3f4d93a0119` |
| `Roboto-Italic.ttf` | 152,208 | `5a2f18bddbe3ac07f14d38817d1fd22df4e8e3b74f661f7ae7b29cc8140092bc` |
| `Roboto-BoldItalic.ttf` | 153,104 | `8234add45022e40d0ad293c8a2854a8f387e53db5c433698fe6d89c8d1b5362e` |

If exact provenance cannot be recovered, replace only after sourcing and recording a specific static TTF release, license file, upstream URL/commit or release archive, per-file SHA-256, and parser/raster qualification. One possible investigation candidate is the archived `googlefonts/roboto-2` v2.138 release; its repository exposes static hinted TTFs and a repository license, but the exact chosen file, release packaging, and license terms still need asset-level verification before adoption. The current Google Fonts Roboto metadata points to v3.015 variable fonts under OFL; variable fonts are outside this subsystem's supported scope and are not a replacement candidate for direct use. References: [Roboto v2 releases](https://github.com/googlefonts/roboto-2/releases), [Roboto v2 repository](https://github.com/googlefonts/roboto-2), [current Google Fonts Roboto metadata](https://github.com/google/fonts/blob/main/ofl/roboto/METADATA.pb).

No external font was downloaded or replaced during FONT13.

## 4. First-boot policy

| Policy | User experience | Compatibility and implementation requirements | Assessment |
|---|---|---|---|
| **A — Bitmap by default** | Fresh desktop uses built-in bitmap. Settings can select an installed and validated TTF. | No new default-selection transition; matches current no-preference behavior and preserves bitmap as boot/recovery/failure renderer. Still requires properly packaged fonts for selection. | Lowest risk and directly supported by current implementation. Recommended for controlled v0.3. |
| **B — TTF preferred graphical default** | Fresh desktop automatically uses a bundled validated TTF when no explicit preference exists. | Must happen after VFS is usable; no kernel boot dependency; bounded scan/load; deterministic package; validated FONT3 path; bitmap on missing assets, VFS failure, invalid file, or unsupported record; preserve explicit saved bitmap/TTF; Settings must show the active selection accurately. Requires startup timing, cold/warm, invalid/missing asset, upgrade, persistence, and explicit bitmap tests. | Feasible as a graphical default, not a kernel dependency. Requires a product decision and additional implementation/qualification. Do not enable automatically as part of this handoff. |

**Recommendation:** Policy A for v0.3. It matches the existing behavior, keeps first boot independent of external assets, and is compatible with the accepted startup path. A fresh install can still offer TTF selection once a legal, validated package exists. If the product owner chooses B, record that decision before implementation and treat it as an explicit no-preference default after VFS startup. Never represent the TTF as required to boot the kernel. Adding bundled fonts must not turn an explicit bitmap record into an automatic TTF selection.

## 5. Preference persistence and upgrade behavior

FONT9 uses two fixed 1 KiB records (`/fontsel0.cfg`, `/fontsel1.cfg`) with sequence/checksum validation and a bounded `/fontsel.tmp` scratch record. The on-disk v1 format is retained. Apply stages the candidate, verifies the staged data, writes the inactive slot, reads it back, and publishes the new in-memory selection only after verification. Failed persistence rolls back in-memory state. This is bounded transactional behavior, not a guarantee of arbitrary power-loss atomicity on non-journaled FAT.

| Case | Established behavior / integration expectation |
|---|---|
| Fresh install, no preference | Built-in bitmap at 7 px; no TTF is assumed present. |
| Valid explicit bitmap preference | Continue using bitmap even if a release adds TTF files. Do not reinterpret an explicit bitmap as “unset.” |
| Valid TTF preference and matching installed file | Restore if path, family/style, size, and content identity still validate. Settings must report the active face. |
| TTF preference, file missing or VFS read fails | Keep the configured preference/status for recovery; active rendering safely falls back to bitmap. Kernel/desktop boot continues. |
| Font file replaced or changed in place | Identity mismatch prevents silent substitution; active bitmap fallback until user selects a valid face. |
| Corrupt preference record | Reject checksum/record; use the other valid slot if available, otherwise bitmap. Invalid records must not cause a boot failure. |
| Unsupported preference version | Reject the unsupported record and use a supported valid slot or bitmap. No unreviewed format migration. |
| Upgrade from earlier releases with no FONTSEL record | No preference means bitmap. Do not infer a TTF preference just because the new release ships fonts. |
| Repeated Apply / Restore Defaults | Keep fixed record bounds and inactive-slot verify-before-publish flow; Restore Defaults explicitly persists bitmap. Repetition must not grow records or overwrite the last verified active slot on ordinary failure. |

Integration must preserve these v1 semantics and the two-slot rollback behavior. Any future format change needs its own compatibility plan and evidence; it is not required for FONT13.

## 6. Integration dependency and conflict matrix

This is a read-only branch/path overlap audit of local remote-tracking refs at the stated snapshot, not a readiness judgment. The audited local tips were `MODERN_INPUT` `63542a1`, `NAVIGATOR_GENERAL_IMPROVEMENTS` `f2561fa`, `NAVIGATOR_JAVASCRIPT_SUPPORT` `69876f0`, `DISK_MANAGER_IMPROVEMENTS` `ed35584`, `AARCH64_SUPPORT` `2d80cf6`, `v0.3_SCI_FI_THEME` `4a02790`, and `v1.1_DOTNET_SUPPORT` `5c62dae`. These are snapshot identifiers, not claims about current upstream tips. Shared path overlap signals likely conflict points; it does not mean another branch is approved or mergeable.

| Component | Current font integration | External dependency / observed overlap | Conflict surface | Recommended integration order | Required tests after integration |
|---|---|---|---|---|---|
| Kernel build and startup | Wildcard core source build; font services initialized after VFS availability in `main.cpp`. | `origin/main` is the base; several active branches touch `main.cpp`. `origin/AARCH64_SUPPORT`, `origin/MODERN_INPUT`, `origin/NAVIGATOR_GENERAL_IMPROVEMENTS`, `origin/DISK_MANAGER_IMPROVEMENTS`, and `origin/v1.1_DOTNET_SUPPORT` overlap startup/desktop paths. | Init order, architecture flags, service ownership, compile sources. | Establish intended target kernel/build baseline first; then add bounded font modules and startup initialization. | Forced AMD64 compile/link; boot with font assets, missing assets, and filesystem failure. Re-run core font tests. |
| Filesystem / VFS / FAT | Discovery and persistence read/write through VFS; package path is FAT-root `/system/fonts/`. | `origin/DISK_MANAGER_IMPROVEMENTS` and `origin/v1.1_DOTNET_SUPPORT` both change `vfs.cpp`/`fs_fat.cpp`, and overlap `main.cpp`, `desktop.cpp`, and app paths. | Directory enumeration, path normalization, read limits, write/readback semantics, mount timing, FAT image generation. | Integrate the selected VFS/FAT changes before font discovery and preference persistence; verify the actual FAT image. | Directory enumeration; bounded reads; missing/unreadable file fallback; corrupt-record recovery; repeated Apply; clean image inspection. |
| Settings | Native Settings Fonts page enters through `kernel_apps.cpp` and shared presentation core. | `origin/v1.1_DOTNET_SUPPORT`, `origin/NAVIGATOR_GENERAL_IMPROVEMENTS`, and other Settings work overlap `kernel_apps.cpp`, `desktop.cpp`, `main.cpp`, and compositor paths. FONT_SUPPORT has no managed C# font query/apply bridge. | App lifecycle, layout/resize, focus/input, Settings navigation, presentation model, managed/native boundary. | Integrate the chosen Settings/App Model baseline first; port Fonts page and C-compatible calls against its actual lifecycle. Treat managed Settings as a separate dependency until an explicit bridge exists. | Open/reopen, preview, Apply/Cancel, Restore Defaults, all saved preference cases, resize/focus, and current-font reflection. |
| Desktop / compositor / window decorations / taskbar and clock | Native drawing adapter routes text surfaces through selected face with bitmap fallback. | `origin/MODERN_INPUT`, `origin/NAVIGATOR_GENERAL_IMPROVEMENTS`, `origin/DISK_MANAGER_IMPROVEMENTS`, `origin/AARCH64_SUPPORT`, `origin/v0.3_SCI_FI_THEME`, and `origin/v1.1_DOTNET_SUPPORT` overlap `desktop.cpp`/`kernel_compositor.cpp`; several also overlap `kernel_apps.cpp`. | Text call ownership, clipping, redraw invalidation, focus/window chrome, taskbar, input-driven resize and visual theme. | Integrate compositor/window model and theme first; apply font drawing hooks surface-by-surface and preserve bitmap coverage. | Start Menu, taskbar/clock/date, multiple windows, decorations, resize/redraw, narrow labels, bitmap and TTF at required geometry. |
| Navigator and native application model | Fonts are integrated into native shell surfaces; native ELF/demo integration is limited. | `origin/NAVIGATOR_GENERAL_IMPROVEMENTS` overlaps `desktop.cpp`, `kernel_apps.cpp`, `main.cpp`; `origin/NAVIGATOR_JAVASCRIPT_SUPPORT` did not overlap the identified font paths in this snapshot. Managed .NET has no font selection bridge. | App navigation, Settings lifecycle, API/ABI and font ownership across native/managed UI. | Stabilize App Model/Navigator and public host ABI before connecting font consumers; keep the service kernel-owned. | Launch/navigation, app and window lifecycle, ABI layout, native and managed compatibility where an explicit API is added. |
| Input and resize acceptance | Fonts page is rendered in a resizable native Settings window; FONT12R3 minimum drag was input-limited. | `origin/MODERN_INPUT` and `origin/AARCH64_SUPPORT` touch input/desktop/compositor; `origin/v1.1_DOTNET_SUPPORT` touches PS/2 mouse and desktop paths. | Relative pointer scaling, PS/2 packet delivery, cursor mapping/clamp, grip hit testing, resize dispatch. | Integrate the selected input/compositor/window manager path before repeating the minimum-size visual gate. | Counter-instrumented resize to 560×360 and a below-minimum request, then confirm minimum clamp and visible/hit-testable font controls. |
| Image/boot packaging | Existing image flow installs ESP contents; no TTF install rule exists. | `origin/MODERN_INPUT`, Navigator branches, and other build work overlap `scripts/create-release-iso.ps1`; boot/image scripts may change with VFS or installer work. | Build dependency ordering, asset copy, FAT directory creation, ISO inclusion, license placement. | Finalize font source/license identity, then add copy rules to the authoritative image build path after the chosen boot/image packaging baseline. | Build clean image from tracked inputs; inspect FAT entries and bytes/licenses; boot it without fonts and with package; verify discovery. |

Do not infer that any of these remote refs is current or integration-ready from this local overlap audit. The owner should inspect the actual intended commits and contracts at integration time.

## 7. Memory, startup, and recovery assessment

FONT12 records font-related static BSS of **1,143,728 bytes** (about 1.09 MiB):

| Contributor | Bytes | Notes |
|---|---:|---|
| TrueType outline and raster scratch | 238,880 | Fixed scratch for validated outline decoding and bounded raster work. |
| Glyph cache | 69,152 | Fixed cache storage. |
| Loader source storage and metadata | 590,624 | Three fixed 192 KiB font source slots (589,824 bytes) plus metadata. |
| Selection/discovery records and scratch | 244,736 | Bounded catalog, metadata, preference and temporary records. |
| Shared contract and adapters | 336 | Contract 256 bytes, desktop adapter 64, selection manager 16. |
| **Total** | **1,143,728** | Sum matches FONT12 accounting. |

The parser caps file size at 192 KiB, table count at 64, and glyph count at 16,384. Discovery/catalog storage is bounded (64 catalog entries); at most three external faces are loaded. Raster output is limited to 128×128 with a 32 million scan-work ceiling. Discovery and validation can read/validate each candidate through fixed scratch, so worst-case scan work is bounded, although no startup latency measurement is recorded. Loaded face sources and cache are fixed runtime state; scratch is fixed rather than proportional to file-supplied values. Failure to enumerate/read/validate a face leaves or restores bitmap rendering.

The accepted guest qualification uses 1 GiB RAM and the current declared test target passes there. No product minimum-RAM requirement was found; none is invented here. The font budget is about 1.09 MiB, while the full kernel ELF BSS measured in FONT12 is approximately 580,180,576 bytes (553.30 MiB), dominated by non-font pools. Candidate v0.3 hardware requirements need an owner-level full-system memory budget that includes kernel pools, applications, graphics buffers, filesystem and boot-time headroom. The font number alone cannot establish a machine minimum. No speculative memory optimization is recommended by this audit.

Option B would perform bounded I/O/validation earlier in first graphical startup. It must be timed on target hardware before adoption; bounded work is not itself a latency measurement.

## 8. Minimum-size acceptance handoff

Status: **`TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN`**.

Evidence remains exactly as qualified in FONT12R2/R3:

- Settings minimum is 560×360 outer geometry; preferred size is 760×500.
- FONT8 demonstrated bitmap rendering at 560×360, but did not prove a same-instance minimum clamp/resize interaction.
- FONT12R2 demonstrated same-window live resize from 760×500 to 825×555, a `(+65,+55)` geometry change, plus focus/control behavior.
- FONT12R3 demonstrated Roboto Bold at 18 px in Settings at 760×500.
- FONT12R3 tried to move the pointer from `(465,264)` to resize grip `(850,565)`: intended displacement `(+385,+301)`. The observed cursor reached about `(501,301)`, displacement `(+36,+37)` (approximately one relative input step). No button-down was sent after the undershoot. No font-specific failure was demonstrated.

Input configuration and evidence:

- QEMU: `pc,accel=whpx`, 1 GiB RAM, 2 vCPU, VGA 1280×800, private OVMF and raw FAT32 PIIX IDE master, no network, loopback QMP/VNC. The configured pointer was QEMU's default relative PS/2 mouse; no absolute tablet device was present.
- FONT12R3 evidence: `docs/evidence/font12r3/guest/settings-pointer-underrun.png`, `qmp-input-trace.jsonl`, `qemu-launch.json`, `observed-window-geometry.json`, and `fresh-process.serial.log`.
- FONT12R2 context: `docs/evidence/font12r2/guest/pointer-motion-underrun-at-minimum-check.png`, `qemu-launch.json`, plus same-window resize screenshots/logs under `docs/evidence/font12r2/guest/`.

Input-team handoff: correct the end-to-end relative pointer path or provide a validated absolute pointer path. Instrument and capture at least: QMP events sent; PS/2 packet/IRQ accepted count; decoded X/Y deltas; mapped X/Y deltas; cursor coordinates before/after and clamp events; button transitions; resize begin/continue/end with window ID and geometry; requested versus clamped minimum geometry. Reproduce on a fresh, isolated QEMU process using the above configuration and launch evidence; use the exact `(465,264)` to `(850,565)` target path or record a new coordinate pair from the actual UI. Do not send a drag until cursor telemetry confirms the grip was reached.

Acceptance requires reliable cumulative movement to the resize grip; a button transition and resize dispatch for the same live Settings window; a visible 560×360 result; an attempted below-minimum size clamped to 560×360; readable/hit-testable Fonts controls showing active Roboto Bold 18 px; no unintended preference change; and no renderer fault. This handoff does not classify the FONT12R3 undershoot as a font renderer defect. Do not repeat the prior unsupported pointer attempt without corrected input delivery and counters. FONT13 did not change input drivers.

## 9. Build, packaging, and regression results

### AMD64 freestanding build

Forced full build and link completed successfully:

```powershell
mingw32-make -B -C kernel ARCH=amd64
```

Evidence: `out/font13-validation/amd64-build.log`; output `kernel/build/amd64/bin/kernel.elf` (2,813,880 bytes) and `kernel/build/amd64/bin/kernel.pe` (2,937,438 bytes). The PE import, bound-import, IAT, and delay-import directories are all zero; no hosted DLL imports were reported. The build used C++14, `-ffreestanding`, `-fno-exceptions`, `-fno-rtti`, `-nostdlib`, `-nostdinc++`, and `-fno-builtin`. `GXOS_FONT11_QEMU_PROOF` did not appear in build flags/log; proof hooks are not enabled in this build.

### Font and dependent regression suites

All available FONT2–FONT11 tests invoked for this audit passed: **6,635 checks**.

| Suite | Result |
|---|---:|
| Shared font contract | 196 / 196 |
| TrueType loading/validation | 1,150 / 1,150 |
| Rasterizer | 4,454 / 4,454 |
| Glyph cache | 277 / 277 |
| Font selection and persistence | 376 / 376 |
| Shared text layout / native adoption | 46 / 46 |
| FONT10 shell typography | 88 / 88 |
| FONT11 cache workload | 48 / 48 |
| **Total** | **6,635 / 6,635** |

Settings-related tests passed: presentation core **2,651**; native runtime **828**; center **89**; Users **32**; Inventory **45**; Default Apps **48**; Settings S6 **42**; Settings S7 **58** — **3,793 checks** total. These include native Fonts page and presentation coverage. App Model action/folder/default-handler/URI/identity/document activation suites passed **261 checks**. Native presentation validation, compositor pointer buttons, compositor resize notifications, native ABI layout, native filesystem contract, and the bounded freestanding Settings presentation link passed. The current uncommitted FONT12R ABI and filesystem test corrections were included in these validations.

The release ISO FAT helper smoke passed using the existing `out/font9-validation/fat-env` Python environment. That smoke validates the generic image helper, not system-font installation; it does not close the missing asset-copy rule. No development dependency was installed.

No production kernel code changed during FONT13, so accepted FONT12 guest behavior was preserved and the audit used the specified narrow smoke/build/regression strategy. No new QEMU guest cycle was run. In particular, the unsupported FONT12R3 pointer-motion attempt was not repeated. The minimum-size acceptance remains open.

## 10. Owner-executed integration checklist

Use this checklist on the actual integration base. Complete the prerequisite/package decisions before release sign-off.

### Source and build

- [ ] Compare and integrate the production font files in Sections 2.1–2.2, reviewing complete diffs for shared files.
- [ ] Confirm `kernel/Makefile` still includes `core/*.cpp` and `core/include`; otherwise explicitly register the 11 font `.cpp` modules and include paths.
- [ ] Preserve AMD64 freestanding flags and bounded fixed-capacity structures. Keep exceptions, RTTI, STL, hosted runtime dependencies, and unsupported allocation out of kernel code.
- [ ] Keep proof-only QEMU hooks disabled in the release configuration.
- [ ] Run all font contract, parser, raster, cache, selection, persistence, text layout, Settings, shell, and cache workload suites on the integrated tree.
- [ ] Force a clean AMD64 kernel compile and link; inspect undefined symbols and PE imports.

### Assets, license, and image

- [ ] Resolve the exact upstream source/version for every selected TTF. If unresolved, select a traceable static TTF candidate and record source URL, release/tag or commit, exact archive/member names, license text, attribution, and SHA-256 per file.
- [ ] Obtain the product/legal disposition for the selected files and include the complete required license and notices with the installed fonts. Do not assume the current notice establishes exact binary provenance.
- [ ] Stay within current bounds: at most three direct `.ttf` faces, no file over 192 KiB, max 576 KiB source total; validate the desired face set against FONT3/FONT4 and Settings identity tests.
- [ ] Add explicit font and license copy rules to the authoritative normal `build.ps1`/ESP and release ISO/package path, creating `/system/fonts/` in the FAT image. Do not depend on FONT5/6 proof scripts or untracked development artifacts.
- [ ] Build from tracked clean inputs, unpack/list the resulting image, and verify filenames, exact bytes, hashes, license/notice files, case behavior, and direct-child layout.
- [ ] Verify VFS discovery on that image and exercise missing directory/file, unreadable file, malformed font, and changed-file identity. Confirm safe bitmap rendering in each failure case.

### Settings, desktop, persistence, and integration dependencies

- [ ] Integrate against the chosen Settings/App Model, VFS/FAT, compositor/window, Navigator, input, and boot/image changes only after checking their actual commit status and API contracts.
- [ ] Review Settings lifecycle/layout and native/managed boundaries; the current FONT_SUPPORT branch has no managed .NET font-selection bridge.
- [ ] Verify system font state is initialized only after VFS is available; verify graphical failure returns to bitmap and cannot block kernel boot.
- [ ] Preserve FONT9 v1 two-slot format, bounded scratch, readback verification, rollback, and explicit bitmap selections on upgrade.
- [ ] Test fresh install, prior-release upgrade, explicit bitmap and TTF preferences, missing/replaced assets, corrupt/unsupported preferences, repeated Apply, failed persistence, and Restore Defaults.
- [ ] Verify Settings reflects the active backend after preview, Apply, Cancel, reboot, and fallback.
- [ ] Verify Start Menu, Settings, taskbar, clock/date, window decorations, focus, multiple windows, redraw and resize under both bitmap and TTF.

### Guest acceptance and release boundary

- [ ] Run a fresh isolated-process QEMU census before launch; use private firmware, disk/ESP, serial, QMP, and display endpoints. Do not attach to or reuse other projects' guests.
- [ ] Boot a clean packaged image with no font assets and verify kernel/desktop bitmap fallback; boot with the approved package and verify deterministic discovery and selection.
- [ ] Complete the input-team counters and minimum-size test in Section 8, including visible 560×360, below-minimum clamp, active font status, and no preference mutation.
- [ ] Record the minimum-size gate as open until it passes; this handoff is not a substitute for the missing visual acceptance.
- [ ] Obtain explicit product choice for Option A/B before changing first-boot behavior. Option A is the recommended v0.3 default.
- [ ] Obtain independent v0.3 release sign-off after branch integration, packaging/legal review, target-memory/hardware requirement review, all guest acceptance, and the repository's normal release qualification. FONT13 does not authorize merge/release and has no release-signoff authority.

## 11. Known limitations and readiness recommendation

The source inventory, bounded runtime design, persistence behavior, dependency hotspots, test evidence, full AMD64 build, and input-team handoff are documented and actionable. The clean-install path does **not** currently package `/system/fonts/`, TTF assets, or their license/notice files. Current Roboto hashes are recorded, but their exact upstream binary provenance remains unresolved. The Settings minimum-size TTF acceptance remains open because the test input did not reach the resize grip; no font-specific failure was observed.

Recommendation: keep the bitmap first-boot default and treat this document as a partial integration-preparation handoff. Before selecting any clean-install TTF package, resolve provenance and license, add the normal image copy rules, then inspect and boot a clean image. Integrate only after checking the current Settings, compositor, App Model, VFS, input, and boot-package branches at their actual tips. Preserve `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` until the corrected input path passes. Do not declare guideXOS Server v0.3 ready on the strength of FONT13 alone.

## References

- FONT12 release qualification and FONT12R/R2/R3 evidence listed in the project documents above.
- Repository source and test manifests in Section 2.
- Upstream Roboto candidate references: [googlefonts/roboto-2 releases](https://github.com/googlefonts/roboto-2/releases), [googlefonts/roboto-2 repository](https://github.com/googlefonts/roboto-2), and [Google Fonts Roboto metadata](https://github.com/google/fonts/blob/main/ofl/roboto/METADATA.pb).

## FONT13R continuation — verified package and integration state

This section supplements, and does not rewrite, the historical FONT13 baseline and findings above. The work remains on `FONT_SUPPORT` at `89a58151c19da3d6532d838ae3a29a2c0ab4711a`; phase tracking remains in integration preparation. No commit, push, merge, or release sign-off was made.

### Approved distributable assets

| Runtime filename under `/system/fonts/` | Bytes | SHA-256 |
|---|---:|---|
| `Roboto-v2.138-Regular.ttf` | 88,780 | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` |
| `Roboto-v2.138-Bold.ttf` | 90,556 | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` |
| `Roboto-v2.138-Italic.ttf` | 94,736 | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` |

Ship these exact files from `assets/Fonts/Roboto-v2.138-package/` with `LICENSE.txt` (complete Apache License 2.0), `NOTICE.txt`, and `ApprovedFontManifest.json`. The source is Google's `googlefonts/roboto-2` release `v2.138`, archive `roboto-unhinted.zip`, pinned at SHA-256 `70f64c718510a601fbcf752aafe644314dacaeb85474dc689c89787c4a72a728`. Source/member hashes, output hashes, license and notice hashes are in the approved manifest. Existing development Roboto binaries are not attributed to v2.138.

### Packaging and qualification status

- The existing `build.ps1` → `generate-wallpaper-pack.ps1` path stages the manifest allowlist in the runtime image's `/fonts/` root, which is mounted at `/system`; runtime discovery uses `/system/fonts/`. Exact FAT image readback confirmed all three bytes/hashes, license, notice, and no extra font. A two-face package also passed image staging when an optional face was absent.
- Package validation passed 21 checks; all three exact outputs passed FONT3/FONT4 parsing and raster checks. FONT6 identity/persistence tests pass for fresh bitmap default, explicit bitmap persistence, packaged identity selection, process-service restart, and reset to bitmap. Existing FONT9 two-slot transaction and rollback behavior is unchanged.
- The normal runtime image packer created a fresh FAT32 image, and a private 128 MiB writable FAT32 ESP with no saved font preference booted to desktop. Serial reports system-font initialization `Ok`, fallback availability, and Navigator smoke `PASS`; a first-desktop screenshot is under `out/font13-validation/fresh-clean-image-20261018/`.
- The top-level `build.ps1 -Arch amd64` run stopped at PacMan linking on unresolved `pacman_audio_load_resources` and `pacman_audio_submit` symbols. The independent forced AMD64 freestanding kernel build passed and its PE import directory is empty.
- The QEMU guest's Start Menu opened by keyboard, but pointer movement was not reliable enough to open Settings. No FONT13R claim is made for real Fonts-page enumeration, preview, Apply, visible TrueType shell rendering, guest preference restart, Restore Defaults, or configured-font missing/corrupt fallback. The screenshot and serialized tests are not substitutes for those visual checks.

### Compatibility and open handoff

New package paths and fingerprints are distinct from existing development or custom font binaries. Family-name equality alone does not restore an old identity. Existing explicit bitmap preferences remain bitmap; a missing or changed configured file follows the existing validated bitmap fallback. Packaging does not overwrite custom source files.

The repeated PS/2 pointer underdelivery is handed to the owning input workstream, as already recorded by FONT12R3. No pointer, mouse, HID, or window-manager code changed here. The minimum-size acceptance marker `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open: FONT8's 560×360 bitmap page and FONT12R3's incomplete same-instance resize attempt do not provide that acceptance.

Before integration readiness can advance, resolve the PacMan link prerequisite, run the complete normal clean-image build, and qualify the listed guest Settings/apply/restart/fallback sequence with visual and serial/configuration evidence. Preserve the bitmap clean-install default and run the post-integration smoke checklist in [FONT13R_VERIFIED_FONT_PACKAGING.md](FONT13R_VERIFIED_FONT_PACKAGING.md). Outcome remains **B — Partial**; no phase advance or FONT14 work is authorized by this handoff.
