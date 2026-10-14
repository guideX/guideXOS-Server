# NDB1R2: Native VFS FAT32 File Shrink

## Scope and starting architecture

NDB1R2 adds a bounded shrink operation to the existing native VFS and FAT32
driver. It does not implement SQL integration, growth, or filesystem journaling.
The source checkout started at `main`, commit
`f1a427bfb2eac2630b4a464b9a20f38be3a74b59`, with
`.native-db.phase` expecting NDB1R2.

The VFS owns the public open handle, including path, open flags, file type,
logical size, and 64-bit cursor. FAT owns a smaller per-open record containing
the first cluster, 32-bit file size and cursor, attributes, and current cluster.
FAT directory entries provide the persistent first-cluster and size fields.
The FAT driver reads and writes sectors through the block-device layer using
shared scratch buffers; it does not maintain a persistent FAT cache. It mirrors
FAT entry writes to each FAT copy and preserves FAT32's reserved high nibble.

Allocation searches the FAT from the volume's in-memory `nextFreeCluster` hint.
This driver does not update FAT32 FSInfo free-count or next-free fields; unknown
FSInfo values stay unknown. A successful release moves the in-memory hint to a
released cluster so subsequent allocation can reuse it.

## API and contract

The public VFS API is:

```cpp
Status truncate_shrink(uint8_t handle, uint64_t newSize);
```

`VFS_OK` means the logical shrink completed. Sizes above `0xFFFFFFFF` return
`VFS_ERR_RESOURCE_LIMIT`. Growth returns `VFS_ERR_NOT_SUPPORTED`; this is
deliberately a shrink-only API. The operation applies to writable regular files
on FAT32. It rejects invalid or closed handles, directories, read-only handles
or mounts, unsupported filesystems, stale same-file handles, malformed chains,
and I/O failures with structured VFS statuses.

The FAT layer exposes the detailed operation and status enum:

```cpp
FileResizeStatus truncate_file_path_status(
    uint8_t volumeIndex,
    const char* path,
    uint8_t fileHandle,
    uint32_t newSize,
    block::Status* outBlockStatus);
```

Related helpers expose an open FAT handle, its last I/O status, combined
directory first-cluster/size updates, and first-cluster allocation for an
empty file before its first write. `file_size()` continues to return `-1` for
an invalid handle, distinct from a valid zero-byte file.

The representable size ceiling is 4,294,967,295 bytes. A particular volume may
have a lower safe chain limit: traversal uses at most 65,536 clusters and also
respects the volume's data-cluster count and cluster size. Requested sizes and
cluster counts are checked before conversion or traversal. File offsets remain
32-bit in FAT and VFS seek operations.

## Shrink and chain update order

The driver validates the full chain implied by the current file size, rejects
free, reserved, bad, out-of-range, prematurely terminated, overlong, and cyclic
chains, then sorts a bounded scratch copy to detect repeated clusters. Before
releasing any suffix, it scans the mounted FAT32 directory tree for another
file or directory that owns any cluster in the target chain. The ownership
scan is bounded to 1,048,576 work steps and 4,096 directories; a larger scan
fails conservatively instead of releasing clusters without checking them.

For a shrink that removes clusters, the operation publishes the shorter size
and new first-cluster field in the directory entry first. For a nonzero result,
the first-cluster field remains the existing head and the kept tail is then
marked EOC. For zero, the directory entry is published with size zero and a
zero first-cluster pointer. The detached suffix is then released through every
FAT copy. This order avoids a directory entry advertising bytes beyond its
chain. It also means a failure after directory publication can leave allocated
clusters leaked or mirrored FAT copies partially updated; the failure is
returned, and the FAT/VFS handle is closed so the caller cannot keep using
possibly stale in-memory state. FAT32 has no journal here, so those metadata
writes are not power-loss atomic.

Within one cluster, the chain is retained and only the directory size changes.
The byte prefix is unchanged. Reads past the new logical EOF return EOF. A
zero-length file has first cluster zero. Newly created empty files now use that
canonical representation; the first write allocates and publishes a cluster
before writing data. Truncating an older zero-size entry that still has a
cluster chain cleans up that chain safely.

The open handle's cursor is preserved, including when it lies beyond the new
EOF. Reads at that position return EOF until the caller seeks or writes. The
VFS blocks truncation while another open handle refers to the same resolved
path or first cluster; callers must close the other handle and retry.

## Failure reporting and durability

Validation, size, permission, type, and cross-link failures happen before the
directory publication point. Block read/write timeouts map to
`VFS_ERR_IO_TIMEOUT`; other device failures map to `VFS_ERR_IO`. Flush is a
separate VFS operation that propagates through FAT, block device, and the
existing ATA flush implementation. A successful flush confirms the requested
device flush completed; it does not make FAT metadata transactional.

The hosted suite injects failures during FAT traversal, directory metadata
write, kept-tail FAT update, detached-cluster release, first-cluster allocation,
and block flush. It checks that failures are returned, ambiguous or partial
metadata writes invalidate the handle, and no injected failure is reported as
success. The suite also exercises reserved and bad markers, invalid cluster
numbers, premature EOC, cycles, and cross-links in both the target directory
and a nested directory. The remaining limitation is ordinary FAT32 crash
consistency: a reset or power loss between metadata writes can leave a leaked
chain, a partially mirrored FAT, or another recoverable inconsistency. This
work makes no general power-loss atomicity claim.

## WAL-like byte contract

The hosted and QEMU checks create a deterministic header-plus-payload file,
flush it, shrink it to 64 bytes, flush again, then reopen and verify the exact
64-byte prefix, logical size, EOF, and FAT chain. The test uses the
FAT-compatible short name `WAL.GWL` because the current file-creation path is
8.3-only. This demonstrates the file-byte contract needed by a future WAL
adapter; it does not run SQL, open a `.gxwal` through the SQL engine, or prove
WAL recovery semantics.

## Verification

The normal AMD64 kernel build is:

```powershell
mingw32-make -C kernel ARCH=amd64 -j4
```

The hosted resize and fault-injection suite is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run-vfs-file-resize-test.ps1
```

The QEMU qualification builds a test-only kernel into a separate output
directory, uses the retained NDB1R1 Multiboot loader, and creates a new,
zero-filled 64 MiB raw disk image under `out/validation/`. The guest checks the
ATA PIO model and serial identity, capacity, and sector size before formatting
the blank image as FAT32. The production ATA PIO, block, FAT, and VFS code
writes and shrinks a multicluster file and a WAL-like file. A second independent
QEMU process mounts that same image and checks file sizes, exact contents, EOF,
FAT chain lengths, and both FAT copies. Host-side image hashes confirm that the
readback process did not change the image. The script exits successfully only
when both serial pass markers appear.

Acceptance proves tested shrink semantics, FAT chain correctness, error and
flush propagation, and clean independent-process persistence on the dedicated
image. It does not prove arbitrary power-loss recovery, SQL WAL durable commit,
native SQL execution, database crash recovery, or partial trailing-page repair.

The final run results were:

| Check | Result |
| --- | ---: |
| Hosted VFS/FAT32 resize and fault injection | 171/171 |
| ATA flush status | 21/21 |
| ATA flush propagation | 12/12 |
| Block-device generation | 5/5 |
| Hosted VFS read | 4/4 |
| App Model identity | 25/25 |
| App Model file activation | 60/60 |
| Normal AMD64 kernel build | Pass |
| QEMU production write/shrink boot | Pass |
| Independent QEMU readback boot | Pass; image hash unchanged |

The additional `run-native-filesystem-contract-test.ps1` was attempted but did
not compile because this checkout lacks
`third_party/mbedtls/tf-psa-crypto/include/mbedtls/private/crypto_adjust_config_enable_builtins.h`.
That host-side Server contract test is independent of the production kernel
VFS/FAT32 path exercised above; its failure is recorded rather than counted as
a resize regression pass.

## Next phase

NDB1R3 can add a native SQL file-provider adapter that maps open, read, write,
seek, size, shrink, flush, close, and structured kernel errors onto this file
primitive. SQL9 integration and database recovery remain separate acceptance
work.
