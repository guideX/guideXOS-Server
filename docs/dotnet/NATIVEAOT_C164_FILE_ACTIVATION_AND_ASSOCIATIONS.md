# NativeAOT C164 File Activation and Associations

## Scope

C164 adds one bounded activation route: Managed File Explorer resolves a
regular VFS file through the shared association resolver, then launches a
fresh Managed Notes instance with the exact canonical path. The only C164
association is **.txt** to **com.guidexos.apps.managed.notes**. There is no
Open With UI, user-editable default, MIME database, command-line framework, or
writable File Explorer operation.

## Native dispatch and App Model audit

Before C164, native **FileExplorerApp::openSelected** in
**kernel/core/kernel_apps.cpp** routed directories to navigation and files by
ASCII case-insensitive extension checks in the explorer itself. Text files
went to legacy Notepad, **.png** to Image Viewer, **.img** to disk-image
handling, and **.gxq**, **.gxapp**, **.elf**, and **.exe** to the established
application-like-file path. Unsupported files stayed in the explorer with a
status. There was no central association table.

C164 routes **.txt** through **kernel/core/app_launch_target_resolver** from
both native and managed File Explorer. The native explorer keeps its existing
Notepad routes for **.log**, **.cfg**, **.ini**, and **.md**, its **.png**
Image Viewer path, and its **.img** / executable-like handlers. These unrelated
routes were retained.

The C150+ App Model already had canonical app IDs, a fresh-surface lifecycle,
and a bounded launch-context byte span. C164 reuses it. A managed application
context now carries ActivationKind alongside that context; the existing bytes
carry the document path. The context is 64 bytes; the managed host callback
table remains ABI v3 / 120 bytes. No command-line parser, second launcher, or
mutable process-global path was added.

Notes already owns document state in its TextArea and C152 state object. C151
supplies bounded VFS read and UTF-8 validation; C152 supplies Open/Save/Save As
and saved-revision tracking; C153 supplies history semantics; C155 supplies
Notes return-session restoration. C164 composes those facilities.

## Association and path rules

The association authority is an immutable fixed table of 16 records, with one
used record. A record contains two pointers; the table occupies 256 bytes on
the 64-bit target. Its entry is **{ ".txt", "com.guidexos.apps.managed.notes" }**.
Duplicate normalized extensions fail the resolver checks. The table is not
mutated during resolution.

Matching folds ASCII case and compares the suffix after the last dot in the
leaf. Therefore **.TXT** and **report.final.txt** resolve; **noextension** and
**.profile** do not. The resolver validates absolute canonical paths, rejects
empty components, dot/dot-dot components, backslashes, and paths over 96 UTF-8
bytes, and asks VFS stat to require a regular file. Directory, non-regular,
missing, invalid, too-long, I/O, and unsupported results are distinct. There
is no guessed fallback.

Managed File Explorer joins its canonical current directory to the complete
VFS name, never display text or a row index. It shares C163's 96-byte path
bound; overflow refuses activation without truncation and preserves Explorer
state. Open/Enter on a directory still navigates. Unsupported files keep
Explorer open and show **No application is associated with this file type.**

The native activation callback copies the path into a fixed request record and
resolves it again against VFS. Failure does not launch. If the target launch
fails, the source Explorer remains active. On success, the App Model transfers
the one active managed surface to the fresh Notes lifetime.

## Notes activation and lifecycle

The managed context copies and validates launch data into a managed-owned
96-byte buffer, rejects embedded NUL bytes, and accepts an old launch-context
prefix as a normal activation. A document activation requires a nonempty path.
Notes copies it into ordinary document state after reading; the launch buffer
is not used as long-lived storage.

Precedence is explicit document activation, then valid C155 restoration on an
ordinary return launch, then normal startup. Explicit Open therefore wins over
a stale Notes session. C155 tests still run, and the requested document is
reloaded through C151 before Notes commits its activation baseline.

The existing C137 wheel proof installs a long multi-line Notes fixture for its
ordinary proof launch. C164 bypasses that fixture on document activations so
proof setup cannot replace the file the user opened. The C155 precedence marker
checks and clears a pending session when one exists; boot 3 creates a named
session through Notes' Settings action, replaces Settings Center with File
Explorer, and verifies that explicit file activation consumes that candidate.

Successful activation uses the existing TextArea and C152 state. The complete
supported file is loaded; the title/path reflects the canonical path; the
saved revision is current; dirty is false; caret and anchor are zero; viewport
is zero; Undo and Redo start unavailable. Typing makes the document dirty, and
Undo to the activation revision clears dirty. Ctrl+S uses C152 read-back
verification and Ctrl+N retains C157 behavior. Notes' close guard remains
authoritative: a dirty Notes close can veto a replacement.

Notes and Settings Center release their managed App Model lifetime entries
when their window close is approved. File Explorer uses its activation-close
callback for the same cleanup. This keeps the three-entry managed lifetime
table reusable through repeated file activations.

The existing Notes bounds are 256 bytes and 32 lines. Missing files, directories,
malformed UTF-8, oversized documents, and over-limit line counts fail through
the bounded load path rather than becoming fabricated or partial documents.

## Focused evidence and regression coverage

Resolver checks cover 28 cases and 1,000 resolution calls. Managed activation
context checks cover 30 cases, including bounds, malformed payloads, and old
context compatibility. Notes activation checks cover 32 cases, including VFS
fixtures, clean/history state, oversized and invalid files, explicit-session
precedence, and 25 hydration cycles. Managed File Explorer retains five
controls and routes Open and Enter through the same method.

The C164 runner includes C163 browsing, C151 Open dialog, C152 Save and document
state, C153 history/TextArea/save-point suites, C155 restoration, C156 shortcuts,
C157 new-document lifecycle, C150 App Model transitions, and C162 Task Manager
smoke. Boot 1 proves load/edit/Undo, boot 2 proves Save/reopen and unsupported
file handling, and boot 3 establishes a real named C155 candidate before 25
fresh File Explorer-to-Notes activations. Fixtures are staged by the normal
proof-filesystem builder and read through VFS. Generated manifests and serial evidence are under
**out/dotnet/c164-file-activation/** and are not source tracked. C128 remains
**unverified**.

The C164 proof and ordinary composites use a fixed **Primary8MiB** managed
heap (8,388,608 bytes). The prior 4 MiB bound was exhausted during boot 3 after
the repeated C163 explorer-lifetime coverage, before C164's required 25 fresh
document activations could run. The larger fixed bound supports the combined
bounded lifecycle workload; it does not add dynamic association or path storage.
The ELF file-size bound remains 4 MiB; the separately bounded mapped image span
is 12 MiB so the 8 MiB static heap and NativeAOT image sections fit. The 12 MiB
span remains below the loader's fixed 4,096-page mapping capacity.

## Deferred work and Git

C164 defers Open With, Default Apps settings, user associations, wildcard/MIME
matching, content sniffing, executable-content dispatch, drag and drop,
double-click infrastructure, association-specific icons, and writable
File Explorer operations.

The final task report records the implementation commit and one authorized
origin query and normal push attempt. The known public-key issue is external to
local validation. SSH configuration, keys, remotes, and Git identity are not
changed.
