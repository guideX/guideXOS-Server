# Disk Manager Phase DM1 Audit

**Audit date:** 2026-09-26
**Repository:** `guideX/guideXOS-Server`
**Outcome:** **Outcome B — Audit complete with important architectural blockers**

DM1 was read-only with respect to storage media. No partition table, filesystem, or disk image was written by this audit. The source is understood well enough to plan DM2, but safe writes must wait until the block-device durability, geometry, identity, and protection gaps below are addressed.

## 1. Repository baseline

| Check | Result |
|---|---|
| Repository path | `D:\dev\guideXOSServer_DiskManagerImprovements` (matches requested workspace) |
| Repository identity | `guideX/guideXOS-Server`, remote `origin` is `git@github.com:guideX/guideXOS-Server.git` |
| Branch | `DISK_MANAGER_IMPROVEMENTS` |
| Starting worktree | Clean |
| Starting HEAD | `f5e0a1021fd4d1d17a9b04d6206c146bf4d2eb67` |
| Configured upstream | `origin/DISK_MANAGER_IMPROVEMENTS` |
| Ahead / behind at start | 0 / 0 |
| Expected baseline | `f5e0a1021fd4d1d17a9b04d6206c146bf4d2eb67`; exact HEAD match and ancestor check passed |

## 2. Architecture map

There are two Disk Manager implementations.

```text
Hosted server desktop
  built-in app metadata / DesktopService dispatch
    -> gxos::apps::DiskManager::Launch()
    -> ProcessTable::spawn("diskmanager", DiskManager::main)
    -> gui.input / gui.output IPC event loop
    -> Windows synthetic disk + read-only .img file reader

Bare-metal kernel desktop
  built-in app metadata / kernel app factory
    -> kernel::app::AppManager registers DiskManagerApp
    -> DiskManagerApp::init() creates KernelWindow
    -> kernel compositor, widget callbacks, framebuffer drawing
    -> kernel::block enumeration and read-only MBR/filesystem probes
```

The hosted process implementation is in root `disk_manager.cpp`; `Launch()` creates a process and `main()` owns its window/event loop until `MT_Close`. It subscribes to `gui.input` and `gui.output`, sends `MT_Create` and draw messages, and handles create, invalidate, mouse, key, and close events. Disk Manager does not own a block device or mount handle; it stores an index and rescans the global registry.

For bare metal, `kernel/core/kernel_apps.cpp` defines `DiskManagerApp`, and the kernel app registry binds the built-in `DiskManager` entry to its factory. It owns a `KernelWindow` through `KernelApp`, scans in `init()`, and scans again only when its Refresh widget is clicked. This implementation is materially smaller than the root process implementation: it has no mount display, image attachment, or storage actions beyond refresh.

`built_in_app_metadata.h` advertises both hosted and bare-metal availability. The actual bare-metal kernel app manager uses `DiskManagerApp`; the root source's `#ifndef _WIN32` image/VFS branch is not evidence that the hosted synthetic-disk path runs in the kernel.

### Current data flow and heuristics

* The hosted process scans at most `MAX_BLOCK_DEVICES` active descriptors by global slot. The kernel app loops from slot 0 to `device_count()-1`, although `device_count()` is an active count, not a highest slot. A hole left by unregistration can therefore hide later devices in the kernel app.
* A displayed disk is stored by vector position and `devIndex`. Neither is persistent identity. Refresh clamps the old selected vector index; it does not re-resolve the same physical device.
* `Disk N` comes from a transient block-table slot. The block descriptor has only transport type, driver-local index, name, capacity, sector size, and callbacks. There is no stable ID, media generation, read-only/removable flag, controller path, or partition identity in the shared descriptor.
* `isSystem` is inferred from ATA/AHCI/NVMe transport, not from the boot/root device. The kernel app similarly labels ATA/AHCI as `System`; it labels NVMe differently. RAM disks are identified by a name prefix in some code, not a device class.
* The root hosted UI's `probeOnce()` reads block slot 0 regardless of the selected disk. The actual bare-metal `DiskManagerApp` has no separate Detect action.
* The root implementation classifies an MBR `0x80` boot flag as “Boot/System” and labels every partition row “Healthy”. Neither claim is backed by boot provenance, filesystem checks, or health diagnostics.
* Suggested mount points are guesses: active or first “system” partition becomes `/`; MBR type `0x82` or `0x83` becomes `/users`; selected FAT-like types become `/shared`. A suggestion is not a mount operation.

## 3. Current UI behavior

The hosted UI draws a disk list, a volumes grid, a partition bar, a mount suggestions grid, action buttons, and `.img` controls. The partition bar sorts the four parsed primary MBR entries and paints gaps as “Unallocated”; this is inferred free space, not a validated partition map. It clips out-of-range entries for display but does not validate overlaps or the table.

The enabled actions are Detect media, Set FS: Auto, Refresh, image navigation, Attach image, and Rescan images. Set FS: Auto is a read-only refresh. Set FS: FAT/TarFS/EXT2, Format as FAT, and Create partition are drawn disabled; their hit boxes still dispatch to handlers, but those handlers only report “disabled” and perform no writes. The host image controls attach read-only images. There is no initialize, delete, resize, mount, unmount, eject, or diagnostics action.

`drawMountsSection()` labels one column “Suggested mount” and another “Mounted”. The former is the heuristic above. The latter is computed by looking up that guessed path and comparing the VFS block index and `fsVolumeIndex` to `partitionIndex + 1`; filesystem volume indexes are allocator slots, not partition numbers. The result can be wrong even if some filesystem is mounted. The bare-metal `DiskManagerApp` does not present mount state at all.

The on-screen MBR states are only “unreadable”, “invalid MBR”, and “valid MBR”. They do not distinguish a raw disk, malformed table, unsupported scheme, empty valid table, no media, or a real I/O error. The kernel app shows “No MBR partitions found” for both empty and unrecognized/unreadable media.

## 4. Hosted versus bare-metal behavior

| Feature | Hosted server implementation | Bare-metal implementation |
|---|---|---|
| Disk list | Always inserts a synthetic 100 GiB “Disk 0 (ATA)” with a fabricated NTFS partition. It does not query Windows physical disks. | Uses active `kernel::block` descriptors. Actual registrations are much narrower than the labels suggest. |
| MBR/filesystem view | Reads attached `.img` files with `std::ifstream`; uses fixed 512-byte sectors. | Reads through the block layer using fixed-size 512-byte buffers in both current UI implementations. |
| Physical media writes | None | Disk Manager writes none. Underlying file-system code can write files on FAT volumes, but there is no disk-initialization or formatter operation. |
| `.img` attachment | Scans relative `disks\*.img`, parses read-only, and labels an image as USB. | Root process branch reads `.img` files from VFS `/disks` and `/`, caps each at 16 MiB, copies them into memory, and exposes a read-only RAM-disk view. The actual kernel `DiskManagerApp` has no image attachment UI. |
| Rescan | Refresh rebuilds the synthetic row and attached image list; Rescan images re-reads the directory. | Refresh re-enumerates the existing block registry; it does not rescan controllers or add newly attached hardware. |
| Proof value | Hosted behavior proves only the synthetic UI and `.img` reader, not bare-metal transport support. | Hardware support depends on registered block drivers, detailed in the next section. No QEMU Disk Manager smoke test was possible in this environment. |

**Bare-metal image lifetime risk:** `refreshHostImageLibrary()` deletes each in-memory image buffer before clearing/rebuilding its vector. `attachSelectedHostImage()` passes that buffer to `ramdisk::create_readonly_at()`, whose contract says the caller must keep the memory alive. Rescan images can therefore leave a registered RAM disk reading freed memory. There is no detach action or ownership handoff. This is documented for a later fix; it was not changed in DM1.

## 5. Block layer and transport capability matrix

`kernel/core/include/kernel/block_device.h` defines a fixed 16-slot global registry and sector callbacks. The wrapper checks slot activity, callback presence, null/zero requests, and LBA/count bounds using subtraction before dispatch. It has no locking, registry generation, snapshot API, media-change notification, or per-device capabilities beyond callback presence.

| Transport | Registered in shared block layer? | Read | Write | Flush / geometry / identity | Audit result |
|---|---:|---|---|---|---|
| ATA PIO | Yes, as `BDEV_ATA_PIO` | Implemented, one sector command at a time; timeout reported on polling timeout. | Implemented, 28-bit and 48-bit commands. Write completion checks idle but does not reject ATA error/device-fault bits at that point. | `ata_pio_flush` exists, but waits only for BSY clear and does not check ERR/DF after issuing flush. Logical sector size is hard-coded to 512. ATA descriptor stores model and serial, but the block descriptor does not. | Real implementation is legacy IDE/PIO only. `ata::init()` probes IDE channels; it does not enumerate AHCI. |
| AHCI | No | No shared callback registered. | No shared callback registered. | Header constants and enum labels exist; `ata::init()` comments that AHCI support would be added later. No AHCI transport implementation was found. | Unsupported despite UI/API labels. |
| NVMe | Yes, namespace 1 only | Implemented synchronously with polling. | Implemented synchronously with polling. | Sector size is derived from the namespace LBA format; model/serial are held in the driver descriptor but not copied to `BlockDevice`. `flushFn` is null. `PRP2` is set to zero and the PRP-list path is TODO, so multi-page/misaligned buffers are not generally supported. Errors collapse to `BLOCK_ERR_IO`; polling timeout is not surfaced as `BLOCK_ERR_TIMEOUT`. | Writable callback exists, but the shared flush API reports success without issuing an NVMe Flush command. This is a write-durability blocker. |
| USB mass storage | No | Standalone Bulk-Only/SCSI `READ(10)` code exists. | Standalone `WRITE(10)` code exists. | Standalone driver records inquiry/capacity data and has USB address/interface/LUN; no block adapter, flush callback, hot-removal unregister path, or call site for `usb_storage::probe()` was found. `READ CAPACITY(10)` and `READ/WRITE(10)` constrain addressing/counts. | Source implementation is not connected to Disk Manager or shared block I/O. Do not claim bare-metal USB disk support. |
| RAM disk | Yes, incorrectly typed as `BDEV_USB_MASS` | Implemented, 512-byte sectors. | Implemented for writable RAM disks; read-only image disks register `writeFn == nullptr`. | No flush callback (in-memory writes are immediate). Device identity is a name/index; no special device type. `destroy()` marks the private RAM descriptor inactive but does not unregister the global block slot. | Works as a memory block device in the current block path; lifecycle/classification are incomplete. Registration return values are ignored by create paths. |
| VirtIO block (extra) | No, separate `kernel::virtio::block::BlockDevice` API | Public API exists, but request submission is a stub. | Public API exists, but request submission is a stub. | `flush()` returns success when feature absent, and the common `submitRequest()` currently returns success without queueing I/O. It is not registered in `kernel::block`. | Not usable as proven storage I/O. Do not confuse its method names or QEMU initialization with a functional Disk Manager transport. |

### Registry and I/O guarantees

* `device_count()` is a count of active entries; `get_device(i)` takes a global slot index. `register_device()` returns that slot, but ATA/NVMe/RAM disk creation does not consistently retain/check the returned global index. Registration does not validate capacity, sector size, or callbacks.
* `unregister_device()` only clears `active` and decrements the count. No ATA/NVMe/USB hot-removal caller was found. RAM disk `destroy()` does not call it. Reusing an inactive slot can make an old `devIndex` name a different device.
* `write_sectors()` returns `BLOCK_ERR_UNSUPPORTED` if `writeFn` is null. `flush()` instead returns `BLOCK_OK` if `flushFn` is null, based on a broad “synchronous means durable” comment. The block layer cannot prove that assumption. NVMe is a concrete counterexample: writes complete but no Flush command is issued.
* Block statuses preserve callback statuses where supplied, but per-transport error fidelity varies. USB uses its own `TransferStatus` and is not mapped. The layer has no read-only/removable/online/offline properties and no per-device lock around callback execution.

## 6. Partition-table capability matrix

| Capability | Current status | Details |
|---|---|---|
| MBR recognition | Partial | Disk Manager checks bytes 510–511 for `55 AA`. This proves only a signature, not a structurally valid partition table. |
| MBR validation | None beyond signature | No status-byte validation, range/overflow checks, overlap checks, type/length consistency, CHS validation, or full table semantics. A boot sector with the same signature can be misread as MBR. |
| Primary entries | Partial | Four 16-byte entries are read into 32-bit start/count fields. Empty entries are filtered for display by zero count. Entries outside disk capacity can still appear in the grid. |
| Extended/logical partitions | Unsupported in Disk Manager | Extended MBR types can be displayed as a primary entry; there is no EBR chain walk or logical partition representation. |
| Protective MBR | Not recognized as GPT | Type `0xEE` is shown as an ordinary MBR partition. |
| GPT header/entries | Absent | No GPT signature/header parser, entry parser, CRC validation, usable-LBA validation, or disk/partition GUID handling was found anywhere in source. |
| Backup GPT | Absent | No backup header/array reads, comparison, or recovery logic. |
| Hybrid MBR | Unsupported | Only four MBR primary entries are visible; there is no reconciliation with GPT. |
| Empty/raw/invalid classification | Insufficient | Read error remains “unreadable”; readable sector without `55 AA` becomes “invalid MBR”; all-zero media is not distinguished from corrupt/unsupported media. A signature with four empty entries is displayed as valid/empty, but receives no stronger structural validation. |
| Partition-table writes | Absent from Disk Manager and installer | No runtime MBR/GPT writer, partition create/delete/resize, repair, or wipe implementation was found. `kernel::block::write_sectors()` is a primitive used by FAT file writes, not a partition-table implementation. |

The only parsing found is in root `disk_manager.cpp`, the duplicate bare-metal `DiskManagerApp` in `kernel/core/kernel_apps.cpp`, and the FAT driver's limited search for the first FAT primary partition during mount. No other reusable MBR/GPT parser or writer exists in the installer, bootloader, kernel, or storage tools.

## 7. Filesystem capability matrix

The matrix describes source capabilities, not a claim that Disk Manager can format or safely manage a filesystem.

| Filesystem | Probe / mount | Read | Write | Create / mkfs | Partition-aware mount | Unmount / sync / metadata |
|---|---|---|---|---|---|---|
| FAT12/16/32 | `fs_fat` probes FAT16/FAT32 boot sectors; VFS heuristically calls the family FAT32. The driver's whole-disk mount can inspect four MBR primary entries and choose a supported FAT partition. | Implemented file and directory traversal. | FAT driver and VFS implement file/directory mutations on the FAT32 VFS path; this is filesystem data writing, not formatting. | No OS-runtime formatter. | Limited to the FAT driver's first matching supported MBR primary entry; no extended/GPT support. | `fs_fat::flush()` calls block flush; VFS close/flush only does this for `FS_TYPE_FAT32`. Volume label is read for FAT16/32. VFS free-space returns 0; `total_space()` reports the whole block device. No checker/repair API. |
| exFAT | Probe and mount code exists in `fs_fat`; VFS recognizes/mounts `FS_TYPE_EXFAT` when detected or explicitly selected. Whole-disk MBR path does not mount an exFAT partition by offset. | Read paths are routed through the FAT driver; feature completeness is partial. | VFS write dispatch excludes `FS_TYPE_EXFAT`, despite mount-table `readOnly` being inferred only from the block callback. | None. | Direct volume only; not a general partition adapter. | Unmount routes through `fs_fat`; VFS flush/close does not issue FAT flush for exFAT. Label is empty in the exFAT mount path. No repair/check. |
| ext2/ext4 | `fs_ext4::mount()` checks ext magic at block-device LBA 2. VFS distinguishes ext2/ext4 by a feature bit. | Directory/inode/file data read code exists. | No ext write implementation or VFS write dispatch. | None. | No partition-offset mount. | `unmount()` exists; no sync/writeback implementation. Volume name is parsed internally; VFS has no label getter. No free-space or repair/check support. Geometry math assumes 512-byte block-layer sectors. |
| UFS | Standalone UFS1/UFS2 superblock mount/probe and volume structures exist. | Read-only directory and file traversal code exists. | None. | None. | No partition offset. | Standalone unmount exists, but VFS's UFS mount case is explicitly commented out. No label/free-space/check support exposed to Disk Manager. Code converts bytes using 512-byte sectors. |
| NTFS | `fs_ntfs::probe()` and mount metadata code exist. NTFS is not in VFS's `FSType` enum. | Path resolution/open/read contain TODO/stub paths and return unsupported. | Stub/unsupported. | None. | None. | Sync is a stub; label parsing and free-space are TODO. Not integrated with VFS or Disk Manager. |
| XFS | `fs_xfs::probe()` and mount metadata exist, but XFS is not in VFS's `FSType` enum. | Path/open/read are stubs/unsupported. | Stub/unsupported. | None. | None. | Sync and metadata APIs are incomplete; not integrated with VFS or Disk Manager. |
| ISO9660 | `FS_TYPE_ISO9660` enum/name only. No filesystem driver or mount case. | None found. | None. | None. | None. | Unsupported. |
| TarFS / initrd | Disk Manager checks `ustar` bytes at offset 257. No TarFS block filesystem driver or VFS mount case was found. | No TarFS driver read path. | None. | None. | None. | Boot `ramdisk.img` is used as a wallpaper/package source and VFS subdirectory/alias source in other flows; that does not create a TarFS block device. |
| RAM disk | Block device only; `FS_TYPE_RAMDISK` is not handled by the VFS mount switch. | Block reads work for in-memory disks. | Writable pool disks accept memory writes; image-backed RAM disks are read-only. | No filesystem creation or format operation. | Not applicable. | No persistence across reboot. Image memory lifetime is caller-owned. |

No runtime `mkfs`/formatter for FAT, exFAT, ext, NTFS, UFS, ISO9660, or XFS was found. FAT formatting occurs in host-side release/test tooling (`tools/release_esp.py`, `scripts/create-test-disks.*`) to generate images, not in Disk Manager or on bare-metal devices. The installer’s similarly named FAT32 helper is a stub that returns success.

## 8. VFS and mount findings

* `vfs::mount_partition(path, blockDevIndex, partitionNumber)` is a confirmed TODO stub and returns `0xFF`.
* Whole-device `vfs::mount()` can mount direct FAT/exFAT or ext superblocks. For an MBR containing FAT, `detect_fs_type()` notices a FAT MBR type and `fs_fat::mount()` scans the four primary entries for a FAT16/32 boot sector and saves its `partitionOffset`. This is the only meaningful partition-offset mount path found; it is a narrow implicit FAT path, not the `mount_partition()` API. There is no GPT path. ext4 is mounted at raw LBA 2 and is not offset-aware. exFAT is tried at LBA 0 rather than through the MBR partition loop.
* `MountPoint` records active path, filesystem type, block-device slot, filesystem-driver volume slot, a `readOnly` flag, alias and source prefix. It does not expose a partition number/start LBA, disk GUID, label, actual capacity of the mounted partition, or media generation.
* `get_mount()` can report an actual active VFS mount. Disk Manager's heuristic comparison of `fsVolumeIndex == partitionIndex + 1` is not a supported partition mapping. The kernel `DiskManagerApp` does not consult the mount table.
* `mount_type()` computes `readOnly` only as “block descriptor absent or no `writeFn`”. A writable block callback makes ext4 and exFAT appear writable even though VFS write dispatch is FAT32-only. This flag is not a reliable filesystem write-capability indicator.
* `total_space()` returns `BlockDevice.totalSectors * sectorSize`, which is whole-device capacity, not the mounted partition/filesystem size. `free_space()` is hard-coded to zero. There is no VFS label/checker API for Disk Manager.
* `unmount()` exists and refuses when VFS file handles are open, then calls a filesystem unmount and removes the mount entry. It does not protect `/`, check active directory iterators or alias dependents, or explicitly flush the device before detaching. Disk Manager exposes no unmount action. Treat it as insufficient for safe removal/system-volume operations.

## 9. Device identity and operation target safety

The only common identity material is a reusable global slot number plus `DeviceType`, local driver index, short display name, capacity and sector size. These fields are not sufficient to revalidate a destructive target after refresh or device removal.

Some lower-layer information could be surfaced later: ATA's driver descriptor has model/serial and I/O channel/drive; NVMe's descriptor has model/serial, BAR and namespace ID; USB storage has USB address, interface/LUN and SCSI inquiry strings. None is represented in the common block descriptor, and no complete controller/port path is stored across all transports. Host images have a path, but the target record omits size/timestamp/hash and uses a relative path.

Recommended bounded identity model for DM2:

1. Give each registered device a monotonically changing registry/media generation and a transport-specific identity record. Include controller path/port or PCI BDF where available, namespace/LUN, model/serial where available, sector size, capacity, and explicit read-only/removable/online flags.
2. Build a target fingerprint from the transport identity plus geometry and a digest of the currently observed partition-table sectors. For image files include canonical path, length, modification metadata, and preferably a content digest. Keep the original slot only as a lookup hint.
3. If stable identity is absent or duplicated, fail closed for destructive operations or require a test-only RAM/image target. Do not silently fall back to `Disk 0`.
4. Before write, re-resolve by identity, confirm the registry/media generation and geometry, reread relevant table sectors, and compare with the captured fingerprint. Reject ambiguity or change. Serialize operations against registry updates and mount/unmount changes.

This gives a practical revalidation mechanism without claiming hardware serials are globally unique or making raw-device writes atomic.

## 10. Destructive-operation safety findings

The Disk Manager's format and create handlers are disabled and do not write. The lower layer does contain write primitives and FAT filesystem mutations, so future storage writes cannot be gated only by the UI.

Before a destructive path is enabled, DM2 needs: explicit device capabilities; sector-size-safe I/O; truthful flush behavior; a stable target fingerprint; mount/root protection; an operation lock; a second target validation immediately before write; bounded write sets; flush and read-back validation; and a post-operation rescan. User confirmation must name the resolved model/identity, capacity, sector size, scheme, and exact action. A changed target or table must cancel the operation before any write.

No raw-disk operation can be promised atomic across multiple sectors or across power loss. The UI and error model must distinguish “failed before write” from “partial write / recovery required”. Never report success solely because a callback returned; verify written bytes and parse the resulting state independently.

## 11. Sector-size, geometry, and arithmetic assumptions

`BlockDevice.sectorSize` says it can be 512 or 4096, and NVMe derives it from namespace data. Multiple consumers nevertheless assume 512-byte logical sectors:

* Both Disk Manager implementations use `uint8_t[512]` for a one-sector read. On a 4096-byte device the callback writes 4096 bytes into that buffer. The root `probeOnce()`, MBR read, and filesystem probes share this issue. `kernel_apps.cpp` repeats it.
* VFS `detect_fs_type()` reads one sector into a 2048-byte local buffer. This is also too small for 4096-byte logical sectors.
* `fs_ext4` uses a 4096-byte buffer but reads LBA 2/count 2 for the 1024-byte-offset superblock, and computes filesystem blocks as `blockSize / 512`; on a 4096-byte block device this can both overrun the buffer and address the wrong byte offset.
* `fs_ufs` uses 512-byte LBA conversions and reads four sectors into an 8192-byte buffer; four 4096-byte sectors exceed it. Its superblock location is not translated by device logical sector size.
* FAT uses a larger shared sector buffer and BPB byte-size fields, but volume-relative sector numbers are passed as block logical LBAs without validating that filesystem bytes-per-sector matches device logical sector size. This is not a generic 4Kn adapter.
* MBR fields are 32-bit by format; Disk Manager cannot represent GPT's 64-bit LBA entries. The common block API has 64-bit LBA, but transport/FS users do not uniformly preserve it.
* Fixed byte offsets 510/511 are used for MBR/FAT signatures. For GPT/MBR support on 4Kn, these byte offsets remain inside the first logical sector, but the read/write buffer and full-sector preservation must use the actual logical size.
* Host images are unconditionally interpreted as 512-byte-sector images. `readHostSectors()` multiplies LBA/count by sector size and checks `offset + bytes` without checked-overflow helpers. Disk capacity and displayed byte totals use unchecked `totalSectors * sectorSize` arithmetic.

The 4Kn buffer overruns are an important blocker. DM2 should define a sector-I/O contract that provides/validates buffer length and byte offsets, and should fix all readers used by Disk Manager, VFS and mounted filesystem paths before real writes are considered.

## 12. Boot/system disk protection

`isSystem` is not sufficient protection. ATA/AHCI/NVMe are marked system-like regardless of actual role; the root filesystem can be on another transport; RAM and USB are also currently conflated in the block enum. An MBR active flag is not boot provenance.

The kernel does have an authoritative view of the current VFS root mount: `main.cpp::mount_persistent_storage()` chooses the first non-transient block descriptor with read/write callbacks that successfully mounts at `/`, and the mount table records its block-device index. This is the current mounted root, not the physical firmware boot device. `vfs::get_mount("/")` exposes the mount, but not partition offset in the common mount descriptor.

Canonical UEFI `BootInfo` carries memory, framebuffer, command line, ramdisk and NIC fields; it does not carry a storage device path/handle. The bootloader's loaded-image/EFI device relationship is not propagated as a stable block identity for Disk Manager. The executing system volume/firmware boot device therefore cannot currently be authoritatively joined to a block descriptor.

Future rules: never initialize/partition/format a device used by the active `/` mount or a boot dependency; do not unmount `/`; require VFS open-file/directory/alias checks and successful sync before an allowed unmount; disallow operations on read-only images and unknown/removable devices unless supported policy explicitly permits them; use explicit RAM/image classes rather than names; and treat missing boot identity as an unresolved protected-target condition until DM2 provides a reliable source.

## 13. Existing write/destructive code and reusable infrastructure

Repository-wide searches found:

* No MBR/GPT writer, partition creation/deletion/resizing, GPT repair, or wipe implementation.
* `kernel::block::write_sectors()` is implemented for ATA PIO and NVMe and RAM disk callbacks. The USB storage driver has standalone SCSI write code but is not registered with the block layer. VirtIO request submission is a stub.
* `fs_fat` writes files/directories to mounted FAT volumes and flushes through the block layer. It has no mkfs operation and must not be repurposed as a disk formatter without a separate audited formatter.
* `HDInstaller` scans a fabricated placeholder disk. `partitionDisk()`, `formatPartitions()`, `copySystemFiles()`, `installBootloader()`, and `formatFAT32Partition()` are simulated/TODO paths; the format helper returns success without formatting. It is not reusable partitioning code.
* `tools/release_esp.py` and `scripts/create-test-disks.*` use host tooling to create test/release filesystem images. They are useful fixture-generation references, not kernel-side safe formatters.
* `kernel::virtio::block::BlockDevice` contains a separate API and feature constants, but `submitRequest()` does not queue requests and returns success. It is not a working fallback.

Reusable foundations are the range-checked shared block dispatch, ATA/NVMe read/write callbacks, read-only RAM image adapter (after fixing ownership and registration), FAT volume-relative offset code for a limited MBR-primary case, VFS mount records, and existing prebuilt FAT/ext4 test images. None is sufficient by itself to enable destructive Disk Manager actions.

## 14. Major risks, ordered by dependency

1. **Memory corruption on 4Kn I/O:** fixed 512/2048-byte readers are passed to callbacks that write a full logical sector; ext4/UFS have additional 512-based sector conversions.
2. **False durability:** missing block flush callback reports `BLOCK_OK`; NVMe has writes and no Flush implementation.
3. **Wrong-target risk:** transient indexes and vector row selection are used as identity; registry reuse/hot-removal is unsupported and the UI does not revalidate.
4. **Weak system protection:** transport and MBR boot flags are heuristics; firmware boot-device identity is not propagated.
5. **Wrong mount state:** suggested path and `partitionIndex + 1` comparison do not identify actual partition mounts.
6. **Partition misclassification:** signature-only MBR recognition, no GPT, no range/overlap checks, and no raw/invalid/unsupported distinction.
7. **RAM image lifetime/registry errors:** bare-metal image rescan can free backing memory still referenced by a read-only RAM disk; RAM disk destruction leaves the block slot active.
8. **Filesystem capability confusion:** writable block callback is treated as a writable VFS mount even for filesystems with no write implementation; “Healthy” is unconditional.
9. **Unbounded product claims:** documented USB/AHCI support and installer operations are not backed by connected implementations.

## 15. Proposed common operation architecture

Implement a kernel-owned storage operation service, not one set of raw write loops per button. A descriptor should include operation ID/type, resolved target fingerprint, expected current table state, exact write ranges, planned resulting state, required block capabilities, owner/request ID, and expiry/generation. Validation should produce typed issues (invalid target, read-only, mounted/system protected, unsupported geometry, malformed/unsupported table, insufficient capacity, flush unsupported, identity changed, I/O error, verification mismatch, partial/recovery-required).

Recommended lifecycle:

```text
Idle -> Validating -> AwaitingConfirmation -> Revalidating
     -> Writing -> Flushing -> Verifying -> Rescanning -> Completed
                                      \---------------------> Failed
```

Cancellation is always allowed before the first write. After the first write, cancellation cannot promise rollback; finish the bounded recovery-safe sequence where possible and report partial state. Serialize operations per target, block eject/unregister during an active operation, log identity and sector ranges, flush where supported, read back, independently parse, then rescan the UI from authoritative state. Use an explicit “durability unavailable” result instead of treating a missing callback as proof.

Keep each operation's write set small and specified. GPT's redundant copies improve recoverability but do not make multi-sector writes atomic. Do not attempt generic rollback by restoring stale sector buffers after a device or identity change.

## 16. Phase DM3 Initialize Disk design

GPT should be the normal/default choice. Require DM2's identity revalidation, sector geometry, real durability semantics and parser/test infrastructure first.

### GPT layout and algorithm

* Validate logical sector size, capacity, overflow-safe LBA bounds, supported minimum capacity, alignment, and absence of active mounts. Use actual logical sector size for allocations and writes.
* Choose a fixed entry count/size (for example, 128 entries of 128 bytes). Compute entry-array sectors from byte size and logical sector size with checked ceiling division. Place primary header at LBA 1 and primary array after it; place backup header at the last LBA and backup array immediately before it. Compute usable bounds around both arrays and align the first usable LBA to the selected alignment (normally 1 MiB in bytes, converted to logical sectors). Refuse devices too small for both copies and the usable gap.
* Generate a nonzero disk GUID using a suitable kernel RNG; zero the partition entries for an empty initialized disk. Compute entry-array CRC32 and header CRC32 with the header CRC field zeroed. Fill current/backup LBAs, usable bounds, disk GUID, array LBA/count/size, and array CRC explicitly.
* Write backup array, then backup header; flush and read back/validate. Then write primary array and primary header; flush and validate. Write the protective MBR last as the compatibility marker, preserving a full logical-sector buffer and placing the MBR signature and one `0xEE` entry in the standard first 512-byte layout. Flush, reread all written ranges, validate both GPT copies/CRCs and the protective MBR, then rescan.
* A power loss can leave one valid GPT copy or partially written metadata. On restart, classify primary/backup independently and expose “initialization incomplete/recovery required”; never silently call it initialized or automatically wipe/reinitialize.

### MBR alternative

Offer MBR only as an explicit compatibility option, with a hard addressability check. The entry LBA/count fields cap at `0xFFFFFFFF` logical sectors: about 2 TiB with 512-byte logical sectors and about 16 TiB with 4096-byte logical sectors. MBR layout stores the signature at byte offsets 510–511 even when the logical sector is larger; write the whole logical sector to avoid truncation/overwrite. Create four zeroed primary entries and a valid `55 AA` signature. A nonzero disk signature can be generated for compatibility but is not a stable hardware identity. Do not include boot code, partition creation, or formatting in initialization.

## 17. Test and proof architecture

DM1 found no Disk Manager, block-layer, partition-table, or VFS unit tests in `tests/`. Later phases should add deterministic tests around a memory-backed fake block device and disposable raw image files only.

* Read-only parser fixtures: blank/raw sectors; valid empty/populated MBR; out-of-range/overlapping entries; extended/logical entries; protective and hybrid MBR; valid primary/backup GPT; bad signature/header size/CRC/entry CRC; truncated arrays; inconsistent usable bounds; backup-only and mismatched copies.
* Geometry fixtures: 512-byte and 4096-byte logical sectors, sizes near alignment/entry-array boundaries, capacity arithmetic overflow, >2 TiB MBR rejection and GPT 64-bit LBAs. Exercise 4Kn through a fake block adapter even if QEMU hardware emulation is unavailable.
* Operation fixtures: read-only/no-flush/flush-failure devices; short/error reads; writes that fail after selected sectors; post-write readback mismatch; media generation change between confirmation and write; duplicate/ambiguous identities; currently mounted root; RAM/image targets.
* QEMU proof should create a fresh disposable blank image per run, attach it as a scratch disk, and never point at a host physical drive or the checked-in fixture. Keep known MBR/GPT/FAT/ext images read-only; use `-snapshot` or an explicit temporary copy for any mutating run.
* Independently inspect image sectors with a separate parser such as `sgdisk --verify`/`gdisk`, a small Python `struct` + CRC32 verifier, `xxd`/sector dumps, and filesystem-specific read-only checkers. Preserve raw images and validation output as artifacts. Inject power-loss/failure points between GPT copy writes.

## 18. Recommended DM2+ roadmap

| Phase | Scope and dependencies | Safety boundary | Expected proof | Explicit exclusions |
|---|---|---|---|---|
| **DM2 — Storage safety foundation and read-only parser** | Add explicit transport capabilities/identity, registry generation and unregister discipline; fix sector-size-safe buffers/offsets and checked arithmetic; make flush capability truthful; add pure validated MBR/GPT parser and typed disk/table states; add fake block-device tests. | No physical storage writes; exercise only in-memory fake devices and parser fixtures. | 512/4096-byte tests, malformed-table cases, missing/failed flush cases, target identity and registry-change cases pass. | Initialize, create/delete/resize partitions, mkfs, automatic repair, hardware destructive tests. |
| **DM3 — Initialize Disk** | GPT-default and explicit MBR initialization using DM2 operation service, confirmation and read-back verification. | First write path only against disposable image/fake media in tests; runtime policy must reject mounted/root/ambiguous targets. | Independently validate both GPT copies/CRCs and MBR; injected interruption leaves diagnosable/recoverable state. | Partition creation, format, mount, deletion, resize. |
| **DM4 — Authoritative Disk Manager state/UI** | Replace synthetic hosted claims and heuristic “system/healthy/mounted” labels with the shared state model; show identity, capacity, sector size, writable/removable/scheme, and raw/unsupported/error distinctions. | Read-only UI and refresh actions. | Hosted and QEMU UI consume the same descriptor/parser outputs; no write controls enabled. | Partition creation or filesystem changes. |
| **DM5 — Create Partition** | Plan and create one partition in validated unallocated extents; support GPT first, MBR only with explicit limits. | Revalidate target/table immediately before write; reject overlap, root-mounted and ambiguous targets. | Disposable images, exact-sector readback and independent parser verification, injected write failure. | Delete/resize, formatting, filesystem repair. |
| **DM6a — Partition-aware VFS mount foundation** | Add a bounded partition block adapter (start/length, overflow checks, identity) and truthful mount-table partition metadata; safe sync/unmount rules. | Read-only mounts first; never detach `/`, open directories/files, alias dependencies, or active operation targets. | Mount FAT/ext test partitions without whole-disk ambiguity; confirm actual mount metadata and safe unmount refusal cases. | Formatter and destructive volume operations. |
| **DM6b — FAT32 format/mount workflow** | Add a separately reviewed FAT32 formatter for a selected, validated partition; then mount and display actual label/capacity/free space where supported. Keep supported filesystem scope narrow. | Strong confirmation; revalidate; verified flush/readback; only disposable images in automated writes. | Independent `fsck.fat`/mtools inspection, remount/read/write smoke tests, injected partial-format failures. | exFAT/ext/NTFS formatting, repair, multi-filesystem wizard. |
| **DM7 — Quality of life and diagnostics** | Properties, logs, hardware rescan/hotplug where drivers support it, supported removable eject, diagnostics, clearer progress/error/recovery messages. | No automatic destructive repair or background write. | Simulated removal/error cases and read-only diagnostics against fixtures. | Unsupported transport claims or unbounded repair. |
| **DM8 — Hardening** | Fuzz parsers; concurrency and unplug stress; power-loss injection; review persistence, UI confirmation and boot/root policy; close discovered lifecycle bugs. | Continue disposable-only writes; no claim of raw multi-sector atomicity. | Fuzz corpus, repeated regression suite, QEMU scratch-media evidence, independent sector validation. | New filesystem formats or broad transport expansion. |

## 19. Capability matrix

| Capability | Hosted | Bare Metal | Read | Write | Proven | Stub / partial | Notes |
|---|---|---|---|---|---|---|---|
| Disk enumeration | Synthetic row plus attached `.img` files | Shared block registry | Yes for registered block entries | No via Disk Manager | Source-level | Partial | No host physical disk enumeration; block refresh is not hardware rescan. |
| ATA | Synthetic label only | ATA PIO registration | Yes | Yes | Source-level | Partial | 512-byte sectors; identity data not in common descriptor. |
| AHCI | Synthetic label only | No registered driver found | No | No | No | Stub/absent | Header enum/constants and misleading scan comments only. |
| NVMe | Synthetic label only | Namespace 1 registration | Yes | Yes | Source-level | Partial | No block flush callback; PRP list TODO; no removable/read-only metadata. |
| USB mass storage | `.img` images may be labeled USB | Standalone BOT/SCSI driver not connected to block layer | Standalone code only | Standalone code only | No integration proof | Partial | No `usb_storage::probe()` caller or block registration found. |
| RAM disk | No kernel RAM disk in hosted synthetic path | Registered memory device | Yes | Yes for writable pool disk; read-only image has no write callback | Source-level | Partial | Reuses USB type; destroy does not unregister. |
| Image attachment | Relative `disks\*.img`, read-only | VFS `.img` to read-only RAM disk in root branch | Yes | No | Source-level | Partial | Bare-metal backing-buffer lifetime issue on rescan; not present in kernel `DiskManagerApp`. |
| MBR parsing | Read-only on `.img` | Four-entry signature-only parser | Yes | No table write | Source-level | Partial | No structural validation, EBR, protective-MBR handling, or robust empty/raw classification. |
| GPT parsing | No | No | No | No | No | Absent | No header/entry/CRC/backup handling found. |
| Raw disk recognition | No host hardware state | Signature failure becomes invalid MBR | Reads first sector | No | No | Absent/ambiguous | Cannot distinguish blank/raw from corrupt, unreadable, or unsupported scheme. |
| Filesystem probing | Synthetic NTFS row; probes images | FAT/EXT/Tar heuristic in UI | Sector reads | No probe writes | Source-level | Partial | No mount or consistency proof; 512-byte buffer risk. |
| Partition creation | Disabled UI | No action | No | No | Yes: disabled handler | Stub | No MBR/GPT writer anywhere. |
| Disk initialization | No | No | No | No | Yes: absent | Absent | DM3 design only. |
| Formatting | Disabled UI; external image tooling only | No action | No | No Disk Manager format | Yes: disabled handler | Stub | FAT helper in installer returns success without formatting. |
| Partition mount | No | VFS `mount_partition()` returns failure | Limited FAT primary scan through whole-device mount | N/A | Source-level | Partial | No GPT/extended/general partition adapter. |
| Unmount | No action | VFS API exists | N/A | Metadata only | Source-level | Partial | Not exposed; no root/iterator/alias guard or explicit final flush. |
| Flush | No action | ATA callback; NVMe/RAM no callback | N/A | Callback where present | Source-level | Partial | Null callback becomes `BLOCK_OK`, which is not reliable durability proof for NVMe. |
| Refresh/rescan | Refresh synthetic state; separate `.img` directory rescan | Re-enumerates current block registry | Yes | No | Source-level | Partial | Does not query transports for newly attached hardware. |
| Hot removal | N/A for synthetic disk | No unregister caller found; block API has unregister primitive | No reliable state | No | No | Absent | RAM disk destroy also leaves global entry active. |
| Device identity | Image relative path only; synthetic disk | Slot/name/capacity/sector size | N/A | N/A | Source-level | Insufficient | ATA/NVMe lower descriptors have some identity, but not shared or revalidated. |
| Boot/system protection | Fake disk marked system | Transport/name heuristic; active root mount known only in VFS | N/A | No Disk Manager writes | Source-level | Insufficient | Firmware boot-device identity is not propagated to block descriptor. |

## 20. Files inspected

Primary application and UI: `disk_manager.cpp`, `disk_manager.h`, `docs/DISKMANAGER_IMPLEMENTATION.md`, `kernel/core/kernel_apps.cpp`, `kernel/core/include/kernel/kernel_apps.h`, `built_in_app_metadata.h`, `desktop_service.cpp`, `process.cpp`, `process.h`, `ipc_bus.cpp`, `gui_protocol.h`.

Storage/VFS/filesystems: `kernel/core/include/kernel/block_device.h`, `kernel/core/block_device.cpp`, `kernel/core/include/kernel/ata.h`, `kernel/core/ata.cpp`, `kernel/core/nvme.cpp`, `kernel/core/include/kernel/nvme.h`, `kernel/core/usb_storage.cpp`, `kernel/core/include/kernel/usb_storage.h`, `kernel/core/ramdisk.cpp`, `kernel/core/include/kernel/ramdisk.h`, `kernel/core/virtio_block.cpp`, `kernel/core/include/kernel/virtio_block.h`, `kernel/core/vfs.cpp`, `kernel/core/include/kernel/vfs.h`, `kernel/core/fs_fat.cpp`, `kernel/core/include/kernel/fs_fat.h`, `kernel/core/fs_ext4.cpp`, `kernel/core/fs_ufs.cpp`, `kernel/core/fs_ntfs.cpp`, `kernel/core/fs_xfs.cpp`.

Boot/install/build/test tooling: `kernel/core/main.cpp`, `guideXOSBootLoader/guidexOSBootInfo.h`, `hd_installer.cpp`, `hd_installer.h`, `tools/release_esp.py`, `scripts/create-test-disks.ps1`, `scripts/create-test-disks.sh`, `scripts/run-qemu-fs-test.ps1`, `build.bat`, `kernel/Makefile`, `tests/` inventory, and relevant project source lists.

Repository-wide searches covered MBR/GPT signatures, partition-table reads/writes, format/mkfs, block registration/removal, transport callbacks, filesystem driver entry points, and partition-aware VFS call sites. Searches excluded generated `tmp/`, `out/`, and vendored `third_party/` content when identifying implementation code.

## 21. Tests and builds run

* `cmd /c build.bat`: **blocked before C++ compilation**. The stb image reproducibility check passed; `scripts/verify-mbedtls-profile.ps1` stopped because `third_party/mbedtls` is missing. No dependency bootstrap was run.
* Hosted `disk_manager.cpp` translation-unit syntax check with MinGW `g++ -std=c++17 -fsyntax-only -D_WIN32`: **passed**. Existing warnings: misleading indentation in `gui_protocol.h::packPins()` and an unused local in the disabled create-partition handler.
* Root `disk_manager.cpp` non-Windows branch syntax check with MinGW and quote-only kernel include paths: **passed**, same warnings. This does not build the freestanding kernel app implementation.
* No Disk Manager, block-layer, partition-table, or VFS unit tests were found under `tests/`.
* QEMU is not installed/discoverable in the environment. The existing QEMU filesystem launcher attaches repository test images writable, so it was not run. No boot/runtime smoke test was performed.
* No storage image, partition table, filesystem, or physical disk was written during DM1.

## 22. Final outcome

**Outcome B — Audit complete with important architectural blocker.** The architecture and current capabilities are mapped. DM2 must first make sector geometry safe, flush status truthful, device identity/revalidation available, and root/mount protection authoritative before any DM3 disk initialization write path can be considered safe. The previous implementation summary has been replaced with a current factual status note and points here for the full audit.
