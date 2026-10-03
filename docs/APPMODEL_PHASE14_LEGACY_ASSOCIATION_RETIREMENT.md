# App Model Phase 14 — Legacy Image Association Retirement

Date: 2026-10-02
Status: Outcome A — hosted BMP/GIF associations retired and supported document routing audited.

## Goal

Retire hosted `.bmp` and `.gif` document routes because the production ImageViewer path cannot decode either format, then audit the File Explorer and DesktopService document-open boundary for handler selection outside AppRegistry.

## Decoder and route audit

Before this change, `FileExplorer::openSelected()` passed selected files to `DesktopService::OpenFilesystemEntry()`. That service first resolved registered AppRegistry document handlers, then used a Phase 4B fallback record for BMP/GIF to launch ImageViewer directly. The application route existed, but the format capability did not: `ImageViewer::isSupportedImagePath`, the production `ImageAdapter`, and the enabled PNG/JPEG loader translation units accept PNG/JPEG only. ImageViewer therefore rejected a BMP/GIF path before it could display content.

The source search found no production BMP/DIB loader and no GIF87a/GIF89a parser, LZW decoder, or animation path in ImageViewer. The vendored stb header contains dormant format code, but it is not enabled by the production PNG/JPEG loader translation units or called by ImageAdapter. Existing BMPs used by Windows/GDI UI resources and GIF assets used by unrelated content do not make either format a hosted ImageViewer document capability. No tracked BMP/GIF ImageViewer decoder fixture was found.

Phase 14 removes the BMP/GIF legacy table rows and the `FilesystemEntryLaunchTarget::ImageViewer` path, including its direct launch branch and legacy direct-path diagnostics. Unsupported files now resolve to `Unsupported` and ordinary Open returns the existing `No file association registered for <path>` error. AppRegistry does not declare `.bmp` or `.gif`. ImageViewer's Open filter and folder listing remain `.png`, `.jpg`, and `.jpeg` only.

The generic unknown/risky fallback classifications remain for safe diagnostics and rejection of unknown, executable, package, and ELF paths. They do not select an application. The bare-metal `ImgViewer` alias is application-launch compatibility metadata and is not a hosted document association or bypass.

## File Explorer and DesktopService routing audit

`FileExplorer::openSelected()` keeps directory navigation inside File Explorer. Selected files reach `DesktopService::OpenFilesystemEntry()`, where AppRegistry resolution is the only document handler selection step. The existing generic typed dispatch and built-in document dispatcher run only after AppRegistry returns a capable, available handler and an owned `AppActivationContext`. Open With builds its menu from the same capable-handler enumeration and suppresses the menu when there are no eligible choices.

| Document/type | Old hosted route | Current route | AppRegistry-owned handler selection? | Remaining issue |
| --- | --- | --- | --- | --- |
| Directories | File Explorer navigation | File Explorer navigation | Separate shell route | None; directories are not documents. |
| `.txt`, `.log`, `.ini`, `.cfg` | Phase 6 text table and later AppRegistry migration | AppRegistry → Notepad; `.txt` may also have Developer Studio as a capable choice when its experimental backend is available | Yes | None. |
| `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx` | No hosted extension switch | AppRegistry → Developer Studio when the NativeElf manifest/backend is available | Yes | Ordinary hosted build keeps this backend unavailable; experimental build enables it. |
| `.png`, `.jpg`, `.jpeg` | Legacy image fallback for each type before migration | AppRegistry → ImageViewer → owned activation context → production image adapter | Yes | Corrupt content is rejected by the decoder after extension-based selection. |
| `.html`, `.htm` | No File Explorer extension switch | AppRegistry → Navigator → owned activation context | Yes | None in the local document route. |
| `.bmp`, `.gif` | Legacy table → direct ImageViewer launch | No handler; safe unsupported result | No handler exists | Neither format is decoded by the production ImageViewer path. |
| Unknown, extensionless, dotfile, malformed/trailing-dot | Generic unsupported fallback | AppRegistry returns no association, no extension, or invalid extension; DesktopService fails closed | No handler exists | No arbitrary fallback or content sniffing. |
| `.exe`, `.gxapp`, `.elf` | Risky fallback classification | Unsupported; no application is selected from the filename | No handler exists | Executable/package/ELF launch remains outside this file-open path. |

The separate `DesktopService::LaunchApp` branches and the generic built-in dispatcher are dispatch infrastructure, not handler discovery. No extension-based Notepad, ImageViewer, Navigator, or Developer Studio handler selection remains outside AppRegistry. ImageViewer's own internal extension filters are application UI/decoder guards, not File Explorer routes.

## User-facing and Settings behavior

- BMP/GIF normal Open fails with the no-association error and does not silently succeed.
- Open With has no eligible BMP/GIF handlers and therefore shows no menu.
- Explicit ImageViewer document activation for an unsupported suffix is rejected by AppRegistry capability validation.
- Default Apps enumerates known AppRegistry capabilities and persisted policy entries. With no stale stored entry, BMP/GIF rows are absent. A stale stored `.bmp`/`.gif` policy key may remain visible under Phase 8 semantics as `CapabilityMissing`, with no effective default, no capable handler, and no eligible Settings choice; Phase 14 does not rewrite the store.
- `.xyzphase14`, extensionless paths, dotfiles such as `.gitignore`, and trailing-dot names fail closed. Normal routing remains extension-based: a JPEG renamed to `.bmp` is not opened as an image; a bitmap renamed to `.jpg` may select ImageViewer by suffix, after which the decoder rejects its content.
- A corrupt `.jpg` remains distinct: AppRegistry selects ImageViewer for the genuine suffix, then ImageAdapter returns its decoder failure rather than a no-handler result.

## Regression and build closeout

### Focused runtime evidence

The Phase 14 hosted runtime smoke passes **27/27 checks** on the standard executable. It resolves `.txt`, `.log`, `.ini`, `.cfg`, `.png`, `.jpg`, `.jpeg`, `.html`, and `.htm` through AppRegistry; BMP, GIF, unknown extensions, extensionless paths, dotfiles, and trailing-dot names return unsupported/no-handler diagnostics. It performs **106/106** deterministic failed opens (34 each for BMP, GIF, and an unknown extension, plus four edge/rename paths). Process-list output, compositor window ownership, AppRegistry declarations, registered apps, Recent Programs, and the persisted default-policy snapshot remain unchanged.

The Phase 13 runtime smoke passes **20/20**, including baseline and progressive JPEG loads, three ordinary Open activations, Open With, seven decoder failure/bounds probes, two Settings rows, and a real valid JPEG copied to a `.bmp` name. The renamed JPEG returns the no-association error and does not increment the ImageViewer document-dispatch count. The JPEG codec suite passes **56/56** with pixel hash `8adec27d637a1e32`. The Phase 11 PNG suite passes **21/21** (three normal activations, one Open With activation, eight decoder failure probes, Settings); Phase 12 Navigator passes **11/11**.

| Regression | Result |
| --- | --- |
| Phase 14 unsupported-route runtime | 27/27; 106/106 no-handler opens; no process, window, dispatch, registration, recent, or policy mutation |
| BMP/GIF production adapter rejection | 11/11; both suffixes return `UnsupportedFormat` before file reads |
| Phase 13 JPEG runtime / codec | 20/20; 56/56 codec checks; pixel hash `8adec27d637a1e32` |
| Phase 12 Navigator | 11/11, including local HTML, relative navigation, Back, Reload, CSS, and relative PNG |
| Phase 11 PNG | 21/21, including AppRegistry, Open With, decoder failures, and Settings |
| Phase 10 Developer Studio | 10/10 on experimental build; 4/4 real NativeElf launch/close cycles; `.txt` competition preserved |
| Phase 9 Settings Default Apps | 39 model checks + 9 interaction checks; 100 lifecycle cycles |
| Phase 8 default-handler persistence | 54/54; 100 reload, ordinary-open, and one-time-choice cycles; stale BMP/GIF policies remain inert and byte-preserved |
| Phase 7 File Explorer Open With | 4/4 runtime checks with 20/20 explicit menu activations; ordinary default unchanged |
| Phase 6 document activation | File Explorer 7/7, Notepad 29/29, negative runtime 3/3; 20 exact-path activations |
| Phase 5B readiness | PASS; ready, 0 unresolved, 0 high-risk; 13 association records |
| File Explorer Open With model | 17/17; 100 menu lifecycle cycles; no BMP/GIF choice |
| Generic built-in document dispatcher | 21/21; 100 PNG, 100 Navigator, and 100 JPEG lifecycle cycles |
| Settings regression | Center 89/89; S6 42/42; S7 21/21 accessibility, 23/23 Developer, 14/14 navigation/search |
| Startup / Recent Programs | Startup registration, explicit launch, and persistence smoke PASS; Phase 4D Recent Programs PASS |
| Phase 4B association closeout | PASS; current no-handler markers and generated-state restoration verified |

The hosted production dispatcher remains format-neutral: AppRegistry selects the handler, then the generic owned document dispatcher launches the selected app. File Explorer's normal open service does not choose Notepad, ImageViewer, Navigator, or Developer Studio by suffix. Its independent directory navigation route and the ordinary application-launch implementation branches remain valid.

### Builds and platform boundary

The standard hosted build passes with **181 warning lines**. The experimental hosted NativeElf build passes with **180 warning lines**. Both compile the hosted `desktop_service.cpp`; neither reported a Phase 14 warning. Existing warning output comes from unrelated allocator, GUI protocol/configuration, compositor, web parser, vendored stb, and other legacy code.

QEMU was not run: Phase 14 changes are limited to hosted `desktop_service.cpp`, hosted tests, smoke scripts, and documentation. No kernel, shared bare-metal AppRegistry, or native launch source changed, so a kernel runtime scenario would not exercise the modified route. The known PacMan audio link issue was not encountered or tested.

### Git closeout

- Starting HEAD: `6c63d9d1ecdbb6e40d7512436ff83a65c5c54c93`
- Starting branch: `main`; cached upstream divergence: 0 behind / 1 ahead.
- The Phase 14 changes are committed on `main`; ending divergence against the cached `origin/main` is 0 behind / 2 ahead.
- A normal `git push` was attempted and rejected because GitHub SSH authentication returned `Permission denied (publickey)`.
- The modified desktop configuration files (`desktop.json`, `desktop.state`, `window-bounds.cfg`) and untracked `tmp/` artifacts remain outside the Phase 14 commit.
