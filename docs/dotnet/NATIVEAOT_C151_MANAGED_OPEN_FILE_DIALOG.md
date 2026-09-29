# NativeAOT C151: Managed Open File Dialog

## Outcome

C151 adds an application-owned, reusable Open File chooser to the existing managed modal system and uses it from Managed Notes. The chooser browses the real managed VFS, returns one bounded path, and leaves Notes running in the same application instance. It does not launch an application or arm the C150 return target.

The reproducible build and QEMU proof is `scripts/dotnet/run-c151-managed-open-file-dialog.ps1`. Its generated `out/dotnet/c151-managed-open-file-dialog/c151-proof-manifest.json` records build hashes, serial-log hashes, the three C151 boots, the three ordinary restoration boots, fixture hashes, and the protected-artifact restoration result.

## Existing VFS support audited

Managed VFS directory enumeration already existed in the C114 host contract, so C151 adds no VFS ABI operation. `GuideXosHost.TryListDirectory` invokes the existing `directoryList` function, which calls `vfs::stat`, `vfs::opendir`, and `vfs::readdir`, copies a fixed snapshot of at most 64 entries, and closes the iterator on success, malformed entry, unsupported entry type, overlong name, and truncation. The bridge exposes entry name, regular-file/directory type, size, and a `HasMore` flag. It exposes no hidden/system attribute, so the chooser applies no invented hidden-file rule.

The existing host also supplies `TryGetInfo` through `FileStat` and `TryReadAllBytes` through `FileRead`. C151 adds a caller-buffer `TryReadInto` overload so Notes can read without allocating a buffer sized from file metadata. The file read callback is synchronous and retains no caller pointer or open handle. The chooser itself only lists and stats entries; it never reads file contents.

The chooser requires `DirectoryList` and `FileStat`. Loading a selected file requires `FileRead`. Paths use the existing managed application VFS root `/system/apps`. Save remains governed by the pre-existing Notes `FileWrite` capability and is outside C151's Open workflow.

The host ABI stays at version 1 and table size 104 bytes. No native ABI, capability, VFS, or settings format was added or changed for C151.

## Dialog and bounded directory model

`GuideXosOpenFileDialog` is an application-independent managed component. Its owner supplies the host, current surface, parent `GuideXosControlHost`, and a deterministic initial directory. The chooser composes one existing `GuideXosDialog`, a `GuideXosListBox`, labels, and Open/Cancel buttons. Its result is a value containing `Pending`, `Selected`, `Cancelled`, or `Error`; only `Selected` contains the copied path. The dialog is not an application and does not create a modal stack, recursive control tree, focus manager, or capture stack.

C145's bounded leaf-member policy now admits `GuideXosListBox`. Pointer hit testing, focus routing, keyboard input, rendering, and wheel input are routed through the existing dialog child scope. The existing ListBox keeps its selection and viewport behavior. Panel, GroupBox, ScrollView, and another Dialog remain unsupported dialog members. The list itself is fixed at 64 rows with an eight-row viewport.

Directory data is bounded by the existing 64-entry C114 snapshot and a fixed 64-row chooser model. A `[MORE]` row reports omitted entries; C151 does not page or allocate from an unbounded directory. In a non-root directory, one row is reserved for `[UP] ..`; when truncation is needed, another row is reserved for `[MORE]`, leaving up to 62 actual entries. At the managed VFS root there is no parent row.

Ordering is deterministic: parent first, directories before regular files, ASCII case-insensitive name order, then ordinal name order as a tie-break. `.` and `..` from the VFS are ignored because parent navigation is represented by the chooser's own row. Names and types come from VFS metadata; rows are labeled `[DIR]` or `[FILE]`.

The existing path contract allows up to 96 path bytes and up to 127 filename bytes. The chooser confines paths to `/system/apps`, rejects traversal, separators within a filename, malformed components, and overlong composed paths, and never silently truncates a path. The parent row is a no-op at `/system/apps`; child navigation starts a fresh deterministic snapshot and selection. A selected directory is entered by Enter. Open is disabled unless a regular file is selected; Enter on a directory navigates instead of returning its path.

## Notes integration and failure behavior

Managed Notes exposes `File -> Open`. It opens the chooser at `/system/apps`, then continues in the same Notes instance after selection or cancellation. Escape and Cancel return no path and preserve the document. Choosing a regular file closes the chooser with one path; Notes revalidates that path and stats it again to account for changes between listing and load.

Notes has a 256-byte, 32-line TextArea. C151 reads into a 256-byte stack buffer, then validates the complete input under the existing text contract: printable ASCII and LF are accepted; CR, other control bytes, and non-ASCII input are rejected. Files over 256 bytes are rejected before replacing the editor; a file that grows after stat is rejected by the bounded read. `SetUtf8` is called only after the read and validation succeed, so a failed load keeps the previous text and path. A successful load updates the path, resets the caret to the start, and displays the loaded content. Dirty tracking and Save As do not exist in this phase.

Listing, stat, read, invalid-path, capability, and unsupported-content failures are kept in bounded chooser or Notes status text. A failed open leaves Notes active, preserves the document, and allows another attempt. The selected path is rechecked as a regular file before the dialog returns it; Notes performs its own stat and bounded read after the modal closes.

## Wheel settings and regression coverage

The chooser's ListBox consumes the existing shared wheel event and runtime settings. The proof seeds settings format v2 with `NaturalScroll=true` and `ScrollLinesPerNotch=5`; the modal list must move exactly five rows in the configured direction while Notes' background ListBox and TextArea remain unchanged.

The C151 launch runs the focused 10-case C151 suite for path bounds/traversal, sorting, snapshot truncation, dialog ListBox admission and isolation, keyboard/focus containment, pointer/outside-click behavior, wheel policy, and Escape restoration. It also runs the existing 49-case C145 modal suite, the 46-case C137 suite, and the relevant C150 return-target/application-lifetime checks. Existing managed file-read bounds and capability checks are reused; the 256-byte exact-fit and 257-byte rejection paths are also exercised with real files.

## Proof procedure and evidence

The runner builds the production `C151Composite` NativeAOT ELF with the `Primary4MiB` heap, builds the proof kernel with C112-C151, and stages fixtures only on a separate proof FAT image. The fixtures cover nested navigation, zero-byte and exact-256-byte files, a 257-byte rejected file, and a 66-entry directory.

It performs three fresh C151 QEMU boots:

1. Pointer Open, outside-click modal blocking, a real wheel event under the non-default five-line policy, and opening the 256-byte file.
2. Keyboard Tab/Shift+Tab, Enter into a child directory, parent navigation, opening nested and root files, then Escape with the document unchanged.
3. Twenty-five cancel cycles and twenty-five successful open cycles, directory overflow, oversized-file rejection with preservation, and empty-file open.

Each C151 boot also checks the C137 46-case marker. The serial proof requires the Notes instance/surface identity to remain unchanged, the modal and popup/pointer captures to be clear after completion, and the C150 return target to remain unarmed. The third boot is the repeated-lifecycle and explicit error-path proof.

Afterward, the script restores the canonical kernel, ESP kernel, and protected ESP ramdisk from byte-identical backups and verifies their hashes before running three fresh ordinary QEMU boots. It checks the same protected hashes again after those boots. C151 media and fixtures are never copied into the canonical ramdisk.

For each boot, the runner records the serial log and the staged kernel/ramdisk hashes, then removes that boot's disposable ESP copy. This keeps the evidence reproducible without retaining six extra 64 MiB ramdisk copies.

The generated manifest is the authoritative record for the current run's NativeAOT and proof-kernel SHA-256 values, all six serial hashes, protected-artifact hashes, and restoration status. Historical phase evidence is not substituted for these reruns.

## Scope kept out of C151

C151 supplies Open File only. Save As, overwrite prompts, file creation, rename/delete, multi-select, folder-only results, tree views, search, bookmarks, recent files, preview, and File Explorer replacement remain deferred. The component is an in-application modal chooser; opening a file does not relaunch Notes or any other application.
