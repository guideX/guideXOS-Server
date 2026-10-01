# App Model Phase 6 — File Associations and Document Activation

Date: 2026-10-01
Result: **Outcome A — hosted document activation works through the App Model**

## Starting checkout and baseline

- Starting HEAD: `bec46eba3468cd8a5f6938bb2181544fd20fa559`
- Branch: `main`
- Upstream: `origin/main`; starting divergence was `0 ahead / 0 behind`.
- Pre-existing worktree state: a user edit to `desktop.json` and four untracked `tmp/startup-appmodel-regression-*` directories. The edit and directories were preserved and excluded from the Phase 6 commit.
- No history was rewritten. A normal push was attempted after the Phase 6 commit; see Git closeout below.

Phase 5B established canonical App IDs and owned launch targets, but the file-open boundary was incomplete. The hosted `DesktopService` had a static extension-to-display-name table. `.txt`, `.log`, `.ini`, and `.cfg` selected Notepad by its display/dispatch label; images followed a legacy direct route; unknown and executable-style paths failed closed. File Explorer already routed its selected files through `DesktopService::OpenFilesystemEntry`, while directories navigated within File Explorer.

Notepad's former path launch passed a path in `argv`, but its process entry only logged that it would load the file. The ordinary Notepad Open action did use `loadFile()` and its VFS reader. On Windows, File Explorer lists the checkout through the host filesystem while Notepad's VFS had only an in-memory tree, so the hosted file-open path could not read a selected host file. Navigator had no local-document activation contract. Developer Studio was registered with `openSupported=false`. Wallpaper/image selection was not a general document viewer contract.

## Architecture and identity

Application registration, file association, and document activation are separate values:

1. AppRegistry owns application registrations and derives a bounded extension index from their declarations.
2. A resolved association returns the canonical App ID and a value-owned activation for one exact path.
3. DesktopService revalidates that activation and dispatches it through the handler's current hosted backend.

File Explorer does not own or rank handlers and contains no `.txt`-specific launch rule. `OpenFilesystemEntry` is the production open service called by `FileExplorer::openSelected()`. Its active dispatch and fallback paths consume the same owned resolution. The direct `desktop.open` server command exercises that same service for hosted runtime diagnostics.

The built-in Notepad registration is `gxos.builtin.notepad`. It declares document activation support and a current hosted dispatcher in AppRegistry. Its supported built-in associations are `.txt`, `.log`, `.ini`, and `.cfg`; the sample manifest's duplicate Notepad association was removed. The App Model manifest loader also reads the optional `supportsDocumentActivation` capability for future registrations. A declaration alone does not make an app launchable: current registration, capability, and backend availability are checked.

Developer Studio remains unavailable for Open because its hosted metadata says `openSupported=false`. Navigator has no local-file activation backend. `.md`, `.html`, `.htm`, and image formats are not added to the AppRegistry defaults. Existing image extensions remain on the legacy Image Viewer route; this phase does not claim to expand its format support or associate wallpaper selection with document opening.

## Bounds and extension rules

The shared bounds in `app_model_limits.h` are:

| Value | Bound |
| --- | ---: |
| Canonical App ID | 128 bytes |
| Document path | 4096 bytes |
| Extension, including leading dot | 32 bytes |
| Declarations per app | 16 |
| Indexed association records | 256 |
| Registered apps contributing declarations | 512 |

Therefore candidate construction is bounded by 8192 declarations before the retained index cap. Overflow is recorded; an omitted extension resolves as `capacity-exceeded` and is not assigned to an incidental handler. Index order is deterministic by normalized extension, canonical ID, owner, and generation. Duplicate defaults are marked ambiguous and rejected; display name and registration order never select a winner.

Manifest extensions require a leading dot and 1–31 following ASCII letters, digits, underscore, or hyphen. The index lowercases ASCII `A`–`Z`; extension matching is case-insensitive, while path bytes and filesystem case semantics are unchanged. The resolver uses the final suffix in the final basename and supports both `/` and `\\` separators. Thus `archive.tar.TXT` resolves as `.txt`; `.hidden` and extensionless names have no extension; a trailing dot and malformed/overlong suffix are invalid. No content sniffing, MIME database, or path normalization is introduced. The original path (including valid `..` segments) is retained exactly for the existing VFS to interpret.

Empty paths, paths over 4096 bytes, control bytes, and DEL are rejected. Overlength paths fail rather than truncate. Association resolution distinguishes invalid path, no extension, invalid extension, no association, ambiguity, stale/missing handler, missing document capability, unavailable backend, and capacity exhaustion.

## Owned document handoff

`AppActivationContext` contains an activation kind, canonical App ID, owned document path, registration owner, and registration generation. The path is a `std::string` value copied from the caller; `ProcessTable::spawn` validates the kind, App ID match, controls, and byte bound, then stores another value copy in the `Process`. The process worker exposes a value copy through its thread-local activation accessor while its entry point runs. Notepad copies the exact path before entering its event loop.

Before dispatch, DesktopService resolves the path again and compares canonical ID, owner, and generation. A removed temporary registration cannot use a reused ID/generation slot: old activation fails revalidation. Current registration declarations are generated from the same registry-owned app objects, so a dangling association cannot be constructed by normal registration/unregistration; stale temporary lifetimes are tested directly. The production hosted dispatcher currently supports the canonical built-in Notepad ID. Other registered apps do not gain a dispatcher merely by declaring an extension.

Notepad's `LaunchWithActivation` rejects non-document activations, another app ID, and invalid paths. Its normal app activation remains `Launch()` with no document. A document launch constructs a fresh Notepad object/process and consumes the activation after the GUI window is created using the existing `loadFile()` method. Notepad state is per instance, so a second document activation creates another independent editor; it does not replace or discard an existing dirty document. The existing Recent Documents write remains inside successful `loadFile()`; document paths are not added as Recent Programs.

The existing hosted File Explorer maps virtual paths to the process working directory. On Windows the VFS reader now checks its in-memory tree first and then reads the same host-backed virtual path in binary mode. This lets Notepad's existing `loadFile()` read the exact file selected by hosted File Explorer. LF and CRLF bytes are preserved by VFS; Notepad's existing parser handles line endings. Native builds continue to call the kernel VFS implementation and are not changed by this hosted fallback.

## Shell boundaries

- **Directories:** remain a separate File Explorer navigation route. They do not appear as extension-association records. The Phase 4B marker remains as a compatibility status, and Phase 6 diagnostics explicitly report `appModelPhase6DirectoryActivationIsSeparate=true`.
- **Unknown and extensionless files:** fail with a bounded error and never guess an app.
- **Executables and package files:** `.exe`, `.elf`, and `.gxapp` remain unsupported in document open. Association does not authorize process execution; App Model application launch stays a separate path.
- **Missing associated file:** association dispatch remains typed to Notepad, then the existing VFS read reports failure; it does not fall back to another app.
- **Recent Programs:** opening a document records the handler as the launched program, following existing app-launch policy. The document path is stored only by Notepad's existing Recent Documents behavior after a successful read.
- **User defaults/Open With:** no UI or per-user persistence was added. No Settings category or Control Panel applet was added.

## Diagnostics and tests

`desktop.appmodel.file-associations` reports declarations, canonical IDs, extension, document capability, backend availability, ambiguity, owner/generation, resolution status, and bounded registry capacity. `desktop.open.resolve <path>` distinguishes a `directory-route` from an App Model extension. Diagnostics contain no internal pointers.

Focused results from this checkout:

- App Model file association and owned target tests: **41/41** (26 association registry checks and 15 owned-target checks).
- Hosted VFS file read tests: **4/4**, including LF, CRLF, in-memory precedence, and missing file.
- Phase 5B identity regression: **25/25**.
- Hosted production runtime: **20 repeated activations** across `.txt`, `.log`, `.ini`, and uppercase `.TXT`, nested and top-level directories, LF and CRLF files. The runtime harness reported File Explorer service checks **7/7** and Notepad checks **29/29**. For every activation it checked the canonical Notepad ID, the exact path received after the caller returned, and the byte count read by `loadFile()`.
- One exact sample from the final hosted run was host path `D:\dev\guideXOSServer\tmp\appmodel-phase6-runtime-b618f754901a44d4a54b437ed69d735f\document-00.txt`, delivered as virtual path `/tmp/appmodel-phase6-runtime-b618f754901a44d4a54b437ed69d735f/document-00.txt`. This is the smoke's temporary document fixture; the fixture was removed after the run.
- Negative runtime checks: **3/3 groups passed**. Unknown and extensionless files, `.exe`, a registered handler without document capability, and an overlength path did not launch another app. A missing `.txt` was dispatched to Notepad and its existing VFS read reported the missing file. Overlength input was rejected before diagnostic path logging.

The runtime harness calls `DesktopService::OpenFilesystemEntry`, the same production service invoked by File Explorer's `openSelected()` for a file. It verifies the directory route and file-open route separately, but it does not simulate a literal mouse double-click inside File Explorer. The harness does not provide visual native-window acceptance; no screenshot/visual claim is made. Runtime logs prove canonical Notepad selection, the exact owned path received after the caller returned, the exact path and byte count read, and safe negative outcomes.

## Regression and platform results

The supported hosted `cmd /c build.bat` completed without compiler or linker errors. Focused C++ regressions passed as listed above.

The preserved integration regressions also passed:

- Phase 4B file association smoke: **PASS**; 4 AppRegistry document extensions resolved to Notepad, 5 legacy image routes stayed on Image Viewer, directory routing remained separate, unknown/risky cases remained unsupported, and generated state was restored.
- Phase 4D Recent Programs smoke: **PASS**; canonical app recents, file/folder recents, force-off/reset behavior, and temporary-state restoration verified. Its old text-open reason assertion was updated to the Phase 6 owned-activation diagnostic.
- Phase 5B canonical closeout: **PASS**; status/inventory agreed, default dispatch and fallback gates remained intact, and no unresolved or high-risk records were reported.
- Startup App Model regression (`-SkipBuild`): **PASS**; normal startup launched no apps, all five registered built-ins launched explicitly with canonical window ownership, and restart persistence restored no unexpected application windows.
- Settings and App Model regressions: Network contract **45/45**; Settings Center **89/89**; inventory Device **21/21** and Storage **24/24**; S6 **42/42**; S7 Accessibility **21/21**, Developer **23/23**, navigation/search **14/14**; Users **11/11**, navigation **8/8**, interaction/layout **13/13**.

The S7 test compiler emitted an existing `desktop_config.h` unused-local warning; the tests passed. No QEMU run was needed: the changed file-open and host-backed read paths are in hosted services, and no bare-metal VFS or kernel launch implementation was changed. Physical hardware was not used.

QEMU is not required for this change. The association resolver, File Explorer hosted bridge, hosted `ProcessTable` activation storage, Notepad hosted entry, and `_WIN32` VFS fallback are hosted-server paths. Bare-metal File Explorer and Notepad continue to use the existing kernel VFS paths; no kernel/core launch or VFS implementation was edited. Physical hardware was not used.

## Future Open With / Default Apps

The smallest next step is a bounded API that enumerates capable registrations for one normalized extension, returns only canonical App IDs plus capability/backend state, and separates that candidate list from the single current default. A later user-selected default can persist the canonical ID after validating it against current capability. Keep display names presentation-only and re-resolve owner/generation at dispatch. That enables Open With without weakening Phase 5B identity guarantees.

## Final Git closeout

The Phase 6 implementation was committed on branch `main` as:

- Commit: `5c71e323ad235390382fe5d37e747a1405d3655d` — `Add App Model file associations and document activation`
- Starting HEAD: `bec46eba3468cd8a5f6938bb2181544fd20fa559`
- Upstream before closeout: `origin/main`; the implementation commit was **0 behind / 1 ahead**.
- A normal `git push` was attempted and rejected by SSH authentication: `Permission denied (publickey)`. No credentials or remote settings were changed. The push was retried after this report update and received the same rejection.
- The report update is a separate documentation-only follow-up commit; the final branch is **0 behind / 2 ahead** of `origin/main` because push access is unavailable.

The implementation commit excludes the user's `desktop.json` edit and all startup regression fixtures. The final worktree retains the original two `desktop.json` preferences (the `PacMan PGM1 Ready Input Validation` pin and `desktop.accessibility.enhancedFocusIndicator=false`), the four pre-existing untracked `tmp/startup-appmodel-regression-*` directories, and one additional timestamped fixture produced by this task's Startup App Model regression. The test-created fixture was left in place after the cleanup command was rejected. `desktop.state` and `window-bounds.cfg` were restored to their clean starting contents.
