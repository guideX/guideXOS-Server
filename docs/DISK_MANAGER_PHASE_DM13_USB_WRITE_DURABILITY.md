# Disk Manager Phase DM13 — USB Write Durability and Destructive Eligibility

## Outcome

**Outcome A.** USB Mass Storage writes are enabled through the shared block path after private production-path QEMU proof. On QEMU, WRITE(10), Sync Cache, exact read-back, bounded multi-sector writes, VFS persistence, the normal destructive-operation preflight, and the full Initialize/Create/Format/VFS/restart lifecycle all passed. The independent verifier passed on each counted lifecycle image.

The final repeatability cohort passed **3/3 fresh blank 68 MiB images** after an earlier WHPX CSW timeout. Including the earlier 130 MiB pass, the focused series completed four lifecycle passes and one failed attempt. The failed attempt’s raw image was independently confirmed blank; it did not become a pass. QEMU hot-unplug/reinsert and physical USB media were not tested.

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `dffd55c6a8584f20d6fac8938b899f05977a1a90` (DM12)
- Tracked worktree was clean at start; existing untracked `out/` evidence was preserved.
- Cached `origin/DISK_MANAGER_IMPROVEMENTS` was equal to the starting HEAD (ahead/behind `0/0`). Normal fetch failed with `git@github.com: Permission denied (publickey)`, so remote freshness is unknown.
- Reports through DM12 and `docs/DISKMANAGER_IMPLEMENTATION.md` were reviewed, along with the production UHCI, BOT/SCSI, block persistence, boot provenance, lifetime, and QEMU paths.

## WRITE(10)/(16), bounds, and BOT data-out

WRITE(10) uses a 10-byte CDB, opcode `0x2A`, a big-endian 32-bit LBA, and a big-endian 16-bit block count. WRITE(16) uses a 16-byte CDB, opcode `0x8A`, a big-endian 64-bit LBA, and a big-endian 32-bit block count. The implementation selects the command only when the complete range is representable. Encoding is covered deterministically; the available QEMU images are smaller than the 32-bit LBA boundary, so no real high-LBA WRITE(16) claim is made.

The transport rejects zero counts, unsupported block sizes, end-of-device crossings, range overflow, command-field overflow, and excessive transfer-byte requests before CBW submission. Transfers are split into BOT data phases no larger than 64 KiB. The host-to-device CBW uses direction `0x00`, the Bulk OUT endpoint and exact transfer length. The data-out path follows endpoint toggle and packet progression; the Bulk IN CSW must have the expected signature and tag, legal status, zero residue, and a complete data phase before a write is reported successful. Failures issue REQUEST SENSE with stale sense validity cleared.

The private production path passed before the shared callback was enabled. QEMU then passed 1-, 2-, 7-, and 128-sector writes, with adjacent-sector canaries and exact restoration. The 128-sector case covers the 64 KiB BOT boundary and multiple TDs. A dedicated 100-cycle stress passed deterministic write → Sync Cache → read-back → restore cycles, followed by the 2-, 7-, and 128-sector cases and a common-block-callback write/flush/read-back/restore. The 256 MiB disposable image SHA-256 was identical before and after: `A6D72AC7690F53BE6AE46BA88506BD97302A093F7108472BD9EFC3CEFDA06484`.

Evidence:

- Initial private write proof with common callback absent: `out/dm13-qemu-usb-write-pass-01/`
- Shared callback proof: `out/dm13-qemu-usb-write-final/`
- Dedicated 100-cycle proof and host hash check: `out/dm13-qemu-usb-write-stress-100/`
- Disposable raw image: `out/dm13-qemu-usb-private-boundary.raw`

## Write protection, Sync Cache, and persistence

QEMU’s `readonly=on` USB mode reported write protection through the SCSI path. Production reads succeeded; private writes returned read-only, the common write callback was absent, common writes were rejected, and the disposable image hash remained unchanged. Evidence: `out/dm13-qemu-usb-readonly-proof/`.

The implementation issues SYNCHRONIZE CACHE(10) as a no-data BOT command and requires a validated CSW. The command covers the whole device, so a ranged SYNCHRONIZE CACHE(16) is not needed by this path. REQUEST SENSE distinguishes illegal request/invalid command from other failures.

Persistence states are reported per registration:

- **TrustedFlush:** a valid Sync Cache completion was observed.
- **UnsupportedFlush:** the device reported an illegal/unsupported command.
- **FailedFlush:** a supported attempt failed.
- **Unknown:** behavior has not been established.

Only TrustedFlush satisfies the destructive persistence gate. A runtime failed/unsupported cache command removes trusted persistence from capability reporting; preflight remains blocked until a later successful Sync Cache restores it. A completed write followed by failed or unsupported synchronization reports `BLOCK_ERR_DURABILITY_UNVERIFIED`. USB FAT32 mounts remain read-only when trusted flush semantics are unavailable, even though a technically correct block write callback may be exposed. No USB-specific destructive-operation exception was added.

The shared callback is registered for a non-write-protected device only after the private QEMU write proof had passed during implementation. Flush support is registered separately and only after trusted Sync Cache. Fake tests cover Sync Cache success, failure, unsupported response, timeout, disconnect, stale sense, recovery after a later successful flush, and the resulting destructive eligibility state.

## Ambiguity, disconnect, and identity lifetime

The block layer distinguishes a write not submitted, a submitted write with unknown completion, a completed write awaiting durability, a definitive failure, removal before submission, removal with unknown completion, and partial/uncertain progress. It never reports a missing or invalid CSW as success and does not blindly retry an ambiguous write. Disconnect during data-out, before CSW, or during Sync Cache marks the exact registration offline; a pending write followed by removal reports durability unverified. Registration IDs prevent a stale callback/view from targeting a replacement in the same registry slot.

Deterministic fake transport tests inject removal before CBW, during data-out, before CSW, and during Sync Cache. They also cover stale views/mounts, replacement registration, and reinsertion with a fresh incarnation. QEMU hot-unplug/reinsert during active I/O was not run, so the hardware timing path remains a DM14 proof item.

## USB boot provenance

BootInfo supplies the validated UEFI loader-source Device Path and resolved PCI segment/BDF. The UHCI controller BDF is carried through USB enumeration and registration together with the root port, interface, VID/PID, and LUN. The USB matcher requires a structurally valid direct USB path and hard-drive media node; optional class and LUN identities are checked. A different controller BDF or direct root-port/interface/class/LUN endpoint is authoritative evidence for `DefinitelyNotBoot`; a matching path is protected as boot backing.

VID/PID alone is never sufficient. USB WWID paths that depend on a USB serial string remain `Unknown` because the USB core does not retain the descriptor; hub paths remain unknown because hub topology is unsupported. Fake tests cover matching USB boot identity, definitely non-boot identity, and insufficient identity. In QEMU, firmware reports its boot source at PCI `00:01.1` / ATA while the USB disk is behind UHCI at PCI `00:03.0`; USB therefore becomes definitely non-boot without a QEMU-specific exception.

The ordinary QEMU destructive preflight printed `Ready for confirmation`, `issues=0x00000000`, writable, and trusted persistence before Initialize. The production pipeline—not a proof-only bypass—performed Initialize GPT, Create Partition, and Format FAT32.

## Full USB lifecycle and independent verification

Every counted lifecycle run attached only a fresh repository-owned raw file as writable USB storage; each image passed an all-zero host-side precheck. No physical host disk was passed to QEMU.

| Evidence | Fresh image | Result |
| --- | ---: | --- |
| `out/dm13-qemu-usb-lifecycle-run-05/` | 130 MiB | Full lifecycle, cold restart, and corrected independent verifier passed. The first verifier invocation had an expected-label padding typo; the verifier was corrected and the same image then passed. |
| `out/dm13-qemu-usb-lifecycle-run-06/` | 68 MiB | Initialize reported metadata write failure after a Bulk IN CSW timeout. This is a failed attempt, not a pass; `out/dm13-qemu-usb-repeat-04-68m.raw` was independently confirmed all-zero afterward. |
| `out/dm13-qemu-usb-lifecycle-run-07/` | 68 MiB | Full lifecycle, 10 VFS mount/write/read/unmount cycles, cold restart, and independent verification passed. |
| `out/dm13-qemu-usb-lifecycle-run-08/` | 68 MiB | 100-cycle BOT stress, full lifecycle, exact GPT checks after Format and VFS writes, cold restart, and independent verification passed. |
| `out/dm13-qemu-usb-lifecycle-run-09/` | 68 MiB | Full lifecycle, exact GPT checks after Format and VFS writes, cold restart, and independent verification passed. |

Runs 07–09 are three consecutive fresh-image passes after run06. Each invoked normal preflight, Initialize GPT, Create Partition, FAT32 Format, mount, deterministic file write/read, unmount, remount/read, and a second QEMU boot that rediscovered and read the same file. The repeated VFS loop completed 10 mount/write/read/unmount cycles per image. Exact primary and backup GPT bytes were compared before and after Format; runs 08 and 09 also compared them around all VFS writes. No GPT metadata changed during formatting or file operations.

The independent Python verifier does not call the production parser or writer. It verifies the all-zero source image before boot, image size, protective MBR, primary and backup GPT CRCs/copy agreement, partition bounds and gaps, FAT32 BPB geometry, FSInfo and backup boot sector, FAT mirrors, root/directory/file entries, exact 39-byte payload, and zeroed unallocated data outside the expected file.

## Regressions, tests, and builds

- Storage suite: **497 checks, 0 failures**, including **96 USB checks**; DM12 baseline was 474 total / 79 USB.
- QEMU USB low-level proof: private write before callback enablement; shared callback proof; exact read-back; canaries; 100 Sync Cache cycles; image hash restored.
- QEMU USB write-protect proof: read works; private/common writes blocked; image hash unchanged.
- Full USB lifecycle: **3/3 fresh images after the run06 timeout**, plus the earlier corrected run05 pass.
- DM12 read-only regression: `out/dm13-dm12-readonly-regression/` passed registration, GPT parse, read-only FAT32 mount, deterministic file read, and unmount. The 600 MiB reference image hash remained `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE`.
- ATA regression: **5/5 fresh 600 MiB images** passed full Initialize/Create/Format/VFS write/read/unmount/remount/restart and independent GPT/FAT32 inspection in `out/dm13-ata-regression-01/` through `out/dm13-ata-regression-05/`.
- Final AMD64 kernel build passed with proof-only flags disabled: `out/dm13-final-kernel-build.log`.
- Final UEFI x64 Release rebuild passed: `out/dm13-final-uefi-build.log`.
- `git diff --check`, Python verifier syntax, and PowerShell runner parsing passed.

## Remaining limits and DM14

- Real QEMU hot-unplug/reinsert was not exercised. The deterministic disconnect stages and registration lifetime tests passed; real removal while idle, mounted, with open handles, during data-out, and during Sync Cache remains unverified.
- Real WRITE(16) above the 32-bit LBA range was impractical on available images; CDB encoding and limits are tested.
- USB WWID/serial boot paths and USB hub topology remain `Unknown`.
- No physical USB device or host physical disk was accessed.
- Partition mounting remains 512-byte-only; FAT32 formatter/rollback retains its existing approximately 3.66 GiB bound. NVMe Flush and AHCI shared storage are not implemented.

DM14 should prove QEMU hot-unplug/reinsert with the disk idle, mounted, open handles, during/after data-out, and around Sync Cache, then verify old handles/views stay stale and a replacement registration is independent. This is the main remaining USB adoption risk after DM13.