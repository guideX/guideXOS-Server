# FONT1 — Font Infrastructure Audit and Architecture Design

**Status:** Audit completed with validation limitations
**Date:** 2026-10-16
**Branch / starting HEAD:** **FONT_SUPPORT** / **6b89e42ef9a188da4c55fc0a5e80863e32ad79a4**

This document separates verified repository facts from proposed architecture. No renderer, application, Settings page, third-party font library, or production configuration was changed for FONT1.

## Summary

guideXOS already has a bitmap system-font layer called **SystemFont**. Hosted rendering loads Roboto PNG atlases, while the freestanding kernel embeds four generated atlases. The layer has proportional per-glyph advances, ascent/descent and line-height metrics, scaling, and a built-in 5x7 fallback. It does not load the Roboto TTF files that are present in the repository.

There are parallel legacy paths that bypass **SystemFont**: the bare-metal kernel compositor and built-in applications draw a fixed 5x7 font; framebuffer and boot consoles use fixed 8x16 cells; the bare-metal native application host calls **BitmapFont** directly; and ARM64 has its own 8x16 font. User-facing text is therefore not controlled by one font implementation today.

**Recommendation:** preserve the existing API as a transition façade, then add a freestanding-compatible font manager behind a shared text-layout and rasterization contract. Keep bitmap fallback and early boot diagnostics independent. Evaluate FreeType first for untrusted installed fonts because it supports custom memory managers and input streams; keep **stb_truetype** as a compact prototype candidate only. Do not use unmodified stb for user-supplied font files: upstream explicitly says its file offsets are not bounds-checked and that it is unsafe on untrusted font data.

The existing Settings presentation model already has a Personalization category and a hosted Settings page flow. Persist a stable family ID and pixel size using the existing display/personalization options storage. Hosted and bare-metal paths currently use related but different file names and adapters, so FONT2 must select and document the canonical schema and migration behavior.

## 1. Audit scope and checkout state

### Verified checkout state

- Working directory: D:\dev\guideXOSServer_FONT_SUPPORT
- Repository branch: FONT_SUPPORT
- Starting HEAD: 6b89e42ef9a188da4c55fc0a5e80863e32ad79a4
- Starting worktree: clean; no staged, unstaged, or untracked changes.
- No **.phase** marker was found at the repository root or under **docs**. Phase state is currently represented by named documentation and evidence folders. FONT phases should use that convention unless a later repository-wide phase mechanism is introduced.

### Scope and evidence labels

- **Verified** statements below come from checked-in source, assets, build configuration, or the validation commands recorded at the end.
- **Proposed** statements are design recommendations. They are not current behavior or approved implementation details.
- Source links are relative to this document and include a function or declaration location for navigation.

## 2. Existing font and rendering architecture

### 2.1 Shared bitmap SystemFont

**Verified:** [system_font.h](../../kernel/core/include/kernel/system_font.h#L20) defines **FontSize**, **FontWeight**, **FontSlant**, **FontRole**, **BitmapGlyphMetrics**, **BitmapFontFace**, and the static **SystemFont** API. Its metrics include per-glyph bounds and advance plus face ascent, descent, baseline, and line height. APIs provide measurement, scaling, HDC drawing for the hosted build, and pixel-buffer drawing for freestanding rendering.

**Verified:** [system_font.cpp](../../kernel/core/system_font.cpp#L55) declares nine Roboto bitmap faces, with nominal 9 and 12 pixel roles, several weights and slants. The hosted build loads atlas PNGs through **ImageAdapter**, validates atlas dimensions, copies alpha into an allocated buffer, and derives each glyph's bounds from non-transparent pixels. The buffer is retained in static face records for process lifetime; this path does not free it. If an atlas cannot be loaded, it marks the face as fallback and uses **BitmapFont**.

**Verified:** the freestanding build uses the generated [system_font_roboto.h](../../kernel/core/include/kernel/generated/system_font_roboto.h#L1) arrays instead of reading image assets. The embedded atlas lookup only selects regular and bold faces at the two supported sizes. Declared italic and thin face records therefore fall back in the bare-metal build even though hosted atlas images exist for them.

**Verified:** the supported atlas range is 95 printable ASCII characters, codepoints 32–126. Measurement and drawing index the input one byte at a time. For a byte outside the range, the atlas path advances by a fallback width and paints no glyph. There is no UTF-8 decoder or Unicode scalar-to-glyph mapping in **SystemFont**. See the byte-indexed lookup and measurement path in [system_font.cpp](../../kernel/core/system_font.cpp#L80).

**Verified:** atlas metrics are proportional rather than fixed-width: the loader finds pixel bounds and computes an advance per glyph; **MeasureWidth** sums advances. The current width loop does not apply kerning between glyph pairs. **GetFaceForPixelSize** selects one of the two stored bitmap sizes and scales it, capped at 72 pixels. This is bitmap enlargement, not outline rasterization.

### 2.2 Other bitmap renderers and fixed-cell paths

| Path | Verified implementation | Current behavior |
|---|---|---|
| Shared fallback | [bitmap_font.h](../../bitmap_font.h#L22) | 5x7 fixed bitmap covering ASCII 32–126. It includes HDC and buffer drawing helpers. |
| Bare-metal desktop | [desktop_font.cpp](../../kernel/core/desktop_font.cpp#L117) | **measure_text** delegates to **SystemFont**; scale-1 drawing also delegates when a 32-bit framebuffer is available. Other scale values use a local 5x7 glyph table and fixed advance. This can make measurement and drawing disagree at non-unit scale. |
| Legacy kernel compositor | [kernel_compositor.cpp](../../kernel/core/kernel_compositor.cpp#L39) | Contains a second 5x7 glyph table. **measureText**, window titles, and widget text use fixed glyph width and spacing, independently of **SystemFont**. |
| Bare-metal application drawing | [kernel_apps.cpp](../../kernel/core/kernel_apps.cpp#L442) | **drawChar** and **appDrawText** use a fixed 5x7 glyph table. The built-in File Explorer and the small built-in Settings UI use this path. Navigator has a local adapter to **SystemFont** for proportional text and retains the 5x7 path for monospace and fallback. |
| Bare-metal GXApp surface | [native_elf_baremetal.cpp](../../kernel/core/native_elf_baremetal.cpp#L157) | The host **draw_text** callback paints through **BitmapFont::DrawStringToBuffer**, bypassing **SystemFont**. |
| Kernel framebuffer console | [fb_console.h](../../kernel/core/include/kernel/fb_console.h#L47), [fb_console.cpp](../../kernel/core/fb_console.cpp#L16) | Embedded 8x16 printable-ASCII glyphs, fixed character cells, cursor and scrolling. It is intended for console/diagnostic text. |
| Bootloader framebuffer console | [fb_console.h](../../guideXOSBootLoader/fb_console.h#L5), [fb_console.cpp](../../guideXOSBootLoader/fb_console.cpp#L21) | Separate embedded 8x16 ASCII renderer, fixed cells and no dynamic allocation after boot services exit. |
| ARM64 graphics text | [graphics.cpp](../../kernel/arch/arm64/graphics.cpp#L32) | Architecture-specific 8x16 ASCII bitmap. |
| MIPS64 graphics text | [graphics.cpp](../../kernel/arch/mips64/graphics.cpp#L112) | Drawing functions are stubs; there is no built-in glyph renderer there. |
| Serial consoles | Architecture serial console sources | Emit bytes to serial devices; they do not use framebuffer fonts. Keep this path available for recovery diagnostics. |

**framebuffer.cpp** provides pixel and rectangle primitives, not a universal text API. **SystemFont::DrawTextToBuffer** is the shared atlas-to-buffer renderer. The paths above show that compositor and application layers still own several separate text policies.

### 2.3 File formats, selection, and build dependencies

**Verified:** the repository contains Roboto PNG atlases and four Roboto **.ttf** assets under **assets/Fonts/roboto**. The runtime references and [font generator](../../tools/generate_system_fonts.py#L7) use PNG files; no code reference to the TTF assets, **.otf**, or the two **.btf** assets was found. The generator embeds PNG alpha, not outlines.

**Verified:** **ImageAdapter** maps the current **/system/fonts/roboto/*.png** paths to repository assets in hosted mode. Bare-metal mode instead uses generated C++ arrays. This is a packaged bitmap asset arrangement, not a user font directory or discovery service.

**Verified:** no TrueType/OpenType parser or font rasterization library is currently referenced. **third_party/stb/stb_image.h** is an image decoder, not a font engine. There is no font-family setting or global **SystemFont::Set...** selection API. Current style requests can choose a bounded size, bold/regular, italic/normal, and a legacy monospace family; they cannot select an installed family.

**Verified:** the repository’s **guideXOSServer.csproj** targets .NET 9 and runs the kernel build before the managed build, but the checkout contains no application **.cs** source beyond generated files under **obj**. The managed application UI integration path is therefore not present to audit.

## 3. Application integration matrix

“Hosted” here means the C++ desktop/service build that sends drawing requests to the hosted compositor. “Bare-metal” means the freestanding kernel and its built-in application host.

| Application or surface | Hosted path | Bare-metal path | FONT2 integration consequence |
|---|---|---|---|
| Desktop shell, taskbar, window title bars | [compositor.cpp](../../compositor.cpp#L4723) draws titles and desktop text through **SystemFont**, with some legacy bitmap text helpers. | **desktop.cpp** uses **draw_text** and therefore the scale-1 **SystemFont** route when available; the separate [kernel compositor](../../kernel/core/kernel_compositor.cpp#L1086) retains fixed 5x7 drawing. | Update shell text and measurements together; do not leave one title or widget renderer using a separate width model. |
| Native widgets | The hosted compositor renders ordinary text and **DrawTextAtStyled** items through **SystemFont**; styled requests can select only proportional/Roboto or legacy monospace. | Kernel-compositor widgets and built-in app controls use fixed 5x7 drawing. | Native controls need a default/inherited font request that resolves centrally. |
| File Explorer | [file_explorer.cpp](../../file_explorer.cpp#L1431) publishes text messages that the hosted compositor renders. | The built-in File Explorer in [kernel_apps.cpp](../../kernel/core/kernel_apps.cpp#L4771) uses **appDrawText**, the fixed 5x7 path. | Hosted change would not cover bare-metal Explorer until its drawing and measurement adapters move. |
| Settings | [SettingsCenter](../../settings_center.cpp#L486) renders the hosted category and content UI through compositor requests; its text uses the hosted compositor font path. | The built-in Settings surface in [kernel_apps.cpp](../../kernel/core/kernel_apps.cpp#L16987) is a separate, reduced System/Display/About UI drawn through **appDrawText**. | Add a Fonts route in the hosted Personalization page model and decide whether the reduced kernel Settings surface is upgraded in the same integration phase. |
| Navigator | [navigator.cpp](../../navigator.cpp#L1398) sends styled text requests; the hosted compositor selects its bitmap face and scale. Existing CSS styles include size, weight, slant, and generic family. | [kernel_apps.cpp](../../kernel/core/kernel_apps.cpp#L899) selects and scales **SystemFont** faces for proportional text; monospace/fallback use **appDrawText**. Text wrapping and hit positions are computed locally. | Keep CSS family selection separate from the system default; share scalar decoding, metrics, and layout instead of adding another rasterizer in Navigator. |
| Notepad | [notepad.cpp](../../notepad.cpp#L145) sends positioned text messages to the hosted compositor. | The repository has no separate kernel Notepad renderer source; native applications use kernel app host callbacks and/or their provided app surface. | The hosted compositor will pick up inherited system defaults; verify the chosen bare-metal app ABI route when implementing. |
| Console window | [console_window.cpp](../../console_window.cpp#L333) sends text to the hosted compositor. | Kernel framebuffer and serial consoles have distinct paths; framebuffer console text is fixed 8x16 and serial output has no font. | Keep early and recovery consoles on a known fallback; apply user selection only to the desktop console window. |
| Native ELF/GXApp drawing API | Hosted application requests are presented by the hosted compositor. | The bare-metal callback draws directly with **BitmapFont**, as shown in [native_elf_baremetal.cpp](../../kernel/core/native_elf_baremetal.cpp#L157). | Route inherited app text through the shared manager while preserving an explicit style override where the ABI supports it. Use opaque integer handles if a future ABI needs them. |
| Managed UI | No checked-in managed UI source was found. | No managed runtime UI surface was found. | Define any future managed bridge over a small C-compatible request structure; do not expose parser pointers or C++ objects. |

### Shared rendering versus application-specific work

- Hosted applications commonly publish **MT_DrawText**, **MT_DrawTextAt**, color, or styled-text messages. The compositor owns final drawing and is the best first integration point for hosted applications.
- Bare-metal applications frequently draw into framebuffer surfaces directly. **appDrawText**, **desktop_font**, **kernel_compositor**, and the GXApp callback must be migrated or adapted explicitly.
- Navigator already has application-level font-size and generic-family parsing. Its layout currently treats UTF-8 as bytes in width and character-position loops, so a bitmap-only renderer change would not provide Unicode-correct wrapping or hit testing.
- Boot and panic output should keep deterministic built-in glyphs and serial output. User-installed fonts must not become a dependency for reaching Settings or diagnosing a failed font load.

## 4. Existing limitations and technical risks

1. **Several owners:** **SystemFont**, two 5x7 tables, the 8x16 framebuffer consoles, architecture graphics, and direct native app rendering do not share one renderer.
2. **ASCII byte indexing:** the shared atlas accepts only printable ASCII and does not decode UTF-8. Unicode text is not a supported end-to-end contract.
3. **Different geometry:** fixed-width paths use character counts; **SystemFont** uses proportional advances. A single display-wide switch will change wrapping, clipping, button widths, title centering, and hit testing.
4. **Metrics and layout are incomplete:** the current atlas sums advances but has no kerning. Navigator has a separate width/wrapping/hit-test implementation. Baseline and line height vary by face; callers often still use fixed offsets.
5. **Hosted/bare-metal mismatch:** hosted can load italic and thin PNG atlases; bare-metal embeds only four regular/bold atlases and falls back for the other declared face keys.
6. **Unbounded lifetime and synchronization details:** hosted atlas alpha allocations are retained by static records with no cleanup. The **s_initialized** flag is not synchronized. The new manager needs explicit lifetime and access synchronization even if a particular renderer is presently single-threaded.
7. **Untrusted font input:** a user-installed font is attacker-controlled binary input. File-size bounds alone do not validate internal table offsets, arithmetic, glyph composites, or allocation counts.
8. **Heap budget:** the kernel has a compile-time fixed heap and custom allocation/free routines in [cxx_runtime.cpp](../../kernel/core/cxx_runtime.cpp#L381). Its size is architecture-dependent (32, 128, or 512 MiB in the current source), but this heap is shared with other kernel work. The font manager must have lower explicit per-file, per-face, and cache-byte limits and must handle allocation failure.
9. **Persistence split:** hosted settings use **display-options.cfg** and maintain compatibility with **desktop.json**; the bare-metal desktop loader currently uses **/desktop.cfg** and reads **/display-options.cfg** as a legacy option path. A font setting needs one documented schema and migration rule.
10. **Asset-license provenance:** no license file was found beside the checked-in Roboto TTF assets. Google Fonts currently labels its Roboto family as OFL, but the exact provenance/version of the repository’s static files has not been established in this audit. Preserve the applicable font license and notices with the assets when that provenance is verified.
11. **OpenType scope:** glyph mapping and outline rasterization do not provide script shaping, bidirectional layout, combining-mark positioning, or ligature handling by themselves. Those require a separate shaping/layout decision.
12. **Text buffer semantics:** most current APIs take NUL-terminated strings or a byte length. A shared API should use an explicit byte span and return bounded output counts so malformed or oversized text cannot drive unbounded scanning or layout.

## 5. Rasterizer evaluation

No candidate was imported or built in FONT1. The kernel compile settings in [kernel/Makefile](../../kernel/Makefile#L99) use C++14, freestanding mode, no exceptions, no RTTI, no standard C++ headers, and no hosted runtime. A candidate must pass those actual target settings and the intended architecture link, not only a hosted test.

| Candidate | Fit for guideXOS | Risks and work |
|---|---|---|
| **stb_truetype** | Compact C/C++ single header. Provides font metrics, codepoint-to-glyph helpers, bitmap APIs, and rasterization. It has hooks for allocator, math, assertion, and string functions, so a freestanding adapter is possible. Its license offers MIT or public-domain/Unlicense choices. | Upstream’s header explicitly says it does not range-check font-file offsets, is not safe for untrusted font files, and still lists crashproof handling and non-Microsoft cmaps as TODOs. Default implementation includes hosted math/stdlib/assert/string headers. The caller must retain the source bytes after initialization; allocation-returning APIs need replacement or avoidance. It has no UTF-8 decoder or full text shaping. Do not deploy the unmodified parser for Settings-installed fonts. |
| **FreeType** | C API and broad font-format support. Its documented custom **FT_Memory** supports allocator callbacks, and **FT_Open_Face** supports a caller-provided stream or memory source. The application can keep the font bytes under manager ownership. Its module configuration can be narrowed to reduce unnecessary formats. The project’s freestanding C toolchain can compile C sources in principle. The FreeType license offers FTL or GPLv2-or-later. | It is a multi-file dependency with module/configuration and build integration cost. Default initialization uses library allocation; a custom library instance and memory manager must be wired and bounded. More code does not make hostile inputs automatically safe; retain fuzzing, bounds, and update obligations. The FTL requires acknowledgement in distribution documentation; licensing choice must be recorded. A freestanding kernel-target build has not been demonstrated here. |
| **Win32 GDI** | Hosted-only fallback option already available in the HDC surface. | Cannot serve bare-metal, boot, or native kernel rendering and would preserve two different system-font behaviors. Not a common font manager. |
| **Repository-native parser** | No current TTF/OTF parser or font rasterizer was found. Existing **.btf** files have no source references in the searched runtime code. | Writing a parser and rasterizer in-house creates the largest bounds, format coverage, and maintenance burden. Not recommended for FONT2. |

**Proposed decision:** evaluate FreeType first behind a narrow provider interface because user-installed font files are untrusted and the library exposes memory/input customization. Make it conditional on an actual freestanding kernel compile, bounded allocator prototype, and malformed-font campaign. If its code size or build footprint is unacceptable, compare a maintained, bounds-checked **stb_truetype** fork; do not accept the upstream parser unchanged. A font parser is only one stage: UTF-8 decoding, layout, fallback, and drawing remain guideXOS responsibilities.

External primary references used for this evaluation:

- [stb_truetype source and documented security/feature limits](https://github.com/nothings/stb/blob/master/stb_truetype.h#L2179)
- [stb license choices](https://github.com/nothings/stb/blob/master/LICENSE#L263)
- [FreeType custom memory-manager design](https://freetype.org/freetype2/docs/design/design-4.html)
- [FreeType face creation and memory/stream input](https://freetype.org/freetype2/docs/reference/ft2-face_creation.html)
- [FreeType license alternatives](https://github.com/freetype/freetype/blob/master/LICENSE.TXT)
- [Google Fonts Roboto family metadata](https://github.com/google/fonts/blob/main/ofl/roboto/METADATA.pb)

## 6. Proposed font manager architecture

### 6.1 Responsibilities and boundaries

The manager should coordinate separate components instead of combining parsing, layout, and framebuffer drawing into one renderer.

1. **Font catalog and loader**
   - Enumerate only approved font directories through VFS.
   - Check regular-file type, bounded file size, signature, table directory, and checked table spans before handing data to a provider. Treat extension as a hint only.
   - Record family/style names, face index, supported character map, and stable family ID in fixed-capacity records.
   - Keep the source byte buffer alive as long as the provider face refers to it.
2. **Provider/parser**
   - Wrap FreeType or a later approved parser behind a narrow interface.
   - Return explicit status values and bounded metrics; never leak provider types into app APIs.
   - Use a bounded allocator/stream adapter and reject allocation requests beyond manager policy.
3. **Glyph rasterizer**
   - Rasterize one glyph at one pixel size to an 8-bit coverage mask in a caller-provided or manager-owned buffer.
   - Return bitmap width/height, left/top bearing, advance, and baseline-relative placement.
   - Do no VFS reads, family selection, UTF-8 scanning, or direct framebuffer painting.
4. **Text layout**
   - Decode a bounded UTF-8 byte span to Unicode scalar values, rejecting overlong sequences, invalid continuation bytes, surrogates, and values above U+10FFFF.
   - Map scalars through the active face and fallback chain; compute advances, kerning when supported, line height, baseline, wrapping, and positioned glyph runs.
   - Keep shaping separate. Initial TTF support may cover individual mapped glyphs and simple Latin text; complex scripts and bidi need a later shaper.
5. **Drawing backend**
   - Blend a positioned coverage mask into a caller-provided clipped surface. Hosted HDC and kernel framebuffer surfaces can use separate backends over the same positioned run.
   - Retain no application buffers and do not read font files.
6. **Settings service**
   - Own the applied system selection and generation number.
   - Expose an inherited system-font request to apps; explicit app styling remains an optional override.

### 6.2 API shape

The following is a design sketch, not a committed interface. The public contract should be POD/C-compatible, fixed-capacity, and independent of the selected rasterizer.

    struct FontFamilyId { uint64_t value; };
    struct FontHandle { uint16_t slot; uint16_t generation; };
    struct FontRequest {
        FontFamilyId family;       // zero means current system family
        uint16_t pixelSize;
        uint8_t weight;
        uint8_t slant;
    };
    struct FontMetrics {
        int16_t ascentPx;
        int16_t descentPx;
        int16_t lineGapPx;
        int16_t baselinePx;
    };

    FontStatus RefreshCatalog();
    FontStatus Resolve(const FontRequest&, FontHandle*);
    FontStatus Measure(FontHandle, Utf8Span, TextMetrics*);
    FontStatus Layout(FontHandle, Utf8Span, LayoutBuffer*, uint16_t capacity,
                      uint16_t* written);
    FontStatus Rasterize(FontHandle, uint32_t glyphIndex, GlyphBitmapBuffer*);
    FontStatus SetSystemSelection(const FontSelection&, uint32_t* newGeneration);
    void Release(FontHandle);

**FontHandle** is an opaque slot plus generation, not a pointer and not a persisted identifier. **FontFamilyId** is stable across a process restart; the installation catalog can derive it from content identity or store a catalog ID. Persistent settings must never contain a runtime pointer, slot, or generation. Use explicit input byte lengths, output capacities, checked arithmetic, and status codes in every public call.

Initially preserve the existing **SystemFont** static methods as compatibility wrappers around the manager for call-site migration. Avoid putting **std::string**, **std::vector**, RTTI, exceptions, or library-private structures in the kernel-facing API. The hosted implementation may adapt to STL internally, but the contract and kernel implementation must remain freestanding-safe.

### 6.3 Ownership, limits, and lifetime

**Proposed ownership model:**

- The catalog owns each loaded face record and its provider instance. The manager owns the backing file bytes or a pinned VFS source for the whole provider-face lifetime.
- A face is not unloaded while selected as the system face or while a caller holds a valid handle. Use fixed face slots with generation counters and a small retain/release count if rendering can outlive a settings update.
- The glyph cache owns rasterized alpha masks. It is fixed-capacity with a hard byte budget and deterministic LRU/clock replacement; cache keys include family/face, glyph ID, pixel size, and rasterization mode.
- The text run buffer and drawing surface are borrowed from the caller. Layout writes no more than the supplied capacity and returns a truncation/overflow status rather than allocating an unbounded run.
- Keep the built-in bitmap font in static read-only storage. It is always available before VFS initialization, after font-load failure, and for recovery consoles.

Every limit must be explicit and derived from the smallest supported target’s RAM budget: maximum font bytes, table count, family count, face count, family-name bytes, glyph count, bitmap dimensions, per-glyph bytes, total cache bytes, layout glyphs, and fallback-chain length. Use checked addition/multiplication before allocation or indexing. The fixed kernel heap is available through the custom runtime, but it is shared; allocation failure must return an error without leaving partial face state.

For replacing an active font, build and validate the candidate face first. On failure, keep the prior selection and generation intact. On success, persist the selection, atomically publish a new system-font generation, invalidate dependent layout/cache state, and retire old faces after their last handle is released. If persistence or apply fails, restore the previous selection. Missing or corrupted persisted fonts resolve to the built-in bitmap fallback and emit one bounded diagnostic.

### 6.4 Cache and invalidation

- Cache glyph coverage masks rather than reparsing outlines on each draw.
- Bound the cache by total bytes as well as entry count; large glyphs must not evade the entry limit.
- Reuse an entry only when family ID, face index, glyph ID, pixel size, and rasterizer options match.
- Increase the system selection generation on a successful apply. Text layout caches and UI measurements carry the generation and are discarded when it changes.
- Keep the font-data generation separate from the UI selection generation so an install/remove invalidates catalog state without falsely changing unrelated UI settings.
- On generation wrap, clear all dependent cache entries before reusing a generation value.

### 6.5 TTF validation and failure behavior

For initial **.ttf** support, validate the file from a bounded byte view before accepting it. Do not trust extensions, face names, table counts, offsets, or lengths. Use subtraction-form range checks (**offset <= fileSize** and **length <= fileSize - offset**) to avoid overflow. Set limits on table count, glyph count, name length, **loca/glyf** relationships, composite depth, bitmap dimensions, and allocation requests. A malformed table, missing required table, unsupported cmap, or exhausted cache returns a deterministic status and selects fallback for drawing.

Install should copy into a controlled font directory only after validation, using a temporary file plus atomic rename where the active VFS supports it. Scan and parse off the paint path. If an installed file changes or disappears, the old active face remains pinned until a valid replacement is committed or fallback is selected.

## 7. Settings and persistence design

### Verified Settings infrastructure

- **settings_presentation_core** already defines a Personalization category and a **PersonalizationBackground** target. Its fixed page catalog has a “Theme and appearance” entry but no font target. See [settings_presentation_core.cpp](../../settings_presentation_core.cpp#L7).
- Hosted [SettingsCenter](../../settings_center.cpp#L486) renders categories through the existing Settings presentation model and has a Personalization page flow. This is the natural location for **Settings → Personalization → Fonts**.
- The hosted option path uses [DisplayOptionsStore](../../display_options_store.h#L20) with the key-value file **display-options.cfg**; **display_options.cpp** loads/saves it and mirrors compatibility data to **desktop.json**. A few settings, such as the enhanced focus preference, read and write **desktop.json** directly.
- Bare-metal **desktop.cpp** currently loads **/desktop.cfg**, then **/display-options.cfg** as a legacy option path, and stores its display/personalization keys through a bounded VFS parser. The built-in bare-metal Settings surface is much smaller than hosted Settings.
- Settings presentation operations have bounded payload storage; family names and font file paths should not be transported in an operation payload when a stable catalog ID can be used.

### Proposed page behavior

Add a **Fonts** target under Personalization, searchable as “system font” and “font family”. The page should provide:

- Available family/style list from the font catalog, with clear fallback/unsupported status.
- Bounded pixel-size choices and a live preview.
- Install through the existing file picker, followed by validation and a clear error result.
- Apply, Revert, and Restore Defaults.
- Persistent selection by stable family ID and pixel size, not by pointer or transient handle.
- Immediate in-memory apply without OS rebuild or restart.
- Safe fallback to the built-in bitmap face if the configured file is missing, invalid, unsupported, or over a resource limit.

### Proposed persistence

Extend the existing display/personalization option model with a stable **systemFontFamilyId** and bounded **systemFontPixelSize** field. Version and validate the fields on load. Keep the font file itself in an approved installed-font directory; the settings file stores only the selected family identity and size.

Before implementation, settle the current dual-runtime storage adapter: hosted **display-options.cfg** with **desktop.json** compatibility, and bare-metal **/desktop.cfg** with legacy **/display-options.cfg** support. Prefer one canonical pair of schema keys and one parser/service, retaining an explicit migration path. Do not add a second font-only settings file.

The Settings page should stage a preview selection separately from the persisted active selection. If the preview fails, or the user selects Revert, restore the last committed family. Persist only after a successful apply; report save failure and keep the previous persisted setting as the startup choice.

## 8. Recommended FONT2–FONT8 phases

This ordering adjusts the roadmap to address current renderer and storage duplication before adding user fonts.

| Phase | Proposed scope | Exit criteria |
|---|---|---|
| **FONT2 — Text contract and provider decision** | Define UTF-8 span, glyph metrics, baseline, line-height, family ID/handle, fallback, and draw-surface contracts. Prototype FreeType with a bounded VFS stream and allocator under the actual AMD64 freestanding flags; compare a hardened stb fork only if needed. Decide file limits, initial Unicode scope, font directory, and canonical config migration. | Provider compiles and links in the actual kernel-target build; memory limits and malformed-input failure behavior are specified; no hosted-only runtime symbols remain. |
| **FONT3 — Font catalog and bounded loading** | Implement file discovery, validation, family metadata, stable IDs, handles, ownership, and deterministic fallback while keeping bitmap rendering. | Invalid files, oversized files, missing configured family, VFS errors, and allocation failure all preserve a usable bitmap default. |
| **FONT4 — TTF glyph metrics, rasterization, and layout** | Add glyph lookup/rasterization, shared metrics, basic UTF-8 decoding, bounded alpha cache, simple advances/kerning, and line layout. Keep shaping explicitly out of scope for the first profile. | Measure, wrap, and draw use identical glyph advances and baseline data; every buffer/cache has a hard limit. |
| **FONT5 — Shared renderer migration** | Migrate hosted compositor, framebuffer desktop, window decoration, kernel compositor, **appDrawText**, and native app text callbacks behind the shared layout API. Keep serial and pre-VFS framebuffer consoles on built-in bitmap output. | Desktop, taskbar, title bars, native widgets, and both app-host modes resolve inherited system text through the manager. |
| **FONT6 — Settings and live apply** | Add Personalization → Fonts route, family list, size, preview, install, Apply/Revert/Restore Defaults, persisted selection, generation updates, and rollback on failure. | Apply changes current UI without reboot; Revert and restart restore the right choice; corrupt/missing config falls back safely. |
| **FONT7 — Application integration and coverage** | Migrate File Explorer, Settings, Navigator, Notepad, desktop shell, taskbar, native apps, and any newly introduced managed UI. Add fallback chains and app-level inherited/explicit style rules. Add shaping only after selecting a separate text-shaping component. | App text and its measurement/hit testing agree; unsupported glyphs resolve predictably through the fallback chain. |
| **FONT8 — OpenType expansion and release hardening** | Add selected OpenType face support after TTF stability, expand cmap/fallback coverage, decide shaping/bidi strategy, run malformed-font and resource-exhaustion campaigns, verify license notices and packaging, and document update/rollback policy. | Supported-format matrix, resource budgets, license notices, security tests, and recovery behavior are release-ready. |

## 9. Known unknowns and decisions for FONT2

1. Which supported architectures must load user fonts in the first release, and what is the minimum memory budget for the font pool/cache?
2. Is the intended first-release text scope ASCII, Latin plus common symbols, or Unicode scalar coverage without shaping? Complex-script, combining-mark, and bidirectional behavior needs an explicit product boundary.
3. Is FreeType’s configurable module footprint acceptable, or should the project maintain a hardened subset/fork of a smaller parser?
4. What install path is writable and protected by the existing user/permission model? Should fonts be system-wide or per-user? The current font work has no such policy.
5. Which option file is canonical in the bare-metal build: **/desktop.cfg** or the shared **/display-options.cfg** adapter? Hosted has its own **desktop.json** compatibility path.
6. Does applying a system font update currently open windows in place, or should each UI surface redraw at the next compositor frame? Existing surfaces have separate invalidation mechanisms.
7. The exact source/version/license notice for the checked-in static Roboto TTF files has not been established. The upstream Google Fonts family metadata currently lists Roboto as OFL; retain the exact asset provenance before redistributing or replacing these files.
8. There is no checked-in managed UI source now. Any future managed UI should consume the same C-compatible text contract as native apps.
9. The current **SystemFont** initialization and atlas ownership are process-lifetime/simple-static patterns. The new manager must establish synchronization and retirement rules for the actual hosted and kernel concurrency models.

**Roadmap adjustment:** do not begin with only a TTF rasterizer. FONT2 should first make the shared text and settings contracts explicit, select a parser only after freestanding and hostile-input checks, and name the canonical font/config locations. Otherwise the implementation would add a third font path while leaving current application and metric mismatches intact.

## 10. Validation and baseline status

### Checkout and source audit

- Confirmed the requested branch and starting HEAD before inspection; the starting worktree was clean.
- Inspected the shared system font API/implementation, generated atlases, bitmap fallback, desktop font wrapper, both compositor paths, framebuffer and bootloader consoles, ARM64/MIPS64 graphics text, application drawing paths, Settings presentation and persistence, VFS-facing bare-metal options, native app host callbacks, kernel build flags, font assets, and managed project inventory.
- No QEMU guest was launched.
- No production source, application, configuration, font asset, or third-party dependency was changed.
- No test was added.

### Baseline commands

| Command | Result | Interpretation |
|---|---|---|
| **mingw32-make ARCH=amd64** from **kernel/** | **Blocked before compilation.** The Makefile stops because the Mbed TLS dependency/profile is incomplete or generated files are missing; **third_party/mbedtls/include/mbedtls/version.h** is absent in this checkout. | Existing environment/dependency baseline, not a font change. No kernel compile result is claimed. The actual freestanding kernel target still needs a successful run in a provisioned checkout before a FONT2 provider is accepted. |
| **powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-settings-presentation-core-test.ps1** | **PASS — 2647/2647.** | Hosted Settings presentation model baseline. This does not replace the kernel-target compile and does not test font rendering. |

The overall outcome is **substantially completed with a validation limitation**: source inspection, design, and the Settings baseline succeeded; the actual AMD64 kernel build could not pass its existing Mbed TLS dependency preflight. This failure is recorded separately from the font findings.

## 11. Source inventory

Primary files reviewed:

- Font API and renderers: [system_font.h](../../kernel/core/include/kernel/system_font.h), [system_font.cpp](../../kernel/core/system_font.cpp), [bitmap_font.h](../../bitmap_font.h), [desktop_font.cpp](../../kernel/core/desktop_font.cpp), [kernel_compositor.cpp](../../kernel/core/kernel_compositor.cpp), [framebuffer.cpp](../../kernel/core/framebuffer.cpp), [fb_console.cpp](../../kernel/core/fb_console.cpp), [virtio_gpu.cpp](../../kernel/core/virtio_gpu.cpp).
- Applications and compositor: [compositor.cpp](../../compositor.cpp), [compositor.h](../../compositor.h), [gui_protocol.h](../../gui_protocol.h), [kernel_apps.cpp](../../kernel/core/kernel_apps.cpp), [native_elf_baremetal.cpp](../../kernel/core/native_elf_baremetal.cpp), [file_explorer.cpp](../../file_explorer.cpp), [settings_center.cpp](../../settings_center.cpp), [navigator.cpp](../../navigator.cpp), [notepad.cpp](../../notepad.cpp), [console_window.cpp](../../console_window.cpp).
- Settings and persistence: [settings_presentation_core.h](../../settings_presentation_core.h), [settings_presentation_core.cpp](../../settings_presentation_core.cpp), [display_options_store.h](../../display_options_store.h), [display_options.cpp](../../display_options.cpp), [desktop_config.h](../../desktop_config.h), [desktop.cpp](../../kernel/core/desktop.cpp).
- Runtime/build: [kernel/Makefile](../../kernel/Makefile), [cxx_runtime.cpp](../../kernel/core/cxx_runtime.cpp), [guideXOSServer.csproj](../../guideXOSServer.csproj), [generate_system_fonts.py](../../tools/generate_system_fonts.py).
