# DM38 — Physical Storage Readiness and Non-Destructive Qualification

**Latest disposition (2026-11-05): Outcome B — `DM38_INVENTORY_AWAITING_OWNER`.**

The DM38-1 disposition and evidence below are retained as the prior
qualification record. The DM38-2 continuation at the end of this document
supersedes its forward-looking recommendation and records the current source
audit and inventory boundary.

The production storage architecture has been audited, the identity and explicit
read-only mount gaps found in that audit have been corrected, the hosted
regression suite and requested builds pass, and current-source AHCI and UHCI
USB BOT guest enumeration pass under QAC. Physical qualification remains open:
SARAH_DT has not been inventoried or booted in this continuation, and the
AMD64 kernel has no xHCI host-controller driver. The reported HP 600 G6 MT
model and Intel 10th-generation-era platform do not establish its exact
controller, firmware mode, port mapping, or attached devices. No physical disk
was connected to or modified by this work.

This is DM38-1 readiness work, not full DM38 acceptance. No physical write,
Flush, formatting, repair, partition operation, or physical filesystem mount
was performed. The DM37 acceptance limits remain in force. The phase marker is
unchanged.

## Repository baseline and scope

- Repository: guideXOS Server Disk Manager Improvements.
- Branch: DISK_MANAGER_IMPROVEMENTS.
- Starting HEAD: 560f598141ea2cb6b61547a8f50bfb5aac163cf7
- Starting worktree and index: clean.
- Starting and ending phase marker: last_completed=DM37; next_expected=DM38.
- NVME_WRITE_PROVEN and NVME_FLUSH_PROVEN remain 0.
- No branch, worktree, reset, clean, stash, merge, rebase, cherry-pick, or
  phase-marker change was made.
- The desktop build temporarily replaced the existing ignored executable.
  Its original SHA-256 was restored after the DM38 output was captured.

The final full HEAD is recorded in the closeout response and in the
E-drive artifact folder because this report is included in the commit whose
hash it would otherwise need to name.

## Storage support matrix

“Hosted” means production-linked tests over host block-device fixtures.
“Guest” refers only to virtual hardware. No row establishes physical-hardware
qualification.

| Capability | Production source | Hosted evidence | QEMU guest evidence | Physical status |
|---|---|---|---|---|
| Legacy ATA/IDE PIO | Implemented. PCI IDE-class scan plus legacy-port fallback. | Covered by storage suite. | Historical QEMU legacy-IDE regressions. | Unknown; requires IDE-compatible presentation. |
| SATA/AHCI | Implemented directly through PCI class 01/subclass 06/progIF 01, ABAR MMIO, IDENTIFY, DMA and port commands. It does not depend on an IDE-compatible mode. | Covered by production-linked storage tests. | Historical Q35 ICH9 AHCI regressions; DM38-1 candidate kernel registered both the boot disk and a separate AHCI target under QAC. | Unverified on SARAH_DT. |
| USB Mass Storage BOT/SCSI | Implemented in the USB storage driver, including Inquiry, geometry, READ/WRITE, and Sync Cache handling. | USB Mass Storage checks included. | Historical and current QAC PIIX3 UHCI BOT enumeration; DM38-1 USB fixture stayed byte-identical. | Unverified; BOT transport is not a host-controller driver. |
| UHCI | Implemented for x86 and AMD64; root ports only, without hub topology support. | USB transport tests. | PIIX3 UHCI BOT guest evidence. | Unverified. |
| OHCI | Implemented in the RISC-V64 USB host-controller backend. It is not the AMD64 backend. | Architecture-specific coverage only. | No DM38-1 guest claim. | Unverified. |
| EHCI | No production implementation found. | None. | None. | Unsupported. |
| xHCI | No general xHCI implementation in the AMD64 target. An ARM DWC OTG backend is architecture-specific and does not provide a general AMD64 xHCI path. | None for AMD64 xHCI. | QEMU UHCI results do not cover xHCI. | Likely qualification blocker if SARAH_DT exposes only xHCI USB ports; actual controller remains unknown. |
| NVMe enumeration and reads | PCIe NVMe discovery, Identify, namespace registration and read path are implemented. | Covered by storage suite. | Historical QEMU NVMe read/enumeration proof. | Unverified. |
| NVMe writes and Flush | Command code exists, but common write and Flush admission remains disabled by the default proof flags. | The disabled policy is tested. | No DM38-1 NVMe-write or Flush qualification. | Not authorized or qualified. |
| GPT and MBR discovery | Production parsers support both; discovery does not imply repair. | GPT/MBR tests and verifier. | Historical QEMU fixtures. | Unverified. |
| FAT32 mount and read | Production FAT32 mount/read path. A partition mount can now explicitly request read-only mode. | Read-only and writable mount regressions. | Historical read-only USB and AHCI guest cases; DM38-1 guests only enumerated devices. | Unverified. |
| Registration identity and generation | Block registrations use non-reused registration IDs and a registry generation. USB removal/offline and replacement use new registration identity. | Identity, replacement and generation regressions. | Historical USB removal/replacement guest cases. | Unverified. |
| Runtime device discovery | USB storage removal/offline handling exists. ATA, AHCI and NVMe do not have runtime hotplug rescan support. | USB lifecycle coverage. | Historical USB lifecycle cases. | No physical hotplug qualification. |
| UEFI boot-source protection | BootInfo device-path matching carries controller and device location into storage boot provenance. Unknown provenance is fail-closed for destructive admission. | Boot/root/system protection regressions. | Historical UEFI guest checks. | Source-backed only; physical path matching unverified. |

The production Disk Manager inventory is the bare-metal kernel application in
kernel/core/kernel_apps.cpp. The repository-root disk_manager.cpp is a hosted
image-preview application; it is not physical-drive inventory.

## Controller and device paths

### ATA and SATA/AHCI

The ATA driver probes PCI IDE-compatible controllers and legacy channel ports,
then issues PIO commands. This path applies only when firmware or the
controller exposes IDE-compatible presentation; it is not generic access to a
modern AHCI controller.

The separate AHCI driver scans PCI for class 01/subclass 06/progIF 01, maps
BAR5 as the AHCI ABAR, initializes controller ports and performs IDENTIFY and
block I/O. It has one serialized command slot and uses DMA PRDTs. The current
source includes write and Flush callbacks, but physical durability has not
been tested. A firmware setting that places a controller in RAID/RST or some
other unsupported presentation cannot be assumed to work; SARAH_DT’s setting
must be read from its firmware and Windows inventory.

### USB Mass Storage

The USB BOT/SCSI storage driver depends on an implemented USB host controller.
The AMD64 backend is UHCI, with two root-hub ports and no hub topology support.
The QEMU BOT result is specifically a PIIX3 UHCI result. It says nothing about
real xHCI, EHCI, or the electrical behavior of a physical USB port.

### NVMe

The NVMe path enumerates PCIe controllers and namespaces and can read
namespaces. Write and Flush opcodes are present in source but remain gated off
in the default build and common block registration. The two proof flags remain
disabled; no capability was promoted based on source presence or QEMU alone.

### Display and input

The UEFI framebuffer/GOP path and PS/2 keyboard and mouse paths exist.
AMD64 USB host support is UHCI-only, so a USB keyboard, mouse, boot device or
storage device attached through an xHCI-only controller is not assured to
work. The renderer retains the primary display target; multi-monitor behavior
is outside this storage continuation. None of these paths was exercised on
SARAH_DT.

## Device identity, replacement and protected media

The block-device capability snapshot now reports the transport-reported model,
serial and firmware revision when available; logical and physical sector
geometry; PCI segment/BDF; ATA channel and target; AHCI port; USB VID/PID,
port, interface and LUN; and NVMe NSID, NGUID and EUI-64 fields. Unsupported
or unavailable values are left unknown rather than fabricated.

Destructive target snapshots include the registry generation and non-reused
registration ID, transport and driver slot, capacity and logical/physical
geometry, model/serial/firmware, controller and port location, USB or NVMe
namespace identity, and captured boot provenance. Revalidation compares the
captured fields before admission. Reusing a registry slot or controller port
does not make a removed registration valid again. The UI must refresh and
capture a new target after device replacement.

Identity confidence is stable only when a printable media serial of at least
four characters with an alphanumeric character is present, or for NVMe when a
nonzero NGUID or EUI-64 is available. A writable device without one is
classified uncertain and fails destructive preflight. Capacity or a registry
index alone is not a media identity. Whole-disk erase also requires a usable
serial that is unique among enumerated disks.

Partition discovery preserves MBR entry/extent information and GPT disk and
partition GUIDs. Partition block views retain the exact parent registration
and partition extent. VFS mount snapshots retain a non-reused mount generation,
registry identity, partition identity and view generation. These identities
serve different scopes: device replacement, partition selection and a live
mount are independently revalidated.

UEFI BootInfo matching uses transport-specific path elements, including PCI
location and AHCI port, ATA channel/target, NVMe BDF/NSID, or the supported
direct USB path. A missing, malformed, legacy or unmatched path leaves boot
provenance unknown. Destructive admission rejects a known boot backing device
and unknown boot identity. Root, /boot and /system mounts and active mount
dependencies retain the existing DM37 protection. No boot protection was
disabled to expose a device.

## Read-only mount correction

The production VFS partition mount API now accepts an explicit read-only
request. Disk Manager opens the mount dialog in read-only mode by default. The
R key toggles to writable mode only when the block device has a write callback
and its durable-write capability is supported. Read-only and writable
partition views are not reused across modes, so a read-only request cannot
inherit a writable cached view.

The production-linked regression mounts a GPT FAT32 partition read-only,
attempts a file creation, and verifies VFS returns read-only with zero block
writes and zero Flush calls. The existing writable mount path remains
available when explicitly selected and when capability policy allows it.
GPT/MBR discovery does not invoke repair. No FAT32 automatic repair,
initialization, formatting, TRIM/Discard, or writeback is part of a
read-only mount request.

## Hosted regression and build results

The complete production-linked hosted storage run passed:

- Storage manager suite: 1,523 checks, 0 failures. The retained DM37 closeout
  baseline remains 1,521 checks; DM38 adds two meaningful assertions.
- USB Mass Storage: 249 checks included in the suite.
- DM34 wipe: 17/17; whole-disk erase: 34/34.
- DM35 surface verification engine: 48/48; GUI: 34/34.
- DM36 FAT32 integrity metadata: 259/259; GUI: 31/31.
- Wipe UI policy: 32 checks; whole-disk erase UI: 39 checks.
- DM37 Gate G failure cases: 11 checks, 0 failures.
- DM37 input-context, focus, widget activation and PS/2 queue regressions:
  PASS, including 24 input checks and 9 PS/2 queue checks.
- USB trace classifier: 7/7.
- GPT verifier: 1/1.
- DM30 fixture verifier: 1/1.

The suite log is stored at
E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm38\hosted-regressions.log.
Existing compiler warnings were reported in unrelated kernel GUI and
filesystem helper code; there were no test failures.

Builds:

| Build | Result | Artifact | SHA-256 |
|---|---|---|---|
| AMD64 freestanding kernel | PASS | kernel-amd64/bin/kernel.elf | 085902C6D38C1DAD010EE567225F56B58307FB94AA924EAF50E2B844A94494BD |
| AMD64 PE kernel image | PASS | kernel-amd64/bin/kernel.pe | 957AAB21FB828606FFD8DAA3E306C3771BECD750EA164FAE1F4A7E44C82581C0 |
| build.bat hosted desktop | PASS | guideXOSServer.exe.dm38-build | DD86510BC442C96C6D0D6955C4561DAED165D1DE3DD06A094E7D1CD74F56407F |
| UEFI x64 Release bootloader | PASS | uefi-x64-release/image/EFI/BOOT/BOOTX64.EFI | 330911113B0E37C0F9DCACC53AF8844B0A76FB883F101A447D028BCAAF439AEC |

Build logs and artifacts are under
E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm38.
The repository's existing guideXOSServer.exe was restored with its original
SHA-256 C0842C7249AA54EC0B2FB3B69E6B48093E1A3E8959E4DC137DF26732D4C29B84.

## QAC guest qualification

QAC 2.0.0 was used with direct QEMU children for all guest runs. Fresh QAC
status/list and Windows process censuses showed no active QEMU process before
each launch; the final QAC state had no managed session or reservation. No
foreign QEMU process, QMP endpoint or sibling project was contacted. QEMU
guest media were disposable copies under the E: artifact directory.

During command-line preparation, a QEMU device-topology probe was briefly
invoked outside QAC with the guest CPU paused using the -S option. It was not
guest evidence, no guideXOS kernel ran, and it was gone before the fresh QAC
census. All guest boots used direct QAC-managed QEMU children.

The current-source AMD64 kernel booted under QEMU 11.0.0. An ICH9 AHCI guest
registered both the UEFI boot disk and a separate 600 MiB GPT/FAT32 target at
AHCI port 1, with test serial DM38AHCI0001. The target's SHA-256 before and
after was
BE4466C03E52D0D05EE5A60FDC8BB540EFC5C665DA3218857CB5283450D03F25.
QAC observed the child exit code 0 after the owned guest was closed with QMP
quit.

A separate PIIX3 UHCI guest registered a BOT/SCSI disk with read-only access
reported by the kernel. The USB test fixture's SHA-256 before and after was
the same
BE4466C03E52D0D05EE5A60FDC8BB540EFC5C665DA3218857CB5283450D03F25.
QAC observed child exit code 0 after QMP quit. QEMU printed an interrupt
vector warning during startup in the successful runs; device registration,
QMP shutdown and fixture integrity checks completed successfully.

The first two AHCI launches were setup failures before guest boot. QEMU refused
to attach an IDE disk backed by a read-only block node, first with the default
cache setting and then with write-cache disabled. The first failed reservation
also reported an incomplete QEMU process census after launch; QAC released
that child, and a subsequent status/list census and direct process census
showed an empty lane before retry. The successful AHCI run used a writable
disposable clone, requested no guest write operation, and verified its full
image hash unchanged. These setup attempts produced no guest storage I/O and
do not count as guest PASS evidence.

QAC artifacts, serial logs, stdout/stderr and image copies are in
E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm38\qac-guest.
These are driver enumeration/readiness smoke tests. They do not qualify
physical devices, physical writes, physical Flush, FAT32 file workflows,
removal behavior on a physical controller, or clean in-guest shutdown.

## SARAH_DT readiness and operator checklist

Owner-provided context only: SARAH_DT is reported as an HP 600 G6 MT with
hardware similar to LEON2_RDT and an Intel 10th-generation-era platform. The
owner reports multiple monitors, several USB ports (one damaged), and
disposable spare drives. The exact CPU, chipset, controller identifiers,
firmware settings, ports, disk identities, and working input device have not
been verified. Record observed values; do not fill unknowns from a model
guess.

### Read-only Windows inventory

Run these inventory-only PowerShell commands on SARAH_DT and preserve the
output with the machine inventory:

    Get-CimInstance Win32_ComputerSystem | Format-List Manufacturer,Model,SystemType
    Get-CimInstance Win32_BIOS | Format-List SMBIOSBIOSVersion,ReleaseDate,SerialNumber
    Get-CimInstance Win32_Processor | Format-List Name,Manufacturer,ProcessorId,SocketDesignation
    Get-CimInstance Win32_BaseBoard | Format-List Manufacturer,Product,Version,SerialNumber
    Get-CimInstance Win32_SCSIController | Format-List Name,PNPDeviceID,Status
    Get-CimInstance Win32_IDEController | Format-List Name,PNPDeviceID,Status
    Get-PnpDevice -PresentOnly | Where-Object { $_.Class -in @('SCSIAdapter','USB') } | Select-Object Class,FriendlyName,InstanceId,Status
    Get-Disk | Format-Table Number,FriendlyName,SerialNumber,BusType,PartitionStyle,OperationalStatus,IsBoot,IsSystem,LogicalSectorSize,PhysicalSectorSize,Size -Auto
    Get-CimInstance Win32_DiskDrive | Format-Table Index,Model,SerialNumber,InterfaceType,PNPDeviceID,Status,Size,BytesPerSector -Auto
    Get-Partition -DiskNumber N | Format-Table PartitionNumber,DriveLetter,Type,Size,Offset -Auto

Replace N only with a disk number from the preceding inventory. These commands
query Windows inventory and do not initialize, repair, format, repartition or
write to disks. Do not run Set-Disk, Clear-Disk, Initialize-Disk,
Set-Partition, Format-Volume, chkdsk with repair options, Repair-Volume,
diskpart write commands, or firmware-setting changes as part of this
inventory.

### Record before any physical read-only pilot

- [ ] Exact machine make/model, chassis serial and owner confirmation.
- [ ] CPU model and motherboard/chipset identifiers.
- [ ] BIOS/UEFI version and date; current UEFI/Legacy and Secure Boot state.
- [ ] Storage controller PNP/PCI IDs, BDF if available, installed driver, and
  current SATA presentation (AHCI, RAID/RST, IDE-compatible, or Unknown).
- [ ] Physical SATA connectors available and their board labels; current cable
  connections; whether each port is enabled.
- [ ] USB controller PNP/PCI IDs and the exact physical port planned for the
  boot medium and any USB storage. Avoid the owner-reported damaged port.
- [ ] Current Windows boot/system disks and their model, serial and capacity.
- [ ] Intended guideXOS boot device, its own model/serial, physical connection,
  and confirmation that it is separate from the disposable target.
- [ ] Intended test disk model, exact serial, capacity, logical/physical sector
  sizes, current partition style, and written owner confirmation that its
  contents are expendable.
- [ ] Keyboard and mouse connection type and port; whether PS/2 devices/ports
  are available; display output and the monitor used for the first boot.
- [ ] Existing firmware, boot, display, keyboard or mouse limitations.
- [ ] A photograph or written record of SATA data/power and USB connections.

### Operator procedure: first physical inspection

Operator actions:

1. Confirm the machine and every item above. Do not infer the target from disk
   order, capacity, or “first non-boot disk.”
2. Power off before installing or swapping a SATA disk. Connect only the
   identified disposable target and the separate verified guideXOS boot
   medium. Record each port and cable.
3. Boot the verified guideXOS image. Do not change another machine's Windows
   boot setup or alter firmware settings to force an unsupported controller.
4. Confirm the desktop, display and supported keyboard/mouse input work. If
   input is unavailable, stop and preserve serial/firmware observations; do
   not use a hidden storage command.
5. Open Disk Manager and match the target by trustworthy displayed identity
   fields: model/serial plus controller and port location where available.
   Confirm the boot disk and any system disk are separately protected. If the
   target identity is Unknown or uncertain, stop.
6. Inspect GPT/MBR and partition layout without invoking repair. If the
   partition scheme or filesystem is unsupported, record that fact and stop.
7. If a supported FAT32 partition is present, select the explicit Read Only
   mount mode. Read only a known, non-sensitive test file supplied by the
   owner. Do not attempt initialization, format, repair, file creation,
   deletion, write, discard or Flush qualification.
8. Close every file and directory reference, perform the normal Dismount
   action, and confirm the UI reports the volume unmounted without an error.
9. Shut down through the normal guideXOS UI. Do not force-disconnect the disk
   while mounted or while shutdown is active.
10. After power-off, preserve guest serial logs, photographs, UI observations
    and the Windows inventory. Recheck model/serial, capacity, partition
    style and partition layout. Compare available read-only media evidence;
    record what was and was not independently compared.

Codex actions after the owner supplies the inventory and test evidence:

- Confirm controller and device identifiers against production driver paths.
- Review logs for exact device identity, read-only mount state, read errors,
  Dismount state and boot/root/system protection.
- Compare the captured target identity and available media evidence.
- Report pass, fail, unsupported or unknown for each attempted step.
- Do not authorize or run a physical write as part of this procedure.

If the disk lacks a supported partition table or FAT32 filesystem, do not
format it. A failed xHCI USB boot or unavailable input is a hardware-readiness
result, not permission to change firmware or install a driver.

## Physical write authorization and durability work

DM38-1 does not authorize physical writes. Any later physical write test needs
a separate explicit owner decision naming all of:

- Exact host machine.
- Exact target disk model, serial or sufficient stable identifier, capacity
  and physical connection.
- Confirmation that the contents are expendable.
- Exact operations to perform and the expected changes to its partition table,
  filesystem and files.
- Exact boot medium and all non-target boot/system disks that must remain
  protected.

Do not select a target automatically from an attached-device list. General
availability of spare drives is not operation-specific authorization.

Before qualifying physical ATA/SATA writes, prepare an isolated disposable
fixture and validate error handling in hosted and QEMU tests. A future
supervised test must record identity and geometry, use a narrowly scoped
owner-approved data region, verify write completion and controller/transport
ordering, issue the documented Flush and require a successful result, perform
normal Dismount and final Flush, shut down normally, remove/re-enumerate only
after power-off, verify the same media identity, explicitly Remount, and make
a fresh read whose bytes match the expected test content. Failed write,
failed Flush, replacement, removal and final-Flush errors must remain visible
and fail closed. A successful physical read does not qualify a write or Flush.

NVMe write and Flush require their independent proof gates to remain disabled
until the NVMe command path, persistence behavior, failure handling, removal,
identity and remount verification are separately qualified. No NVMe flags
changed in DM38-1.

## Proposed DM38 acceptance roadmap

These gates remain proposed and open for owner review. DM38-1 does not close
any physical gate.

| Gate | Objective | DM38-1 status |
|---|---|---|
| A — Physical controller inventory | Identify and report actual machine/controller capabilities. | Open: SARAH_DT inventory not supplied. |
| B — Physical device identity | Distinguish attached drives and reject uncertain identity. | Source and hosted coverage improved; physical proof open. |
| C — Boot and system protection | Keep boot/root/system and uncertain-provenance devices protected. | Existing source/hosted safeguards retained; physical proof open. |
| D — Non-destructive partition discovery | Read actual partition tables/filesystems without modification. | Hosted and virtual evidence only; physical proof open. |
| E — Read-only physical FAT32 workflow | Explicit read-only mount, read, reference close and Dismount. | Not performed on hardware. |
| F — Supervised writable qualification | Explicitly authorized write, final Flush, Dismount, Remount and persistence proof. | Not authorized; open. |
| G — Failure and replacement safety | Preserve identity/error behavior under physical or representative failures. | Hosted/QEMU coverage only; physical proof open. |
| H — Evidence and operator safety | Retain target identity, provenance, integrity evidence and authorization. | Procedure prepared; physical evidence open. |

DM37 remains accepted with the documented qualification limits for Gates B,
C, G, H and I and the historical Gate A QEMU ownership/shutdown limitation.
Those are evidence constraints, not guest PASS results; this report does not
reopen or expand DM37 acceptance.

## DM38-1 recommendation for the next continuation

First collect and review SARAH_DT's read-only Windows and firmware inventory,
including exact SATA and USB controller IDs, current SATA mode, boot-medium
path, and keyboard/mouse connection. Then make an explicit readiness decision
for a supervised read-only pilot. If the AMD64 USB ports are xHCI-only and
USB is needed for boot, input or storage, scope a separate xHCI implementation
milestone before physical testing through those ports. If a verified AHCI
SATA path and supported input/boot path exist, proceed only with the
operator-controlled read-only checklist above. Physical writes and NVMe
write/Flush qualification remain separate decisions.

## DM38-2 — Inventory access decision and physical compatibility audit

**Outcome B — `DM38_INVENTORY_AWAITING_OWNER`.** This continuation began at
full HEAD `049cca40f2c793d3f93a8f7b4790d88f509da2e0` on
`DISK_MANAGER_IMPROVEMENTS`, with a clean worktree and index. `.phase` remains
`last_completed=DM37`, `next_expected=DM38`. The continuation makes no source
or test-fixture changes, performs no physical-device access, and makes no
firmware, boot, partition or filesystem changes.

### Inventory access boundary and current evidence

SARAH_DT is treated as the separate physical machine identified in the
owner-provided request. No authorized remote session, SARAH_DT inventory
attachment, Windows export, firmware capture, or connected service exposing
that machine was available in this checkout. The repository contains the
earlier report of an HP 600 G6 MT and an Intel 10th-generation-era platform;
those are unverified owner context and do not establish the installed
controller, firmware mode, port mapping, input devices or attached disks. No
network discovery, remote administration, firewall change or agent
installation was attempted.

| Inventory item | DM38-2 finding |
|---|---|
| Exact manufacturer/model, CPU, board/chipset, firmware version/date | Not verified; owner collection required. |
| UEFI/Legacy mode and Secure Boot | Not verified; owner collection required. |
| Storage PCI vendor/device/class/subclass/progIF, BDF, driver and SATA mode | Not verified; owner collection required. |
| USB host-controller identities, physical port mapping and input path | Not verified; owner collection required. |
| Windows boot/system disks and intended guideXOS boot medium | Not identified. |
| Disposable SATA target model, unique identity, size and sector geometry | Not identified. |
| VT-d/IOMMU configuration | Not verified; the AHCI DMA path has no IOMMU translation support. |
| Physical partition/FAT32 contents or test results | Not inspected; no physical test occurred. |

### Production AHCI compatibility audit

The audit reviewed `kernel/core/ahci.cpp` and
`kernel/core/include/kernel/ahci_logic.h` at the starting HEAD. AMD64 scans
PCI configuration space and admits exactly base class `01`, subclass `06`,
programming interface `01` (SATA/AHCI). It records controller BDF and
vendor/device ID in serial diagnostics. IDE-compatible presentation is a
separate ATA/PIO path; a RAID/RST/VMD or other controller presentation is not
assumed to be AHCI and is unsupported unless its actual PCI class and
initialization behavior satisfy the driver's requirements.

The current AHCI profile has these concrete requirements and limits:

- BAR5 must parse as a nonzero 32-bit memory BAR. I/O BARs and 64-bit memory
  BARs are rejected. The driver maps a fixed `0x1100` byte ABAR window using
  the device MMIO mapper; an invalid BAR or failed uncached mapping rejects
  the controller. The code does not identify a vendor-specific RAID mode or
  apply vendor workarounds.
- The driver enables PCI memory decoding and bus mastering, performs AHCI
  BIOS/OS ownership handoff when CAP2 advertises it, issues the HBA global
  reset, enables AHCI mode, disables HBA/port interrupts, and polls status.
  It assumes standard AHCI register behavior and a functioning PIT for
  time-based timeouts, with bounded fallback polling.
- It scans implemented ports and accepts the direct SATA-disk signature.
  SATAPI, port multipliers, SEMB and other signatures are not registered.
  There is one serialized, non-NCQ command slot; no AHCI interrupt path,
  general runtime hotplug rescan, or physical device-lifetime qualification.
- IDENTIFY supplies model, serial, firmware, sector count, cache-flush support,
  and logical/physical sector geometry. Logical sectors from 512 through
  4096 bytes are accepted when IDENTIFY reports valid size data; absent or
  invalid optional sector information uses the ATA 512-byte default or fails
  if the reported value is malformed. Physical-sector size is reported when
  its IDENTIFY fields are valid. This parsing has hosted evidence, not a
  SARAH_DT device result.
- DMA uses aligned kernel-resident command/FIS/bounce buffers and a
  kernel-image virtual-to-physical offset based on the boot-provided physical
  base. It validates the DMA address range (up to the driver's 52-bit bound),
  also enforcing below-4-GiB addresses unless CAP.S64A is set. Data transfers
  are limited to 128 KiB per command and one bounded PRDT entry. The driver
  assumes device DMA addresses are physical addresses: no AMD64 IOMMU/VT-d
  translation or mapping setup was found in the source audit. If firmware
  leaves remapping active without identity mappings, AHCI DMA compatibility is
  unestablished and the pilot must stop. Record the current VT-d/IOMMU and
  Kernel DMA Protection state; do not change it for this continuation.
- Commands use polling with 30-second data/IDENTIFY and 60-second Flush
  limits (plus shorter port/controller waits). Errors are surfaced; the
  submitted command is not replayed. Recovery stops and restarts the port,
  re-identifies the device and compares identity; failed recovery marks the
  registration offline. These paths have hosted/virtual evidence only.

Accordingly, the driver is a plausible candidate only if SARAH_DT exposes a
standards-conforming AHCI PCI function with a compatible BAR/MMIO mapping,
usable port and DMA setup, supported direct SATA disk, and matching identity.
PCI class alone does not prove those conditions. RAID/RST/VMD, an unsupported
class/progIF, a rejected BAR, active unmapped IOMMU, unsupported port device,
or uncertain DMA address mapping blocks physical qualification. No
speculative hardware support was added.

### Input readiness audit

The AMD64 kernel initializes PS/2 keyboard and mouse drivers and has USB HID
keyboard/mouse polling integrated with the input manager. Its AMD64 USB host
backend is UHCI, with two root ports and no USB hub topology support. A direct
device on a usable UHCI root port is the only source-level USB input candidate
in this profile. No general AMD64 xHCI or EHCI host-controller driver exists.
Firmware legacy-USB emulation in Windows/UEFI is not evidence that input will
continue after guideXOS initializes its own controller path. Windows' PS/2
device enumeration is also not proof of a physical PS/2 connector; record the
actual ports and attached keyboard/mouse and verify them during a later
supervised boot.

Serial routines provide diagnostic output. The feature report lists serial
input as not present, and no safe interactive serial/Shell control path for
Disk Manager was established. Therefore physical keyboard/mouse input must be
demonstrated before operating Disk Manager; if the machine only exposes
xHCI/EHCI USB input and no usable PS/2 or direct supported UHCI path, input is
a readiness blocker. A missing input path requires a separately scoped input
milestone, not an xHCI implementation hidden in DM38-2.

### Boot-media, target identity and read-only inspection

The existing UEFI boot-source matcher carries controller/device path elements
when BootInfo provides them. Missing, malformed, legacy or unmatched
provenance remains unknown and destructive admission fails closed. The
production target snapshot uses a registration identity/generation plus
transport identity, geometry, model/serial and controller/port location; a
capacity or current OS disk number is not identity. For a physical pilot,
owner-supplied inventory and later guideXOS logs must agree on the boot medium,
Windows boot/system disks, disposable target, model, stable serial/unique ID,
capacity, sector sizes, controller and attachment/port location. A duplicated,
missing or inconsistent identity, unknown boot/target separation, or failed
boot protection observation stops the pilot. Never infer a target from its
disk number, capacity or list order.

Source inspection shows GPT/MBR discovery does not repair metadata. Disk
Manager defaults Mount to read-only; the read-only path cannot create or
modify files. VFS Dismount explicitly skips FAT/exFAT filesystem Flush for a
read-only mount. The retained DM38-1 host regression verifies a rejected
read-only file creation performs zero block writes and zero Flush calls. This
supports a non-modifying inspection path in source/host tests, not physical
controller, media or boot-protection qualification. The requested actual
physical test remains unperformed.

### Owner-run read-only Windows and firmware inventory

For DM38-2 use this expanded procedure in place of the shorter DM38-1 command
list above. Run it locally on SARAH_DT from a normal PowerShell session. These
commands print inventory to the console and do not create a report file or
change disks, drivers, mount points, drive letters, partitions, boot
configuration or firmware. The output includes storage serials and host
identifiers: keep the exact target identity private, and share only a redacted
copy. If a command/module is unavailable, record that fact instead of
substituting guessed values. Do not redirect output or write a transcript to
an attached test disk.

```powershell
'=== PowerShell and command/module availability ==='
$PSVersionTable
Get-Module -ListAvailable CimCmdlets,PnpDevice,Storage |
    Select-Object Name,Version | Format-List
Get-Command Get-CimInstance,Get-PnpDevice,Get-PnpDeviceProperty,
    Get-Disk,Get-Partition,Confirm-SecureBootUEFI -ErrorAction SilentlyContinue |
    Select-Object Name,Source

'=== Machine, firmware, processor and board ==='
Get-CimInstance Win32_ComputerSystem |
    Select-Object Manufacturer,Model,SystemType,PCSystemType | Format-List
Get-CimInstance Win32_BIOS |
    Select-Object Manufacturer,SMBIOSBIOSVersion,Version,ReleaseDate,SerialNumber |
    Format-List
Get-CimInstance Win32_Processor |
    Select-Object Name,Manufacturer,ProcessorId,SocketDesignation | Format-List
Get-CimInstance Win32_BaseBoard |
    Select-Object Manufacturer,Product,Version,SerialNumber | Format-List
$firmwareType = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control' `
    -Name PEFirmwareType -ErrorAction SilentlyContinue).PEFirmwareType
'PEFirmwareType (1=Legacy BIOS, 2=UEFI, other/empty=unknown): {0}' -f $firmwareType
if (Get-Command Confirm-SecureBootUEFI -ErrorAction SilentlyContinue) {
    try { 'SecureBoot: {0}' -f (Confirm-SecureBootUEFI -ErrorAction Stop) }
    catch { 'SecureBoot query unavailable: {0}' -f $_.Exception.Message }
}

'=== Windows storage controller presentations and drivers ==='
Get-CimInstance Win32_SCSIController -ErrorAction SilentlyContinue |
    Select-Object Name,PNPDeviceID,Status,DriverName | Format-List
Get-CimInstance Win32_IDEController -ErrorAction SilentlyContinue |
    Select-Object Name,PNPDeviceID,Status,DriverName | Format-List
Get-CimInstance Win32_USBController -ErrorAction SilentlyContinue |
    Select-Object Name,PNPDeviceID,Status,ProtocolSupported | Format-List

'=== Present PCI controllers, class codes, driver service and location ==='
if ((Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) -and
    (Get-Command Get-PnpDeviceProperty -ErrorAction SilentlyContinue)) {
    $present = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object {
            $_.InstanceId -like 'PCI\*' -or
            $_.Class -in @('SCSIAdapter','USB','System','Keyboard','Mouse','HIDClass')
        }
    foreach ($device in $present) {
        $values = @{}
        foreach ($key in @(
            'DEVPKEY_Device_HardwareIds',
            'DEVPKEY_Device_CompatibleIds',
            'DEVPKEY_Device_Service',
            'DEVPKEY_Device_LocationInfo',
            'DEVPKEY_Device_LocationPaths',
            'DEVPKEY_Device_Parent',
            'DEVPKEY_Device_DriverProvider',
            'DEVPKEY_Device_DriverVersion'
        )) {
            $property = Get-PnpDeviceProperty -InstanceId $device.InstanceId `
                -KeyName $key -ErrorAction SilentlyContinue |
                Select-Object -First 1
            $values[$key] = if ($property) { $property.Data } else { $null }
        }
        $classCodes = @($values['DEVPKEY_Device_CompatibleIds'] |
            ForEach-Object {
                if ($_ -match 'PCI\\CC_([0-9A-Fa-f]{6})') { $Matches[1] }
            })
        [pscustomobject]@{
            Class = $device.Class
            Name = $device.FriendlyName
            Status = $device.Status
            InstanceId = $device.InstanceId
            PCIClassSubclassProgIF = ($classCodes -join ',')
            HardwareIds = ($values['DEVPKEY_Device_HardwareIds'] -join '; ')
            CompatibleIds = ($values['DEVPKEY_Device_CompatibleIds'] -join '; ')
            Service = $values['DEVPKEY_Device_Service']
            DriverProvider = $values['DEVPKEY_Device_DriverProvider']
            DriverVersion = $values['DEVPKEY_Device_DriverVersion']
            LocationInfo = $values['DEVPKEY_Device_LocationInfo']
            LocationPaths = ($values['DEVPKEY_Device_LocationPaths'] -join '; ')
            Parent = $values['DEVPKEY_Device_Parent']
        } | Format-List
    }
} else {
    'PnP cmdlets unavailable; use the Win32 controller results and Device Manager Details/Location Paths.'
    Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like 'PCI\*' } |
        Select-Object Name,PNPClass,PNPDeviceID,Service,Status,ConfigManagerErrorCode |
        Format-List
}

'=== Keyboard and pointing-device Windows presentation ==='
Get-CimInstance Win32_Keyboard -ErrorAction SilentlyContinue |
    Select-Object Description,PNPDeviceID,Status,Layout | Format-List
Get-CimInstance Win32_PointingDevice -ErrorAction SilentlyContinue |
    Select-Object Description,Manufacturer,PNPDeviceID,Status,DeviceInterface |
    Format-List

'=== Physical disks and current Windows boot/system flags ==='
if (Get-Command Get-Disk -ErrorAction SilentlyContinue) {
    Get-Disk | Select-Object Number,FriendlyName,SerialNumber,UniqueId,
        BusType,PartitionStyle,OperationalStatus,IsBoot,IsSystem,
        LogicalSectorSize,PhysicalSectorSize,Size | Format-List
}
Get-CimInstance Win32_DiskDrive |
    Select-Object Index,Model,SerialNumber,InterfaceType,PNPDeviceID,
        Status,Size,BytesPerSector | Format-List
if (Get-Command Get-Partition -ErrorAction SilentlyContinue) {
    Get-Partition | Select-Object DiskNumber,PartitionNumber,DriveLetter,
        Type,Size,Offset | Format-List
}
```

The six-digit PCI compatible class code, when Windows exposes it, is
base-class/subclass/programming-interface. In particular, `010601` is the
AHCI class tuple; USB `0C0330`, `0C0320`, and `0C0300` indicate xHCI, EHCI,
and UHCI programming interfaces respectively. Preserve the original hardware
IDs and location fields. If compatible IDs do not expose a full class code,
record it as unavailable and capture the Device Manager hardware-ID and
location screens; do not infer it from the device name.

Windows reporting is only one part of inventory. Open System Information
(`msinfo32`) and record **BIOS Mode**, **Secure Boot State**, and **Kernel DMA
Protection** if present. In firmware setup, observe and record the existing
SATA controller mode (AHCI, RAID/RST/VMD, IDE-compatible or unknown), VT-d/
IOMMU setting, USB legacy support, enabled SATA ports and boot mode; exit
without saving changes. Photograph or write down the physical SATA labels,
cable/device attachment, USB port used for each device, keyboard/mouse
connection and display output. Do not unplug Windows disks or test devices
while Windows is running to determine port identity.

The class code, device name and Windows driver together do not by themselves
prove the firmware SATA setting or guideXOS compatibility. The Windows disk
number can change across operating systems and boots; retain it only as a
within-this-Windows-session cross-reference. Match eventual guideXOS logs to
the private model/serial, capacity, sector geometry, controller and port
record.

After collecting the output, label the intended disposable target explicitly
in the owner record by its private model/serial/unique identity and physical
port. Do not designate it by `Number`, `Index`, bus order or list position.

### Bounded physical read-only pilot plan

This is a proposed later owner-supervised procedure, not a claim of a test.
Do not begin until inventory review confirms all prerequisites below:

- Exact SARAH_DT identity, current UEFI/Legacy and Secure Boot state, SATA
  mode and controller PCI identity are recorded.
- The function is supported AHCI `01:06:01`, BAR/MMIO and DMA assumptions are
  compatible, VT-d/IOMMU is not leaving an unsupported DMA translation path,
  and the actual controller/port remains a hardware qualification risk.
- A usable keyboard and mouse path is identified (PS/2 or a supported direct
  UHCI path) and confirmed on the guideXOS boot. xHCI/EHCI-only input blocks
  the pilot. A serial log is evidence capture, not interactive control.
- Firmware boot medium, running root/system medium, Windows boot/system disk
  and a separate disposable SATA target have distinct, recorded identities.
  Target model/serial or trustworthy unique identity, capacity, logical and
  physical sector sizes, BDF/port/location and the owner's expendability
  confirmation agree. No disk number alone authorizes selection.
- Boot provenance and target protection are visible and unambiguous in
  guideXOS. Any unknown or conflicting identity/protection result stops.
- An initial Windows disk/partition inventory and physical connection record
  are retained. The selected disk already has supported GPT/MBR and FAT32
  content needed for the read-only workflow; no fixture is to be created on
  physical media.
- The owner has explicitly authorized this named machine and identified disk
  for a read-only inspection. Record machine/firmware/controller, boot-media
  and target identities, source/kernel hashes, exact reads, partition/FAT32
  observations, Disk Manager state, errors/timeouts and shutdown outcome.

When prerequisites pass, the operator powers down, installs only the
identified target beside the separately identified boot media, records the
connections, and boots a hash-identified current-source image. The operator
confirms display and input, observes the AHCI BDF/controller/port and disk
registration, and matches model, serial, geometry and location before opening
Disk Manager. The operator inspects GPT/MBR without repair, explicitly
selects **Read Only**, and reads only an owner-selected non-sensitive test
file if one exists, recording the exact path and bytes read. No Initialize,
Format, Repair, writable Mount, create, delete, write, Trim/Discard,
Flush/durability experiment or Windows firmware mode change is allowed. If
there is no supported filesystem/file, or any identity, protection, read,
timeout or mount error occurs, stop without repair or retry and preserve the
error evidence. Close all references, normally Dismount, verify unmounted
state, and use normal shutdown. If Dismount or shutdown does not complete
normally, do not unplug or force-disconnect the target. Preserve logs,
kernel/source hashes, UI observations, errors and shutdown classification.
After power-off, recheck identity and layout. A Windows inventory comparison
is not a full-media integrity proof; claim full unchanged-media evidence only
if all media bytes were actually compared using an appropriate read-only
method. Do not write to the target to create integrity evidence.

### Qualification evidence and next scope

This continuation changed documentation only. No focused or full hosted tests,
DM37 regressions, builds, QAC sessions or physical operations were rerun.
Retain the source-matched DM38-1 evidence at this exact HEAD: storage suite
1,523/1,523; AMD64 freestanding kernel, `build.bat` desktop and UEFI x64
Release builds PASS; current-source QAC AHCI and UHCI BOT registration smoke
results PASS. Those QAC guests establish virtual registration only. No result
establishes SARAH_DT hardware support, physical read behavior, disk integrity,
keyboard/mouse usability, writes, cache synchronization or device lifetime.
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain unchanged.

No narrow production safety defect requiring a correction was identified in
this documentation-only, source-reviewed continuation. The actual hardware
controller, its bus mode, IOMMU state, supported physical input and disk
identities remain owner-supplied prerequisites. No change to `.phase` was
made; DM38 remains active.

**Recommended DM38-3 scope:** review the owner-supplied inventory against the
actual AHCI PCI function, BAR/MMIO, DMA/IOMMU and port requirements; confirm
boot-medium/target identity and input readiness; then decide whether the
bounded read-only physical pilot can be authorized. If xHCI/EHCI input or an
unsupported DMA remapping path is required, prepare the smallest independent
support milestone. Do not change firmware mode automatically and do not
authorize physical writes in that continuation.
