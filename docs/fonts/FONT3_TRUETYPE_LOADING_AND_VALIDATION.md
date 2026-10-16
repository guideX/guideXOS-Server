# FONT3 — Safe TrueType Loading and Validation

**Status:** Outcome A — bounded metadata loading accepted; glyph rendering remains out of scope

**Branch:** `FONT_SUPPORT`

**Starting HEAD:** `f4cfd7a500925cd56f0684ee90c26438061c9144`

FONT3 adds a bounded TrueType SFNT loading boundary to the FONT2 manager. It validates enough structure to extract metadata and register an owned source. It does not render glyphs, execute hinting instructions, or change the desktop's selected bitmap renderer.

## Supported input and trust boundary

The accepted outline signature is the standalone TrueType SFNT value `0x00010000`. A file must contain one face and the required `head`, `maxp`, `cmap`, `hhea`, `hmtx`, `loca`, `glyf`, and `name` tables. It must include a Unicode format 4 or 12 cmap subtable that passes the implemented bounded structural checks. Format 4 segment end values and start/end pairs are checked; format 12 groups are checked for range ordering, scalar limits, and glyph-ID bounds.

The loader rejects TTC collections (`ttcf`), CFF/OpenType signatures (`OTTO`), `CFF ` and `CFF2` outlines, and the recognized variable-font tables `fvar`, `gvar`, `avar`, `cvar`, `HVAR`, `VVAR`, and `MVAR`. Other optional tables are accepted only as bounded directory entries; they are not interpreted. Duplicate tags of any kind are rejected. Non-empty table ranges may not overlap each other or the offset-table/directory area. Every table offset must be four-byte aligned and every range must fit the source buffer.

The validator checks the SFNT offset-table search values, directory bounds, minimum required table sizes and versions, `head` magic and units-per-em, glyph and horizontal-metric counts, required `hmtx` size, monotonic `loca` entries bounded by `glyf`, name-record/string ranges, and supported cmap structure. Reads use checked bounds and checked size arithmetic. The directory uses a fixed 64-entry stack array. Parsing has no recursion.

This is a defensive metadata-loading boundary, not complete TrueType validation. It does not verify table checksums, parse glyph contours or composite glyph graphs, validate instruction streams or hinting behavior, or prove that future rasterization is safe. It does not promise that every cmap entry resolves to a meaningful glyph. A future rasterizer must validate the glyphs and instructions it consumes and enforce independent work, recursion, and allocation limits.

## Fixed limits

| Resource | Bound |
|---|---:|
| Source file | 192 KiB maximum |
| Tables per file | 64 |
| Faces per file | 1 |
| Glyphs | 16,384 |
| Name table records | 128 |
| Language-tag records in name format 1 | 128 separate from name records |
| Family/style source name string bytes decoded | 256 maximum |
| Cmap encoding records | 32 |
| Cmap format 12 groups | 4,096 |
| Cmap format 4 segments | 8,192 |
| Returned family or style name | 63 UTF-8 bytes plus NUL |
| Registered external sources | 3 |
| Persistent source storage | 576 KiB total (`3 × 192 KiB`), statically allocated |
| Parser recursion | 0 |

Table, name, cmap-record, cmap-segment, and cmap-group loops are bounded by those caps. The manager retains its four-slot FONT2 registry: the bitmap fallback occupies one slot, leaving at most three external fonts. No font parsing path uses dynamic allocation or variable-length stack storage.

## Metadata and fallback rules

The parser returns family and style names from name IDs 1/2 or preferred IDs 16/17 when available. It accepts Unicode platform and Windows Unicode records, validates UTF-16BE surrogate pairs, and emits bounded UTF-8. It validates name and language-tag counts separately; family/style source strings longer than 256 bytes are skipped. Malformed name-table string ranges invalidate the table; an individually undecodable or empty candidate is skipped. Missing or unusable family/style names become `Unknown` and `Regular` respectively. Filename text is never used as font identity.

The metadata record includes source byte length, table count, units-per-em, glyph count, horizontal metric count and maximum advance, ascender, descender, line gap, face count, and flags describing supported cmap formats/platform encodings. These flags describe observed records, not full language coverage or shaping support.

## Loading, ownership, and manager behavior

`FontTrueTypeValidate` borrows its input only for the call and returns no views into it. `FontTrueTypeLoadMemory` copies caller bytes into one of three fixed source records before validation. `FontTrueTypeLoadFile` requires a bounded NUL-terminated VFS path, stats a regular file, rejects empty or oversized files, reads the exact stat-reported length, then checks file type and size again before validation. The caller supplies the stable `FontId`; a path or filename is not an identity.

Successful validation registers a metadata-only backend. The manager stores a copied definition whose context and metadata point into the reserved source record; those pointers remain valid until unload or manager shutdown. `FontManagerGetTrueTypeMetadata` copies metadata under the manager lock. `FontTrueTypeLoadMemory` and `FontTrueTypeLoadFile` return a runtime slot/generation handle; an unload clears the owned source bytes and record. Unloaded handles stop resolving, and a later registration receives a new generation.

Any failed validation, capacity check, VFS read, or manager registration clears the reserved source record and leaves the registry unchanged. Metadata-only fonts return `MetadataOnly` for metrics and glyph lookup and cannot become the default. The built-in bitmap font cannot be unloaded, and the current default cannot be unloaded. Loading never selects a default, so the bitmap face remains the fallback before and after external registration. Unregister callbacks run while the manager lock is held and must not re-enter manager APIs.

FONT2's `FontDefinition` now has optional TrueType metadata and an unregister callback; `FontCapabilityMetadataOnly` identifies metadata-only registrations. The manager adds metadata retrieval and per-handle unload operations. The public TrueType interface and fixed limits are declared in `kernel/core/include/kernel/font_truetype.h`; VFS-backed and memory-backed entry points are declared in `kernel/core/include/kernel/font_contract.h`.

## Fixture license and provenance

The valid fixture is the existing `assets/Fonts/roboto/Roboto-Regular.ttf` file (146,004 bytes; SHA-256 `dbd285b518e398832f6f4a736109c355ce25a49546bfce41bab256c9ef7e56eb`). Its embedded name table records the Roboto Project Authors copyright, SIL Open Font License 1.1, and upstream repository. `assets/Fonts/roboto/OFL.txt` carries the complete license, and `NOTICE.txt` records attribution and hashes for all four bundled TTF variants. The files predate FONT3; their exact upstream binary release was not established, so no exact upstream version is claimed. The [Google Fonts Roboto metadata](https://github.com/google/fonts/blob/main/ofl/roboto/METADATA.pb) identifies the family as OFL and points to the Roboto Classic repository; the [Roboto OFL text](https://github.com/google/fonts/blob/main/ofl/roboto/OFL.txt) is the source for the included license.

## FreeType qualification

No FreeType code or dependency is introduced into production. The experiment pinned FreeType 2.14.3, resolving tag object `c740f0fda4274d6ffd2e5b64a25b06ef69803a07` to commit `0a0221a1347e2f1e07c395263540026e9a0aa7c7`. The module configuration retained the TrueType driver, SFNT, PostScript names, and raster modules, disabled the TrueType bytecode interpreter, and disabled optional zlib, bzip2, PNG, HarfBuzz, and Brotli support.

The experiment did not qualify for kernel integration. The upstream CMake build still selected the Windows `ftsystem.c`, which failed under the target's `-mno-sse` flags at `__builtin_ia32_sfence`; using the generic system port in the ignored experiment copy allowed a static archive to build. The CMake `FT_DISABLE_STREAM_SUPPORT` setting was not recognized, and CMake continued compiling its broad source set. A freestanding probe using `FT_New_Library`, `FT_Add_Default_Modules`, and `FT_New_Memory_Face` then failed to link because of unresolved setjmp/longjmp, stdio file I/O, heap allocation, string/memory routines, `qsort`, and `getenv`. Thus there is no demonstrated minimal memory-only link or fixed-allocation runtime on the current target.

FreeType provides custom `FT_Memory` allocation callbacks, but the target still needs an allocator with bounded, failure-safe behavior (including transactional `realloc`) and a compatible system/stream port. Its memory-face API borrows source bytes until the face is destroyed, so source ownership must outlive every face. These requirements are supported by the [FreeType system interface](https://freetype.org/freetype2/docs/reference/ft2-system_interface.html) and [face creation reference](https://freetype.org/freetype2/docs/reference/ft2-face_creation.html). The upstream [FreeType license](https://freetype.org/license) offers the FreeType License or GPLv2; license terms were reviewed, but no library is shipped by this change.

## Dependency and native qualification

The earlier FONT2 record reported that the AMD64 build stopped because Mbed TLS provisioning was incomplete and `third_party/mbedtls/include/mbedtls/version.h` was absent. The documented, pinned setup was run with `scripts/bootstrap-mbedtls.ps1 -Install -PythonPath python`; it refused to overwrite an existing destination, applied the repository's three tracked patch series, generated the required headers, and verified the profile. The restored revisions are Mbed TLS 4.1.0 at `0fe989b6b514192783c469039edd325fd0989806`, TF-PSA-Crypto 1.1.0 at `29160dd877d29658279fd683b2ae57b320ddcf09`, and Mbed TLS framework at `dff9da04438d712f7647fd995bc90fadd0c0e2ce`. The expected `version.h` and `build_info.h` are now present.

`mingw32-make ARCH=amd64` from `kernel/` **passes** and links `kernel/build/amd64/bin/kernel.elf`. The build produced target objects for `font_contract.cpp`, `font_bitmap_backend.cpp`, `font_truetype.cpp`, and `font_truetype_loader.cpp`. Independent compilation of those production translation units also passed with the kernel's C++14 freestanding options (`-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-exceptions`, `-fno-rtti`, `-fno-builtin`, and the AMD64 large-code-model/no-SSE/no-red-zone architecture flags). No QEMU boot was performed.

The existing `scripts/run-native-abi-layout-test.ps1` still fails its unchanged assertion that `sizeof(gx_host_calls) == 248`; the configured MinGW compiler reports 256. This discrepancy was present in the FONT2 record. FONT3 changes neither the SDK ABI declaration nor that assertion.

## Test evidence

| Command | Result |
|---|---|
| `scripts/run-font-truetype-test.ps1` | **PASS — 1,150 checks, 0 failures.** Includes the real Roboto fixture; malformed and truncated inputs; overflow and table-count bounds; missing/duplicate/overlapping tables; invalid names; CFF, TTC, and variable-font rejection; a fixed-seed (`0xF03`) 512-case mutation loop; VFS fixture loading, missing path, empty file, truncated fixture, and oversized file; source-copy lifetime; failed-load rollback; capacity exhaustion; repeated load/unload; stale handles; fallback preservation; and shutdown/reinitialization. |
| `scripts/run-font-contract-test.ps1` | **PASS — 78 checks, 0 failures.** |
| `scripts/run-settings-presentation-core-test.ps1` | **PASS — 2,647/2,647.** |
| `scripts/run-settings-center-model-test.ps1` | **PASS — 89/89.** |
| `scripts/run-native-present-validation-test.ps1` | **PASS.** |
| `scripts/run-native-abi-layout-test.ps1` | **Known baseline failure — 248 expected, 256 reported.** |

An AddressSanitizer/UndefinedBehaviorSanitizer hosted build was attempted. The configured MinGW toolchain lacks `-lasan` and `-lubsan`, so the test executable could not link with those runtimes. The regular deterministic hosted suite passed; sanitizer execution remains unavailable on this host.

## Outcome and FONT4 handoff

Outcome A is recorded: the actual Roboto TTF is accepted and its bounded metadata is registered; failed loads roll back; unload invalidates handles; the built-in fallback remains selected and protected; malformed-input tests pass; all new production translation units compile under the target flags; the full AMD64 kernel links; and FONT2 plus Settings regressions pass. No QEMU boot or sanitizer run is claimed.

FONT4 has completed that bounded glyph-provider and renderer work; its per-glyph validation, unhinted subset, limits, and test evidence are documented in [FONT4_SCALABLE_GLYPH_RASTERIZATION.md](FONT4_SCALABLE_GLYPH_RASTERIZATION.md). The bitmap fallback remains the default. Sanitizer execution was attempted for FONT4 but the configured MinGW libraries are unavailable. Cache ownership and invalidation, shared renderer migration, Settings selection, shaping, bidirectional layout, and broad language coverage remain separate future decisions.
