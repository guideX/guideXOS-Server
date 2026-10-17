# guideXOS Server — FONT9: Persistent Font Preferences & Storage Reliability

**Outcome: A — Accepted on the supported writable FAT32 block-storage path.**

## 1. Root-cause analysis

FONT8 staged a serialized preference in `/fontsel.tmp` and then used rename to publish it at `/fontsel0.cfg` or `/fontsel1.cfg`. The observed folder-backed QEMU run created and wrote the scratch file, then emitted `Error handling renames (-2)` when the destination slot already existed. The same serial trace records `LFPASTE_RENAME_BEGIN old=/fontsel.tmp new=/fontsel0.cfg`.

This was a combination of a preference-service state-machine assumption and backend behavior. The service assumed rename would replace an existing destination. The shared FAT rename implementation explicitly rejects an existing destination, and VFS maps a failed FAT rename to `VFS_ERR_NOT_SUPPORTED`. QEMU's VVFAT host reported the rename error. The guest's precise VFS return for that host-side VVFAT failure was not captured, so the host error is not presented as a guest status. There is no evidence of a general VFS defect, and FONT9 changes no shared VFS or FAT code.

FONT9 removes rename from preference publication. It uses existing VFS `write_file` behavior for the inactive slot. The supported raw FAT32 block path completed repeated overwrites and restarts. VVFAT direct overwrite remains unqualified; details are in §11.

## 2. VFS and FAT behavior

`vfs::write_file` first calls FAT `overwrite_path_status`; if the path is absent it falls back to `create_file_path_status`. For an existing FAT file, `overwrite_path_status` writes the file's existing cluster chain, updates the directory entry's size, writes that directory sector, and flushes the volume. It returns an error when the chain is too short or block I/O/flush fails. This is in-place replacement, not an atomic transaction.

`fs_fat::rename_path` returns false when the destination path already exists. VFS maps a false result to `VFS_ERR_NOT_SUPPORTED`. FAT has no journal or general atomic-replace primitive here. FONT9 therefore writes only the inactive slot while preserving the currently selected slot. The strategy does not assume rename replacement or emulate an atomic filesystem operation.

## 3. Preference publication protocol

The on-disk record remains the FONT6 v1 fixed-width 1,024-byte text record. For each Apply or persisted reset, the service:

1. Serializes the candidate with the next nonzero sequence value.
2. Writes `/fontsel.tmp`; if a stale undersized scratch record exhausts its allocation, it removes that fixed scratch path and retries creation once.
3. Stats, reads, byte-compares, parses, and checks identity, size, and sequence in the scratch record.
4. Writes the same bytes directly to the inactive preference slot. If an undersized inactive slot cannot be overwritten, it removes only that inactive slot and creates it anew while the selected slot remains intact.
5. Reads and verifies the slot. Only after verification succeeds does it update the in-memory configured/active state and best-effort remove the scratch file.

A save never reports success unless the slot write and readback verification succeed. Rename is not called. Startup reads both fixed slots, rejects malformed/checksum-invalid/unsupported records, and chooses the newest valid record with serial-number arithmetic. Sequence zero is skipped, including `UINT64_MAX → 1` wraparound. Bitmap is always the final active fallback.

## 4. Crash and interruption recovery boundaries

The guarantee is: **while one selected valid slot exists, an ordinary failed or interrupted update does not deliberately modify that slot.** Scratch is never authoritative. A partial scratch write is discarded at startup. A partial inactive-slot write is rejected by record validation and the untouched selected slot remains available. The bitmap fallback remains usable when neither record is valid or the selected external font cannot be loaded.

The implementation does not guarantee recovery from arbitrary power loss during FAT cluster, directory-sector, device-cache, or flush updates. FAT is non-journaled. Checksums detect accidental corruption but are not authentication and cannot rule out collisions. A transient readback failure may be ambiguous if best-effort removal also fails: the save returns failure, the prior selected record stays untouched, but a fully valid newer record could still be selected after reboot. If cleanup fails, only the fixed scratch or inactive path is involved; paths and file count remain bounded. No prior preference can be preserved if there was no valid slot before the operation.

## 5. Slot rotation and startup reconciliation

There are exactly two authoritative paths: `/fontsel0.cfg` and `/fontsel1.cfg`. The writer targets the slot other than the slot selected at startup/current commit. It never intentionally deletes or overwrites the selected slot. Short/corrupt inactive data can be reclaimed only after the selected slot has been identified. Readback failure triggers best-effort removal of the candidate slot; startup independently validates both slots and does not accept a newer corrupt record over an older valid one.

Startup removes only `/fontsel.tmp` best-effort. It does not delete a preference slot during reconciliation. Missing slots, missing preference directory, unsupported versions, corrupt records, and missing external fonts resolve deterministically to bitmap active state. A missing external font's valid preference can remain configured for diagnosis while bitmap is active, preserving the existing FONT6 contract.

## 6. Failure-injection coverage

The updated `tests/system_font_selection_test.cpp` suite covers: no valid records/bitmap default; one and two valid records; newer checksum-corrupt data; unsupported record versions; stale/orphan scratch cleanup; partial inactive-slot writes; target readback failure; scratch and target no-space conditions; short-slot recreation and failed deletion; failed rename injection with zero rename calls; failed selection rollback; repeated slot reuse; sequence rollover; case-sensitive filenames; same-family fonts with distinct paths/identities; missing font fallback; and preview isolation. Existing generation and cache-generation checks remain in the FONT6 regression suite.

The runtime test also applies 24 successive persisted size changes, reconstructs the service, and confirms that the final configured identity and size remain persisted. The QEMU run separately uses actual Settings Apply operations and reuses both slots.

## 7. Memory and storage bounds

FONT6's measured selection-service static BSS remains approximately 223 KiB: 192 KiB file staging, 16 KiB candidate glyph probe storage, 8 KiB directory names, 4 KiB preference buffers, and about 3 KiB of records/state. FONT9 adds no static buffer, heap allocation, container, VFS/FAT resource, or hosted runtime dependency; it reuses existing serialized/text buffers.

Each preference or scratch record is exactly 1,024 logical bytes. At most two authoritative records plus the one fixed scratch record exist: maximum 3,072 logical file bytes across at most three paths, plus FAT directory/allocation overhead rounded to the volume's cluster size. Successful saves remove scratch best-effort, leaving two records. Repeated saves overwrite the same three fixed paths and cannot accumulate temporary files.

## 8. FONT6 preference compatibility

FONT9 preserves `format=1`, the fixed 1,024-byte record, identity fields, checksum, and existing preference paths. No migration is needed. Existing uppercase/lowercase display names on FAT are handled through the mounted VFS path lookup. Unsupported versions are rejected rather than reinterpreted. Sequence rollover is now handled by serial-number arithmetic with zero skipped.

## 9. Regression and Settings results

All requested regression groups passed:

| Suite | Result |
|---|---:|
| FONT2 shared font contract | 196/196 checks |
| FONT3 TrueType loading and validation | 1,150/1,150 |
| FONT4 rasterization | 4,454/4,454 |
| FONT5 glyph cache and desktop rendering | 228/228 |
| FONT6 system-font selection (updated) | 376 checks, 0 failures |
| FONT8 native desktop adoption | 46/46 |
| Settings presentation core | 2,651/2,651 |
| Settings Center | 89/89 |
| Settings users | 32/32 |
| Settings device/storage | 45/45 |
| Settings Default Apps | 39 checks plus 9 related checks; 100 lifecycle cycles |
| Settings S6 / S7 | 42/42; 58/58 |
| Native Settings runtime | 828 checks; 100 lifecycle cycles, 250 refreshes, 1,000 transitions |

Native presentation validation and the Settings freestanding build passed. FONT9 changes no Settings UI code; the 560×360 minimum-window behavior and existing layout rules are unchanged. Failed selection leaves active/configured state unchanged in the injected failure cases.

A separate pre-existing native ABI layout discrepancy remains outside FONT9: `tests/native_abi_layout_test.cpp` expects `sizeof(gx_host_calls) == 248`, while the observed layout is 256. No ABI was changed to silence it. This is separate from the successful kernel build and the font/Settings regressions above.

## 10. Full freestanding AMD64 build

`mingw32-make ARCH=amd64 -B` forced a full AMD64 kernel build and link. It completed with `Built: build/amd64/bin/kernel.elf`. The modified production translation unit was compiled for the kernel with `-ffreestanding -fno-exceptions -fno-rtti -nostdlib -nostdinc++ -fno-builtin`; no hosted runtime was introduced. Output sizes were 2,808,008 bytes for `kernel.elf` and 2,929,124 bytes for `kernel.pe`.

The complete build log is [`../evidence/font9/kernel-build-forced.log`](../evidence/font9/kernel-build-forced.log). The unrelated ABI assertion above is not a build failure.

## 11. QEMU backend comparison

**Writable FAT32 block image — qualified.** A private 512 MiB raw FAT32 image and private OVMF copy were used with networking disabled, 1 GiB guest RAM, and a fresh QEMU process for each run. Processes reused the same image only after guest shutdown and process exit. Actual Settings successfully changed fonts/sizes enough to reuse both slots. Serial logs show 24 slot publications in guest 1, including 23 successful overwrites of existing slot files; each overwrite reports 1,024 bytes, `FAT_FILE_WRITE_OK`, block status 0, and `VFS_OK`. No preference rename was issued.

**Folder-backed FAT — read/boot only.** A separate `fat:rw:` folder-backed probe mounted FAT32, initialized FONT6 successfully, and opened both preference slots. The QEMU input path did not reliably deliver clicks to the desktop, so no Settings Apply or slot overwrite was attempted. The probe ended through its own QMP `quit`; it was not a graceful guest shutdown or a persistence test. Thus FONT9 does not claim QEMU VVFAT direct-overwrite qualification. This is a test-access limitation; production uses ordinary VFS writes and does not depend on VVFAT-only behavior.

The earlier FONT8 VVFAT rename failure and exact observed operation are documented in [`../evidence/font9/folder-backend/README.md`](../evidence/font9/folder-backend/README.md). Folder probe serial and launch evidence are adjacent. No other project's guest was attached to, stopped, or modified. Sanitized private/foreign process census details are in [`../evidence/font9/qemu-census-sanitized.json`](../evidence/font9/qemu-census-sanitized.json).

## 12. Restart and slot-reuse proof

Guest 1 used the actual Settings Fonts page and performed 24 successful persisted changes, including TrueType/bitmap switching and size changes. The serial log shows 23 existing-slot overwrites alternating between the two slot paths (the remaining publication created the first slot in this run). Before restart, offline FAT inspection found sequence 25 Roboto Bold 18 px in slot 0 and sequence 26 Bitmap 7 px in slot 1, with no scratch file. Guest 1 shut down through the guest shell and exited before the image was inspected or reused.

Guest 2 was a fresh QEMU process on that same disk. It opened both records, initialized the selection service successfully, and the actual Settings Fonts page restored the bitmap selection. Settings then applied Roboto Bold 17 px; serial records a successful overwrite of slot 0. After guest shutdown, offline inspection found sequence 27 in slot 0 and sequence 26 in slot 1, with no scratch. The slot hashes and records are in [`preferences-after-replacement.json`](../evidence/font9/block-storage/preferences-after-replacement.json).

Guest 3 was another fresh process and restored the sequence-27 TrueType preference in the actual Fonts page. After guest shutdown, a controlled 49-byte incomplete `/fontsel.tmp` record was injected while slot hashes remained unchanged. Guest 4 then booted fresh, selected the same sequence-27 TrueType preference, opened the actual Fonts page, and removed the orphan scratch record at initialization. Offline inspection confirmed the two slot hashes were unchanged and `/fontsel.tmp` was absent. The scratch injection and recovery snapshots are in [`incomplete-scratch-injection.json`](../evidence/font9/block-storage/incomplete-scratch-injection.json) and [`preferences-after-scratch-recovery.json`](../evidence/font9/block-storage/preferences-after-scratch-recovery.json).

All four complete serial logs, exact launch manifests, snapshots, and Settings captures are under [`../evidence/font9/block-storage`](../evidence/font9/block-storage). The private raw disk image and full VNC/PPM working captures remain in the pre-existing untracked `out/font9-validation/` tree; they were preserved and are not part of the commit.

## 13. Known limitations

- VVFAT folder-backed direct overwrite was not exercised due to unreliable QEMU input delivery. The original failure was specifically the rename-to-existing-slot path, which FONT9 no longer uses.
- Ordinary FAT writes and flushes are not power-loss atomic. The guarantee protects an independent selected slot from an attempted update; it does not promise complete recovery from arbitrary device-cache or directory-sector loss.
- A readback failure combined with failure to remove the target can leave a newer valid candidate on disk even though Apply reported failure. The previous selected slot remains untouched, and startup still validates checksums and identity before choosing either record.
- FAT cluster allocation and metadata consume more than the 3,072 logical record bytes; total is bounded by three fixed records and the volume's cluster/allocation geometry.
- The ABI size assertion discrepancy described in §9 remains out of scope.

## 14. FONT10 recommendations

Keep FONT10 out of the persistence protocol. If additional backends must be certified, add a backend-level direct-overwrite test that exercises the actual VFS result code without depending on desktop pointer injection. For any future stronger crash guarantee, first define a VFS/FAT durability primitive and test its block ordering and flush semantics; do not infer journaling or atomicity from checksums, sequence numbers, dual slots, or QEMU emulation. Any change to record format should use an explicit versioned migration while preserving the current bitmap recovery path.
