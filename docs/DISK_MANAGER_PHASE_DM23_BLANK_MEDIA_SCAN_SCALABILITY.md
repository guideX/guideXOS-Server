# Disk Manager Phase DM23 — Blank-Media Scan Scalability

**Outcome A.** The mandatory full-partition zero scan remains intact. Capability-capped 1 MiB USB reads, complete scan accounting, target-pinned verification, and a one-use blank-proof token are in place. Three fresh 80 MiB writable USB lifecycles passed, and the 600 MiB writable USB lifecycle also reached complete scan coverage, format, restart, and independent verification.

## Starting state and DM22 blocker

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`, branch `DISK_MANAGER_IMPROVEMENTS`.
- Starting commit: `814228a6d59e21ecf9761c6b59d0c6e8b886322b` (`DM22: scale FAT32 formatting with bounded rollback`).
- Cached upstream status at start: `0 0`; `git fetch` failed with `git@github.com: Permission denied (publickey). fatal: Could not read from remote repository.` Upstream freshness is unknown.
- A pre-existing tracked `.gitignore` edit adds `/out`; it is being preserved outside the DM23 commit. Existing and new `out/` evidence is retained.

DM22 had already removed the old FAT32 cluster-count ceiling and passed the 10 GiB AHCI lifecycle. Its USB writable lifecycle remained incomplete because a traced UHCI run advanced too slowly during the required full-volume zero scan. DM22 used a fixed 64 KiB scan buffer. The common block and USB transfer caps were also 64 KiB, so a 1 MiB scan range required sixteen BOT reads. No scan was permitted to be shortened.

## Existing read path and transport limits

The formatter sends each scan batch to the common block API once. The block layer validates the byte count against `BlockDevice.maxTransferBytes`, bounds the LBA range, and pins the registration for the callback. A partition view checks local and translated parent bounds with subtraction-based overflow checks, translates the LBA, and forwards the same buffer; it does not copy the whole request.

| Transport | Advertised common read limit | Work inside one common read | Completion behavior |
| --- | ---: | --- | --- |
| ATA PIO | `0` (no smaller cap advertised) | Loops once per sector; each sector issues its own ATA PIO command and copies 16-bit words directly into the caller buffer. | Polls ready/DRQ/completion with a 30-second command timeout and bounded poll fallback. No read flush. |
| AHCI | 128 KiB | One polling command slot and PRDT-backed 128 KiB DMA bounce buffer; copies between bounce buffer and caller buffer. | 30-second command timeout. No read flush. |
| USB BOT/UHCI | 1 MiB after DM23 | One READ(10)/(16) data command per common read; USB HCI segments the data phase into 65,535-byte calls and bounded 64-TD UHCI chains using packet-sized DMA buffers. | One CSW per BOT command; exact data length and zero residue required. Each TD wait retains the 1,000-frame production timeout. No read flush. |
| NVMe | 128 KiB | Common request is divided into 4 KiB commands through one aligned bounce page (up to 32 commands per common request). | Serialized queue with a 30-second read-command timeout. Production write and Flush callbacks remain absent. |

The old zero test called `bytes_zero` over the 64 KiB buffer. DM23 checks eight bytes per loop group through alignment-safe byte loads, exits at the first group containing nonzero data, then locates and reports the first nonzero byte. The final short batch is limited to the exact remaining sectors. The scan is synchronous and does not flush between reads.

## New batching, coverage, and memory policy

The scan scratch buffer is one aligned, static 1 MiB array. For each operation, the formatter computes the number of sectors as:

`min(1 MiB / logical-sector-size, maxTransferBytes / logical-sector-size)`

rounded down to a whole sector. A zero `maxTransferBytes` means no smaller transport cap is advertised. The helper rejects invalid sector sizes and caps smaller than one sector. FAT32 formatting still accepts only 512-byte sectors; the helper's 4 KiB arithmetic test is not a claim of 4Kn formatter support.

The scan visits the selected partition in increasing LBA order. Each request starts at `partition.startLba + sectorsAlreadyVerified`, and its count is exactly the smaller of the fixed batch size and the remaining sectors. The scan succeeds only when both the sequential relative-LBA cursor and the verified-sector count equal the partition sector count. It records bytes, requests, minimum/maximum request size, current absolute/relative LBA, percent, elapsed ticks, and first nonzero relative LBA/byte offset. A nonzero byte or read failure aborts before formatter writes; the diagnostic says that no formatter writes were made. DM22's metadata-sector zero rechecks and bounded rollback records are unchanged.

The scan buffer grows from 64 KiB to 1 MiB: **+983,040 bytes (960 KiB)** of fixed storage, independent of volume size. No per-sector map is added. Supporting the bounded 64-entry UHCI TD chain adds 48 descriptors (768 bytes at 16 bytes each) and 51 packet buffers (3,264 bytes at 64 bytes each), for 4,032 bytes of additional static UHCI storage. The USB diagnostic additions occupy another 380 fixed bytes: 120 bytes across the four storage-device contexts and shared context copy, 256 bytes for the 16-entry BOT history, and 4 bytes for the last-transfer diagnostic. Together, the fixed resident addition is **987,452 bytes (about 964.3 KiB)**. This total excludes build-specific alignment padding around separately aligned static objects.

Per live `Fat32FormatResult`, scan telemetry adds 72 bytes; this is caller-owned result state, not fixed BSS. The one-use token is 392 bytes on the active format/probe stack. The UHCI call's two bookkeeping arrays use 640 stack bytes for 64 entries, up from 130 bytes for the old 13-entry arrays (+510 bytes); the 1 MiB scan buffer itself is never on the stack. DM22's rollback bound remains at most 64 bytes (48 bytes for six records in the 8 GiB unit fixture).

## Lifetime, revalidation, and blank authorization

Formatting owns the exclusive storage-operation lease and pins the exact registration for the full scan. Every batch checks lease currency and target identity before reading. The registration identity is monotonic and cannot be reused while pinned; removal or a replacement registration makes the old read fail. VFS partition mounts are rejected while a destructive operation is active, preventing a mount from appearing after stale selection. An already-mounted partition, open VFS writer, boot target, read-only target, or target without trusted persistence fails destructive preflight.

Immediately after the scan and before metadata reads, Flush, or writes, the formatter reruns target, partition-table, bounds, mount, boot, capability, and lease validation. A private one-use `BlankVerificationToken` binds successful complete coverage to the target registration/incarnation, partition identity/bounds, geometry, operation owner token, and verified sector count. The formatter consumes it once after revalidation. Probe operations discard their token; KnownZero is not published or persisted after an operation. Per-sector metadata zero rechecks remain in place.

The existing Disk Manager is synchronous. DM23 exposes scan percentage and LBA/byte totals in diagnostic serial output at bounded 10% increments and in the returned result; it does not add a desktop progress surface or cancellation API. The current desktop redraw is deferred until the synchronous operation returns, so progress cannot repaint in the dialog without changing the event-loop model. Completion is reported only after the scan, formatter transaction, and post-format verification return. Cancellation remains unavailable, and the UI can remain unresponsive during long scans.

## USB command and UHCI proof

On the tested QEMU UHCI device, a 1 MiB scan request is READ(10), 2,048 512-byte sectors, and exactly one BOT command plus one 13-byte CSW. QEMU reports 64-byte maximum packets. The HCI divides 1 MiB into sixteen 65,535-byte portions plus a 16-byte remainder; that requires 16,385 data TDs total (16,368 64-byte packets, sixteen 63-byte chunk-ending packets, and one 16-byte final packet). UHCI links at most 64 TDs per QH batch. The CSW must have the expected signature/tag, passed status, 13 received bytes, zero residue, and the expected endpoint toggle; DM20 diagnostic runs confirmed the toggle progression and CSW completion.

The 1 MiB common USB cap reduces steady-state BOT command count from 16 per MiB at the old 64 KiB limit to 1 per MiB. It does not reduce the number of full-speed 64-byte packets; UHCI still services those through bounded packet buffers and QH batches. The DM23 read diagnostic records opcode, LBA, sectors, expected/actual bytes, TD count, max packet, CSW status/tag, and residue. A complete read requires exact bytes and zero residue.

## Measured scan throughput

The first 68 MiB diagnostic run completed an untraced 70,237,696-byte partition scan in 17,568 PIT ticks (175.68 seconds, 0.381 MiB/s), using 67 shared reads, average 1,048,323 bytes, min 1,031,680 bytes, and max 1,048,576 bytes. Each full READ(10) had 2,048 sectors and 16,385 TDs; the final read covered exactly 2,015 sectors. Coverage was complete.

| Transport / run | Partition bytes | Scan seconds | MiB/s | Common reads | Average read | Max read | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| USB, 68 MiB image, DM20+DM23 diagnostics, QEMU trace off | 70,237,696 | 175.68 | 0.381 | 67 | 1,048,323 B | 1,048,576 B | Complete, all sectors zero |
| USB, fresh 80 MiB cohort run 1 | 82,820,608 | 204.78 | 0.385 | 79 | 1,048,362 B | 1,048,576 B | Complete, all sectors zero |
| USB, fresh 80 MiB cohort run 2 | 82,820,608 | 204.71 | 0.385 | 79 | 1,048,362 B | 1,048,576 B | Complete, all sectors zero |
| USB, fresh 80 MiB cohort run 3 | 82,820,608 | 204.81 | 0.385 | 79 | 1,048,362 B | 1,048,576 B | Complete, all sectors zero |
| USB, 600 MiB image, QEMU trace off | 628,080,128 | 1,552.45 | 0.385 | 599 | 1,048,548 B | 1,048,576 B | Complete; 598 full reads + exact 1,031,680 B tail |
| USB, 68 MiB image, full selected QEMU trace | 70,237,696 | 173.62 | 0.385 | 67 | 1,048,323 B | 1,048,576 B | Complete; 66 full reads + exact 1,031,680 B tail |
| ATA PIO, 68 MiB lifecycle | 70,237,696 | 13.11 | 5.109 | 67 | 1,048,323 B | 1,048,576 B | Complete; 137,183 one-sector PIO commands |
| AHCI, normal 600 MiB lifecycle | 628,080,128 | 2.99 | 200.329 | 4,792 | 131,068 B | 131,072 B | Complete; 128 KiB command limit |
| AHCI, 10 GiB lifecycle | 10,736,352,768 | 50.33 | 203.436 | 81,912 | 131,071 B | 131,072 B | Complete; 128 KiB command limit |

The traced 68 MiB run used the same quiet DM13+DM23 kernel profile as the untraced cohort, completed the scan in 173.62 PIT seconds (0.385 MiB/s), and produced a 572,025,001-byte (545.3 MiB) selected UHCI trace. The untraced 80 MiB cohort measured 0.385 MiB/s; within the rounded PIT metric, this shows no scan-throughput penalty from QEMU tracing. The traced first boot took 7m12s of host wall time including boot, formatting, and ten VFS cycles, so that number is not a scan-only wall-time comparison. Trace correlation found 7,063 mass-storage submits and 7,063 matching QEMU status records (6,949 kernel commands and 114 pre-kernel commands), no active CSW timeout, and no failure classification. The quiet kernel does not emit DM20 guest command chronology, so the correlator could not join guest tags to QEMU status records. DM22's earlier traced USB attempts produced roughly 400 MiB of trace after about 20 minutes (TCG) and were stopped after about 17 minutes (WHPX), before a complete scan.

The 600 MiB USB first boot recorded full coverage in 1,552.45 PIT seconds (25m52s). Its first runner attempt rejected 599 valid requests because the metric gate had a fixed 128-request ceiling. The gate now uses the measured byte count and maximum request size; a separate restart-only run on the same formatted image passed persistence and independent verification. The three fresh 80 MiB runs remain the required end-to-end cohort and passed through the normal lifecycle runner.

## Tests, lifecycle results, and limitations

The storage suite passes **842 checks, 0 failures**, preserving all 832 DM22 checks and adding ten DM23 checks. USB Mass Storage contributes 249 checks, and all seven DM20 trace-classifier tests pass. Added deterministic coverage includes capability-capped batches, exact final partial coverage, an 8 GiB sparse scan with 8,192 1 MiB reads, first/batch-boundary/middle/final nonzero bytes, first/middle/final read failures including timeout, short USB data/residue, a blocked mount during the lease, post-scan partition-identity change before writes, and removal followed by registration of a replacement that receives no scan reads.

The DM23 writable USB lifecycle cohort passed **3/3** on three fresh 80 MiB raw images. Each run completed GPT initialization, partition creation, a full blank scan, FAT32 format, mount/write/read/unmount, ten VFS stress cycles, restart/remount persistence, and the independent GPT/FAT32/file verifier. End-to-end runner times were 781, 777, and 777 seconds. QEMU tracing was disabled for this performance cohort; scan and READ/CSW metrics were captured by the quiet DM23 proof kernel. Evidence: `out/dm23-usb-cohort-20261002-131010/cohort-summary.txt` and each `run-01` through `run-03/manifest.txt`.

Additional QEMU regressions passed:

- **600 MiB USB:** complete 598.984 MiB scan; first-boot lifecycle marker passed; separate restart-only persistence and independent GPT/FAT32/data-isolation verifier passed. Evidence: `out/dm23-usb-600m-20261002/`.
- **ATA:** fresh 68 MiB lifecycle, restart rediscovery, and independent image inspection passed. Its 67 common reads each cause one ATA PIO command per sector (137,183 total). Evidence: `out/dm23-ata-qemu-proof-20261002/`.
- **AHCI:** normal 600 MiB lifecycle and reboot rediscovery passed. Evidence: `out/dm23-ahci-small-20261002/`.
- **10 GiB AHCI:** initialize, full scan, FAT32, 96 KiB high-cluster file, restart rediscovery, and independent read-only verification passed. Evidence: `out/dm23-ahci-large-20261002/`.
- **USB write stress:** 100 WRITE → Sync Cache → readback → restore cycles passed; host image SHA-256 remained `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF`. The selected trace was 11,571,178 bytes. Evidence: `out/dm23-usb-write-proof-20261002/writable/`.
- **USB read-only:** reads passed, writes were blocked, and the formatted reference image SHA-256 stayed `C7D69DE89D75919CA7507E27E7EA860CE5A68DC62C74FC5C4E7632A99EFE54A5`. Evidence: `out/dm23-usb-write-proof-20261002/readonly/`.
- **USB hotplug:** remove/reinsert, same-port media replacement, open-handle unplug, VFS write removal, uncertain multi-TD write removal, and Sync Cache removal all passed. The run exposed and fixed USB removal-outcome classification when UHCI reports a TD timeout while the port connection-change bit shows removal. Evidence: `out/dm23-usb-hotplug-retry-20261002/`.
- **NVMe:** the read callback remains present, common write and Flush callbacks remain absent in the default build, and destructive-target preflight rejects the default NVMe descriptor. No writable NVMe run was performed; the storage suite covers this gate.

The final AMD64 production kernel and USB diagnostic, quiet USB lifecycle, USB 100-cycle, AHCI large, AHCI small, ATA, and DM14 hotplug kernel profiles built successfully after the removal-classification fix. The UEFI x64 Release bootloader build also passed. The NVMe default build remains read-only, and no physical host disk or host USB device was passed through to QEMU.

Known limitations remain: scan work is O(partition size); 1 MiB is a maximum, not a throughput promise for every device; the tested QEMU full-speed UHCI path measured about 0.385 MiB/s, so a 10 GiB USB scan would still take many hours at that emulator rate; ATA still issues one PIO command per sector; UHCI remains packet- and frame-limited; desktop progress/cancellation and asynchronous I/O are not added; formatting/mount support stays 512-byte-only; and the historical DM21 WRITE(10)/CSW cause remains unknown. Disk Manager's current flow performs a full read-only probe before presenting Format, then performs a second independent full scan after confirmation because the probe lease and token are discarded. The format transaction still performs only one scan. DM24 remains the next geometry step: 4Kn FAT32 formatting, parsing, partition-view, and VFS support with independent verification.
