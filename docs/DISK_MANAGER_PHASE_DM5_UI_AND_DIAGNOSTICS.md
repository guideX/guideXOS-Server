# Disk Manager Improvements — DM5 UI and Diagnostics

## Outcome

**Outcome A — authoritative UI implementation complete.** The bare-metal Disk Manager now presents the normalized storage model, bounded partition and unallocated lists, safe selection across rescans, selected-object Properties, diagnostics, and the existing DM3 initializer. DM5 adds no storage write operation.

## 1. Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `e6c15ec165b14f8de17af8f5bbddf8eb434500f6`
- Initial worktree: clean
- Initial upstream state: one local commit ahead, zero behind
- Prior phases DM1 through DM4 and `DISKMANAGER_IMPLEMENTATION.md` were read before editing.

## 2. Previous UI problems

The legacy action grid mixed detection with filesystem-driver switching and displayed disabled format/create controls. The hosted viewer inserted a fake “host physical disks” row, called image files USB disks, suggested mount paths without VFS evidence, and colored every gap as unallocated. The bare-metal UI also reduced the normalized parser to four partition rows and did not expose the storage diagnostics available from DM2–DM4.

## 3. New information architecture

The resizable bare-metal window has a device list on the left and a selected-device workspace on the right. The workspace contains authoritative state and geometry, a scrollable partition/unallocated list, a proportional disk map, selected-object Properties or Storage diagnostics, and a responsive footer for Refresh, Properties, Diagnostics, and the contextual Initialize Disk action. Existing kernel window, framebuffer, widget, and input APIs are used.

## 4. Authoritative state model

Presentation reads `PartitionTableModel`, `DeviceCapabilities`, `TargetIdentity`, `BootSafety`, `MountSafety`, and the structured initialization result/status. DM5 does not implement UI-side boot, mount, write-eligibility, validity, persistence, or initialization policy. The common DM3 safety pipeline remains authoritative for writes.

## 5. Disk-list behavior

The bare-metal list enumerates up to the block registry limit of 16 devices and shows each device name with a human-readable parser state. It shows no synthetic device when the registry is empty. The selected-device summary and Properties include capacity, logical sector size, writable state, model/serial when present, transport, boot provenance, root/mount relationship, partition style/count, media removability when known, persistence, and flush support. Unknown fields remain unknown.

## 6. Partition-list behavior

The normalized parser’s one-based on-disk partition number is retained while rows are sorted by physical LBA. The UI supports the parser’s bounded maximum of 128 partitions. The table reports the total partition and unallocated counts and a visible row range; mouse wheel, Up/Down, and PageUp/PageDown move through the merged list. GPT names are shown when decoded by the parser. Filesystem probes are labeled as signatures; filesystem used/free space remains `Unknown`.

## 7. Unallocated-region model

`disk_manager_model.h` computes read-only gaps only from structurally valid MBR/GPT models. MBR begins at LBA 1, leaving the partition-table sector reserved. GPT is clipped to first/last usable LBAs, excluding GPT metadata. Gaps carry inclusive start/end LBAs, sector count, checked byte capacity, 1 MiB alignment information, and usable-range membership. Invalid, unsupported, hybrid, extended-MBR, out-of-range, overlapping, capacity-overflow, and ambiguous GPT-copy states fail closed. A degraded GPT with one validated copy can be presented from that copy; two valid but disagreeing GPT copies do not produce free-space claims. No gaps authorize writes.

## 8. Selection identity model

Disk selection follows a non-reused registration incarnation and matching device identity/geometry while allowing the global registry generation to change because of unrelated devices. GPT partition selection follows a nonzero unique GUID. MBR selection follows bounds and type. Unallocated selection follows exact start/end bounds. If an identity no longer exists after refresh, the selected child object is cleared; a removed disk clears disk selection.

## 9. Graphical disk map

The map scales validated partition and unallocated LBA ranges across the full device capacity. Adjacent segment boundaries use scaled endpoints so rounding stays consistent. GPT reserved areas remain dark and outside the unallocated list. Map clicks select a partition or validated unallocated extent; areas without a selectable validated range report that fact. Raw and invalid layouts are visibly unavailable for normal partition mapping.

## 10. Contextual actions

The old Detect media, Set FS, disabled Format, and disabled Create Partition controls were removed. The bare-metal footer provides Refresh, Properties, and Diagnostics. Initialize Disk appears only for a selected raw/not-initialized disk; eligibility is still decided by the storage service. Partition and unallocated selection exposes details without adding mount, unmount, format, delete, or create actions.

## 11. Properties views

Disk Properties show available device identity, model/serial, transport, capacity, logical and known physical geometry, partition style/count, access, boot/root/mount state, persistence, flush status, and initialization status/blocker. Partition Properties show on-disk number, scheme, inclusive LBA bounds, sectors, checked capacity, filesystem signature, GPT name/type/unique GUID/attributes, or MBR type and active flag. The MBR active flag is explicitly not boot provenance. Unallocated Properties show bounds, size, 1 MiB alignment, and usable-range membership.

## 12. Diagnostics

The Diagnostics view shows block and driver indices, registry generation and target registration identity, transport, sector geometry/capacity, read/write access, persistence and flush semantics, boot provenance, root/mount state and path, parser state/error, GPT primary/backup health and agreement, protective MBR state, MBR extended/logical detection, and initialization eligibility with the structured blocker. PCI segment/BDF, ATA channel/target, and NVMe namespace are included when available. No raw pointers are exposed.

## 13. GPT degraded-state UX

The disk list distinguishes online MBR/GPT, raw, unreadable, invalid, unsupported, and GPT degraded states. Degraded GPT properties distinguish primary-valid/backup-invalid from primary-invalid/backup-valid. If both valid copies disagree, diagnostics say they disagree and the gap model withholds unallocated claims. DM5 adds no GPT repair action.

## 14. Refresh/rescan behavior

Refresh re-enumerates block devices and recomputes capabilities, parser state, protection, mount state, initialization eligibility, partitions, and gaps. It preserves disk and child selections by the identity rules above and clears stale selections. F5 performs the same refresh.

## 15. Hosted-mode behavior

The Windows viewer lists attached `.img` files as `Image: <name>` with `Host image` transport. It does not enumerate physical host disks. Its preview is limited to 512-byte MBR images: it checks the signature, entry flags, capacity bounds, and overlap; GPT/protective MBR is called unsupported; no MBR signature is reported as raw-state-unknown. The hosted map draws validated MBR partitions only and never infers the remaining bytes as unallocated. Image attachment remains read-only. The hosted viewer does not claim bare-metal parser parity and retains its current fixed 920×560 window protocol.

## 16. Keyboard/accessibility improvements

F5 refreshes; Tab moves focus among device, list, action, and details panes; Up/Down and PageUp/PageDown navigate lists or details; Home/End move to list bounds; Enter/Space activates focused actions or advances initializer dialogs; Left/Right choose the initializer scheme; Escape cancels dialogs and clears child selection. Mouse wheel scrolls bounded lists/details.

## 17. Many-partition handling

All 128 bounded parser entries are retained by the UI model. The list displays a page-sized range and total count rather than silently dropping rows. The same windowing model covers zero, one, four, sixteen, 32-plus, and maximum-count entries. Device enumeration covers the block registry’s 16 slots and an empty registry produces an empty state.

## 18. Testing

`scripts/run-storage-manager-tests.ps1` passed **188 checks, 0 failures**. DM5 coverage includes raw/no-gap behavior, empty and populated MBR/GPT gaps, GPT usable-bound clipping, 4Kn gap capacity/alignment, one-copy degraded GPT, conflicting GPT copies, overlaps, bounded output, state and boot vocabulary, device-level mount wording, visible ranges for empty/large lists, disk incarnation refresh, slot reuse, GPT GUID selection, MBR bounds/type selection, active-flag changes, unallocated-bound selection, and contextual action availability with Initialize Disk as the only write action. Existing DM2–DM4 fake-device safety, boot provenance, initialization rejection/rollback, persistence, and RAM-disk checks remain in the same suite. Tests use in-memory fake media; no host raw disks are opened.

The hosted `disk_manager.cpp` translation unit passed a MinGW C++17 syntax-only check using the repository's Windows thread/include settings. It emitted one pre-existing `-Wmisleading-indentation` warning in `gui_protocol.h:96`.

## 19. Visual/runtime proof

No visual or runtime smoke test was performed. QEMU and MSVC `cl` are unavailable. The normal `cmd /c build.bat` stopped before C++ compilation at the required Mbed TLS profile check because `third_party/mbedtls` is missing. The available MinGW compiler is a Windows-target compiler; a syntax-only attempt against bare-metal `kernel_apps.cpp` selected incompatible Windows/host headers and is not a valid kernel build. This matches the cross-target limitation recorded in DM4. No UI screenshot or runtime result is claimed.

## 20. Remaining limitations

- VFS mounts identify a block device, not a partition identity. Disk Properties therefore show conservative device-level mount paths and partition Properties say the mapping is unknown.
- The hosted `.img` viewer only previews simple validated MBR layouts; it does not parse GPT or enumerate host physical drives.
- Filesystem detection is signature-level, not a mount operation. Labels and used/free values remain unknown where the storage/VFS layer cannot prove them.
- Physical sector size is not exposed by the current capability model and is shown as unknown.
- The hosted viewer uses the existing fixed-size window protocol; only the bare-metal Disk Manager adapts its layout to a resizable window.
- Bare-metal visual layout has not been runtime-verified because the build/runtime tools are unavailable.

## 21. Recommended DM6 scope

Design one Create Partition request through the existing storage operation lease and exact target revalidation. Restrict candidates to a validated unallocated extent; define GPT/MBR type/name inputs, 1 MiB alignment, table-specific reserved ranges, and checked sector arithmetic; stage metadata snapshots, durable writes, parser read-back, rollback, and explicit uncertain-state reporting. Add fake-media tests before exposing a Create action. Preserve the DM5 rule that UI presentation never makes the safety decision.
