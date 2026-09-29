# NativeAOT C152 — Managed Notes Save Workflow

**Outcome A: complete bounded Notes Save/Save As workflow validated.** The
proof used the existing managed Dialog, VFS, input, and App Model paths. It did
not add a modal stack or a second filesystem UI framework.

Run date: 2026-09-29. Branch: `v1.1_DOTNET_SUPPORT`. Starting HEAD:
`2652202eb66e366691320969223818bd7bb460ba` (the C151 baseline).

## C151 baseline and document state

C151 supplied the reusable managed Open File dialog, bounded VFS directory
enumeration, path validation, file revalidation, transactional Notes loading,
and same-instance Open behavior. Its focused 10-case suite and a real Open
workflow were rerun for C152; the full C151 baseline details remain in
[`NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG.md`](NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG.md).

The Notes `GuideXosTextArea` remains the only document-text buffer. Its bound
is 256 bytes and 32 lines. `GuideXosNotesDocumentState` stores only
`CurrentPath`/`HasCurrentPath` and `Dirty`; it does not copy or serialize
controls. This distinguishes untitled clean, untitled dirty, named clean, and
named dirty documents.

User edits mark the document dirty through the TextArea change event. Initial
TextArea setup and a successful programmatic load do not mark it dirty.
Successful Save/Save As changes the path if needed and clears dirty only after
the write has passed verification. A failed Save leaves the TextArea text and
current path unchanged and keeps dirty set.

The current Notes surface starts with the known C152 fixture file. No New
command was added. Untitled state and its Save As default are covered by the
document-state and chooser tests; there is no production menu path that creates
an untitled document in this phase. Save without a current path routes to Save
As.

## Save File dialog and filename policy

`GuideXosSaveFileDialog` chooses a destination and returns `Selected`,
`OverwriteRequired`, `Cancelled`, or `Error`; Notes performs the document
write. It shares C151's directory snapshot, ordering, parent navigation,
bounded paths, ListBox, and modal input routing. It displays the current
directory, a list bounded to 64 rows with 7 visible at once, filename input, Save, Cancel, and a bounded
status line. Directory rows navigate on Enter and never populate the filename.
Selecting a regular file copies its name but does not write it.

The dialog reuses `GuideXosTextInput`. C145's Dialog admits this leaf control
narrowly while continuing to reject recursive/container members such as
Panel, GroupBox, ScrollView, and Dialog. Save File has six dialog members,
four registered focusable controls, registration capacity eight, and one
active modal scope. Its real QMP wheel proof enters `/system/apps/FULL`, sends
wheel-down and wheel-up events through the native input path, and verifies the
ListBox viewport moved for both events.

The picker enforces a 127-byte component buffer and a 96-byte complete path
limit (`GxAbi.MaxDirectoryNameBytes` and `GxAbi.FilePathMaxBytes`). It rejects
empty names, spaces, non-ASCII input, `/`, `\\`, `.`/`..`, and names containing
`..`; it never truncates a path. These are the chooser bounds. The current FAT
creator is narrower for new files: `make_short_name` supports an 8-character
base plus an optional 3-character extension and has no long-filename create
path. Extensionless names are supported; `.txt` is suggested by the default
`untitled.txt` name but never appended automatically. The proof created the
extensionless path `/system/apps/C152/c152new`.

Save As starts in the current document's directory, or `/system/apps` for an
untitled document. Existing files require overwrite confirmation. The chooser
closes before Notes opens the C145 confirmation, so the one-modal-scope rule
holds. Cancel recreates the chooser from bounded directory and filename
strings; Replace revalidates the parent and target immediately before writing.
The target is checked again if it appeared after enumeration, and a directory
at the target path is rejected. A missing/non-directory parent fails without
changing the document state.

## VFS write and failure guarantees

The managed application uses `DirectoryList`, `FileStat`, `FileRead`, and
`FileWrite` capabilities for browsing, revalidation, load/read-back, and Save.
Paths remain under `/system/apps`; the managed application does not receive a
settings or system-file write capability. The host ABI remains version 1 with
the 104-byte host table.

For Notes' bounded 256-byte files, the C152 host write path snapshots an
existing regular file, calls the VFS write, checks type and exact length, and
reads the bytes back for an exact comparison. It restores the bounded original
on a reported failure or verification mismatch. If a failed new-file write
left a visible partial regular file, it unlinks that target. Only after the
managed read-back also succeeds does Notes commit the new path and clear dirty.
Empty files are valid and are verified by their zero-byte stat/read result.

The VFS exposes create, read, write/overwrite, stat, directory-list, unlink,
and rename operations, but no flush operation or atomic replace operation is
used by this workflow. Existing files are overwritten in place. The rollback
and verification protect against API-reported failures while the system is
running; rollback itself can fail. They do not provide power-loss atomicity,
durable flush, journaling, or a guarantee across sudden power loss. Serial
output records `flush=unavailable replace=non-atomic`; no stronger durability
claim is made.

The injected production failure performs a partial write to the existing
`/system/apps/C152/alpha.txt`, restores and reads back its original bytes, and
then retries successfully. The Notes text remains unchanged, the prior path is
retained, and dirty remains true after the failed attempt. The retry verifies
the new bytes and clears dirty. VFS read helpers open and close synchronously;
the directory bridge closes its iterator on successful completion and on
malformed/error results. No file handle is retained by Notes or either dialog.

## Dirty Open, Close, and Settings Center

Opening a new file while dirty asks whether to Save, Discard, or Cancel. Save
continues to Open only after a verified write. Discard is deferred until a new
file loads successfully; cancelling the Open chooser keeps the original dirty
document. Cancel leaves Notes and its current document unchanged.

Dirty Close uses `KernelApp::onWindowCloseRequested()` before teardown. Cancel
keeps Notes open; Discard authorizes close. A dirty unnamed document follows
the Save As path when Save is chosen, although the current production menu has
no New command to create one.

Dirty Notes-to-Settings Center is deferred until a decision. Cancel blocks the
transition; Save completes and verifies before C150 launches Settings Center.
The C150 one-level return behavior and clean transition remain in place. This
phase does not restore a Notes document session after C150 relaunch.

Save, Save As, and ordinary Open remain on the same Notes instance. Production
logs verify same-instance behavior for Save and Open, with modal owner none,
popup capture none, and drag owner none after Open. Only the Settings Center
transition uses the existing C150 lifecycle.

## Tests and regressions

Each Notes launch ran the following startup suites:

- C152 Save File chooser: **16/16**. Covers configuration/open, filename and
  path bounds, text editing, new/existing/directory targets, directory and
  parent navigation, existing-file name population, a target appearing after
  chooser open, Escape/modal cleanup, repeated open, shared ordering/capacity,
  shared wheel policy, and chooser state recreation.
- C152 document state: **10/10** core state cases, plus real VFS proofs for
  empty and 256-byte files, 50 alternating Save/read-back cycles, and five
  distinct Save As paths with current path advancing to the last success.
- C145 Dialog/MessageBox/modal routing: **49/49**.
- C151 focused Open dialog suite: **10/10**.
- C137 input/TextArea/ListBox wheel suite: **46/46**.
- C150 return-target and managed-lifecycle suites: **10 return-target cases**
  and **8 lifecycle cases**.
- C146/C148 settings compatibility: GXSETT.BIN stat/read checks passed and
  runtime setting file format v2 loaded with its existing scroll policy.

The Save As production boot exercised a new extensionless file, an existing
file, overwrite Cancel with chooser/path/name recreation, then Replace and
exact read-back. The same boot exercised dirty Open Cancel, deferred Discard
followed by chooser Cancel, deferred Discard followed by a successful C151
Open, and dirty Close Cancel then Discard. The Settings boot exercised dirty
Settings Cancel and verified Save before C150 launch. The failure/recovery
boot exercised partial-write restoration and a successful retry.

The dedicated full C151 scripted suite remains historical evidence; C152
reran the C151 focused suite on each fresh Notes launch and exercised a real
production Open of `/system/apps/C151/alpha.txt`. C145, C137, and C150 focused
regressions were rerun on each Notes launch, not carried forward as historical
claims.

## Build, QEMU proof, and restoration

The production NativeAOT composite and a fresh C112-C152 kernel build passed
with zero errors. Existing toolchain/kernel warnings remain. Settings format
v2, ABI v1/table 104, one active application surface, one modal scope, and the
existing application-owned control model remain unchanged.

The final six-boot proof is recorded in
`out/dotnet/c152-managed-notes-save-workflow/c152-proof-manifest.json` and was
run by
[`run-c152-managed-notes-save-workflow.ps1`](../../scripts/dotnet/run-c152-managed-notes-save-workflow.ps1).
The final runner invocation reused the identical proof kernel from the
preceding fresh full kernel build. Composite and proof-kernel hashes:

| Artifact | SHA-256 |
| --- | --- |
| NativeAOT composite ELF | `A6888D9EAAF5B1E8CF7FC36D4F33E575FC6CBB9944BC8C5B892E3CF861396112` |
| C152 proof kernel | `AF2B22F13F13F0F78774118D2BF4E6D4390E85080E098A8DE4953BEB5D7A29E6` |

The three fresh production QEMU boots passed:

| Boot | Scenario | Serial SHA-256 |
| --- | --- | --- |
| 1 | Existing-file partial-write failure, rollback, retry | `1689EA53C1C1F439D3227BAF5D81771C34643DF59046FBF10DAE120ACE3B359C` |
| 2 | Save As, real Save dialog wheel, overwrite, dirty Open/Close | `71E6DAD3148BB70DC355898FA8BCB837F291C4886FC94C5D4D462936AAEB8783` |
| 3 | Dirty Settings Cancel, verified Save, C150 launch | `C93B8724D1456D64E61FBEE53E45C2BFA87189569798DE56C125B9490795DD82` |

After the proof boots, canonical kernel and protected filesystem hashes were
restored and verified byte-for-byte:

| Protected artifact | Before and after SHA-256 | Status |
| --- | --- | --- |
| `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` | `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` | Restored; both match |
| `ESP/ramdisk.img` | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` | Restored; matches |

Three ordinary restoration boots then passed using the canonical kernel and
ramdisk:

| Boot | Serial SHA-256 |
| --- | --- |
| 1 | `F2B1E78E8B6019F4CDE18BA87A8ECF30ED60CAC856FC587617AF2E228D9E6F76` |
| 2 | `E53A8D0649835A22988172129F985BA2F3005D24E188CB0E98955EEAC877F9AA` |
| 3 | `166B4203F4E73DC118696040C4764EE0D516D4188277670777A8F53896CCDE93` |

The proof documents and mutated fixtures live only under `out/dotnet/` and the
temporary proof ESPs. Their changes did not enter the canonical ramdisk; its
before/after hash is identical. The manifest also records per-boot kernel and
proof-ramdisk hashes and each serial-log path.

## Deferred scope and limits

C152 adds no New command, autosave, crash recovery, journaling, undo/redo,
history, recent files, multiple documents, tabs, background saves, file
locking, collaboration, cloud sync, rename/delete UI, generic file-manager
operations, or document-session restore across a C150 relaunch. Save File
remains an in-app destination chooser, not File Explorer. Failed saves retain
the in-memory document, and dirty Open/Close/Settings paths ask before
discarding it.
