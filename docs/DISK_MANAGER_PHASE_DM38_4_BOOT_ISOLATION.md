# DM38-4 — Physical boot isolation and AHCI DMA boundary

## Scope and disposition

DM38-4 adds an explicit UEFI storage boot profile so the desktop can start while kernel storage transports remain quarantined. It does not qualify physical AHCI, NVMe, ATA, USB storage, a physical USB boot, or SARAH_DT hardware. No physical disk was accessed and no physical USB was written.

DM38-4 reaches **Outcome A — `DM38_PHYSICAL_STAGE_A_READY` for the isolated software package and bounded operator procedure**. Both QAC virtual scenarios, the hosted regressions, kernel/loader/desktop builds, and source review passed. Physical Stage A is still owner-supervised and retains the disconnect-all-internal-drives prerequisite; no native boot is claimed. `.phase` remains `last_completed=DM37`, `next_expected=DM38`; `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain unchanged.

Starting source HEAD was `cb6c3af783ace3ba18b516d705e5e5c535863271` on `DISK_MANAGER_IMPROVEMENTS`; ending commit is recorded in the external artifact manifest and final report. The isolated package is staged at:

`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm38\dm38-4-physical-storage-isolation\`

The accepted DM38-3 package remains in its original directory and was read-only during this work.

## Early storage initialization trace

For the UEFI path, `guideXOSBootLoader/main.cpp` reads `boot-profile.cfg` from the ESP that launched the loader. The physical reconnaissance Release build requires the exact `storage_profile=physical-reconnaissance` value and stops before kernel handoff if the file is absent, unreadable, malformed, or selects another value. The optional normal loader accepts `storage_profile=normal`; an absent file retains the historical normal default. The selection is copied through `BootInfo.Flags` bits 3–4 without changing the BootInfo v3 layout. The loader emits `[BOOT-PROFILE] storage=...`.

`kernel_main` validates the boot environment, derives the profile, and prints `[STORAGE-BOOT] ...` before framebuffer or storage startup. The software-only `set_kernel_physical_base` calls do not access controller hardware. Framebuffer/GPU and audio initialization remain available. Later, the actual VirtIO block, ATA, AHCI, NVMe, and UHCI/USB Mass Storage startup sequence is inside `storage_boot::initialize_controllers_if_allowed`. Only the historical default and explicit normal profile enter its callback. Reconnaissance and invalid profiles return without entering it. Persistent mount and partition/filesystem discovery are separately gated; QEMU proof callbacks and the optional display-configuration storage path use the same admission rule.

With normal storage enabled, the startup callback first probes VirtIO block and ATA. ATA scans PCI IDE class and performs ATA PIO IDENTIFY commands on discovered or fallback legacy channels. AHCI then scans PCI class `01:06:01`, reads BAR5, maps the ABAR, enables PCI Memory Space and Bus Master, may request BIOS/OS handoff, resets and enables the HBA, starts candidate port engines, and issues IDENTIFY DMA to direct SATA disks. NVMe scans PCI class `01:08:02`, enables memory decoding and bus mastering, initializes/resets the controller, sets up queues, and issues Identify Controller/Namespace DMA. UHCI initialization and USB enumeration then dispatch Mass Storage to BOT. Automatic persistent mount follows controller startup and may read partition and filesystem metadata from registered devices.

The first unqualified storage DMA point is AHCI controller/port startup and IDENTIFY from `ahci::init`; PCI/ABAR operations and engine reset/start precede the IDENTIFY command itself. NVMe queue and Identify DMA follows later. Ordinary GUI read-only policy does not prevent these early probes. Reconnaissance keeps these calls unreachable from this boot path.

## AHCI DMA and remapping assessment

`kernel/core/ahci.cpp` converts a kernel virtual pointer with `KernelPhysicalBase + (VA - 0x100000)`. This is a boot-image linear-offset assumption, not a page-table walk and not an IOMMU map. The driver uses statically allocated per-device arrays: command lists (1024-byte aligned), received-FIS buffers (256-byte aligned), command tables (128-byte aligned), 128 KiB data buffers and 512-byte IDENTIFY copies (4096-byte aligned). Command lists, FIS buffers, command tables, and data buffers live for the driver lifetime; I/O is serialized. The driver checks address ranges against a 52-bit limit and the controller's 32-bit/64-bit DMA capability, verifies the applicable alignments, and constructs one bounded PRDT up to 128 KiB. It has polling timeouts (up to 30 seconds for command/data, 60 seconds for Flush, 5 seconds for port operations, 10 seconds for controller operations), barriers, stop/restart recovery, and failure/offline handling. x86 coherent memory is assumed; the kernel exposes no DMA cache-maintenance contract.

These bounds do not prove that the computed addresses are device-visible. There is no OS IOMMU/VT-d DMA mapping API or driver mapping lifecycle in this path. CPU page tables and `KernelPhysicalBase` do not establish a device translation. SARAH_DT's active VT-d, DMA remapping, Kernel DMA Protection, identity-map, and firmware handoff state are unknown. Therefore:

| DMA state | Finding |
|---|---|
| No remapping | The linear physical-address assumption still needs proof against the active RAM map and controller constraints. |
| Identity-mapped DMA | No explicit OS setup or verified contract establishes this assumption. |
| Active nonidentity translation | Current driver has no mapping API and may submit addresses invalid for the device. |
| Actual SARAH_DT state | Unknown; firmware settings alone would not prove the runtime translation. |

The physical controller drivers must remain uninitialized until a separate DMA mapping contract and target-specific qualification are complete. Do not disable VT-d or DMA protections to accommodate the current driver.

## Profile semantics and test evidence

`boot-profile.cfg` accepts exactly `storage_profile=normal` or `storage_profile=physical-reconnaissance`, with at most one ordinary CR/LF line ending. Unknown values, empty content, whitespace variants, extra content, and file errors become invalid. Invalid and reconnaissance profiles quarantine controller startup and automatic mounts. The recon-required loader build additionally refuses to hand off on any missing/invalid profile. Multiboot and legacy UEFI packages without profile bits retain the old normal behavior.

The host suite exercises the same `storage_boot_policy.h` callback wrapper used by `kernel_main`: on a quarantined profile, fake controller initializers are not called, no block registrations or fake reads/writes/Flush/Discard/repair occur, partition/filesystem discovery and auto-mount are skipped, and a normal profile still admits the ordinary fake transport. It also checks invalid profile encoding and absence of stale device identity. The hosted qualification run passed **1,536 storage-manager checks, 0 failures** (1,523 retained baseline checks plus 13 profile checks), along with the test script's DM34, DM35, DM36 integrity, DM37 Gate G, GPT/MBR, USB BOT, input-context, PS/2 FIFO, compositor, trace-classifier and fixture-verifier regressions. The run completed with exit code 0. The existing DM30 fixture verifier took 118.459 seconds. This is a fresh run, not a claim based only on DM38-3 historical evidence.

## Build and package

- AMD64 freestanding kernel: PASS. Built with `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; outputs and log are under `kernel-build\` and `build-logs\`.
- UEFI x64 Release, recon-required profile: PASS. `RequirePhysicalStorageReconProfile=true`.
- UEFI x64 Release, normal profile: PASS. `RequirePhysicalStorageReconProfile=false`.
- `build.bat`: PASS. The generated desktop executable hash was `6C7D7E1F120435097E86A09367A66417D04B16212DF8C2AADE52CF9D024BD0AE`; the pre-existing `guideXOSServer.exe` was restored and its original SHA-256 `C0842C7249AA54EC0B2FB3B69E6B48093E1A3E8959E4DC137DF26732D4C29B84` verified unchanged.
- `git diff --check`: PASS at the time of source validation; it is rerun before commit.

The physical ESP contains the recon-required removable loader, current AMD64 ELF, required ramdisk, runtime assets, and `boot-profile.cfg`. A separate private normal-profile ESP is reserved for Scenario B. Both are distinct from the accepted DM38-3 payload. `boot-manifest\esp-sha256.csv`, package validation, and artifact manifest record every payload path, size, and SHA-256. No raw physical-disk image or physical drive is part of the package.

| Physical ESP payload | Bytes | SHA-256 |
|---|---:|---|
| `EFI/BOOT/BOOTX64.EFI` (x64 EFI Application) | 61,440 | `127C9AB14DAEC333267D98698F4873EDFCFB872F989197EA39FCC708F42F5546` |
| `kernel.elf` (ELF64 little-endian AMD64) | 3,190,216 | `06378D78B226685022BCB0FD47DB2D81D7530E95955DA7DA02772954E2A10D5D` |
| `ramdisk.img` | 67,108,864 | `FD7BC1B738F2ED7E7436407EF1D8892F7642E7CEBFC0EA0AB42E7E77BCB6DE24` |
| `boot-profile.cfg` | 39 | `927E2DADDA3A8527DACEB52ACD4AC80602FBF0A87D564ADF467772861F9A825E` |

The physical root config contents are exactly `storage_profile=physical-reconnaissance`. The complete physical ESP is 13 files / 72,681,794 bytes. The separate normal QAC ESP is 13 files / 72,681,764 bytes and uses normal-loader SHA-256 `F145BAE33C1D4ECAEB3A735CB83687D573FE5E961C132B8D89773D934915A58C`; its root config is `storage_profile=normal` (SHA-256 `4089C58E43D420874AEF76A330E6ED1730D18B9FE1FAA65A1BEE6B2387E9A012`). Both loaders verify as PE machine `0x8664`, PE32+, EFI Application subsystem `10`; the kernel verifies as ELF class 2, little-endian data encoding 1, AMD64 `e_machine=62`. The read-only OVMF image is 3,653,632 bytes, SHA-256 `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`. The separate kernel PE artifact is 3,322,044 bytes, SHA-256 `8688394822CA1C7A2B757D01EBC1E5618F03B8126537F66CBAE382F50261D0E8`.

## QAC Scenario A — isolated boot

The final exact staged package ran under QAC 2.0.0 / QEMU 11.0.0 exclusive acceptance reservation `855f6bbcfda346c399e0fae7a79000dc`, QEMU PID `10808`; the earlier submission waited behind an unrelated AArch64 run, then received the slot. The reservation exited with code 0 after controlled QMP `quit` and was released. The guest used read-only OVMF pflash and a disposable private copy of the staged ESP as a directory-backed FAT IDE device. `usb=off`; no AHCI, NVMe, USB storage, or host physical disk was attached.

The trace showed the required boot profile, kernel entry, `[STORAGE-BOOT] profile=physical-reconnaissance controller-discovery=QUARANTINED`, framebuffer, desktop, the explicit transport-startup skip, automatic-mount skip, and `[NAVIGATOR-SMOKE] result=PASS`. No AHCI/NVMe/ATA/USB transport initialization markers appeared. QMP reported the IDE ESP's block counters unchanged across a 5-second idle interval: 70,244,864 read bytes / 568 reads; 10,420,224 write bytes / 222 writes; zero Flush and zero unmap operations. This cumulative activity was on the private virtual boot ESP before the idle sample and includes UEFI/loader boot activity; it is not physical storage evidence. The counters remained unchanged during the interval.

QMP `quit` caused a host-controlled SHUTDOWN event; QEMU exited 0 and QAC released the reservation. This is controlled QEMU process shutdown, not proof of a production Shell shutdown. The post-run census showed zero managed sessions/reservations and zero QEMU processes. All 13 private ESP files matched their staged hashes afterward. Evidence is in `qemu-smoke\scenario-a-final-*`.

## QAC Scenario B — normal storage regression

The exact normal-profile QAC package used the separate `qemu-smoke\normal-esp` and a private, read-only 8 MiB USB BOT image. QAC 2.0.0 granted exclusive acceptance reservation `5161e9fd6a80461d9966fbbcc394f023`, QEMU PID `19016`; it exited with code 0 after controlled QMP `quit`, and QAC released the reservation. The fresh preflight census had zero QEMU processes/reservations. The earlier unrelated AArch64 reservation had completed before this fresh final preflight; no foreign process was modified.

Serial showed `[BOOT-PROFILE] storage=normal`, controller discovery enabled, ATA found the private directory-backed IDE boot ESP, UHCI enumerated the attached virtual USB device, and `[KERNEL] USB Mass Storage devices: 01`. The normal path also mounted the private boot ESP as `ata0m` and completed Navigator smoke. QEMU exposed no AHCI controller; this scenario qualifies normal ATA/USB BOT startup, not AHCI hardware. QMP confirmed `usbtest` was `ro=true`; its backing image saw 21,504 read bytes / 27 read operations, zero write bytes / operations, one Flush operation, and zero unmap operations. The 8 MiB image hash remained `2DAEB1F36095B44B318410B3F4E8B5D989DCC7BB023D1426C492DAB0A3053E74` before and after. All 13 private normal ESP files matched the staged manifest after exit. QAC's released reservation record and postflight census show zero active reservations/sessions/QEMU processes. Evidence is in `qemu-smoke\scenario-b-final-*`.

## SARAH_DT input and physical operator boundary

The owner-reported target is an HP ProDesk 600 G6 Microtower with an Intel i5-10500, reported AHCI `8086:06D2`, Intel xHCI `8086:06ED`, two monitors, and Windows Seagate system HDD. A 256 GB NVMe may be present; a separate 1 TB NVMe has uncertain reliability. Account for every installed internal drive rather than inferring current hardware from this inventory.

The AMD64 USB path initializes UHCI and its device enumeration probes USB Mass Storage BOT. There is no general xHCI implementation for the reported controller, and the USB enumeration path does not call the existing USB HID probe. Kernel PS/2 keyboard and mouse initialization remains available, but working PS/2 devices on SARAH_DT are not established. Firmware USB keyboard access, bootloader operation, kernel keyboard/mouse operation, and desktop interaction are separate observations. A visible desktop with no usable input remains a valid Stage A result; do not operate Disk Manager without input.

The physical package does not authorize boot with internal drives connected. The bounded owner procedure still requires full power-off and physical disconnection of the Windows HDD and every installed NVMe/internal storage device, then one-time UEFI USB selection. Use only an owner-identified expendable FAT32 USB volume, confirm manufacturer/model/serial or unique ID, capacity and `BusType=USB`, and obtain explicit approval before replacing that exact volume's contents. Never select by disk number, drive letter, capacity alone, or enumeration order. Do not use `diskpart clean`, `Clear-Disk`, automatic disk selection, or a physical target drive. No physical USB write, SARAH_DT boot, firmware change, or physical storage probe was performed in DM38-4.

Stage B remains blocked until the device-visible DMA mapping contract, actual runtime remapping state, controller/target identity, read-only command admission, and supervised diagnostic/input route are independently qualified. No physical read, write, Flush, Discard, repair, or erase is authorized by this report.

## Git and phase state

The coherent source, host tests, and documentation are committed on `DISK_MANAGER_IMPROVEMENTS`; the final commit ID and optional one-attempt push result are recorded in the external artifact manifest. `.phase` is intentionally unchanged at DM37→DM38. DM39 is not started.
