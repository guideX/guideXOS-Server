# App Model Phase 7 — Capable Handlers and Open With

Status: **implemented and validated on 2026-10-01.** This report supersedes the Phase 6 report's “Future Open With / Default Apps” note for the current checkout. Phase 6 remains the historical source for the original owned document-activation path.

## Starting checkout and pre-existing support

- Repository: `D:\dev\guideXOSServer`
- Starting branch/HEAD: `main` / `9b3e623aea8152d1adf5216e04e55baf67c0acb0`
- Upstream: `origin/main`; starting divergence was 0 ahead / 0 behind.
- Existing worktree items were `desktop.json` plus five generated `tmp/startup-appmodel-regression-*` directories. They are outside this change and must be preserved.

The checkout had AppRegistry association indexing, Phase 6 default file activation, and owned `AppActivationContext` values. It did not have capable-handler enumeration, explicit non-default activation, a default override API, or an Open With menu. Each index record already represented one app's extension declaration (capability), but Phase 6 resolution rejected more than one record for an extension as ambiguous. Phase 7 keeps the capability records and resolves the effective default separately.

## Capability and default model

An association record means “this canonical app ID declares this extension.” It does not mean “this is the only/default handler.” Duplicate declarations by the same app for the same normalized extension collapse to one capability record. Different apps declaring one extension remain separate handlers.

`AppRegistry::EnumerateCapableHandlers(extension)` returns a fixed `DocumentHandlerList` value. Each handler contains the canonical App Model ID, display label, owner/generation snapshot, declared document-activation support, registration-current state, backend availability, current launchability, and effective-default status. The value contains no pointers.

Bounds and truncation:

- Existing association-index bound: 256 records.
- New `kAppModelMaxDocumentHandlersPerExtension`: 16 returned handlers.
- Each app remains limited to 16 declarations and each normalized extension to 32 bytes.
- `declaredHandlerCount`, `availableHandlerCount`, and `truncated` report the complete bounded-index view. Overflow never displaces the effective default from the 16 returned records.
- File Explorer offers only entries whose current `available` flag is true.

The extension argument must be a dotted extension such as `.txt`; ASCII letters are case-folded. Path-based enumeration uses the same final-suffix rules as Phase 6, including `/` and `\` separators, dotfile and extensionless handling, malformed suffix rejection, and the 4096-byte document-path bound.

## Ordering and effective default

The effective default is resolved independently of capability enumeration. The current built-in defaults stay:

```text
.txt → gxos.builtin.notepad
.log → gxos.builtin.notepad
.ini → gxos.builtin.notepad
.cfg → gxos.builtin.notepad
```

If a built-in default is not currently launchable, resolution chooses the first currently launchable declared handler by canonical App ID. For extensions without a built-in default, the first currently launchable canonical ID is the deterministic default. Unavailable declarations are sorted after available declarations and remain visible in API metadata, but they are not offered by File Explorer. A list's effective default is first, then other available IDs in canonical order, then unavailable declarations in canonical order. Registry pointer order and display labels do not participate.

No persistent override was added. The repository has no human account/profile model and no established App Model association-preference owner. Reusing `desktop.json` (which currently owns desktop pins, recents, and layout state) would make a new machine-wide setting owner ambiguous. The current API already accepts canonical IDs for one-time choices; bounded canonical-ID override storage and its authoritative global owner can be added with a later Default Apps phase. No per-user preference is claimed.

## Explicit activation and stale menu safety

`AppRegistry::ResolveDocumentActivation(canonicalAppId, path, expectedOwner, expectedGeneration)` validates the exact requested app ID against the current registration, the normalized document extension, declared document activation, current backend availability, and the owned 4096-byte path. The overload taking `DocumentHandlerInfo` also checks the owner/generation captured by enumeration.

`IsDocumentActivationCurrent()` now revalidates a document activation against the selected app's current capability rather than requiring that app to still be the default. Normal Open uses `ResolveFileAssociation(path)` and therefore still selects the effective default. Open With passes the saved handler snapshot to `DesktopService::OpenFilesystemEntryWithHandler()` and does not alter default state.

The File Explorer menu stores a value-owned, fixed-capacity snapshot containing canonical app IDs and temporary-registration owner/generation. On selection, `DesktopService` revalidates it before dispatch and the document dispatcher validates it again immediately before launch. If a temporary registration disappears or its slot/ID is reused with a new generation, the stale item fails closed. A reused slot with a different canonical ID cannot match the saved ID. Duplicate labels are presentation-only; duplicate rows receive stable numeric suffixes while each row retains its own canonical ID. Long labels are shortened to 30 characters in the menu without shortening the stored ID.

## File Explorer integration

The existing File Explorer context-menu model is a bounded-coordinate mouse menu represented by action codes and rendered in `renderContextMenu()`. It previously had no submenu handling or keyboard traversal. Phase 7 extends the same menu action list, hit testing, renderer, and lifecycle state with an `Open With >` child menu; it does not create a separate menu framework. The submenu is bounded to the 16 handler-result slots.

- A file gets `Open With >` only if enumeration finds at least one currently launchable handler.
- A directory remains a navigation item and never gets document handlers.
- A single handler is displayed as a one-row submenu (currently Notepad for the four built-in text extensions).
- Multiple handlers use current display labels, with stable disambiguation for duplicate labels. Selection dispatches by the saved canonical ID.
- Unknown, extensionless, malformed, executable-style, and unsupported image targets do not get arbitrary application choices.
- Native ELF or other backend-unavailable declarations are not offered as launchable hosted choices.
- Existing normal Open/double-click flow still calls `DesktopService::OpenFilesystemEntry()` and resolves the default.

The AppRegistry dispatcher currently has a production document launcher for Notepad only. No production capabilities were added to Calculator, Developer Studio (`openSupported=false`), Navigator, or Image Viewer. Multi-handler behavior is exercised using isolated temporary test registrations.

## Association mutation and backend availability

Manifest declarations remain immutable while registered. Scans and temporary registration/unregistration rebuild the bounded AppRegistry association index. Enumeration is a detached value snapshot, so later mutation cannot rewrite a menu row. Explicit activation rejects temporary owner/generation mismatches and rechecks the declaration and backend. For non-temporary registrations the canonical app ID remains the identity; generation is not used as a substitute for identity.

The current hosted document dispatcher is authoritative: Notepad is marked backend-available; apps without a current document dispatcher remain unavailable even if they declare an extension. This Phase 7 code changes the hosted AppRegistry/File Explorer path only; no bare-metal App Model or kernel handler route was changed.

## Validation and runtime evidence

Validation completed on 2026-10-01:

- Final hosted `build.bat`: passed (`Build successful: guideXOSServer.exe`). Changed translation units passed `-fsyntax-only`; only existing warnings in `gui_protocol.h` and `desktop_config.h` appeared.
- AppRegistry association, handler enumeration, and activation checks: 57/57.
- File Explorer Open With model checks: 16/16; menu lifecycle stress: 100 create/select/reset cycles passed.
- Production File Explorer Open With runtime smoke: 20/20 explicit menu activations selected `gxos.builtin.notepad` and delivered the exact mixed-case nested path; ordinary Open afterward still used the unchanged Notepad default (4/4 runtime assertions).
- Phase 6 document-activation regression: 39/39 checks passed, including 20 repeated file loads and fail-closed unknown, extensionless, executable-style, unavailable, missing, and overlong targets.
- Phase 5B closeout and Phase 4D Recent Programs regressions: passed.
- Startup App Model regression: passed with no unexpected launch or restored windows.
- Settings regressions: Center 89/89; inventory device 21/21 and storage 24/24; S6 42/42; S7 58/58 across accessibility, developer, navigation/search; Users 32/32 across model, navigation, and interaction/layout.
- Network Settings contract 45/45 and network configuration transaction 21/21.
- QEMU was not run: the change is confined to the hosted AppRegistry, DesktopService, and File Explorer path; no kernel or bare-metal handler route changed. Physical hardware testing was not required.

The acceptance run uses [the Phase 7 hosted Open With smoke](../scripts/smoke-appmodel-phase7-open-with.ps1) with a real nested `.TXT` document. It drives the production File Explorer menu through `gui.mouse`, checks the canonical ID and exact owned path at Notepad dispatch, repeats explicit activation 20 times, and checks that subsequent ordinary Open still uses the unchanged default. The script saves/restores the pre-existing `desktop.json` and `window-bounds.cfg` bytes and metadata and removes only its uniquely named fixture directory. The startup smoke's newly generated controlled fixture was removed after the run; the five startup-regression directories present before this work remain untouched.

The repository currently has only one production document backend, Notepad. Multi-handler/default-versus-explicit selection, duplicate-label routing, unavailable-handler filtering, stale-generation rejection, and 100 menu lifecycle cycles are proved with isolated temporary AppRegistry registrations and the File Explorer menu model; no fake production capability or second runtime backend was added.

No Settings Default Apps UI was added. No image viewer, package installation, MIME database, content sniffing, arbitrary executable chooser, or user-profile model was introduced.

## Deferred work

A later Settings → Apps → Default apps page may store only normalized extensions and canonical handler IDs under a clearly named global configuration owner (unless a user/profile owner exists by then). On load or write it must validate the app's current capability and backend using the same AppRegistry APIs. A failed or stale override must fall back safely to the built-in/default declaration or report no usable default. Phase 7 deliberately leaves persistence and the Settings page deferred.

## Phase 11 follow-up

The production handler counts above are the Phase 7 baseline. Phase 11 later added hosted ImageViewer as the `.png` capable handler through AppRegistry and verified its one-handler Open With route. Settings Default Apps and the persistent default policy were added in later phases; the `.png` row now appears from the same capability/default data without ImageViewer-specific menu or Settings logic. See [the Phase 11 report](APPMODEL_PHASE11_IMAGEVIEWER_DOCUMENT_ACTIVATION.md).
