# NativeAOT C115 Managed Open and Save File Picker

Status: implementation and validation record for C115.

## Handoff and boundary

C114 finished the generic managed filesystem layer at commit
`9b0cc5f74fb387e08fca43f0c509b6aeea2f0d5d`. C115 uses that layer unchanged:
`TryListDirectory` discovers candidates, `TryGetInfo` validates the selected
target, and `TryReadAllBytes`/`TryWriteAllBytes` remain the document service.
No VFS operation, FAT structure, host table field, capability bit, NativeAOT
runtime, GC algorithm, or native dialog callback was added.

The boundary is deliberately three-layered:

| Layer | Responsibility |
|---|---|
| `GuideXosFilePicker` | enumerate, filter, select, validate, cancel, and return a path |
| `GuideXosFile` | read/write bytes and query generic metadata |
| application | decide which document data to load or save |

This lets Notes, a word processor, spreadsheet, drawing app, or Developer
Studio share the same conceptual service without knowing the Server VFS ABI.

## Managed API

The shared implementation is
`samples/managed/HostLogProof/GuideXos/GuideXosFilePicker.cs`.

```csharp
var result = picker.OpenFile(host, surface,
    GuideXosFilePickerOptions.Open("/system/apps", "Open document", ".TXT"));

var save = picker.SaveFile(host, surface,
    GuideXosFilePickerOptions.Save(
        "/system/apps", "Save document", ".TXT", "THIRD.TXT", true));
```

`OpenFile` and `SaveFile` begin a bounded operation and return `Pending` after
the surface is rendered. The caller forwards compositor action IDs to
`HandleAction`. Completion is therefore a state-machine result, not a nested
runtime thread or unsafe synchronous wait.

`GuideXosFilePickerResult` includes:

- `Status`;
- `Path` when selected;
- selected `GuideXosFileInfo` when metadata exists;
- the underlying `GuideXosFileResult` where applicable;
- a bounded user-visible `Message`.

Statuses distinguish `Selected`, `Cancelled`, `InvalidRequest`,
`CapabilityUnavailable`, `NoMatchingFiles`, `InvalidSelectedPath`,
`DirectorySelectionRejected`, `OverwriteConfirmationRequired`,
`OverwriteDeclined`, `MetadataFailure`, and `IoFailure`.

Options are intentionally small: mode, initial directory, title, one
extension filter, suggested Save filename, `RequireExistingFile`, and
`ConfirmOverwrite`. Notes uses `/system/apps`, `.TXT`, and a case-insensitive
filter. The UI exposes the title, directory, filter, candidates, current
selection, proposed filename, status, and Open/Save/Cancel actions.

## Path and filter policy

`GuideXosPickerPath` is the shared path-construction helper. It canonicalizes
the initial directory by removing trailing separators, accepts only printable
absolute paths under `/system/apps`, rejects backslashes, repeated
separators, traversal, and paths over 96 UTF-8 bytes. A filename component
rejects empty names, `.`, `..`, any `..` traversal sequence, `/`, `\`,
control/non-printable characters, and a full path over 96 bytes.

The filter is empty (all regular files) or one simple extension such as
`.TXT`; wildcard and filter-group syntax is not accepted. Directories are
displayed as `[DIR]` when present but never pass the Open regular-file type
gate. Save appends `.TXT` when the suggested name has no extension and rejects
an unrelated explicit extension. It never silently changes an unrelated
extension.

The picker obtains its bounded candidate snapshot from real
`TryListDirectory` output. Repeated operations in the same directory reuse
that managed snapshot; a newly selected Save destination is added to it so a
later Open sees the new file without re-entering the VFS directory iterator.
Notes does not contain a filename list or perform its former directory-browser
enumeration.

## Open behavior

Open validates and stats the initial directory, enumerates it, filters regular
files, renders a bounded list, and tracks a selection with `Next`. `Open`
stats the selected full path again, rejects directories, and returns the
canonical path. Notes then calls `ReadAllTextUtf8` itself. Open does not read
document contents inside the picker.

Cancellation returns `Cancelled`; no document state is changed. An empty
filtered result renders `No matching files`, disables the possibility of a
valid selection, and still permits Cancel.

## Save behavior

Save validates the directory and proposed filename before rendering. The
current Server control set has no general text editor, so C115 uses a bounded
suggested filename field supplied by the application. The field is visibly
separate from the eventual selected path. Notes uses deterministic fixture
names for proof (`THIRD.TXT`, then `NOTES.TXT` for the overwrite scenario).

On Save, the picker validates the destination and compares it with the
bounded directory snapshot. An existing candidate is re-statted and, when it
is a regular file, enters explicit overwrite-confirmation state. A new
destination returns `Selected` without writing. `Decline` returns
`OverwriteDeclined` and keeps the picker available for a retry; `Overwrite`
re-stats and returns `Selected`.
Notes performs `WriteAllTextUtf8` only after the selected result. A missing
`FileWrite` capability therefore affects Notes' write step, not path selection.

## Notes migration and proof flow

The C115 build of `ManagedNotes` uses `GuideXosFilePicker` for Open and Save
As. Its application-owned surface is reused as the picker surface, so there
is one logical window and one managed runtime. Notes retains document state,
while transient picker state is reset on Begin, Cancel, and completion. The
bounded directory snapshot is retained as a managed cache for the same
directory and updated when Save creates a new destination.

The fresh-boot harness exercises:

1. enumerate and open `NOTES.TXT`;
2. select and open `SECOND.TXT`;
3. edit and Save As `THIRD.TXT`;
4. enumerate and reopen `THIRD.TXT`;
5. Save As existing `NOTES.TXT`, decline overwrite, verify original bytes;
6. retry and explicitly confirm overwrite, verify replacement bytes;
7. Open Cancel, Save Cancel, `.ZZZ` no-match, invalid path cases, and
   capability downgrades;
8. Workspace, Status, Counter, native Notepad, and Notes regressions.

The harness is `scripts/dotnet/run-c115-managed-file-picker.ps1`. It stages
only the two C114 fixtures and writes evidence under
`out/dotnet/c011ec115-managed-file-picker/`. It requests three fresh QEMU
boots and records serial logs, hashes, input/action validation, lifecycle
markers, and the composite/kernel inputs. No screenshot is claimed when only
serial/framebuffer observation is available.

## Capability and negative behavior

The picker requires only `DirectoryList` and `FileStat`. Notes separately
requires `FileRead` after Open and `FileWrite` after Save. C115 capability
probes omit each relevant bit and expect `CapabilityUnavailable` without
attempting the missing callback. Negative coverage includes invalid initial
directory, traversal filename, 96-byte path overflow, no selected index,
directory type rejection, no-match cancellation, and overwrite decline.

## Lifecycle and portability

The picker is a managed App Model service above the generic Server bridge. It
does not create a second runtime, image, thread, TLS instance, or surface
implementation. The existing resident NativeAOT model remains the one-runtime,
one-image lifecycle with managed allocations limited to bounded candidate
arrays, labels, paths, and result objects.

The contract is portable in concept: Open/Save intent, initial directory,
extension filter, suggested filename, selected path, cancellation, and
overwrite confirmation. Another guideXOS edition can implement the same
contract with its own surface and storage provider. Recursive navigation,
multiple filter groups, general text input, and a desktop-wide common-dialog
framework remain out of scope.

## Recommended next phase

C116 should add the smallest reusable bounded text-input control needed to
replace the deterministic Save filename proof with user-entered filenames,
while preserving this picker contract and the existing file-service boundary.
