# Disk Manager: Current Implementation Status

This describes the state after DM3. Historical findings and phase details are in the [DM1 audit](DISK_MANAGER_PHASE_DM1_AUDIT.md), [DM2 safety foundation report](DISK_MANAGER_PHASE_DM2_SAFETY_FOUNDATION.md), and [DM3 initialization report](DISK_MANAGER_PHASE_DM3_INITIALIZE_DISK.md).

## Current behavior

There are two Disk Manager presentations:

- The bare-metal app in `kernel/core/kernel_apps.cpp` enumerates the block registry, captures target identities, displays the bounded normalized MBR/GPT parser model, and offers **Initialize Disk...** for raw disks. It offers GPT as the default/recommended choice and MBR as a compatibility choice. Confirmation is tied to the captured target identity. The reusable storage layer owns safety checks and writes; the UI displays stages and the authoritative parser result.
- The hosted Windows path in `disk_manager.cpp` does not enumerate physical host disks. It displays attached `.img` files for inspection with its legacy four-slot MBR viewer. The image view is read-only.

Disk Manager initialization creates an empty partition table only. It does not create partitions, format, repair, wipe, securely erase, or mount a device. Suggested mount points and active MBR flags do not imply mounted or boot state.

## Initialization service and safety boundary

`kernel/core/disk_initialization.cpp` provides one explicit storage operation lease from preflight and exact metadata-sector snapshot through confirmation, revalidation, writes, flush, parser verification, rescan, and release. It rejects competing and nested acquisitions and stale owner tokens. Preparation and execution independently validate the target; execution also compares the captured sector snapshot and revalidates identity before each sector write.

Only a parser-confirmed `Not Initialized` target with clear metadata regions can proceed. The validator requires valid geometry, reads, writes, trusted persistence semantics, unmounted/non-root status, explicit `DefinitelyNotBoot` provenance, current registry generation, and layout capacity. There is no force-initialize option. After writes, the normal DM2 parser must report the requested valid empty scheme with the expected disk signature/GUID and valid GPT redundancy/CRCs. Failures attempt bounded sector restoration when the target identity and persistence model still support it, then report rollback and final-state uncertainty explicitly.

GPT uses 128 × 128-byte entries, 16 KiB arrays, 1 MiB first-usable alignment, primary and backup headers/arrays, and a protective MBR written last. MBR writes a zeroed full logical sector with a nonzero generated signature and empty partition entries. Both paths support 512-byte and 4096-byte logical sectors in the storage/parser test model.

## Boot identity and current transport eligibility

UEFI obtains the loader source through `EFI_LOADED_IMAGE_PROTOCOL::DeviceHandle`, but the canonical BootInfo handoff does not carry that block identity. ATA PIO does not expose a matching controller path, and NVMe namespace discovery is not tied to the UEFI source. The kernel therefore marks ATA and NVMe boot provenance `Unknown`; initialization refuses them even if the mounted root filesystem is elsewhere.

Persistence is also required. ATA PIO registers a flush callback only when IDENTIFY data advertises a supported cache flush and retains known flush semantics, but it remains blocked by unknown boot provenance. NVMe remains blocked because it has no Flush command/capability and boot provenance is unknown. RAM disks are explicitly volatile and are not durable targets. AHCI and USB mass storage have no active shared block registration. The hosted viewer does not access host physical disks. **No current real registered transport is eligible for initialization.**

The block descriptor has explicit boot-provenance states. Fake tests can identify a device as boot-backed, definitely non-boot, or unknown. Only definitely non-boot targets may continue. Root/mount protection remains an independent whole-device check.

## Persistence and geometry

The block layer reports successful supported flush, explicitly synchronous-durable completion, unsupported/unknown flush, and failure separately. Initialization accepts only a known successful durable outcome and flushes before and after metadata writes. A RAM-disk no-cache flush does not make volatile memory durable. Existing VFS filesystem drivers remain 512-byte-only; initialization and filesystem mounting are separate capabilities.

The partition parser and initializer use the registered logical sector size and support 512-byte and 4096-byte sectors. They reject invalid geometry and checked-range/capacity overflow. The test writer confirms GPT and MBR initialization for both sizes; this does not imply all current transports support 4Kn initialization.

## Verification and build status

The deterministic fake-device suite passed **114 checks, 0 failures**. It exercises MBR/GPT metadata, both GPT copies and CRCs, empty entries, protective MBR, 512-byte/4096-byte geometry, raw-only restrictions, mounted/root/boot/persistence policy, registry and identity changes, lock contention and cleanup, write/flush/read-back failures, rollback, and final parser state. It also verifies that modified confirmation plans are rejected before writes. A separate in-memory byte verifier checks on-disk metadata independently of the parser. The harness registers only fake in-memory devices and never opens physical disks. No disk-image fixture or hardware write was performed.

Targeted C++14 bare-metal syntax-only compilation passed for the modified Disk Manager, storage, parser, block, ATA, NVMe, and RAM-disk translation units. It reported existing warnings in kernel-app base methods and unrelated UI code. `cmd /c build.bat` stopped before C++ compilation because `third_party/mbedtls` is absent; the tracked bootstrap path is `scripts/bootstrap-mbedtls.ps1`. No dependency policy change or bootstrap was performed.

## Phase outcome and next work

DM3 is **Outcome B**. The writer and safety service are implemented and fake-tested, but no real transport currently satisfies both authoritative non-boot identity and trusted persistence. The storage lease serializes initialization operations; the block registry has no shared hot-unplug mutation lock, and there is no active shared hot-unregister path today. Registry mutation must coordinate with storage leases before enabling hotplug writes.

Recommended DM4 work: propagate a versioned, authoritative UEFI boot-source identity into kernel storage discovery; retain matching ATA/NVMe controller/namespace identities; coordinate registry mutation with an active storage lease; and add a testable transport-flush seam. Keep partition creation, formatting, repair, forced initialization, and scheme conversion out of DM4 unless separately specified.
