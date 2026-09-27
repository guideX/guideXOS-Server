# Disk Manager Phase DM2 — Safety Foundation

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `a8764286a31e5ac3c57af49f7e9051c7fcbbe3b8`
- The starting worktree was clean. The branch was already one commit ahead of `origin/DISK_MANAGER_IMPROVEMENTS`; that commit is the DM1 audit report and was preserved.
- No `main` merge was made.

## Architecture

Storage management is split across the block dispatch layer, a reusable storage manager, a read-only partition parser, and Disk Manager presentation:

- `kernel/core/block_device.cpp` owns registered devices, checked sector dispatch, flush outcomes, and a registry generation.
- `kernel/core/storage_manager.cpp` validates geometry and ranges, reports device capabilities, captures/revalidates target snapshots, checks mount protection, and produces a structured destructive-target preflight result.
- `kernel/core/partition_table.cpp` reads through the storage layer and returns a bounded normalized MBR/GPT model.
- `kernel/core/kernel_apps.cpp` presents that model in the bare-metal Disk Manager. The UI does not write disks.

The parser stores at most 128 GPT entries. The bare-metal UI shows at most 16 at once and reports the full parsed count. These bounds prevent unbounded allocation and keep the existing window usable.

## Capability and persistence model

`DeviceCapabilities` reports read/write callback availability, flush callback and semantics, removable status when known, transport, geometry, total capacity, DMA buffer constraints, name, model, and serial. A missing flush callback is never treated as proof of durable writes. `FlushReport` distinguishes a supported successful flush, explicitly synchronous durable completion, unsupported/unknown persistence, and a failed attempt. The compatibility `block::flush()` now returns `BLOCK_ERR_UNSUPPORTED` when neither a flush callback nor an explicit synchronous-durable declaration exists.

Transport audit:

| Transport | DM2 finding |
|---|---|
| ATA PIO | Writable. IDENTIFY word 83 must be valid and advertise FLUSH CACHE or FLUSH CACHE EXT before the callback is registered. The callback selects the target drive, uses the advertised command, waits for completion, and checks error status. Write completion alone is not declared durable. |
| AHCI | No AHCI block registration path is active; there is no registered AHCI write/flush capability to claim. |
| NVMe | Writable, but no Flush command is implemented. Persistence is unknown. The current PRP1 path is limited to one aligned 4 KiB page; block dispatch and safe helpers reject larger or misaligned transfers. |
| USB mass storage | No active registration path is present in the shared block registry. |
| RAM disk | Volatile memory. Pool-created disks can be writable; attached image disks are read-only. A no-op flush completes synchronously because there is no separate write cache, while the capability remains explicitly volatile rather than durable. |

The ATA IDENTIFY word-83 validity and cache-flush support bits follow the ATA/ATAPI command-set definition: [ATA/ATAPI-8 command set](https://read.seas.harvard.edu/cs161/2020/pdf/ata-atapi-8.pdf). No transport was assigned durability based only on having synchronous callbacks.

## Geometry-safe I/O

Supported logical-sector sizes are powers of two from 512 through 4096 bytes. Zero, non-power-of-two, out-of-range sizes, zero capacity, and sector-count multiplication overflow are rejected. Range checks use subtraction (`count <= total - lba`) after validating `lba`, avoiding `LBA + count` overflow. Buffer-aware block and storage APIs check byte capacity before dispatch. Transport alignment and maximum-transfer declarations are enforced by raw block dispatch and the storage helpers.

The partition parser and bare-metal filesystem probes use aligned 4096-byte scratch space and the registered device's logical-sector size. MBR fields remain byte offsets 446 and 510 within the first logical sector; GPT structures are read by logical LBA. Existing VFS filesystems remain 512-byte-only and reject other device geometries rather than reinterpret them unsafely. The hosted Windows `.img` viewer uses fixed 512-byte file sectors because it is a host-image inspection path and does not enumerate physical disks.

## Registry generation and target identity

The block registry exposes a nonzero `uint64_t` generation. It advances on registry initialization, registration, and unregistration; ordering is not used. A target snapshot contains that generation, global index, transport, driver index, logical capacity/sector size, name, model, and serial. Capture checks that the generation stayed stable while the snapshot was assembled. Equality compares the full bounded snapshot. Revalidation rejects a changed generation before resolving the saved slot, then checks all identity fields again.

This is an in-session identity, not a persistent disk UUID. A registry change invalidates a snapshot and requires the UI to enumerate and select again. The registry currently has no lock or operation lease spanning validation through a later write; DM3 must add serialization around revalidation and the operation if hotplug or concurrent registry changes can occur.

## Mounted and root protection

`query_mount_protection()` revalidates the target and scans active VFS mount records by block-device index. It reports `DEVICE_ROOT_BACKING`, `DEVICE_MOUNTED`, `DEVICE_UNMOUNTED`, or `DEVICE_IDENTITY_UNKNOWN`. The destructive-target preflight rejects root-backed and mounted devices. The VFS mount record does not retain partition identity or offset, so a match protects the whole block device conservatively.

This detects the current VFS `/` backing device when that mount is registered. The OS does not yet publish authoritative firmware boot provenance for every device; a device that is not represented by an active VFS mount cannot be proven unrelated to boot. That remains a DM3 blocker.

`validate_destructive_target()` returns structured issue bits for identity/registry changes, invalid geometry, unreadable state, read-only devices, unknown persistence, mounted/root devices, and partition state rejected by the request. It does not write or flush a device. Its 17 KiB normalized model uses bounded static storage because the amd64 boot stack is 16 KiB; concurrent preflight calls must be serialized.

## MBR parsing

The read-only parser validates sector-zero readability, the `0x55AA` signature, status bytes, empty-entry consistency, nonzero ranges, disk bounds, and overlap among primary data partitions. It distinguishes raw media from a valid empty MBR. A protective `0xEE` entry is recorded; a protective MBR without a valid GPT is invalid, and hybrid MBR/GPT is explicitly unsupported. Extended/logical partition traversal is not implemented; extended types are reported as unsupported.

All MBR LBA fields are normalized to 64-bit start/end/count values. Invalid entries are not passed to filesystem probing.

## GPT parsing

Primary and backup copies are read independently. The parser checks the `EFI PART` signature, supported revision, header size and CRC, expected current/backup LBAs, nonzero disk GUID, usable range, entry-array placement and size, device bounds, and array CRC. It bounds the entry count at 128 and entry size at 512 bytes, then validates used-entry GUIDs, ranges, usable-LBA bounds, and overlap. GPT names are decoded from UTF-16 into bounded UTF-8 with malformed surrogate replacement.

The model distinguishes valid matching copies, degraded copies (including primary-invalid/backup-valid and primary-valid/backup-invalid), invalid tables, and unsupported revisions/entry counts. A valid backup can supply the normalized partition list when the primary is invalid. DM2 never repairs or writes GPT metadata.

## Raw and invalid classification

Missing MBR/GPT signatures alone do not establish a raw disk. `Not Initialized` is reported only if the first up-to-16 logical sectors and the final logical sector are readable and all zero. A read failure is `Unreadable`; nonzero ambiguous metadata is `Invalid Partition Table`. A valid empty MBR remains `MBR`, and a protective MBR without valid GPT metadata remains invalid.

## Disk Manager integration

The bare-metal app scans all 16 registry slots, keeps the selected device by target identity across refresh, consumes the normalized MBR/GPT model, displays 64-bit partition starts/counts, and labels state as MBR, GPT, GPT Degraded, Not Initialized, Invalid, Unsupported, or Unreadable. It displays read/write and persistence capability, and filesystem detection reads only validated parsed partitions with logical-sector-safe buffers. The bounded UI displays up to 16 of the parser's 128 partitions. Destructive actions remain absent/disabled.

The hosted Windows path does not enumerate physical disks. It displays attached host `.img` files for inspection; that legacy viewer still shows the four MBR primary slots. This does not provide physical-disk operations. Heuristic `System`, `Boot/System`, `Healthy`, and `Mounted` claims were removed or replaced with explicit read-only/unknown labels. Active MBR flags are displayed only as flags, and suggested mount points are not represented as mounted state.

## Attached-image lifetime

Attached image buffers transfer to `ramdisk::create_readonly_owned()` only on successful registration. The RAM disk unregisters its matching global block descriptor and releases the owned `new[]` buffer on destroy. Rescan preserves a live attachment only when its RAM-disk slot, global block index, driver index, and 64-bit instance ID still agree; a stale same-instance attachment is destroyed safely. Newly loaded duplicate buffers are released; an attached image remains represented after its source file disappears. Block writes and `clear()` are refused for read-only images. Pool-backed RAM disks remain allocated because the existing pool is a bump allocator.

## Fake-device tests

`tests/storage_manager_test.cpp` registers deterministic in-memory fake block devices. It does not open a host disk or raw device. Programmatic fixtures cover raw media, empty/populated/malformed/out-of-range/overlapping/protective/extended MBR, valid/degraded/corrupt/out-of-range/overlapping/oversized GPT, 512-byte and 4096-byte sectors, read errors, short buffers, transport alignment/transfer limits, read-only and flush behavior, registry invalidation, root/other mount protection, and read-only owned-image attach/rescan/destroy lifetime.

Run with `scripts/run-storage-manager-tests.ps1`. The current run passed **58 checks, 0 failures**. It removes its generated executable after the run.

## Validation and build limits

- The fake-device suite passed: 58 checks, 0 failures.
- C++14 syntax-only compilation passed for the block/storage/parser/VFS/FAT/ext4/UFS files. It emitted two existing `nameLen > 255` warnings in ext4 and UFS directory readers.
- Bare-metal syntax-only compilation passed for `kernel_apps.cpp`, RAM disk, ATA, and NVMe.
- Hosted Windows syntax-only compilation passed for `disk_manager.cpp`.
- The ATA driver syntax check passed after adding IDENTIFY-gated cache flush capability reporting.
- No hardware or QEMU disk I/O test was run; fake devices and syntax checks do not prove firmware/device behavior.
- The repository has no `.gitmodules` entry for Mbed TLS. The tracked guideXOS Mbed TLS manifest and `scripts/bootstrap-mbedtls.ps1` define a pinned bootstrap path. `third_party/mbedtls` is absent, so the standard build is blocked by its required dependency/profile check. DM2 did not bootstrap or change that unrelated dependency.

## Remaining blockers and DM3 scope

DM2 is **Outcome B: Foundation improved, with blockers remaining**. NVMe durability is unknown; AHCI and USB mass storage are not registered; firmware boot-device provenance is not authoritative; whole-device mount protection is necessary because partition identity is absent; registry mutation and preflight scratch have no lock/lease spanning validation and writes; and existing VFS filesystems reject non-512 logical sectors. Hardware flush behavior has not been exercised.

DM3 should add an operation lease or lock, obtain authoritative boot-device identity, require a known supported persistence policy per target, and validate a serialized plan against this parser and safety result immediately before writing. Then it can implement only the requested MBR/GPT initialization path with bounded read-back verification and fault-injected fake-device tests. Keep partition creation, formatting, repair, erase, and installer writes out of that first operation until separately designed and validated.
