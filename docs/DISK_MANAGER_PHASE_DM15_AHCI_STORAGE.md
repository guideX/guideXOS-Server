# Disk Manager Phase DM15 — AHCI/SATA Storage

## Outcome

**Outcome A — direct AHCI SATA disks are integrated with shared block storage and passed the private write gate plus five fresh-image full lifecycles.** QEMU validated AHCI discovery, IDENTIFY, shared reads and writes, ATA cache flush, boot provenance, initialization, partition creation, FAT32 formatting, VFS file I/O, restart rediscovery, and independent image inspection. The private write stage ran first with the shared AHCI write callback disabled and verified byte-for-byte image restoration before the shared-write lifecycle build was accepted.

No physical host disk was passed to QEMU. New QEMU proof evidence is under `out/dm15-*`; output directories created while D: was full are junction-backed to C: so their contents remain available at the documented `out/` paths. Existing output evidence was preserved.

## Starting state and audit

- Starting branch: `DISK_MANAGER_IMPROVEMENTS`.
- Starting HEAD: `5a55cc70a36c56302bb96d2b10ee9af3cc12bb9b`.
- The repository already had `BDEV_AHCI` labels and a few AHCI register definitions, but no production AHCI transport, no shared block registration, and no AHCI boot-source match. ATA PIO was the only SATA-like path and did not enumerate AHCI controllers.
- At the start, 2,336 pre-existing untracked items were present under `out/`; none were removed.

## Supported profile and controller bring-up

The production implementation is deliberately limited to direct SATA disks on AMD64 AHCI controllers. It scans PCI configuration mechanism 1 on segment zero for class `01h`, subclass `06h`, programming interface `01h`. It validates BAR5 as a supported 32-bit memory BAR, checks the complete HBA register window, maps it through the kernel's uncached MMIO mapper, and records the controller BDF. Invalid, I/O, 64-bit, or out-of-range BARs fail closed.

When advertised, BIOS/OS ownership handoff and HBA reset use bounded waits. The driver enables AHCI mode, reads the implemented-port bitmap, and inspects link/power state and signatures. It registers only an active SATA disk (`PxSIG=00000101h`). Empty links, link errors, SATAPI, port multipliers, and unknown signatures are diagnosed and skipped. In QEMU the port signature became valid after bringing the linked port engine up, so enumeration starts the bounded port engine before final signature classification when needed.

The current limits are four controllers and eight registered disks. PCI segment zero and the direct disk on each port are supported; port multipliers, ATAPI, hotplug enumeration, and non-AMD64 AHCI are outside this phase. The driver uses polling, not interrupts. NCQ is unsupported.

## DMA and command path

Each potential port has static, aligned command-list (1 KiB), received-FIS (256 byte), command-table (128 byte), IDENTIFY (512 byte), and 128 KiB data-bounce storage. Physical addresses are derived from the kernel load base rather than treating virtual addresses as physical. CLB, RFIS, and table alignments are checked before programming `PxCLB/PxFB`; DMA ranges are checked for overflow, HBA 32/64-bit DMA capability, and the supported physical-address limit. Commands use a single bounded PRDT entry backed by the per-device bounce buffer; at 512 bytes per sector the largest request is 256 sectors. Invalid lengths, LBAs, address ranges, busy slots, and unsupported transfers are rejected before issue.

One command slot is used at a time, serialized by a bounded transport lock. The path builds H2D Register FIS structures for IDENTIFY, READ DMA/READ DMA EXT, WRITE DMA/WRITE DMA EXT, and FLUSH CACHE/FLUSH CACHE EXT. It waits for command completion with bounded deadlines, checks `PxCI`, `PxTFD`, `PxIS`, `PxSERR`, and exact PRDBC byte count, then copies read data out of the bounce buffer only after a successful completion.

IDENTIFY is required before registration. Capacity, LBA28/LBA48 support, represented logical sector size, model, serial, firmware revision, write-cache state, and advertised flush commands are parsed. The ATA default sector size is 512 bytes when IDENTIFY does not report a larger logical sector. Invalid represented geometry or capacity prevents registration. ATA strings are byte-swapped and trimmed. LBA48 commands are preferred where supported; otherwise the bounded LBA28 path is used.

Write failures after issue that cannot prove completion return an uncertain-write status; the driver does not replay an ambiguous write. Flush failures report unverified durability. Failure diagnostics retain the ATA command, slot, LBA/count, task-file status, port interrupt status, SATA error register, expected/transferred bytes, and timeout/error classification. Recovery stops the engine, clears captured errors, re-establishes DMA memory and restarts it, then compares a fresh IDENTIFY identity. A failed recovery or changed identity marks the registration offline. Successful commands do not log per-command noise; verbose virtual-to-physical DMA addresses are behind the diagnostic build flag.

## Identity, boot provenance, and shared storage

Each block registration records controller segment/BDF, AHCI port, ATA model/serial, sector capacity, logical sector size, flush support, write support, non-removable state, and a fresh registry registration ID. The transport identity includes the port, so a different AHCI target does not compare equal merely because it has the same capacity or model.

Boot provenance decodes the UEFI SATA Device Path node (type 3, subtype `0x12`). A direct path is authoritative only when PCI segment/BDF, HBA port, direct port-multiplier value `0xFFFF`, and LUN zero match. Matching endpoint means `DefinitelyBoot`; a distinct authoritative endpoint means `DefinitelyNotBoot`; incomplete, unsupported, multiplier, or otherwise ambiguous paths remain `Unknown`. The entire parent disk is protected if firmware identifies a partition. ATA model/serial are retained for identity and diagnostics; they are not substituted for missing firmware path provenance.

An identified SATA disk is registered as `BDEV_AHCI` in the existing common block registry. It receives shared read and write callbacks, and a flush callback only when IDENTIFY advertises FLUSH or FLUSH EXT. The flush contract is marked known only when that supported callback is installed. Storage Manager snapshots and revalidates the controller/port identity and feeds boot provenance, geometry, registration lifetime, mounted/root state, write capability, and trusted persistence through the existing common destructive-operation gates. No AHCI-specific preflight bypass was added. Disk Manager uses the shared registry/model and needs no AHCI-specific UI.

## QEMU proof and evidence

QEMU runs `q35` with the built-in ICH9 AHCI controller. The UEFI boot ESP is attached on AHCI port 0; a separate disposable 600 MiB raw image is attached on AHCI port 1. The manifest records both placements, QEMU version, kernel/bootloader hashes, image hashes, and states that no physical host disk was passed through.

The captured controller is PCI `00:1F.02`, vendor/device `8086:2922`, ABAR `0x81060000`, AHCI version `1.0.0`, `CAP=0xC0141F05`, 64-bit DMA enabled, and six implemented ports. Ports 0 and 1 enumerate as direct SATA disks (`SSTS=0x113`, `SIG=0x101`); ports 2–5 report no device. IDENTIFY reports 512-byte sectors, LBA48, FLUSH and FLUSH EXT for both QEMU disks. Port 0 (`QM00001`) is the firmware boot device at 504 MiB; port 1 (`QM00003`) is the 600 MiB disposable target and is authoritatively `DefinitelyNotBoot`. The first-stage common partition-table read succeeds on the blank target before the private transport-write proof.

The private first-stage proof selects only the QEMU secondary candidate when it is authoritatively `DefinitelyNotBoot`. The shared registry advertises it read-only for this stage, while the proof calls the same production transport internally. The final valid sector is saved, written, flushed, read back, restored, flushed again, and compared against the complete initial image hash. Evidence at `out/dm15-private-proof-cleanup-20260929-r9/` reports `writeStatus=0`, `flushStatus=0`, `readStatus=0`, `restoreStatus=0`, and identical initial/final SHA-256. This last-LBA transfer exercises the capacity boundary; full-image hash equality verifies that restoration left no sector-range collateral.

Only after that pass does the shared-write lifecycle kernel run. `out/dm15-qemu-repeatability-20260929-final5/repeatability-manifest.txt` records **5/5 unique fresh images**. Each run initializes GPT, creates a partition, formats FAT32, mounts, writes and reads a deterministic file, unmounts/remounts, reboots QEMU with the same image, rediscovers AHCI, and verifies the persistent file after explicit remount. Each image is independently checked by `scripts/verify-dm9-qemu-image.py` for protective MBR, primary/backup GPT headers and arrays, CRCs, bounds, FAT32 BPB/FSInfo/backup metadata, FAT copies, root directory, and expected file bytes.

The aggregate gate references two complete lifecycle passes created earlier in this DM15 run after checking their success manifests, image/verifier/log artifacts, reboot markers, and exact kernel and UEFI binary hashes. It then records three additional fresh images from the final run directory. All five attempts passed the same lifecycle and independent-verifier criteria.

## Tests, regressions, builds

- Storage suite: **691 checks, 0 failures**, including 247 USB checks. AHCI helper coverage includes PCI class/BAR decoding, port state/signatures, command-slot selection, command/FIS fields, bounds and PRDT construction, transfer result mapping, flush status, and SATA boot provenance.
- ATA compatibility: **5/5** fresh 600 MiB IDE secondary-image lifecycle regressions passed initialization, partition creation, format, file I/O, reboot rediscovery/remount, and independent verification in `out/dm15-dm10-ata-regression/`.
- USB read-only regression: passed shared registration, read-only FAT32 mount, deterministic file read, clean unmount, and whole-image hash equality in `out/dm15-usb-readonly-regression/`.
- USB writable lifecycle: passed on a fresh blank 68 MiB disposable image in `out/dm15-usb-lifecycle-regression/run/`. It exercised the private write/flush/read/restore gate and boundary canaries, common write callback, GPT initialization, partition creation, FAT32 formatting, VFS file I/O, ten-cycle mount/write/read/unmount stress, QEMU restart persistence, and independent GPT/FAT32 image verification.
- USB hotplug compatibility: passed the DM14 QEMU removal/reinsert, stale-handle, replacement-media, VFS write interruption, multi-transfer data-out interruption, and Sync Cache interruption sequence in `out/dm15-usb-hotplug-regression/`.
- AMD64 production kernel: passed with no proof-only compile defines.
- UEFI x64 Release bootloader: passed.

## Remaining limits

- AHCI is currently AMD64 only, PCI segment zero, limited to a 32-bit BAR5, four controllers/eight disks, direct SATA disks, one serialized non-NCQ command slot, one PRDT entry, and 128 KiB maximum transfer size.
- Port multipliers, ATAPI, NCQ, interrupt-driven commands, enumeration of devices attached after boot, and a full SATA hotplug lifecycle are unsupported.
- The interface is polled. Command, flush, engine, and lock waits are bounded. An ambiguous write is never retried automatically.
- AHCI hardware doesn't supply a stable software registration token; fresh registry IDs protect each registered incarnation. Existing common filesystem limitations remain, including 512-byte-only FAT32 mounting/formatting and the formatter's current size limit.

## DM16 recommendation

With AHCI now validated alongside ATA and USB, prioritize **NVMe Flush/durability** next. That closes the remaining common internal-storage persistence gap before expanding FAT32 geometry, USB hub support, or destructive partition UI.
