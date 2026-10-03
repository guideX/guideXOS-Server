# App Model Phase 16: First-Class Folder Activation

Status: Outcome B (generic folder activation is implemented; concurrent File Explorer instance state remains bounded by its existing process-static model). The folder-default and Settings decisions are deliberate: AppRegistry owns generic folder capability and selection, while persisted folder defaults and a Settings row are deferred until a second genuine production file manager exists.

## Previous behavior and scope

External directory opens used File Explorer directly, often through `OpenFilesystemEntry` and sometimes through a path-taking `FileExplorer::Launch`. File Explorer itself already had a separate internal navigation path: list/tree activation calls its navigation model and does not need AppRegistry. Normal no-argument File Explorer application launches also remain application launches.

Phase 16 adds a distinct folder activation alongside document and URI activation. It reuses `gxos.builtin.fileexplorer`; it does not add a pseudo-application or treat folders as extensions. The canonical flow is `DesktopService::OpenFolder` → VFS inspect/normalize → AppRegistry selection and generation check → generic built-in activation dispatcher → File Explorer with an owned `Folder` context → its existing initial path and directory listing model.

## Capability, identity, and policy

`AppManifest::supportsFolderActivation` is the explicit capability. AppRegistry rebuilds a bounded folder-handler list from registered manifests and exposes capable-handler enumeration. Each row reports canonical ID, registration owner/generation, current identity, capability, backend availability, and built-in/effective-default status. File Explorer is truthfully registered as the built-in folder handler. The model tests add a synthetic handler only in test code and show that enumeration and explicit one-time selection are generic.

Folder selection has no persisted override today. With one real production handler, File Explorer remains the built-in/effective default, while AppRegistry supports a selected capable handler identity for a one-time activation. Settings → Default Apps does not show a Folders row until there is a second genuine production handler and a grounded user choice. This avoids policy persistence and UI state with no available alternative.

The activation context is `AppActivationKind::Folder` with an owned `folderPath`. The App Model path bound is 4096 bytes, shared with the existing document path bound. Resolution and dispatch revalidate canonical identity, registration owner/generation, current capability, and handler backend availability. The dispatcher dispatches by the ID AppRegistry selected; it does not infer File Explorer from the folder kind.

## VFS validation and File Explorer integration

`OpenFolder` uses File Explorer's filesystem provider to apply its existing VFS normalization and verify that the normalized target exists and is a directory before resolving a handler or launching. A file supplied as a folder and a missing path fail before launch. `OpenFilesystemEntry` now stats first: an actual directory goes to folder activation, and an actual file goes to document activation even when the caller's directory hint is wrong. `OpenFilesystemEntryWithHandler` rejects directories before document dispatch.

On hosted builds, the same provider maps and normalizes the hosted VFS. On bare-metal builds the VFS normalizer may truncate to its fixed output buffer; the preflight normalizes into an input-sized temporary and enforces `VFS_MAX_PATH` (256 bytes) before the provider's fixed-buffer stat/list calls. Thus no overlong input is allowed to redirect through truncation. Existing VFS grammar and behavior for repeated separators and `.` / `..` segments are retained.

File Explorer consumes the owned activation into its existing `s_currentPath`, refresh, and entry enumeration path; invalid/disappeared/non-directory targets are rejected before the window is created. Its default launch still selects the first existing root (or `/`), and Delete Confirmation arguments remain a separate special workflow. Internal list/tree navigation calls the existing `navigate` routine directly. It does not re-enter AppRegistry, create new activation history, or pin the external starting path. The runtime smoke also exposed that File Explorer read compositor key input's modifiers/window fields as part of the action string; it now parses the action field so normal targeted Enter navigation is delivered to that same internal model.

## External callers audited and migrated

- Desktop filesystem-entry icons and shortcut targets call `OpenFilesystemEntry`; actual VFS type now chooses folder versus document activation.
- Desktop “Open Target Location” uses generic `OpenFolder`.
- The hosted `desktop.open.folder <path>` API and path-taking `fileexplorer <path>` command use generic folder activation. `desktop.open <path> [hint]` preserves normal document opening and uses actual VFS type, so a directory with a misleading hint still opens as a folder.
- Start/shell folder shortcuts use the generic filesystem-entry/folder API after ensuring their standard directories. File Explorer’s internal tree/list navigation remains direct.
- No additional application-specific “open containing folder” feature was found. No separate volume activation exists: mounted roots and removable mount paths are ordinary VFS directories and follow the same contract; unmounted paths fail normal VFS inspection. Hosted path inspection retains `std::filesystem` existence/directory behavior, including resolution of host symlink targets. Bare-metal shell reports symbolic-link creation as not implemented; no symlink-specific activation policy was added.
- Remaining direct no-argument File Explorer launches are ordinary application launches from desktop/server/module-manager entry points. The delete-confirmation path launches its dedicated workflow with a parent path; it is not an external request to open a folder.

## Lifecycle, Recent Programs, and bounded limitation

Successful external folder activation records File Explorer once according to existing Recent Programs semantics. Internal navigation does not record another launch. Runtime smoke compares the Recent Programs section before and after navigating into a child folder.

File Explorer stores window and navigation state in process-static fields. Its current hosted launch path can start a process-table activation for each external request, but those activations do not own independent copies of this static state. Phase 16 smoke proves sequential external launch, process/window ownership, initial path, navigation, and clean close. Concurrent folder activations are not claimed to have independent state or reliable reuse behavior; resolving that requires a focused instance/reuse policy in File Explorer. This is the bounded Outcome B limitation.

## Verification

The focused model test passed 26/26 assertions, including capability, enumeration, built-in/effective default, root/nested/empty/spaced/dotted/file-like paths, path ownership/bounds, synthetic alternate selection, stale-generation defense, no-handler handling, 100 resolution/current cycles, and 100 invalid-path/no-handler/stale-generation cycles.

Hosted runtime smoke passed 53/53 checks. Root opened and enumerated; the nested folder enumerated its file and child folder; empty, spaced, dotted, and `photo.jpg` directories opened as directories. Repeated separators and `..` normalized through the existing provider. File-as-folder, missing, overlong, and control-containing paths failed before a File Explorer process/window was created. A real document with a wrong directory hint reached Notepad. The owned File Explorer window title and owner matched each requested initial path. Internal child navigation and Back history worked without a new Recent Programs entry, and every tested File Explorer/Notepad process and window closed cleanly. The smoke performed eight sequential external folder activations; concurrent activations are outside the verified instance behavior described above.

The final standard hosted build succeeded with 177 warnings and zero errors, matching the pre-Phase-16 build warning count. No new warnings appeared in changed source files; the existing `process.cpp` warning and the existing compositor warnings are unchanged. The overlong document-path check runs before VFS normalization so it retains the established document-specific error without including the oversized path in diagnostics.

The implementation is hosted-only in the current production target. `kernel/Makefile` builds `kernel/core/*.cpp`; hosted AppRegistry, DesktopService, and File Explorer sources are not in that QEMU source set, and no kernel/core source changed. The preflight still preserves the existing bare-metal provider path bound. QEMU boot evidence is therefore not applicable to this hosted consumer change. Native window screenshot inspection was unavailable; runtime log path/entry count, process snapshots, and compositor owner/title output are used instead.

## Regression evidence

The hosted regression pass succeeded for Phase 4D Recent Programs, Phase 5B, Phase 6 document activation, Phase 7 Open With, Phase 11 ImageViewer PNG, Phase 12 Navigator HTML, Phase 13 ImageViewer JPEG, Phase 14 legacy-association retirement, and Phase 15 URI activation. Phase 6 passed its 39 assertions, including 20 repeated document loads, missing-file delivery to Notepad, unknown-extension rejection, and overlong-path rejection. Phase 14 passed 27/27 checks, including 106 unsupported-open cycles. Its snapshot wait now waits for a fresh association diagnostic before comparing AppRegistry state; this avoids reading an incomplete earlier response.

The Phase 8 default-handler checks passed: 54/54 model checks, 100 persistence reload cycles, 100 ordinary dispatch cycles, and 100 one-time Open With dispatch cycles. The Phase 9 Default Apps model passed 39/39 checks, its interaction checks passed 9/9, and 100 lifecycle cycles passed. Settings Center passed 89/89 model checks. Startup registration and persistence passed; Phase 4D verified canonical Recent Programs behavior and restoration of persistent desktop state. Phase 15 URI activation passed against the final standard hosted binary.

The final experimental hosted build succeeded with 176 warnings and zero errors. Phase 10 passed 10/10 checks, including the production Open With path to Developer Studio, four process/window close cycles, canonical Notepad one-time selection, and the AppRegistry-selected `.CPP` default. QEMU boot evidence is not applicable for this hosted-only consumer change. Screenshot capture was unavailable; validation used File Explorer's logged path and entry count, process snapshots, and compositor owner/title evidence.
