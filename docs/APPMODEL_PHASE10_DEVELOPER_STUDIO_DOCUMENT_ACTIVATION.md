# App Model Phase 10: Developer Studio document activation

Date: 2026-10-02

## Result

**Outcome B — Developer Studio consumes real App Model document activations and
the editor lifecycle is bounded and tested. The hosted NativeElf execution
backend is still opt-in (`build-native-experimental.bat`); the ordinary hosted
build correctly leaves NativeElf document handlers unavailable.**

The missing document path was repaired across the App Model, NativeElf host
ABI, and Developer Studio's existing workspace/editor loader. In the
experimental hosted runtime, File Explorer's existing Open With menu enumerates
Notepad and Developer Studio for `.txt`; File Explorer's ordinary Open also
resolves a standalone `.cpp` file to Developer Studio under the existing
single-handler fallback policy. No Developer Studio branch was added to File
Explorer or Settings.

## Checkout and preservation

- Server repository: `D:\dev\guideXOSServer`
- Server branch and starting HEAD: `main`,
  `a700dbc8453142260486021ccbb61f27c3b9e93c`
- Server upstream at start: one commit ahead, zero behind `origin/main`.
- Developer Studio repository: `D:\dev\guideXOS_Developer_Studio`
- Developer Studio branch and starting HEAD: `main`,
  `b236243e0af5d22831af5edffa9fa45c4a40bb0a`
- Developer Studio upstream at start: zero ahead, zero behind `origin/main`.

The pre-existing Server `desktop.json` edit and startup/settings fixture
directories were preserved. The unrelated Developer Studio debugger edits and
fixtures were preserved and excluded from Phase 10 commits.

## Previous blocker and repair

AppRegistry already retained an owned `AppActivationContext` for document
selection, but the hosted dispatcher only knew how to launch built-in Notepad.
NativeElf `gx_main` received neither the file path as an argument nor a host
call to retrieve it. Developer Studio consequently started an empty workspace,
and its package declared no document capabilities. Merely marking it available
would have lost the target path at the process boundary.

The Server now appends `get_document_activation_path` to the versioned Native
App host call table. The NativeElf runtime owns the activation context for the
process lifetime and copies the complete NUL-terminated path into a caller-owned
buffer. A short output buffer reports the required size and receives no
truncated path. Ordinary application launches return an empty path. The
existing App Model 4096-byte activation-path validation remains in force.

The generic NativeElf document dispatcher forwards the same owned activation
to the normal runtime launch pipeline. AppRegistry reports a declared NativeElf
handler available only when experimental execution is enabled, the architecture
and launch decision are compatible, and the package entry file exists. This
preserves truthful availability in the ordinary build.

On the Windows hosted server, File Explorer names files under a slash-rooted
virtual filesystem, while NativeElf's hosted filesystem callbacks take absolute
host paths rooted at the server working directory. At the NativeElf boundary,
the dispatcher resolves that virtual path against the working directory; it
leaves the VFS path unchanged for built-in handlers such as Notepad. This keeps
the selected file identity while making the hosted editor's stat/read/save
callbacks address the same file.

Developer Studio obtains the path after creating its normal window, validates
its registered extension set again, and calls
`WorkspaceControllerOpenStandaloneDocument`. That entry point creates a
projectless workspace rooted at the document's parent and then delegates to
`WorkspaceControllerOpenDocument`, the same stat/read/binary-check/document
creation path used by the normal workspace Explorer. It does not create a
project file or fabricate project metadata.

## Identity, types, and defaults

The existing canonical ID is `com.guidexos.developerstudio`. The production
manifest now declares document activation for:

| Extension | Capability | Default after registration |
| --- | --- | --- |
| `.c`, `.cc`, `.cpp`, `.cxx` | C/C++ source text | Developer Studio when it is the only capable handler |
| `.h`, `.hh`, `.hpp`, `.hxx` | C/C++ header text | Developer Studio when it is the only capable handler |
| `.txt` | Plain text through the real editor loader | Notepad remains the built-in default |

The `.txt` declaration creates the first real production competition. The
capable-handler snapshot contains both `gxos.builtin.notepad` and
`com.guidexos.developerstudio`; Notepad remains the effective default until a
valid Default Apps override is selected. `.log`, `.ini`, and `.cfg` remain
Notepad-only. C# and assembly-like formats are not declared because this phase
does not establish safe standalone editing for them.

Source extensions have no built-in default in current policy. The existing
fallback to the first available capable handler therefore makes Developer
Studio the effective `.cpp` handler, without writing a machine-global override.
Registration does not mutate any saved default. Canonical IDs, owner/generation
checks, duplicate-label handling, and durable-handler rules remain in the
shared App Model.

## File, path, and editor behavior

- The activation context owns the path until the native app exits. The C ABI
  returns an exact bounded copy and cannot hand Developer Studio a pointer into
  File Explorer or AppRegistry storage.
- Developer Studio accepts only the nine extensions listed above, case
  insensitively. Its workspace path model is 768 bytes; hosted filesystem calls
  have a 240-byte path bound. A path that exceeds the editor/API bound fails
  safely even though the shared App Model accepts paths up to 4096 bytes.
- The existing editor limit is 256 KiB per document. The loader preserves raw
  file bytes and does not convert newline sequences. This is not a claim of
  universal text-encoding support; binary-looking NUL-containing content and
  oversized files are rejected by the existing loader.
- Empty files retain their existing path identity and open as named documents.
  A missing or unreadable activation target reports the normal loader error;
  there is no empty-file fallback and no redirection to another path.
- Opened documents use the ordinary `Document` buffer, dirty tracking,
  close-protection, and Save operation. Save writes to the retained document
  path. The current UI has no Save As operation, so Save As identity behavior
  was not applicable to this phase.
- Each NativeElf activation has a separate process/runtime context. The focused
  controller tests also open documents in two controllers and verify separate
  paths, buffers, cursor/dirty state, and close behavior. Existing multi-tab
  workspace limits remain unchanged; no cross-process document routing was
  invented.
- Project-only operations continue to use their existing no-project behavior
  for a standalone document. Build Project, Run Project, and project metadata
  are not enabled by fabricating a project.

## File Explorer and Settings integration

File Explorer's existing `GetDocumentHandlersForPath` snapshot and
`OpenFilesystemEntryWithHandler` path select Developer Studio by canonical ID;
the menu remains generated by AppRegistry enumeration. The experimental hosted
runtime smoke clicks the actual File Explorer Open With submenu for a nested,
mixed-case `.TXT` document, observes two production handlers, and checks the
exact activation path, loaded-byte count, and content hash in Developer
Studio's runtime marker. Three launch/read/close cycles pass with a directory
name containing a space and mixed CRLF/LF content. The smoke also selects
Notepad as a one-time choice, verifies it remains the other production handler,
and uses the existing context-menu Open action for `.CPP` to verify the
ordinary typed-dispatch route reaches Developer Studio. It checks Recent
Programs after Studio activations and confirms those activations do not add a
file to Recent Documents.

Settings contains no Developer Studio-specific choice. Its Default Apps model
discovers rows and eligible handlers from the same AppRegistry data, while the
production Settings backend delegates to `DesktopService` default-handler
methods. The App Model integration test loads the production manifest into a
real AppRegistry and passes it through the Default Apps interaction model; it
selects Developer Studio for `.txt`, verifies the canonical override and
effective handler, resolves one-time Notepad without changing the override,
and uses Restore to return ordinary Open to Notepad while leaving Developer
Studio available. The test uses an isolated temporary store. The native Settings
window itself was not visually inspected in this environment.

## Validation

| Area | Result |
| --- | --- |
| Developer Studio document activation checks | 145/145 |
| Developer Studio model lifecycle stress | 100/100 open/close cycles |
| Production AppRegistry + Default Apps integration | 52/52 checks; 100 persistence/reload, ordinary Open, and one-time Open With cycles |
| Settings Default Apps | 36/36 model, 9/9 interaction, 100/100 lifecycle |
| Settings Center | 89/89 |
| File Explorer Open With model | 16/16; 100/100 lifecycle |
| Settings inventory | 21/21 device, 24/24 storage |
| Settings S6 | 42/42 |
| Settings S7 | 21/21 accessibility, 23/23 Developer, 14/14 navigation/search |
| Phase 6 hosted runtime | PASS; 20 document activations, 7/7 Explorer, 29/29 Notepad, 3/3 negative cases |
| Phase 7 hosted runtime | PASS; 20 explicit handler selections with exact Notepad paths; ordinary Open remains Notepad |
| Phase 5B readiness closeout | PASS; readiness predicate true, 0 unresolved and 0 high-risk records |
| Startup App Model regression | PASS |
| Recent Programs Phase 4D | PASS; all assertions true and temporary state restored |
| Standard hosted build (`build.bat`) | PASS; 184 warning diagnostics across the project, none in changed AppRegistry/desktop/ABI code |
| Experimental hosted build | PASS after restoring three omitted sources to the experimental source list |
| Developer Studio production routing smoke | PASS; 10/10 checks, four real process launch/close cycles, exact path/content hashes |
| Native filesystem contract test | Blocked at compilation by the checkout's missing `third_party/mbedtls/tf-psa-crypto/include/mbedtls/private/crypto_adjust_config_enable_builtins.h`; the test did not run. |
| QEMU | Not run: all Phase 10 code changes are in hosted AppRegistry/NativeElf host services and the hosted Developer Studio process; kernel/bare-metal App Model launch paths are unchanged. |
| Visual inspection | Native app windows are not exposed to this automation environment. Menu procedures, runtime ownership, model/service calls, and app markers provide the nonvisual evidence. |

Developer Studio's full `build.ps1` test/package workflow passed using a
temporary compatibility SDK include overlay. The checked-in Server SDK and
standalone Developer Studio source are version-skewed: the Developer Studio
tree references older ABI/debug/run declarations missing from the current
Server SDK. The overlay restored those compatible declarations and appended
the current `play_pcm` and `get_document_activation_path` callbacks at their
current host-table offsets. The overlay is validation-only and was not
committed. Existing compiler warnings came from pre-existing debugger sources;
Phase 10 activation code added no Developer Studio compiler warnings.

## Scope and remaining gate

The shared Server ABI, runtime context, AppRegistry availability calculation,
and generic NativeElf dispatcher are implemented. The ordinary `build.bat`
does not enable NativeElf execution, so it correctly exposes no available
Developer Studio document handler. The demonstrated real process and File
Explorer route require `build-native-experimental.bat`; enabling that executor
by default is a separate runtime-production decision. This bounded backend
gate is why this closeout is Outcome B rather than claiming general hosted
production availability.

Phase 10 does not change Startup state, Recent Programs policy, the kernel
launch path, project formats, Save As, encoding conversion, or document-recent
behavior. Validation restores the starting `desktop.json`, default-handler
configuration bytes (or its absence), and window-bounds state.
