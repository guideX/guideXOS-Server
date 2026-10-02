# App Model Phase 12 — Navigator Local HTML Document Activation

Date: 2026-10-02
Result: **Outcome B — bounded local HTML activation works through AppRegistry**

## Starting checkout and audit

- Starting HEAD: `f75b8d054342a5b137e2baf03d91d723a8182fd`.
- Branch: `main`; cached `origin/main` matched the starting commit, so the observed starting divergence was **0 ahead / 0 behind**. No fetch was used to change the cached comparison.
- The user's existing `desktop.json` edit and all pre-existing temporary fixtures were preserved and excluded. Phase 12 smoke fixtures are uniquely named and removed by the smoke; test logs remain disposable diagnostics.
- Navigator's existing canonical App ID is **`guidexos.navigator`**. Phase 12 keeps it; it does not create a second browser/viewer identity.
- Developer Studio currently declares C/C++ headers/sources and `.txt`, but not `.html` or `.htm`. Phase 12 does not add an editor capability that its registered contract does not advertise.

Navigator already had a local `file://` load path before this change. `loadUrl()` routed file URLs through `loadFileUrl()`, which read via `navigator_file_io` and passed HTML bytes to `parseHtml()`. That path was reachable through Navigator navigation/diagnostics, but HTML had no AppRegistry association and File Explorer's typed open could not deliver an owned document activation to Navigator.

The local-file adapter was also broader than the new activation contract needed: hosted path translation stripped a leading slash and could map traversal outside the working directory. Phase 12 makes its root mapping explicit and rejects traversal, host-drive syntax, controls, and symlink resolution outside the workspace. The bare-metal adapter already delegates to the guideXOS VFS.

## Activation and document source

```text
File Explorer Open / Open With
  → DesktopService
  → AppRegistry association and owned AppActivationContext
  → generic built-in document dispatcher
  → Navigator::LaunchWithActivation
  → validated, copied VFS path
  → encoded file:/// guideXOS URL
  → Navigator::loadUrl → loadFileUrl → bounded VFS read
  → existing parseHtml → DOM/CSS/layout/render path
```

File Explorer, Open With, Settings, and AppRegistry use generic document-capability and handler-enumeration mechanisms. No `.html` or Navigator-specific route was added to those surfaces. The generic built-in document dispatcher registers Navigator under `guidexos.navigator` beside the existing Notepad and ImageViewer adapters.

The activation adapter accepts only a `Document` activation addressed to `guidexos.navigator` with a valid `.html` or `.htm` VFS path within the shared **4096-byte** App Model path bound. Navigator retains its own value copy for its lifetime. Unsupported explicit paths are rejected by AppRegistry capability resolution. A concurrently active Navigator is rejected because its existing process-static document/window state is single-instance; close clears document, image, history, and pending-URL state before releasing the instance reservation.

The public local document URL is a percent-encoded `file:///...` URL rooted in guideXOS VFS, such as `file:///docs/nested%20folder/index.html`. It is not a host Windows path, `localhost` URL, or synthetic web root. The file URL is the document/base URL used by the existing relative URL resolver, history, reload, and resource pipeline.

## Loader bounds and failure behavior

- Maximum local document source: **64 KiB**. Hosted input checks the file length before reading. Bare-metal uses a static `64 KiB + 1` buffer to distinguish an exactly-at-limit document from an oversized one.
- Empty files feed zero bytes to the production parser and remain empty documents.
- Missing files show a new `File not found` error document. Oversized input shows `File too large`; invalid URLs and VFS I/O errors also produce bounded error documents.
- Hosted VFS paths resolve below the process working directory. `..` cannot escape the VFS root; canonicalized symlink targets outside that root are rejected. The bare-metal path continues through `kernel::vfs::read_file`.
- No local HTTP server, temporary web server, copy-to-web-root step, or fake HTTP URL was added.

## Parser, CSS, JavaScript, URL, and trust behavior

Local and network HTML both use the same production `parseHtml()` implementation and `WebDocument` model. Local `<style>` blocks and style attributes use the same CSS parser as network pages. The nontrivial `docs/index.html` fixture was read through the hosted VFS adapter and produced the same parser title/block/CSS diagnostics.

Navigator does not currently execute JavaScript or provide a JavaScript/DOM engine. Script elements are stripped by the existing HTML parser for both remote and local documents. A local origin does not grant script filesystem access, and there is no separate local scripting environment.

Relative HTML links resolve against the local `file:///` document URL and enter the existing URL-only history. Back, Forward, and Reload reuse Navigator's existing controls and history stack. Reload reads the file again through VFS.

Local relative PNG/JPEG references resolve through the same Navigator image-resource decoder and bounds. Phase 12's hosted runtime opened an HTML page with a sibling PNG whose filename contained a space and recorded a successful **126 × 84** PNG decode. Remote HTTP(S) pages are blocked from navigating to `file:///` documents or loading local-file image resources; address-bar entry remains an explicit user action. Local documents may retain normal HTTP(S) navigation through the existing network path.

`file:///` remains a distinct document URL/source classification. Navigator does not implement a complete browser same-origin model, local-origin permission system, cookie jar, or web storage. Local and remote URL policy is limited to the explicit remote-to-local link/resource checks; this phase does not claim a broader browser sandbox.

## Resource support matrix

| Local page capability | Phase 12 result |
| --- | --- |
| Top-level `.html` | Supported and proven through AppRegistry and VFS. |
| Top-level `.htm` | Supported and proven through the same path. |
| Relative HTML navigation | Supported and exercised through Navigator's existing URL resolver/history path. |
| Back and Reload | Supported through the existing controls; Reload rereads changed file content. |
| Inline CSS / style attributes | Supported and proven through the existing CSS parser. |
| Relative PNG images | Supported and proven end-to-end through Navigator's bounded decoder. |
| Relative JPEG images | Existing decoder path resolves local URLs, but no local JPEG was exercised end-to-end in Phase 12; unproven here. |
| Linked local CSS | Deferred. The existing external stylesheet loader fetches HTTP(S), not local VFS files. |
| Local JavaScript | Unsupported. Navigator has no JavaScript execution engine. |
| Other local resources | Not claimed. |

This is a bounded local-document feature, not a claim of full local website compatibility.

## App Model surface and default policy

Navigator's built-in AppRegistry manifest now advertises `.html` and `.htm` as `text/html`, enables its document backend, and chooses `guidexos.navigator` as their built-in default. There is no persisted override. Case-insensitive `.html`, `.HTML`, `.HtMl`, `.htm`, and `.HTM` lookups preserve the exact nested owned VFS path.

Normal File Explorer Open resolves the extension through `DesktopService → AppRegistry`; Open With discovers Navigator through capable-handler enumeration. Settings Default Apps discovers both extensions from AppRegistry's known-extension snapshot and reports Navigator as built-in/effective with no configured override. None of those components contains a Navigator identity special case. Recent Programs continues to use the existing filesystem-open/one-time-handler recording semantics; browsing history remains local to Navigator.

Developer Studio is not a production `.html` handler today, so `.html` remains a single-handler type. There is no Settings choice between browsing and editing in this phase. Adding Developer Studio requires a separate audit and proof that its declared document editor genuinely supports HTML.

## Production evidence

Focused test results:

- Navigator local-document tests: **19/19**. Covered VFS path/URL encoding and decoding, spaces, mixed-case suffixes, invalid authority/control bytes, traversal, path bounds, fail-closed remote-to-local policy, empty/minimal/malformed HTML, exact bytes, title/text parsing, existing `docs/index.html`, inline CSS, script stripping/no JavaScript execution, the existing hosted inline-CSS regression fixture, relative images, relative/parent-relative URL resolution, missing/oversize input, and hosted VFS-root enforcement.
- Built-in document dispatcher/AppRegistry: **18/18**; PNG model cycles **100/100**; Navigator `.html`/`.htm` model-dispatch cycles **100/100**.
- Settings Default Apps model: **37/37**; interaction model: **9/9**; lifecycle cycles: **100/100**. The new rows are discovered dynamically and preserve canonical IDs.
- App Model owned file activation: **57/57** across associations, owned contexts, enumeration, and explicit resolution.
- Navigator URL-resolution smoke: **11/11**.

The Phase 12 hosted runtime smoke passes **11/11** checks. It drives File Explorer double-click Open and Open With and verifies exact owned paths containing nested directories/spaces, process and window identity, the initial parsed document title, local `file:///` address representation, successful relative PNG decoding, relative HTML navigation, history Back, external modification followed by Reload (including the replacement parser title), missing-file error behavior, unsupported-extension rejection, and sequential close/reopen state release. The compositor owner snapshot retained the startup title after the reload probe, so dynamic title propagation after Reload is not claimed by this smoke. Runtime activation counts and exact script checks are recorded in the closeout response.

The smoke snapshots/restores `desktop.json`, `desktop.state`, window bounds, and App Model default-handler state. Its fixture is isolated under a unique `tmp/phase12-navigator-*` directory.

## Regressions, builds, QEMU, and visual evidence

The hosted `navigator_file_io.cpp` path translator and bare-metal VFS read bound are shared by the kernel build, so a QEMU Navigator kernel smoke is required. Phase 12 runs the relevant focused QEMU scenario and reports its result separately.

The standard hosted build keeps NativeElf disabled. The experimental hosted build is also required for the Developer Studio regression. Existing Navigator HTML/CSS, URL/navigation/history, image-resource, form, and Phase 2I regressions, plus App Model Phases 5B–11 and Startup/Recent checks, are listed with exact outcomes in the closeout response.

The desktop environment does not expose native application windows to Computer Use. Therefore no screenshot or pixel-fidelity claim is made. Runtime compositor owner/title diagnostics, source-byte/parser tests, and the successful PNG decode are the available evidence.

## Git closeout

The phase began on `main` at `f75b8d054342a5b137e2baf03d91d723a8182fd`. The final commit, final worktree, cached divergence, and normal push attempt are recorded in the Phase 12 closeout response. The existing user `desktop.json` edit and pre-existing temporary artifacts remain excluded.
