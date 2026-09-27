# Disk Manager Phase DM4 — Boot Provenance, Registry Coordination, and Transport Safety

## Outcome

**Outcome A — the boot-provenance, registry-lifetime, and persistence-test foundation is complete.** DM3's destructive-operation checks remain in force. A matching ATA PIO device can pass the provenance and durability portions of preflight only when firmware identifies a supported legacy ATA/ATAPI endpoint and IDENTIFY authoritatively advertises a usable cache-flush command. No physical disk was initialized. NVMe remains blocked by unknown durability, and AHCI and USB mass storage are not registered in the shared block layer.

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `a738fe57860c0d69124dd114f0d43ee68d0fb536`
- Worktree: clean
- Configured upstream: `origin/DISK_MANAGER_IMPROVEMENTS`, aligned 0 ahead / 0 behind
- Historical phase reports and the DM2 storage manager / DM3 initialization implementation were reviewed before changing behavior.

## UEFI boot-source audit

The loader obtains the image's source handle from `EFI_LOADED_IMAGE_PROTOCOL::DeviceHandle`. Before `ExitBootServices`, it opens Device Path Protocol on that handle, validates and copies the complete bounded path, and attempts to resolve the final PCI controller location. PCI location resolution matches the ACPI root node to a PCI Root Bridge I/O handle, reads that bridge's bus range, follows PCI bridge nodes, and records the root bridge segment plus endpoint bus/device/function. No firmware handle or protocol pointer is serialized.

The implementation does not need a post-handoff UEFI Block I/O or Simple File System handle. The loader's device path is the firmware evidence preserved for kernel matching. If the path is absent, malformed, overlong, has no usable PCI location, or its storage endpoint is unsupported by the kernel matcher, provenance remains `Unknown`.

Relevant normative references: [UEFI Device Path Protocol](https://uefi.org/specs/UEFI/2.10/10_Protocols_Device_Path_Protocol.html), [UEFI PCI Bus Support / PCI Root Bridge I/O](https://uefi.org/specs/UEFI/2.10/14_Protocols_PCI_Bus_Support.html).

## Boot-source descriptor and BootInfo

`BootSourceDescriptor` is a packed, versioned, architecture-neutral value with a 256-byte device-path bound, explicit validity/truncation flags, PCI segment/BDF, and raw copied path bytes. The path validator checks every node length and requires one complete end node exactly at the end of the buffer. It rejects malformed, truncated, and multi-instance paths. The kernel uses only bounded byte parsing for the storage nodes it needs.

BootInfo is version 3 and appends the descriptor after the previous version-2 prefix. The checksum validator accepts the exact legacy v2 size or the exact current v3 size. Missing/legacy descriptors and invalid v3 metadata result in unknown provenance. The kernel copies and revalidates the descriptor during early boot before storage discovery. Hosted and test targets can set the three provenance states explicitly; host Windows boot state is never inferred.

## Matching semantics and whole-disk protection

The matcher first honors an explicit descriptor provenance value, used for controlled fake/hosted state and explicit RAM-disk classification. For firmware matching, it requires a valid source PCI location and a registered device with a valid PCI location:

1. A different segment/BDF is `DefinitelyNotBoot`.
2. An exact BDF plus an NVMe device-path node is matched by namespace ID. Exact namespace is `DefinitelyBoot`; another namespace on that controller is `DefinitelyNotBoot`.
3. An exact BDF plus an ATAPI device-path node is matched by channel and target. Exact endpoint is `DefinitelyBoot`; a different target is `DefinitelyNotBoot`.
4. A SATA device-path node remains `Unknown`: the ATA PIO driver does not retain an authoritative mapping from the UEFI HBA port/multiplier-port identity to its legacy PIO target.
5. Missing, malformed, unsupported, or insufficient identity remains `Unknown`.

The matcher classifies a whole registered block device. It intentionally does not require the firmware Hard Drive media node's partition start or GUID to equal a whole-disk target. Thus a path that includes a partition node still protects its parent controller endpoint and, where endpoint identity is supported, the whole backing disk. Capacity and device names are not identity evidence. Unknown continues to fail closed in DM3 preflight.

## Registry coordination and target lifetime

Each successful registration receives a monotonically increasing, non-reused 64-bit registration ID. Target snapshots include it as well as the slot, registry generation, transport, geometry, and stable descriptive identity. Slot reuse therefore cannot make an old snapshot refer to a new device. Registry generation saturates instead of wrapping; identity revalidation fails closed after saturation.

The registry uses a short internal lock for descriptor copies and mutations. It does not call transport callbacks while holding that lock. Storage initialization pins its exact target registration from preparation through confirmation, execution, final verification, and cleanup. Ordinary unregister refuses a pinned entry. Registration and unrelated removal remain possible. Forced disappearance marks a pinned registration offline and bumps generation; it stays as an inaccessible tombstone until the final pin is released, preventing slot reuse underneath the operation.

Block I/O and flush dispatch make a transient pin and use a copied descriptor so slot reuse cannot rebind a callback in flight. A `NO_MEDIA` result marks the matching registration offline. RAM-disk destroy checks its exact registration and leaves owned memory intact while a storage lease pins it; it can complete after release. These rules handle the current shared registry and RAM-disk teardown paths. There is not yet a general hardware hot-unplug / transport-teardown integration, so future drivers must mark offline and coordinate their private controller state as well.

## Persistence seam and ATA result

The shared flush report distinguishes supported-and-succeeded, synchronous-durable, unsupported/unknown, and failed. A missing callback is not silently durable. A callback with contradictory durable-completion metadata is classified unknown. Capability inspection uses copied registry snapshots.

ATA PIO's callback is installed only if IDENTIFY word 83 has its validity bits set and advertises FLUSH CACHE or FLUSH CACHE EXT. The fact that IDENTIFY advertises support is not treated as proof of a successful flush. The command seam and real path select the device, perform the required alternate-status settling reads, wait for DRDY with BSY/DRQ clear, issue E7h or EAh according to support, poll completion, and report timeout, no-media, ERR/DF, and post-command readiness distinctly. A successful command returns success; transport testing remains a hardware/emulator proof tier beyond the deterministic I/O seam.

## NVMe, AHCI, and USB audit

- **NVMe:** registered read/write namespaces now carry PCI segment/BDF and NSID for provenance matching. Flush remains unavailable. The existing driver shares queue, tail/head, and command-ID state globally across controllers; safely routing a Flush through it is not a narrow change. The descriptor exposes no flush callback or durable-completion claim, so initialization is blocked as unknown durability. The [NVMe Base Specification](https://www.nvmexpress.org/wp-content/uploads/NVM-Express-1_4c-2021.06.28-Ratified.pdf) requires Flush completion to be checked for the specified namespace; no such safe path is implemented here.
- **AHCI:** the ATA source contains AHCI register definitions and an explicit future-work comment, but initialization currently scans and registers legacy IDE PIO only. There is no shared block registration path for AHCI.
- **USB mass storage:** a Bulk-Only / SCSI driver handles discovery, inquiry, capacity, reads, writes, and sense, but it does not register devices with the shared block layer and does not implement SYNCHRONIZE CACHE. No DM4 wiring was added.
- **RAM disk / image-backed RAM disk:** shared-registry RAM disks are explicitly volatile even though they have a no-op flush callback. Read-only image-backed RAM disks use the same volatile in-memory storage model and cannot satisfy durable initialization.
- **Hosted `.img` viewer and test fakes:** these do not enumerate or mutate host physical disks. Fakes inject provenance and persistence deterministically; tests only use in-memory sectors.

## Eligibility matrix

| Transport | Read / write | Persistence and flush | Boot provenance | Geometry / mount protection | Initialization result |
|---|---|---|---|---|---|
| ATA PIO, legacy IDE compatibility channel | Registered read/write | Eligible only when valid IDENTIFY flush support installs the checked ATA callback and the flush succeeds | Exact PCI plus ATAPI channel/target can classify boot vs non-boot; unsupported path stays unknown | Existing geometry, mount/root, raw-layout, lease, and read-back gates apply | Can pass common DM3 preflight when all gates prove safe; no physical run claimed |
| ATA PIO, standard-port fallback or unmatched SATA identity | Registered read/write | Same ATA flush rule | PCI identity or SATA port mapping is insufficient, so unknown | Same DM3 gates | Blocked: boot identity unknown |
| AHCI | No shared block registration | No shared callback | No shared identity | Not applicable through this registry | Blocked: AHCI registration/transport is absent |
| NVMe | Registered read/write; current transfer restrictions remain | No Flush; durability unknown | PCI BDF + NSID can match boot namespace | Common DM3 geometry/mount checks apply | Blocked: durable flush unavailable |
| USB mass storage | Driver read/write exists, not in shared registry | No SYNCHRONIZE CACHE implementation | No shared block identity | Not applicable through this registry | Blocked: registration and persistence contract absent |
| RAM disk | Registered read/write unless read-only | Volatile memory; callback does not make it durable | Explicitly not firmware boot backing | Geometry/mount checks still apply | Blocked: volatile persistence |
| Read-only image-backed RAM disk | Registered read-only | Volatile memory and read-only | Explicitly not firmware boot backing | Geometry/mount checks still apply | Blocked: read-only and volatile |
| Hosted `.img` viewer / test fake | Viewer is inspection-only; fakes exist only in tests | Test contracts are injected and deterministic | Explicit test provenance; no host-disk inference | Test suite exercises common checks | No host physical-disk initialization |

Eligibility is decided by the existing common initialization preflight, not a transport-specific UI bypass. That preflight still requires current identity/generation, valid geometry, readable/writable media, trusted persistence and successful required flush, unmounted/non-root status, definitely non-boot provenance, a confidently raw layout, an exclusive operation lease, and parser-confirmed read-back.

## Disk Manager and diagnostics

The selected-device panel now presents block index, driver index, registry generation, boot provenance, mount/root state, sector size, read/write and persistence summary, model, and serial where available. It labels a boot target as protected and gives the common initialization rejection status, with an explicit NVMe durable-flush explanation. The initialization dialog continues to show model, serial, transport, capacity, and logical sector size. GPT remains the default. No partition creation, deletion, formatting, resizing, repair, wipe, or conversion action was added.

## Tests and build results

The deterministic storage-manager/parser/initializer suite passed **155 checks, 0 failures** via `scripts/run-storage-manager-tests.ps1`. It covers BootInfo v2/v3 checksums and fallback, valid/malformed/truncated device paths, NVMe namespace and ATA target matching, same-capacity different BDF, whole-disk protection, registry incarnation and pin behavior, forced removal and slot reuse, RAM-disk lifetime, flush capability/result disagreements, and ATA command selection/readiness/error/timeout through fake port I/O. The fake suite includes removal during flush and verification and confirms no false success. It does not open a physical disk.

Targeted C++14 syntax-only checks passed for the storage/block/parser/initializer/RAM-disk/ATA/NVMe translation units and the UEFI loader plus GUID definitions. The BootInfo header's static compilation check also passed. The changed bare-metal `kernel_apps.cpp` could not be validated with the available host MinGW compiler because its Windows headers and target configuration collide with this bare-metal project's types; the expected MSVC `cl` tool is unavailable. The normal `cmd /c build.bat` stopped at `verify-mbedtls-profile.ps1` because `third_party/mbedtls` is missing. Its tracked bootstrap/dependency policy was not changed. `qemu-system-x86_64` is unavailable, so no disposable-image boot proof was run.

No ordinary developer disk or other physical medium was written.

## Remaining blockers and recommended DM5

1. Add and validate a shared AHCI block registration path with stable HBA-port identity and a tested durable flush.
2. Redesign NVMe queue state to be per-controller, then add a namespace-aware Flush with checked completion and timeout/error behavior before exposing durable capability.
3. Wire USB BOT into the shared block registry only after implementing and testing SCSI SYNCHRONIZE CACHE semantics and stable device lifetime handling.
4. Connect real transport detach/reset paths to offline tombstones and driver-private I/O shutdown.
5. Run the loader/kernel on QEMU or disposable test hardware with two disks: verify the firmware boot disk is protected and initialize only the separate blank test disk.
6. Build the bare-metal UI with the intended MSVC toolchain and inspect the diagnostics layout on screen.
