# NDB1R3: Native SQL File Contract

## Result

**Outcome A — NATIVE_SQL_FILE_CONTRACT_VERIFIED.** The Server checkout contains
the production VFS-backed file bridge, SQL has a matching `IDatabaseFile`
adapter, the existing SQL regression suite and the new provider tests pass, the
native kernel builds, and the bridge passes a dedicated two-process QEMU
qualification. This qualifies the file-provider contract to begin NDB1R4; it
does not qualify SQL9 execution in the guest or arbitrary power-loss
durability.

## Repository and phase gates

| Checkout | Starting point | Phase at start | Phase at end |
|---|---|---|---|
| `D:\dev\guideXOSServer_NATIVE_DB_STORAGE` | `main`, `5d9bd282830e195a3df7dee20c1a775e51592f26` | NDB1R2 → NDB1R3 | NDB1R3 → NDB1R4 |
| `D:\dev\guideXOS_SqlDatabase` | `main`, `a4baf7d37c2476840dde20880761c3a70206b51a` | SQL9 → SQL10 | SQL9 → SQL10 |

Both checkouts were clean at the specified baselines. No branch switch or
creation, remote change, worktree, pull, merge, rebase, cherry-pick, reset,
clean, stash, or history rewrite was performed. The Server checkout began
eight commits ahead of its local `origin/main`; those commits were preserved.
The SQL `.phase` file was not modified. On acceptance, the Server marker is
advanced in the Server commit to `last_completed=NDB1R3` and
`next_expected=NDB1R4`.

The SQL changes are committed locally on `main` as
`47862b78d3d643483798d8874b5bc321d782df51`. One SQL `git push origin main`
attempt failed with `Permission denied (publickey)`; no retry or credential
troubleshooting was performed. The SQL commit remains local.

## SQL file-provider audit and adapter

SQL's engine uses `IDatabaseFile` for database and WAL files. The contract now
reports status for `getSize`, `close`, filesystem `exists`, and filesystem
`remove`. Positional reads and writes are exact-length operations; failed or
short transfers return failure. `truncateTo` may shrink; native growth through
truncate is explicitly unsupported. Normal database growth is by writes that
extend the file. Flush remains an explicit boolean barrier, and commit code
must observe a successful WAL flush before acknowledging a commit. A subsequent
close error is reported independently.

`database/database_native_io.h/.cpp` implements `NativeDatabaseFile` and
`NativeDatabaseFileSystem` over an injected narrow function table matching the
Server bridge ABI. It maps bridge statuses into SQL results, preserves size
errors, read-only state, path ownership, short-transfer failures, flush and
close errors, and provider-based removal. `database_native_io_test` exercises
this adapter and creates, closes, and reopens a SQL database through it. The
adapter uses hosted C++ runtime facilities and is intentionally not linked into
the freestanding kernel in NDB1R3; full SQL runtime integration is NDB1R4.

Database creation now removes a partially initialized file through the
selected `IDatabaseFileSystem`. If provider cleanup also fails, the operation
reports `RecoveryFailed`. A failed existence lookup is an I/O error, not
absence. Database and WAL size-query errors are also preserved instead of
being represented as zero or a sentinel.

## Native bridge and operation mapping

The Server bridge is the C-compatible, no-STL API in
`kernel/core/include/kernel/native_database_file.h`, implemented in
`kernel/core/native_database_file.cpp`. It uses production VFS operations and
generation-tagged opaque handles. It does not implement a second filesystem.

| SQL operation | Native implementation | Result behavior |
|---|---|---|
| Open/read-only/read-write/create | VFS open on a validated mapped name | Errors fail; create is exclusive when the path was absent |
| Size | VFS `file_size` | Negative size result is I/O error; values above `0xffffffff` are rejected |
| `readAt` | save cursor, seek, read, restore cursor | Short read is `SHORT_READ`; no zero-padding |
| `writeAt` | save cursor, seek, write, restore cursor | Short write is `SHORT_WRITE`; writes may extend the file |
| `truncateTo` | `vfs::truncate_shrink` | Shrink/equal size supported; growth returns `NOT_SUPPORTED` |
| Flush | VFS → FAT32 → block → ATA flush | Lower-layer status is returned |
| Close | VFS close | Close/flush error is returned and bridge handle is invalidated |
| Exists | status-bearing VFS stat | Only `NOT_FOUND` means absent; other lookup failures propagate |
| Remove | status-bearing stat, VFS unlink, verification stat | Idempotent absence; wrong type and I/O failures are errors |

Incoming positional offsets and `offset + length` are checked against the
32-bit FAT/VFS extent before conversion. Zero-byte I/O is accepted without
dereferencing a null buffer. Nonzero I/O requires a buffer. Positional
operations preserve the native file cursor. The bridge caps transfers at the
VFS's 32-bit length range and never treats a partial transfer as complete.

FAT short-name creation supports only 8.3 names. The bridge maps a same-folder
`NAME.gxdb` to `NAME.GDB` and `NAME.gxwal` to `NAME.GWL`; it accepts ASCII
letters, digits, and underscore with at most eight stem characters and rejects
unsupported path components. It preserves parent/mount spelling for VFS
routing while FAT name lookup remains case-insensitive. Data and WAL suffixes
are distinct. Alpha and beta databases therefore map to separate `.GDB` files
and independent `.GWL` sidecars; a shared WAL filename is never used.

FAT lookup now returns a status that distinguishes not-found from I/O,
timeout, invalid mount, and corrupt traversal. `vfs::stat` preserves these
distinctions. `vfs::open` and FAT file creation refuse to create after any
lookup result other than a proven `NOT_FOUND`, so a failed lookup cannot
overwrite an existing database.

## Partial-page recovery

The SQL root cause was ordering: relational open validated that the database
file ended on a complete page before it opened/scanned the WAL. A crash during
append can leave a committed page image in the WAL while the `.gxdb` ends part
way through that page, so strict physical validation prevented the WAL from
repairing a state for which complete redo evidence existed. Conversely,
checkpointing an incomplete WAL before validating that tail could discard the
only recovery evidence.

Relational SQL opens now defer physical page validation until the WAL decision;
direct low-level `DatabaseFile` opens remain strict by default. For a committed
WAL, recovery checks the final header/page count and proves that WAL page images
cover every missing page before modifying the database. It then applies the
committed records, flushes the database, validates the recovered extent and
pages, loads the catalog, and checkpoints the WAL. A read-only open with a
committed WAL returns `RecoveryRequired` without changing either file. An
incomplete or clean WAL is not checkpointed until the base file is valid. A
partial extension with no complete committed redo evidence fails closed.

The recovery fault matrix verifies writable repair and repeated reopen,
read-only no-modify behavior, an uncommitted partial tail, truncated WAL,
record checksum corruption, foreign database UUID, header-only WAL, injected
first-page-repair failure with both snapshots preserved, and indexed heap
growth with post-recovery index validation. Committed data is not silently
discarded and corrupt WAL is not accepted as clean.

## Qualification evidence

- SQL CMake build and all **10/10 CTest targets** pass (**92,655 checks, zero
  failures**), including the SQL1–SQL9 targets, transaction partial-tail matrix,
  and `database_native_io_test`.
- Native bridge hosted contract: **53/53 checks** pass.
- Preserved NDB1R2 hosted VFS resize/fault suite: **171/171 checks** pass.
- Full AMD64 freestanding kernel build passes with
  `mingw32-make -C kernel ARCH=amd64 -j4`.
- QEMU 11.0.0 qualification builds a test-only kernel and uses a fresh zero-filled
  64 MiB raw FAT32 image on a `pc` machine with TCG, 1 GiB guest memory, an IDE
  hard disk on `ide.1` unit 0, and the dedicated serial
  `GXOSNDB1R2TEST01`. It first runs the retained NDB1R2 production VFS resize
  checks and then the NDB1R3 bridge checks. The first QEMU process wrote
  database-like pages, two WAL identities, flushed and shrank one WAL to 64
  bytes, removed a temporary sidecar, and closed/reopened each file. A fresh
  QEMU process verified exact bytes, sizes, sidecar identity, and cleanup. The
  disk image hash was unchanged by readback. Logs and the disposable image are
  under `out/validation/NDB1R3_NATIVE_FILE_BRIDGE_20261014_183716/`.
- `scripts/run-native-filesystem-contract-test.ps1` was attempted but its broad
  hosted Server source list cannot compile because the checkout lacks
  `third_party/mbedtls/tf-psa-crypto/include/mbedtls/private/crypto_adjust_config_enable_builtins.h`.
  The dedicated bridge harness avoids that unrelated hosted application/TLS
  dependency and passes; the freestanding kernel build and actual QEMU bridge
  run both pass.

The QEMU configuration exercises ATA flush on the normal tested path, but the
FAT32 filesystem metadata is nonjournaled. Clean shutdown and independent-boot
readback prove only the tested sequence. They do not prove atomic directory/FAT
metadata under arbitrary power loss. SQL9 is not linked into or executed by the
guest, and native SQL recovery, query execution, and three-boot application
persistence remain NDB1R4 work.
