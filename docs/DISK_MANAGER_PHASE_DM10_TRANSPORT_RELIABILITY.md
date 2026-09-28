# Disk Manager Phase DM10 — ATA/QEMU Transport Reliability

## Goal and outcome

DM10 investigated why a fresh, blank 600 MiB QEMU secondary ATA disk failed during GPT initialization even though the DM9 image-backed runtime proof had passed. The failure was reproduced, traced through the block and ATA layers, and corrected. Destructive-operation results now report which stages ran, how many sectors completed, whether a write may have reached media, whether cache flush ran, and whether rollback was attempted and verified.

The final source acceptance gate `out/dm10-qemu-repeatability-final-340` passed 5/5. It ran five isolated fresh images, stopped on the first failure, preserved every run's logs and image, restarted QEMU between initial operation and persistence read, and independently inspected each resulting image. A separate post-run invocation of the read-only verifier also passed on all five images; its transcript is `out/dm10-qemu-repeatability-final-340/postrun-independent-verification.txt`.

| Attempt | Final image SHA-256 | Bytes | Independent verifier |
| --- | --- | ---: | --- |
| 1 | `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE` | 629,145,600 | PASS |
| 2 | `C821A0C6B7DD200756709A8962ED1E76FE106DBD148AA2BB4168B85F5AEC2BE7` | 629,145,600 | PASS |
| 3 | `11883110C076E604FABDE098BD999EE5794F07F6CFD70B238F5826CDEFEE578F` | 629,145,600 | PASS |
| 4 | `69026C53F5AEC99845888D26D5CD000FA8FE658CBAC9289851370B8FB9567E0E` | 629,145,600 | PASS |
| 5 | `4342F2EA6C394CC979694A6B3B439C682CD9E7C1D31E44956D1C7E061033256F` | 629,145,600 | PASS |

All five had the same fresh blank-image SHA-256 `987523E7780392E283B404990C4E84E580BC75C451138B0C86C4F81C296EEEBE`. Every verifier result was read-only and checked the GPT copies and CRCs, partition bounds, FAT32 metadata and mirrored FATs, root/directory entries, and 29-byte file payload. Each attempt's manifest records initial/final hashes, exact first-boot and rediscovery QEMU command lines, identities, serial logs, and proof result.

No host physical disk was attached or modified. No new storage UI function was added.

## Reproducing the failure

The initial DM10 repro used three newly created 600 MiB raw images, each with the same blank-image SHA-256 `987523E7780392E283B404990C4E84E580BC75C451138B0C86C4F81C296EEEBE`. All three left the image hash unchanged and showed the same misleading UI summary: initialization failed, rollback could not be verified, zero write stages, and a default flush status despite no completed write stage.

The first summary alone could not distinguish a failure before the transport callback from a write whose completion was uncertain. A focused diagnostic run added block and ATA callback details. It showed the callback had run for `WriteBackupArray`, LBA `0x12BFDF`, one sector. ATA reached `WaitCompletion`, had transferred one data sector, but had not recorded a completed sector. The final status was `0xD0` (`BSY|DRDY|DSC`) with no `ERR` or `DF`; the block callback returned timeout. Because this first backup GPT array sector was all zero, the unchanged whole-image hash did not prove that no data had reached the device. The correct media state was therefore *write outcome ambiguous*, not “no write.”

The task-file trace isolated the delay after the data phase: QEMU did not return the device to a terminal completion state within the old five-second command deadline. The first bounded retry gate reached four passes and failed on attempt five in the same post-transfer wait, confirming that the timeout was intermittent rather than a consistent GPT or filesystem error. That attempt stopped the gate and preserved its image and logs. It was not counted as acceptance.

## ATA and block-layer changes

The ATA PIO path now uses an injectable I/O seam exercised by deterministic state-machine tests. The implementation:

- Supports checked LBA28 and LBA48 task-file programming and issues one sector per PIO command.
- Waits for BSY to clear before interpreting status, requires DRQ before data transfer, and treats ERR/DF as command failures.
- Checks terminal status after the data phase. A sector is counted as completed only after command completion is observed.
- Records ATA stage, status, error register, LBA, sector count, and sectors transferred/completed for the last operation.
- Selects E7h or EAh cache flush only when IDENTIFY word 83 validly advertises the command, then waits for terminal completion and reports status/error/timeout.
- Uses a bounded 30-second PIO command deadline and a separate bounded 60-second cache-flush completion deadline. PIT-stalled fallback polling is capped; it cannot spin indefinitely.
- Does not retry a write with ambiguous completion. The caller must stop or run the bounded restoration path because replaying an uncertain write would obscure the true state.

The 30-second command timeout was selected after the five-second deadline failed after data transfer under QEMU load. Cache flush receives a distinct, longer bound because ATA specifications warn that cache-flush completion can take substantially longer than an ordinary PIO command. The reference reviewed during this work is the [T13 ATA8-ACS draft](https://kldp.org/files/D1699r3f-ATA8-ACS.pdf). These deadlines bound waiting; they do not make a timed-out write safe to replay.

The shared block layer retains a last-operation record with stable registration identity, transport, geometry, operation, LBA, count, callback result, and ATA detail. Bounded counters track read/write/flush calls and results. Diagnostic state is attached to the registration rather than a reusable device slot.

## Truthful operation stages and diagnostics

Initialize Disk, Create Partition, and Format now publish explicit operation stages and failure details. Results distinguish:

- Failure before any write callback, where no rollback or flush is claimed.
- A write attempted, including how many sectors the transport confirmed complete and whether media may have been changed despite an incomplete completion report.
- Flush not attempted, unsupported, failed, or completed.
- Rollback not required, attempted, flushed, and read-back verified, or left uncertain.

There is no synthetic flush status on paths where flush did not run. An unreadable preflight or metadata snapshot now retains the relevant block callback diagnostic when one exists. The Storage Diagnostics view reports the selected disk identity and presence, operation outcome and stage, write-completion count, possible media reach, attempted flush result, rollback outcome, and raw ATA stage/status/error. Normal failure text remains short; detailed transport data is available in diagnostics.

Rollback remains bounded and operation-specific. Initialization restores its captured partition metadata, partition creation restores the changed table sectors, and formatting restores its bounded filesystem metadata snapshot. Restoration is reported as verified only after the required flush and read-back succeed. An ambiguous or unverifiable result is not labeled safe.

## QEMU runner, provenance, and persistence proof

`scripts/run-dm9-qemu-proof.ps1` now records the exact QEMU process command line, QEMU version, initial/final raw-image hashes, device placement and observed identity, serial/debug log paths, and proof outcome in a per-run manifest. Fresh images use create-new paths and are never reused. Optional QEMU debug flags are passed explicitly; the runner's repeatability wrapper is `scripts/run-dm10-qemu-repeatability.ps1`, which creates five unique attempts and stops on the first failure.

Each run attaches only a newly created 600 MiB raw image as the secondary legacy ATA target (`channel 0`, `target 1`, primary slave / `index=1`). The UEFI boot filesystem comes from a QEMU `fat:` directory mapping, not a host physical disk. The kernel dynamically selects the ATA candidate by channel/target, expected QEMU model, and `DefinitelyNotBoot` provenance; it does not assume a global disk index. The primary QEMU disk remains separately identified as the boot disk.

The observed QEMU identities are:

| Role | ATA identity | Geometry | Provenance |
| --- | --- | --- | --- |
| Boot disk | `ata0m`, QEMU HARDDISK, serial `QM00001`, channel 0 target 0 | 1,032,192 sectors × 512 bytes | `DefinitelyBoot` |
| Test disk | `ata0s`, QEMU HARDDISK, serial `QM00002`, channel 0 target 1 | 1,228,800 sectors × 512 bytes (600 MiB) | `DefinitelyNotBoot` |

The IDENTIFY response advertises LBA48 and valid FLUSH CACHE / FLUSH CACHE EXT support. After the first QEMU process exits, a second process uses the same image, rediscovers the same secondary identity, and reads the test file after mount. The proof sequence is: initialize GPT → flush and verify GPT → create partition → format FAT32 → mount and create/write/read a file and directory → unmount/remount and reread → restart QEMU → rediscover → mount/read again.

The independent read-only Python image verifier runs after QEMU exits. It checks the protective MBR, primary and backup GPT headers and array CRCs, agreement of the GPT array copies, partition bounds, FAT32 BPB and FSInfo, FAT mirror consistency, directory metadata, and the 29-byte persisted file payload. It does not rely on the kernel's in-memory state or the proof's success message.

The QEMU proof calls storage services from its in-kernel proof path; it does not automate the desktop UI or click through Disk Manager. UI changes in DM10 are limited to diagnostics and truthful result reporting.

## Test and build results

The storage-manager test runner passed **340 checks, 0 failures**. Added coverage exercises ATA status transitions, one-sector LBA28/LBA48 ordering, DRQ, ERR and DF handling, pre- and post-data timeouts, delayed completion, flush support and completion timeout, block callback diagnostics/counters, failed preflight and snapshot reads, rollback flush and verification failures, and no-write stage semantics.

The full AMD64 kernel build and UEFI bootloader build passed after the final production-source change. The PowerShell parser accepted both QEMU runner scripts. `git diff --check` is recorded after final documentation updates.

## Transport audit and destructive-operation eligibility

| Transport | Shared block registration | Read/write path | Durable flush / removal lifecycle | Destructive operations |
| --- | --- | --- | --- | --- |
| Legacy ATA PIO | Yes | LBA28/LBA48 PIO with diagnostic completion state | IDENTIFY-gated cache flush; target pinning and registration lifecycle | Eligible only when all existing identity, provenance, mount/root, geometry, lease, and flush gates pass |
| USB mass storage | No connected shared registration; standalone BOT/SCSI code has no block registration call site | Standalone READ(10)/WRITE(10) code is not an eligible Disk Manager target | No integrated SYNCHRONIZE CACHE or shared unregister/removal path | Blocked |
| NVMe | Namespace read/write registration exists | Shared read/write path | Namespace Flush is not integrated; flush semantics are unknown. Safe support also needs controller-aware queue completion handling | Blocked |
| AHCI | No implemented block transport; initialization scans legacy IDE class only | None | None | Blocked |
| RAM disk | Yes, in-memory | Volatile read/write | Synchronous memory completion is not durable persistence | Blocked |
| Image-backed RAM disk | Yes, read-only | Read-only | No durable write path | Blocked |

This phase does not enable USB, NVMe, AHCI, RAM, or image-backed RAM for destructive operations. No endpoint without known durable flush semantics passes the existing write eligibility gate.

## Limitations and next work

The accepted result demonstrates repeatability in QEMU's legacy ATA emulation, not on physical ATA hardware or every controller. An additional QEMU debug-enabled run passed with an 18,633,231-byte error/instruction/reset log; no independent host CPU load was applied. That diagnostic run preceded the final small change that retained preflight-read callback details; the final non-debug 5/5 gate used the complete final source. The persistence check proves the image contents survive process restart in this environment; it does not model power-loss testing on physical media. The proof is service-driven rather than GUI-driven.

DM11 should first implement a complete USB mass-storage lifecycle through shared block registration, including identity/removal handling and SYNCHRONIZE CACHE, with the same write ambiguity and rollback reporting. NVMe Flush and AHCI transport support should remain blocked until their completion and removal semantics are integrated and independently verified. Advanced partition operations remain out of scope.
