# App Model Phase 11 — ImageViewer PNG Document Activation

Date: 2026-10-02
Result: **Outcome A — hosted ImageViewer is the canonical `.png` App Model handler**

## Starting checkout and audit

- Starting HEAD: `81565ee227ed784862ecc04fc692cbd1904deef4`.
- Branch: `main`, tracking `origin/main`; starting divergence was 1 ahead / 0 behind.
- The existing `desktop.json` edit was preserved and excluded. Pre-existing temporary/startup artifacts were also preserved; runtime fixtures created by this work remain under uniquely named `tmp/` directories.
- Canonical app ID: `gxos.builtin.imageviewer`.
- Backend: hosted built-in application, not a NativeElf app. Ordinary and experimental hosted builds both expose this built-in independently of the NativeElf policy.

ImageViewer is a hosted compositor application whose image, path, folder-list, edit-history, and window state are process-static. Before this phase, its normal production entry point was `ImageViewer::Launch(path)`. That shared state means simultaneous ImageViewer processes are unsafe. The launch path now rejects a second concurrent instance; closing the active window releases its image, owned paths, folder list, and undo/redo snapshots so a later sequential activation can run cleanly.

## Previous route and preserved baseline

Before migration, a hosted File Explorer file open followed:

```text
FileExplorer::openSelected()
→ DesktopService::OpenFilesystemEntry()
→ AppRegistry finds no `.png` capability
→ FilesystemEntryLaunchTarget legacy image fallback
→ ImageViewer::Launch(path)
→ ImageAdapter::LoadFromFile()
→ VFS / hosted file read
→ PngLoader::LoadFromMemory() (stb_image)
```

The generic App Model dispatcher did not yet support a built-in receiving an owned document activation. Phase 6 therefore retained images on the old direct route while Notepad established the path ownership contract. The old route grouped `.png`, `.bmp`, `.jpg`, `.jpeg`, and `.gif` under one image-viewer target.

Before removing the `.png` row from that fallback, the legacy hosted route opened `/assets/Backgrounds/blueflower_thumb.png`. Runtime logs recorded ImageViewer launch, successful decode at **126 × 84** with three source channels, and a compositor window owned by process `ImageViewer` with app ID `gxos.builtin.imageviewer`. This established load and ownership before migration. The runtime environment does not expose native windows to Computer Use, so no visual screenshot claim is made.

## Generic built-in activation and canonical Open

`built_in_document_dispatcher.h` adds a reusable, value-owned dispatcher keyed by canonical App ID. It accepts only a current AppRegistry document activation with a valid `Document` kind, matching canonical target, bounded path, current owner/generation, and registered built-in backend. It does not inspect file extensions or contain PNG-specific behavior. Notepad and ImageViewer are registered through this same dispatcher.

The activation path is copied into an owned string before the activation context/caller can expire. ImageViewer validates the app ID and path again, then passes that exact path into its existing launch/load path and PNG adapter. The old `.png` special row is removed from the legacy table. Normal Open now resolves through AppRegistry and effective default data; the remaining direct image fallback rows are only `.bmp`, `.jpg`, `.jpeg`, and `.gif`.

The production AppRegistry declares `.png` with display metadata `PNG image` for `gxos.builtin.imageviewer`, marks document activation/backend availability, and names ImageViewer as the built-in default. Extension matching folds ASCII case, so `.png`, `.PNG`, and `.Png` resolve identically. The global default store has no `.png` override; effective default is ImageViewer. No second pseudo-app identity, MIME sniffing, or content-based association was added.

Open With obtains ImageViewer from normal capable-handler enumeration. Settings → Apps → Default apps obtains the `.png` row and built-in/configured/effective state from the same AppRegistry/Default Apps snapshot. Neither File Explorer nor Settings contains ImageViewer-specific discovery logic.

## Loader limits and failure behavior

ImageViewer's production entry point currently accepts `.png` only. It reuses `ImageAdapter::LoadFromFile`, the bounded VFS reader, and the existing `PngLoader`; no decoder was added. The hosted adapter caps encoded file input at **4 MiB**, width and height at **4096** each, pixels at **16,777,216**, and decoded RGBA output at **64 MiB**. Dimension multiplication uses 64-bit arithmetic and checks the pixel and output-byte limits before decoding/allocation. File size is checked before the VFS copies file bytes into the adapter buffer.

Missing and empty files return `NotFound`. Non-PNG bytes under a `.png` name return `UnsupportedFormat`; signature-only and truncated PNG inputs return `DecodeFailed`; excessive dimensions and an encoded file larger than 4 MiB return `TooLarge`. AppRegistry may select ImageViewer for corrupt `.png` bytes because the association is extension-based; decoder failure is reported by the loader and does not change the association result. Unknown image-like extensions such as `.xyz` and `.image` remain unassociated. Directory selection still navigates in File Explorer.

## Production evidence

The Phase 11 production smoke drives the hosted server's File Explorer open service, menu/input path, Settings launch, and compositor diagnostics. It passed **20/20** checks:

- Three normal File Explorer opens loaded valid PNGs with different dimensions, mixed-case extensions, different names, and nested paths. Each matched the exact requested path, selected `gxos.builtin.imageviewer`, decoded successfully, and had the expected compositor owner/window state. Closing and reopening worked sequentially.
- One production Open With selection discovered and launched the sole `.png` handler with the exact owned path.
- Eight bounded failure probes covered missing, empty, corrupt, signature-only, truncated header, truncated image data, oversized declared dimensions, and a file over 4 MiB. An unknown `.xyz` path remained unassociated.
- Settings Default Apps reported `.png`, built-in default `gxos.builtin.imageviewer`, no configured override, effective ImageViewer, and one available handler.

Exact counts from focused infrastructure tests:

- Generic built-in dispatcher: **14/14**, plus **100/100** dispatcher lifecycle cycles.
- Image adapter bounds and malformed-input tests: **10/10**.
- ImageViewer live launch/close cycles: **4** production activations (three normal Open and one Open With), with sequential close/relaunch verified. Full GUI process starts were kept lower cost; the 100-cycle stress was at the dispatcher model/lifecycle layer.
- App Model associations and owned activation: **57/57**.
- Handler/default store: **52/52**; persistence/reload **100/100**; ordinary Open and one-time Open With **100/100** each.
- File Explorer Open With model **16/16** and menu lifecycle **100/100**.
- Settings Default Apps model **36/36**, interaction **9/9**, lifecycle **100/100**; Settings Center **89/89**.

The hosted smoke reported exact dispatch and decode state but could not inspect a native window visually. No claim is made about screenshot-level rendering or visual pixel fidelity.

## Regression/build/platform results

- Phase 5B closeout: **PASS**, current readiness retained with **0 unresolved / 0 high-risk** targets. The final preview recorded 43 ready, 8 shell actions, 35 safely unsupported, 0 unresolved, and 0 high-risk records. `.png` is now AppRegistry-owned; `appModelV1ImagesRemainLegacy` describes only the other four legacy extensions.
- Phase 5A status, Phase 4B associations, Phase 4D Recent Programs, and Phase 3E active typed dispatch: **PASS** after updating their PNG expectations to the canonical AppRegistry route.
- Phase 6 hosted file activation: **57/57** model checks; hosted Notepad runtime **7/7** service checks, **29/29** app checks, 20 exact-path activations, and negative cases passed.
- Phase 7 runtime: **20** Open With activations and **4/4** runtime checks passed.
- Phase 8 handler/default tests and Phase 9 Default Apps/Settings tests passed at the counts above. Settings inventory, S6, S7, Users, and Recent Programs regressions also passed.
- Phase 10 experimental Developer Studio runtime regression: **10/10** checks passed, including four exact-path launch/close cycles, the shared `.txt` Open With competition, `.cpp` normal Open, and Recent Programs behavior. The experimental NativeElf ABI path remained confined to the experimental executable.
- Startup App Model smoke (`-SkipBuild`): **PASS**; no startup windows appeared, canonical explicit app launches had owned windows, and repeat startup persistence restored no unexpected windows.
- Standard hosted build (`cmd /c build.bat`): **PASS**, `guideXOSServer.exe` produced. Experimental hosted build (`cmd /c build-native-experimental.bat`): **PASS**, `guideXOSServer.experimental.exe` produced with the NativeElf feature gate explicitly enabled. Both builds reported the repository's existing GUI/configuration, Navigator, and third-party stb/JPEG warnings. The touched `image_viewer.cpp` also reports its pre-existing unused `publishWindowText` helper at line 83; no warning was emitted from the new dispatcher, activation code, or bounded VFS read implementation.

QEMU was not run. The AppRegistry, DesktopService, built-in dispatcher, hosted ImageViewer, hosted ImageAdapter implementation, and root hosted VFS changed here. Bare-metal `kernel/core/image_adapter.cpp`, kernel VFS, `ImageViewerApp`, and kernel launch/registration are separate implementations and were not modified; the hosted built-in document path is not shared with the bare-metal launch path. Physical hardware was not used.

## Other formats audited and follow-up

| Format | Current evidence | Phase 11 decision |
| --- | --- | --- |
| PNG | ImageViewer accepts `.png`; hosted adapter uses bounded `PngLoader`; runtime success and malformed/oversize cases passed. | Registered and defaulted. |
| JPEG (`.jpg`, `.jpeg`) | The hosted shared ImageAdapter has a JPEG parser/decoder and encoded/decoded bounds, but ImageViewer's production file entry point rejects non-`.png`; no end-to-end ImageViewer JPEG activation was verified. | Implemented below the app, not verified as an ImageViewer format; do not register yet. |
| BMP, GIF | ImageViewer's production loader does not accept these suffixes; no supported ImageAdapter path is present for them. | Unsupported. |

JPEG should only be considered after a separate narrow audit verifies ImageViewer's own path/navigation/UI behavior, decoder limits, and runtime activation. The next phase can instead move to another application type; no additional image extension is justified by the Phase 11 evidence alone.

## Git closeout

Implementation and report were prepared from the starting `main` checkout. The commit, final divergence, exact worktree status, and push result are recorded in the Phase 11 closeout response.
