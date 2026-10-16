# FONT2 — Shared Font Contract and Bitmap Backend

**Status:** Outcome A — FONT2 contract and bitmap backend accepted

**Branch:** `FONT_SUPPORT`

**Starting HEAD:** `27d67f113b99bad5878600325efc2ed387777295`

FONT2 establishes a freestanding-compatible interface over the existing built-in 5×7 bitmap font. It does not add TrueType parsing, font-file discovery, font persistence, shaping, or a system-wide renderer migration. FONT1's source findings and recommendations are recorded in [FONT1_INFRASTRUCTURE_AUDIT.md](FONT1_INFRASTRUCTURE_AUDIT.md).

## Implemented contract

The public POD interface is in `kernel/core/include/kernel/font_contract.h` and its bounded manager and text operations are in `kernel/core/font_contract.cpp`.

- `FontId` is a stable 64-bit identity. `FontHandle` is a runtime-only registry slot and generation pair; it is not a persisted identifier.
- `FontSize` explicitly requests a pixel height. The bitmap backend supports height 7 and returns `UnsupportedSize` for other sizes.
- `UnicodeCodePoint`, `GlyphMetrics`, `FontMetrics`, `FontGlyphView`, and `TextMeasureResult` distinguish Unicode scalars, font-specific glyph IDs, bitmap geometry, advances, and whole-text measurements.
- Backend capability flags describe glyph lookup, bitmap data, measurement, UTF-8 text input, and monospace behavior. The measurement loop always sums each glyph's supplied advance; it does not rely on the monospace capability.
- `FontStatus` distinguishes invalid input, invalid handles, unsupported size, invalid UTF-8, bounded-input rejection, arithmetic overflow, backend failure, and successful fallback.

The manager has four fixed registry slots and performs no allocation. Initialization registers the built-in bitmap font and selects it as default. Repeated initialization is idempotent. `FontManagerShutdown` clears registered records and invalidates handles; a later initialization produces a new handle generation. The manager generation starts at 1, changes when the default selection changes, and advances on shutdown. Setting the already-selected handle does not change it. Unknown stable font IDs resolve to the built-in bitmap handle with `FallbackUsed`; invalid or stale handles return an error.

The registry copies each `FontDefinition`, but borrows its backend table, context, and debug-name pointer. Callers must register immutable process-lifetime storage. The manager does not own or free backend data. Its small internal spin lock serializes manager operations; call manager APIs only from task context, and backend callbacks must not re-enter the manager. The built-in backend points directly to `BitmapFont::Glyph`'s immutable static table.

The freestanding implementation uses fixed static records and has no STL containers, exceptions, RTTI, heap allocation, or hosted C++ runtime dependency.

## Bitmap backend and compatibility

`kernel/core/font_bitmap_backend.cpp` adapts `BitmapFont` without editing or copying its original glyph table. It describes column-packed, least-significant-bit-at-top glyphs with a 5×7 bitmap, 6-pixel advance, ascent 7, descent 0, baseline 7, and line height 7. These dimensions are properties of this bitmap backend, not assumptions in the shared text contract.

Unsupported Unicode scalars use the built-in question-mark glyph and set `fallbackUsed`; unsupported code points do not cause an unchecked table access. `FontDrawTextToBuffer` supports the bitmap backend's `ColumnLsbTop` representation and clips to a caller-provided surface. Other bitmap formats return `BackendFailure`; a later rasterizer needs its own drawing adapter.

The selected production integration is the bare-metal GXApp `draw_text` callback in `kernel/core/native_elf_baremetal.cpp`. It initializes the manager, draws through the shared contract at the existing 7-pixel size, and keeps the original direct bitmap call as a fallback if the manager is unavailable. The form-feed clear operation remains intact. Boot, framebuffer-console, serial, and recovery text paths remain independent of manager availability.

The hosted compatibility test compares the new renderer's complete pixel buffer against `BitmapFont::DrawStringToBuffer` for printable ASCII. The existing glyph table is also checked byte-for-byte through the backend view.

## UTF-8 and measurement rules

Span-based text APIs take an explicit byte length and process at most 4096 bytes without allocating. The C-string helper scans at most 4096 bytes including the terminator, so it accepts at most 4095 text bytes. The decoder rejects invalid lead bytes, invalid continuation bytes, truncated sequences, overlong encodings, surrogate values, and values above U+10FFFF. On invalid UTF-8, measurement returns `InvalidUtf8`, reports the offending sequence's starting byte offset, and leaves the other result fields zero.

`TextMeasureResult` reports:

- `byteLength`: input bytes.
- `codePointCount`: decoded Unicode scalars, including each CR and LF in a CRLF pair.
- `glyphCount`: drawable scalars after excluding line breaks; a missing glyph resolved to `?` counts as one glyph.
- `fallbackGlyphCount`: number of missing-glyph substitutions.
- `maxLineAdvancePx` / `pixelWidth`: widest line's sum of glyph advances.
- `lastLineAdvancePx`: final line's sum of advances.
- `lineCount` and `pixelHeight`: line count and line count multiplied by backend line height.

LF, CR, and CRLF are line breaks; CRLF counts as one break. Tabs and other unsupported controls are ordinary unsupported scalars and resolve to `?`. Empty input is one empty line with width 0 and the backend's line height. All additions and line-height multiplication are checked; overflow returns `ArithmeticOverflow`. No kerning, shaping, combining-mark placement, bidirectional layout, or complex-script behavior is claimed.

## Validation

- `scripts/run-font-contract-test.ps1` — **PASS, 78 checks**. Covers metrics, glyph access and bitmap compatibility, widths/heights, empty input, unsupported scalars, UTF-8 errors, byte bound, overflow, invalid/stale handles, bounded registry behavior, bounded C-string scans, initialization, fallback, selection generation, and drawing output.
- `scripts/run-settings-presentation-core-test.ps1` — **PASS, 2647/2647**.
- `scripts/run-native-present-validation-test.ps1` — **PASS**.
- `scripts/run-native-abi-layout-test.ps1` — **FAIL, stale ABI size pin:** expected `sizeof(gx_host_calls) == 248`, MinGW reports 256. The SDK ABI header already appends `get_document_activation_path` after `play_pcm`, while the layout test still pins the pre-append size. FONT2 does not edit either file or change the application ABI. The native present validation suite passes.
- `mingw32-make ARCH=amd64` from `kernel/` — blocked before compilation at `kernel/Makefile:77`: `guideXOS Mbed TLS dependency/profile is incomplete or generated files are missing; run ../scripts/bootstrap-mbedtls.ps1 -Install`. `third_party/mbedtls/include/mbedtls/version.h` is absent in this checkout.
- Independent AMD64 kernel-target compilation — **PASS** for `core/font_contract.cpp`, `core/font_bitmap_backend.cpp`, and the modified `core/native_elf_baremetal.cpp`, using the freestanding C++14 flags from `kernel/Makefile` and the AMD64 flags from `kernel/arch/amd64/Makefile.arch` (`-ffreestanding -nostdlib -nostdinc++ -fno-exceptions -fno-rtti`, AMD64 large code model, no red zone/SSE/PIC/stack protector). The native ELF translation unit emitted warnings in unrelated fault-recovery declarations, key-event conditionals, and an unused helper; all three translation units compiled successfully. This validates FONT2 compilation but does not claim a full kernel link.
- No full kernel image was produced and no QEMU guest was launched. Kernel boot validation remains unperformed; no QEMU process was inspected, attached to, or reused.

Outcome A is recorded because the font contract and integration compile under the actual AMD64 freestanding target flags and the FONT2 tests pass. The full kernel image remains blocked by the incomplete Mbed TLS profile, and the native ABI layout test remains a separate checkout/toolchain discrepancy.

## FONT3 status and FONT4 handoff

FONT2 remains a historical record of the shared bitmap contract. FONT3's bounded TrueType loader, metadata-only registration, owned source lifetime, and validation outcome are documented in [FONT3_TRUETYPE_LOADING_AND_VALIDATION.md](FONT3_TRUETYPE_LOADING_AND_VALIDATION.md). The additional metadata pointer and unregister callback in `FontDefinition`, plus per-font unload, extend the contract described above.

The bitmap face still supports printable ASCII directly, maps other valid Unicode scalars to `?`, supports only a 7-pixel size, and has fixed advances. Manager operations remain task-context operations without an interrupt-safe API; the shared bitmap drawing helper handles only the documented column-LSB format. The accepted FONT4 per-glyph decoder and rasterizer are documented in [FONT4_SCALABLE_GLYPH_RASTERIZATION.md](FONT4_SCALABLE_GLYPH_RASTERIZATION.md). FONT4 preserves this bitmap face as the default and adds no glyph cache; cache and renderer migration remain future work.
