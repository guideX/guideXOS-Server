# App Model Phase 13: ImageViewer JPEG Migration

Date: 2026-10-02

Outcome: **A — `.jpg` and `.jpeg` are canonical hosted ImageViewer capabilities.**

## Starting checkout

- Repository: `D:\dev\guideXOSServer`
- Branch: `main`
- Starting HEAD: `e66ce565820ab366c09905fe36983c7da4916e63`
- Starting cached upstream divergence: 0 behind / 1 ahead (as recorded before Phase 13 changes)
- Phase 11 contract reviewed: [ImageViewer PNG document activation](APPMODEL_PHASE11_IMAGEVIEWER_DOCUMENT_ACTIVATION.md)

The pre-existing `desktop.json` edit and existing temporary fixtures/logs were preserved and excluded from this change.

## Discrepancy and production path

JPEG decoding was already present in the shared hosted `ImageAdapter::LoadFromFile` path. The decoder was neither test-only nor missing from the hosted build: the adapter read file contents through bounded VFS access and selected the existing JPEG loader. The production ImageViewer entry point was the disconnect. Its local `isPngPath()` gate rejected `.jpg` and `.jpeg` at three points: initial activation, the Open dialog, and folder image enumeration. The UI and success log were also PNG-only.

The repair makes the ordinary ImageViewer path recognize `.png`, `.jpg`, and `.jpeg`, then keeps using the one shared loader and its owned RGBA `Image` state. There is no AppModel-specific JPEG loader, File Explorer-specific decoder, or second JPEG implementation. The PNG-only wallpaper action retains its PNG-specific check. Decoder content validation remains downstream of extension-based AppRegistry resolution, so a corrupt `photo.jpg` can activate ImageViewer and then fail safely in the loader.

The runtime smoke activated actual JPEG fixture files through File Explorer and the generic built-in document dispatcher. The log confirms the exact mixed-case/nested virtual path, successful ImageViewer load with dimensions, and compositor window ownership. The adapter test also checked owned RGBA state and a deterministic pixel hash (`8adec27d637a1e32`). No native-window screenshot was available; visual appearance is not claimed.

## JPEG formats, metadata, and safety

The existing shared `STBI_ONLY_JPEG` decoder was retained. Code inspection and fixtures cover baseline sequential, progressive, grayscale, and common 4:4:4 / 4:2:2 sampling; production activation additionally opened tracked baseline and progressive 4:2:0 fixtures. This is evidence for those exercised forms, not a promise of universal JPEG compatibility. EXIF APP1 markers are skipped as unrecognized metadata; metadata is not parsed and EXIF orientation is not applied.

The shared ImageViewer bounds remain:

- encoded input: 4 MiB;
- width and height: 4096 each;
- decoded pixels: 16,777,216;
- RGBA output: 64 MiB.

JPEG workspace estimation is bounded before decode (baseline estimate 3 bytes per pixel, progressive estimate 6 bytes per pixel, plus 256 KiB); size arithmetic uses checked 64-bit calculations. Failed decode output is released and never replaces the owned image with partial pixels. The file-adapter regression verified failure classification and rejection of BMP/GIF. The focused runtime probes verified missing, empty, corrupt, truncated, incomplete-header, over-dimension, and oversized-file cases without stale-image reuse or process failure.

## App Model migration

- AppRegistry associates `.jpg` and `.jpeg` with `gxos.builtin.imageviewer`; suffix lookup remains case-insensitive.
- ImageViewer is each extension's built-in and effective default; no persistent override is written.
- File Explorer normal Open resolves through AppRegistry and the existing owned activation context.
- Open With enumerates ImageViewer as a capable handler through the generic Phase 7 mechanism.
- Settings Default Apps discovers both extensions from the standard known-extension/default enumeration.
- Both extensions were removed from the legacy direct ImageViewer route. There is no dual authoritative route.

The Phase 13 runtime smoke covered `.jpg`, `.JPEG`, and `.JpG` through normal Open and `.jpeg` through Open With; dispatcher tests also covered `.jpg`, `.JPG`, `.JpG`, `.jpeg`, `.JPEG`, and `.JpEg` resolution and default selection.

## BMP and GIF audit

Neither format has a production decoder in the hosted ImageViewer adapter: PNG and JPEG are explicitly selected decoders, and BMP/GIF inputs are rejected as unsupported. The remaining legacy File Explorer table still routes `.bmp` and `.gif` to ImageViewer, which overstates capability and leads to an unsupported-format result. The exact remaining legacy image fallback is `.bmp` and `.gif`; their current association is legacy routing, not proof that they open.

Phase 14 should first remove these misleading legacy associations unless a separate, bounded product decision chooses and implements a decoder. Do not imply either format is supported by retaining a route alone. GIF animation and broad BMP support are outside this phase.

## Regression and build evidence

All commands below completed successfully unless specifically marked otherwise.

| Area | Evidence |
| --- | --- |
| Phase 13 production JPEG runtime | 18/18; normal Open 3/3, Open With 1/1, failure probes 7/7, Default Apps rows 2/2 |
| JPEG codec/adapter | 56/56; deterministic pixel hash `8adec27d637a1e32`; BMP/GIF rejected |
| Built-in dispatcher | 21/21; PNG lifecycle 100/100; Navigator lifecycle 100/100; JPEG lifecycle 100/100 |
| Phase 11 ImageViewer PNG runtime | 20/20; normal Open 3/3; Open With 1/1; eight safe failure probes |
| Image adapter bounds | 10/10 |
| Phase 12 Navigator hosted runtime | 11/11, including local relative navigation, Back, Reload, inline CSS, relative PNG, Open With, and missing-file behavior |
| Phase 10 Developer Studio runtime | 12/12; five process/window close cycles; generic Open With and ordinary `.CPP` Open retained |
| Phase 9 Settings Default Apps model | 38/38; interactions 9/9; lifecycle 100/100 |
| Settings model/navigation regressions | Apps/Default Apps checks 89/89; device model 21/21; storage 24/24 |
| Phase 8 default persistence | Covered by default-handler model and persistence/reload 100/100 |
| Phase 7 Open With | 4/4; 20/20 menu activations |
| Phase 6 activation | File Explorer 7/7; Notepad 29/29; negative 3/3; repeated 20 activations |
| Phase 5B closeout | Passed; App Model ready, 0 unresolved targets, 0 high-risk targets |
| Startup / Recent Programs | Passed controlled normal startup, explicit launches, and persistence/restart checks |
| Standard hosted build | Passed and was rebuilt by the startup regression |
| Experimental hosted build | Passed; Developer Studio Phase 10 runtime then passed |
| Normal ImageViewer QEMU smoke wrapper | Attempted; the PacMan Native ELF link failed on the two unresolved audio symbols below, so the wrapper did not launch QEMU |

The build output contains existing project and third-party warnings. The changed ImageViewer translation unit reports its existing unused `publishWindowText` helper; no new Phase 13 warning was identified. The standard build also reports existing warnings in GUI protocol formatting, unused helpers, stb, and optimized `trash.cpp` bounds analysis.

## Bare-metal and QEMU

Phase 13 changes are in hosted `image_viewer.cpp`, `app_registry.cpp`, `desktop_service.cpp`, hosted tests, scripts, and documentation. The shared kernel ImageAdapter/JPEG wrapper was inspected, but no source compiled into the bare-metal ImageViewer/App Model path changed. Therefore no focused bare-metal build was required as acceptance evidence. The normal ImageViewer QEMU smoke wrapper was attempted, but it stopped before launching QEMU because the PacMan Native ELF build failed to link the known unresolved symbols `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)`. This blocker is unrelated to the hosted JPEG path. No QEMU pass is claimed, and a PNG smoke would not prove bare-metal JPEG activation.

## Changed files

- `image_viewer.cpp`, `image_viewer.h`
- `app_registry.cpp`, `desktop_service.cpp`
- `tests/built_in_document_dispatcher_test.cpp`, `tests/jpeg_codec_test.cpp`, `tests/settings_default_apps_model_test.cpp`
- `scripts/smoke-appmodel-phase13-imageviewer-jpeg.ps1` and updated Phase 4B, Phase 11, and JPEG codec smoke scripts
- `docs/APP_MODEL_CURRENT_STATE.md`
