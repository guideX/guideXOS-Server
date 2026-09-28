# Disk Manager Phase DM12 — UHCI runtime USB proof

## Result

**Outcome A.** Production UHCI now enumerates QEMU USB Mass Storage, registers it with the common block registry as read-only, parses GPT, mounts the FAT32 partition read-only through VFS, reads the deterministic proof file, and unmounts cleanly. The real QEMU read path passed five consecutive fresh boots; a final production-layout run after removing diagnostic guard code also passed.

Shared USB writes remain disabled. Real QEMU hot-unplug/reinsert and GUI interaction were not tested; the retained fake lifecycle coverage is described below.

## Starting state and upstream

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `2979d2a7a8e623a73f327f1595a61eed6d861316` (`Disk manager DM11 USB mass storage integration`)
- The tracked worktree was clean at start. Existing `out/` evidence was retained.
- The local `origin/DISK_MANAGER_IMPROVEMENTS` reference showed 0/0 ahead/behind at start, but its freshness could not be confirmed. `git fetch origin` failed with `git@github.com: Permission denied (publickey). fatal: Could not read from remote repository.` Remote freshness is unknown.

## Reproduced DM11 failure

The reproduction used QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), machine `pc,usb=off`, the PIIX3 UHCI controller, QEMU USB Mass Storage over BOT, and a repository-owned 600 MiB image opened read-only. Its SHA-256 before and after was `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE`. No host physical disk was passed to QEMU.

Enumeration failed on the first endpoint-zero `GET_DESCRIPTOR(Device)` SETUP TD (token `0x00E0002D`) after the root port reset. The wait expired with ACTIVE still set (`0x18800000`), the QH element was terminated, FRNUM was `0x07AF`, and USBSTS was clear. The QEMU trace recorded schedule/queue/TD and packet-processing events, but the original guest diagnostics did not establish that the guest had observed a completed descriptor.

The retained baseline and trace are under `out/dm12-repro-baseline/`. The later bounded physical audit recorded a 16-byte TD whose status changed from `0x188007FF` to `0x18000007`; the direct physical read agreed with the guest descriptor read. This is evidence of controller completion becoming visible in guest memory, not stale CPU cache contents. See `out/dm12-uhci-physical-audit/usb-probe.serial.log`.

## UHCI descriptor and completion audit

The DMA descriptor definitions now match the hardware layout: a TD is 16 bytes and 16-byte aligned; a QH is 16-byte aligned with the hardware's 8-byte head/element prefix and reserved padding. Static assertions cover size, alignment, and field offsets. The prior TD C++ type contained four extra reserved dwords, making the software stride 32 bytes even though hardware consumed only its first 16 bytes.

Virtual addresses are converted through the kernel's physical base with overflow and low-address checks. In the bounded physical audit, DMA base `0x18F4C000` mapped frame-list VA `0x22D20000` to physical/base register `0x3BB6C000`; QH VA `0x22D1F390` to physical `0x3BB6B390` (the frame-list entry was `0x3BB6B392`, including the QH-select bit); and TD VA `0x22D1F3A0` to physical `0x3BB6B3A0`. The TD link `0x3BB6B3B4` selects the next 16-byte TD at `0x3BB6B3B0` with the depth-first bit set. The physical audit's direct descriptor walk saw the same TD status `0x18000007` as the guest virtual read. The final QEMU proof separately reported frame-list VA `0x22D26000`, base register and expected physical address both `0x3BB60000`, and QH physical address `0x3BB5F690` with frame-list link `0x3BB5F692`. Evidence is retained in `out/dm12-uhci-physical-audit/` and `out/dm12-read-only-proof-final/`.

Descriptor memory uses the normal coherent x86 mapping in this kernel. The diagnosis did not find a cacheability or aliasing defect, and no manual cache flush/invalidate was added. Narrow `mfence` barriers order descriptor publication before scheduling and CPU reads after controller progress; volatile descriptor reads prevent compiler reuse of asynchronous status. ACTIVE is set last after deterministic descriptor initialization.

The wait checks TD ACTIVE and all relevant error bits, samples USBSTS and FRNUM, uses bounded frame deadlines plus a bounded poll fallback, and reports controller halt/error explicitly. A terminated QH element is not treated as success by itself. If interrupts are enabled, the polling path yields with `sti; hlt` so timer and asynchronous controller/device work can progress; with interrupts disabled it uses `pause`. This replaced the prior tight million-iteration spin, which could starve the QEMU vCPU/event progress needed by the frame-scheduled controller. No arbitrary sleep is used as a completion signal.

The BOT bulk path also stopped recycling one TD and one DMA buffer packet by packet. It now submits bounded chains of up to 13 distinct TDs with distinct buffers, preserves endpoint toggles, reads each completed TD's actual length, and enables short-packet detection on IN transfers. This fixed the multi-packet BOT data/CSW path. Control Setup/Data/Status ordering remains intact; endpoint-zero toggles and SET_ADDRESS timing follow the status stage. UHCI actual-length decoding includes zero-length transfers.

During full GPT/VFS proof, a separate stack overwrite surfaced: optimized disassembly showed `parse_gpt_array` reserving about 10.8 KiB, `parse_gpt_header` about 8.2 KiB, plus a 4 KiB aligned sector scratch buffer and nested storage/UHCI calls on the 16 KiB bootstrap stack. A failing later CSW read showed the PCI BAR4 still at `0xC061` while cached `s_ioBase` and UHCI reads had become `0xA0A2`/`0xFFFF`; a bounded diagnostic guard was also overwritten below the old stack. The bootstrap stack is now 64 KiB. The final proof passed with the diagnostic guard removed.

## Focused tests and build

The shared storage suite now passes **474 checks, 0 failures**, up from 419. The 55 added UHCI logic checks cover descriptor size/alignment and initialization/reuse, DMA mapping overflow, token/link encodings, ACTIVE and short-packet flags, actual-length corner cases, frame-number wrap/deadlines, error decoding, QH advancement, and controller error classification. The 79 fake USB Mass Storage checks remain included.

`scripts/run-storage-manager-tests.ps1` produced `out/dm12-storage-tests.log`. The AMD64 kernel passed a clean ordinary `mingw32-make -C kernel ARCH=amd64 -j4` build after proof-specific builds; the UEFI x64 Release bootloader passed the VS 18 MSBuild build with toolset v145. Build logs are retained as `out/dm12-full-kernel-build-final.log` and `out/dm12-uefi-build.log`. The pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 profile check passed.

## Real QEMU USB acceptance

Five independent fresh boots (`out/dm12-read-only-proof-15/` through `out/dm12-read-only-proof-19/`) each passed all of these checks:

- PIIX3 UHCI initialized; the root port connected and enumerated.
- USB Mass Storage BOT/SCSI probing registered `BDEV_USB_MASS` through the production shared block registry.
- Device VID:PID `46F4:0001`, capacity `0x12C000` sectors, 512-byte logical sectors, removable QEMU model, read callback present, and write callback absent.
- GPT identified the `DM9 QEMU Proof` partition at LBA `0x800` with `0x12B7DF` sectors.
- The production partition view mounted FAT32 read-only at `/mnt/dm12-uhci-usb`; VFS enumerated the root, read `/mnt/dm12-uhci-usb/dm9/proof.bin`, matched the deterministic contents, and unmounted with mount/view counts restored.
- The full boot completed and the USB image hash remained unchanged.

The final source/layout proof in `out/dm12-read-only-proof-final/`, after diagnostic guard removal, independently passed the same registration, GPT, read-only mount, file-read, and unmount checks. The run manifest records `result=PASS shared-block-registration=yes read-only-fat32-mount=yes deterministic-file-read=yes clean-unmount=yes full-boot=yes usb-image-unchanged=yes`.

The USB probe reports `sync-cache=succeeded`. No `WRITE(10)`/`WRITE(16)` plus read-back test was run. Shared USB writes remain disabled, and the boot provenance remains `Unknown`; the existing destructive-operation eligibility gates therefore continue to reject this USB device.

## Removal, reinsertion, ATA regression, and limitations

Real QEMU hot-unplug/reinsert was not exercised. The retained fake USB lifecycle suite covers disconnect, stale view/handle rejection and cleanup, reinsertion with a new registration incarnation, different-device replacement in the same slot, and 100 attach/parse/mount/read/unmount/remove cycles. GUI interaction was not automated. No USB write or destructive operation was run.

The requested ATA regression passed once on a fresh disposable 600 MiB secondary QEMU IDE image, with the normal boot disk selected separately. First boot and restart rediscovery both parsed the target, mounted/read/unmounted the proof volume, and cleaned up mounts. An independent verifier passed protective MBR, primary/backup GPT CRCs and agreement, FAT32 BPB, FAT mirror, root directory, and deterministic proof-file checks. The runner reports `result=PASS tier=2 full-lifecycle-and-restart-rediscovery`; evidence is in `out/dm12-ata-regression/`. No physical host disk was passed to QEMU.

Remaining limitations are real QEMU hotplug/reinsert validation, GUI smoke proof, USB write/flush/read-back and interrupted-write behavior, and unknown USB boot provenance. USB writes stay disabled until those write-safety contracts receive their own runtime proof.

## Recommendation

Proceed to **DM13 — USB WRITE(10)/(16), SYNCHRONIZE CACHE, disconnect-during-write, and destructive-eligibility proof**. Keep shared USB writes disabled until real-QEMU write protection, CSW, read-back, durability, and disconnect recovery behavior are verified.

## Evidence index

- Baseline UHCI failure and QEMU trace: `out/dm12-repro-baseline/`
- Physical descriptor/status audit: `out/dm12-uhci-physical-audit/`
- Stack corruption and guard diagnosis: `out/dm12-read-only-proof-11/` and `out/dm12-read-only-proof-12/`
- Five repeated read-only proofs: `out/dm12-read-only-proof-15/` through `out/dm12-read-only-proof-19/`
- Final production-layout read-only proof: `out/dm12-read-only-proof-final/`
- Storage test and kernel/UEFI build logs: `out/dm12-storage-tests.log`, `out/dm12-full-kernel-build-final.log`, `out/dm12-uefi-build.log`
- ATA regression and independent image verification: `out/dm12-ata-regression/`
