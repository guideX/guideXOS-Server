# NativeAOT C114 Managed Directory and File Metadata Services

C114 extends the C113 managed file bridge with application neutral directory
snapshots and path metadata. The VFS itself is unchanged. The canonical
`vfs::opendir`, `vfs::readdir`, `vfs::closedir`, and `vfs::stat` calls remain
the only filesystem boundary used by the bridge.

The C113 handoff remains the starting contract: version 1, an 88-byte
read/write prefix, bounded `/system/apps/...` paths, synchronous copies, and
the reusable `TryReadAllBytes`, `TryWriteAllBytes`, `ReadAllTextUtf8`, and
`WriteAllTextUtf8` APIs. C114 adds services beside that prefix and keeps the
read/write proof in the same resident application image.

The C113 v1 prefix remains valid. C113's table is 88 bytes (read at offset 72
and write at offset 80). C114 keeps ABI version 1 and appends a 16 byte
directory-list callback at offset 88 and a file-stat callback at offset 96,
making the table 104 bytes. Clients gate each callback by both table size and
capability. Capability bits 9 and 10 are `DirectoryList` and `FileStat`;
bits 0 through 8 retain their C113 meanings.

`directoryList(context, path, pathLength, entries, capacity, entryStride,
outCount, outHasMore)` validates every pointer, length, stride and capacity
before opening the directory. Paths are printable ASCII absolute paths under
`/system/apps/`, with no backslashes, repeated separators, or `..`; the
approved root accepts both `/system/apps` and `/system/apps/`. The path limit
is 96 bytes. A snapshot is bounded to 64 entries and each name is at most 127
bytes plus a terminator. Each 144 byte entry is:

| offset | field | size |
|---:|---|---:|
| 0 | `nameLength` | 4 |
| 4 | `type` (1 regular, 2 directory) | 4 |
| 8 | `size` (regular file bytes, otherwise zero) | 8 |
| 16 | UTF-8/ASCII name buffer | 128 |

The operation returns success with `outHasMore=1` when the directory has
entries beyond the requested capacity. It never writes beyond capacity and
does not silently truncate. Invalid paths, non-directories, malformed
strides, null buffers, and oversized names return deterministic file-service
results. No native pointer or caller buffer is retained after return.

`fileStat(context, path, pathLength, outInfo, infoSize)` uses the same path
validator and emits a 16 byte `GuideXosFileInfo` (type, reserved, 64 bit
regular-file size). Existing regular files, directories, and missing paths
are distinguished. FAT timestamps and permissions are intentionally omitted:
the application-facing contract exposes only VFS semantics with stable
meaning.

`GuideXosHost.TryListDirectory` and `TryGetInfo` convert the packed ABI into
managed-owned `GuideXosDirectorySnapshot`, `GuideXosDirectoryEntry`, and
`GuideXosFileInfo` objects. The managed wrapper performs a bounded lexical
sort so applications do not depend on FAT on-disk order. `GuideXosFile`
provides reusable forwarding methods.

Managed Notes has a C114 launch context that lists `/system/apps`, selects
`.TXT` entries, obtains metadata, opens the selected path through the C113
read service, and exposes Next, Open, Edit, and Save actions through the
existing compositor/input dispatch. It discovers `NOTES.TXT` and
`SECOND.TXT` from the directory snapshot; no filename list is hardcoded in
the application. The legacy C113 launch contexts retain the original
read/edit/save/reload proof.

The C114 harness stages two deterministic files (`NOTES.TXT`, 24 bytes, and
`SECOND.TXT`, 23 bytes), verifies file and directory stat results, exercises
a one-entry capacity snapshot with `hasMore`, then performs a full snapshot.
It also covers null path/destination, bad stride, oversized path, short stat
buffer, directory and stat capability downgrades, the retained unsupported
ABI probe, native Notepad, Workspace, Status, Counter, and Notes state
isolation. Three fresh QEMU boots are requested by
`scripts/dotnet/run-c114-managed-directory-services.ps1`; serial logs and
hashes are written below `out/dotnet/c011ec114-managed-directory-services/`.

The resident NativeAOT image and managed thread/TLS/heap are reused exactly as
in C113. No VFS, FAT, GC, code-manager, or NativeAOT lifecycle redesign is
included. Recursive traversal, timestamps/permissions, and final Open/Save
dialogs remain future work. C115 can build a reusable picker contract on
`TryListDirectory` and `TryGetInfo` without another VFS bridge change.
