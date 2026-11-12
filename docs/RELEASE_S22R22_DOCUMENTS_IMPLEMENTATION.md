# S22R22 — Documents Implementation and Validation

**Disposition: Outcome B — `PERSISTENCE_IMPLEMENTED_GUEST_PENDING`**  
**Validated source:** `eb61ca10b48c0e96e1a04a302e82d6c52b828f41`  
**Previous S22R21 ISO preserved:** `86a3296adf0558fa509a044232b0086e57edd6c0bd4236677fba90d8a294d5fe`

## Implementation

The implementation reuses ATA PIO, block-device generation/flush, FAT32, and VFS. Main changes are in:

- `kernel/core/user_data_volume.cpp` and `kernel/core/include/kernel/user_data_volume*.h` — stable-identity selection, volume validation, mount, failure reporting, and alias setup.
- `kernel/core/vfs.cpp` and `kernel/core/include/kernel/vfs.h` — open/truncate/append, exact write counts, close/flush status, aliases, and failure cleanup.
- `kernel/core/fs_fat.cpp` and `kernel/core/include/kernel/fs_fat.h` — file/directory result codes, file creation/resize/write, short-write and I/O status propagation, metadata updates, and flush support.
- `scripts/provision-user-data-volume.py` — bounded new-image provisioning; it never formats an existing path or physical disk.
- `scripts/run-qac-s22r10-uefi.ps1` and `tests/qac_s22r22_persistent_preview_test.ps1` — QAC-owned ISO plus one raw writable test image, image identity checked before QEMU opens it, and focused launch-contract regression.

The public-preview boot mounts the read-only ramdisk root and `/system` alias first, then identifies one active ATA device whose stable serial is `GXOS-DATA-V1`. It mounts whole-disk FAT32 at `/Documents`, validates offset 0, 512-byte sectors, `GXOSDATA`, `GXOSDAT.ID`, and `APPDATA`, and then installs `/AppData` as an alias to `/Documents/APPDATA`. Device index/name is logged but is not used as the persistent identity. The mount fails closed and rolls back any newly installed mount/alias if validation or identity recheck fails.

`/Documents` is a VFS mount point, not a directory created in the trusted root. This removes the earlier nine-character `Documents` 8.3 failure without adding long-filename support. User files are real block/FAT writes.

## Supported file operation contract

| Operation | Existing-stack behavior after S22R22 |
|---|---|
| Create | `OPEN_CREATE` and `create_file` create regular files; exclusive create reports an existing file. |
| Read | `open(READ)`, `read`, stat, and directory enumeration use FAT entries and cluster chains. |
| Write | `OPEN_WRITE`, truncate or append, and `write` report I/O, no-space, timeout, corrupt-chain, and short-write conditions. |
| Seek | `SEEK_SET`, `SEEK_CUR`, and `SEEK_END`; negative/out-of-range positions fail. |
| Close/commit | Writable close calls the volume flush and reports errors. `write_file` checks all chunks and close status before returning the byte count. |
| Modify | Existing files can be truncated and rewritten or appended through the same VFS/FAT path. |
| Directories | `mkdir` creates supported short-name directories and reports missing parent, invalid name, full directory, no cluster, or I/O error. |
| Missing paths | VFS returns `VFS_ERR_NOT_FOUND` or the specific mount/path status. |
| Capacity errors | FAT no-cluster/no-entry and VFS no-space/I/O statuses are not converted to successful saves. |

For high-level writes, VFS streams in chunks no larger than 4096 bytes. It treats a zero or partial chunk as an error and always closes the handle; a close/flush error is returned rather than a success byte count. This does not provide an atomic replacement protocol. FAT32 has no journal in this implementation, and a failure during a sequence of sector updates can leave the filesystem needing external recovery.

The driver supports the FAT short-name subset only. Valid names must fit its 8.3 encoding; names are not silently shortened. A guest file saved as `/Documents/untitled.txt` appears to FAT tools as `UNTITLED.TXT`. Long names and unsupported characters are rejected rather than misreported as saved.

## Existing document application

Built-in Notepad already has Save and Save As actions and routes file data through `vfs::write_file`. It stores the document as bytes at the chosen VFS path; there is no separate application-settings serialization format for Notepad. In the guest run, Save As chose `/Documents/untitled.txt`, the window title changed to `/Documents/untitled.txt - Notepad`, and a later read of the actual test image found the document at 161 bytes. The bytes were:

```text
Welcome to guideXOS Notepad!

File/Edit menus available.
Right-click for context menu.
Ctrl+S to save, Ctrl+O to open.

Type here...s22r22persistencepayloadalpha
```

The focused guest UI did not complete a save of a modification to this file, and it did not create the second requested guest document. Hosted FAT/VFS tests cover modification, repeat open/close, directory operations, and restart/reinitialization. The exact limits of guest evidence are recorded in [the guest report](RELEASE_S22R22_PERSISTENCE_GUEST_QUALIFICATION.md).

## Hosted, freestanding, and stack qualification

The following current-source checks passed before candidate construction:

| Check | Result |
|---|---|
| Image-backed FAT/VFS persistence | 32/32, including create/write/read/modify, reopen/close, metadata/size, directory use, and filesystem-state reinitialization. |
| User-data selection/mount policy | 9/9, including missing, ambiguous, wrong/system identity, read-only, no-flush, unsupported transport, and invalid device. |
| VFS mount-table integrity | 19/19. |
| Native filesystem contract / block generation | 5/5. |
| Storage bridge / system bridge / device bridge | 15/15 / 82/82 / 14/14. |
| Native ELF security policy | 27 cases; writable aliases and untrusted sources rejected before native entry; approved identity/resource/ABI checks retained. |
| ABI layout and settings regression | ABI layout passed; settings native runtime 1,426/1,426, plus 100 lifecycle cycles, 250 provider refresh cycles, and 1,000 notification/state transitions. |
| QAC S22R22 profile and launcher tests | 8/8 focused profile assertions; PowerShell parser/diff checks passed. |

AMD64 freestanding build and kernel symbol audit passed. The static 16 KiB stack guard retained at least the required 4,096-byte headroom:

| Guarded path | Static worst-case estimate | Headroom |
|---|---:|---:|
| Settings display/navigation | 11,480 / 16,384 bytes | 4,904 bytes |
| Legacy Display Options | 11,864 / 16,384 bytes | 4,520 bytes |
| COM2 storage | 7,736 / 16,384 bytes | 8,648 bytes |

These are static `.su` estimates, not runtime high-water measurements. The kernel source hash was unchanged by the final QAC runner correction. Storage bridge and COM2 checks were rerun; the new persistent mount does not add a COM2 endpoint or host integration.

## Security-boundary regression

`/Apps` remains rooted at the read-only release ramdisk. The public-preview loader still checks exact package inventory, manifest and ELF identity, resource hashes, source mount/device/generation, supported AMD64 ABI and entry point, and source identity again immediately before entry. Writable storage cannot impersonate the package source through aliases or normalized paths. QAC attached the candidate ISO read-only and the data image as a separately identified, nonbootable raw disk. It attached no network, host share, USB device, or guest-accessible COM2 endpoint.

The 27-case hosted policy suite used synthetic fixtures and a native-entry sentinel; writable source attempts were rejected before the sentinel. No unauthorized payload was executed. A matching fake PacMan package was not placed on the QAC writable image; the live candidate boot confirmed approved `/Apps` and ordinary approved PacMan lifecycle behavior. The residual risk of kernel-privileged PacMan remains.

## Settings and application-data audit for S22R23

| Component | Existing location / format | Write/UI state on this profile | S22R23 action |
|---|---|---|---|
| Notepad documents | User-selected VFS path; raw document bytes. | Save/Save As is implemented and a real `/Documents` save succeeded. | Keep user content in `/Documents`; surface exact I/O/no-space errors. No app-settings file exists. |
| Desktop appearance | `/desktop.cfg` text settings; legacy `/display-options.cfg`, `/wallpaper.id`, `/desktop.wallpaper.id`, `/desktop.taskbar.position`, `/desktop.background.scale`, `/desktop.system.icons`. | Shell read/write call sites exist, but these root paths are on the read-only system image in Public Preview; writes do not persist. Legacy display configuration is explicitly unsupported by S22R21. | Define the supported subset and route writes to valid 8.3 names under `/AppData`; preserve unsupported display behavior. |
| Desktop icon layout | `/.desktop_icons`, text/layout data written through VFS. | Save code exists under the read-only root; no persistent write is qualified. | Move only if icon placement is declared supported; bound parser and report failures. |
| Desktop shortcuts | `/desktop.shortcuts`, tab-separated shortcut records. | Save/load code exists under the read-only root; no persistent write is qualified. | Choose supported shortcut records and `/AppData` schema; migrate without changing launch identity. |
| Settings / Control Panel | Settings UI/model state; public-preview System view is read-only. | No supported persistent settings save path was qualified; display configuration remains explicitly unavailable. | Implement only owner-approved writable settings and retain read-only display boundary. |
| PacMan | No supported persistent user-data path identified for this release profile. | No score/settings save action qualified. | No persistence claim unless a separate feature is approved. |
| Recent Programs | `/.startmenu_recent`, newline-separated recent names. | Previously failed on dotted FAT short-name constraints; recent history is optional by owner decision. | Leave optional; do not add to the required persistence contract by default. |

S22R23 should define the settings schema, use names accepted by the current FAT 8.3 driver, report failures to users, and test full-shutdown reload. No S22R22 code claims persistence for settings that are not saved today.
