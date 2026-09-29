# Settings S5: Devices and Storage

## Result

**Outcome A: Settings now has bounded Devices and Storage inventories backed
by guideXOS kernel state when the system-service peer is available.** The
QEMU proof exercised the production `SystemServiceClient` through COM2 and
matched its device and storage response fields against kernel serial markers.
Settings never substitutes Windows host inventory. A missing guest service is
shown as unavailable.

The live hosted Settings window was not exercised. The full Visual Studio
Release build remains blocked by pre-existing project-wide CRT include and
duplicate-output issues; a direct syntax compile of the changed Settings
translation unit passed. The model tests and kernel/client QEMU proof passed.

## Existing ownership found

There is no central kernel device registry on this branch. The provider reads
existing owners:

- Network interfaces from `network_settings_provider` and the NIC owner.
- Audio controllers from `pci_audio`.
- PCI VGA information from `vesa`, plus initialized VirtIO GPU devices from
  `virtio::gpu` (duplicate PCI identities are coalesced).
- Block devices from the block-device registry.
- Present USB devices and their interface classes from the USB registry.

The current owners do not expose a PS/2 keyboard/mouse inventory, a general
PCI device walk with driver binding, or ACPI/platform inventory. USB input
interfaces can be categorized as Input, but absence of a row does not claim
that no input device exists. Unknown USB classes remain USB devices. Product
names are used only where the owner provides them; otherwise the UI shows a
bounded device class and vendor/device identity.

Storage comes from the block-device registry and VFS mount table. The provider
reads sector zero through the block read API to inspect the MBR signature and
primary entries. It does not write sectors or probe filesystems. The existing
branch recognizes GPT only as a protective MBR and does not enumerate GPT
partitions. Partition-to-mount association is not reliable in the current VFS
API, so partition mount state remains unknown; VFS mounts appear separately as
volume rows.

The existing Disk Manager launch route remains the advanced storage entry
point. Initialization, partitioning, formatting, and other destructive
operations were not added to Settings or to the read-only inventory service.

## Snapshot service and bounds

The existing COM2 system-service protocol gained `GET_DEVICE_SNAPSHOT`
(`0x0003`, response `0x8003`) and `GET_STORAGE_SNAPSHOT` (`0x0004`, response
`0x8004`). It remains versioned, little-endian, fixed-capacity, and
pointer-free. The largest inventory response is 4,364 bytes. The Settings
wrapper checks the built-in Settings app identity before using the production
client. Requests contain no caller-supplied identity. The guest endpoint
continues to treat COM2 as its trusted system-service peer; this is not
cryptographic peer authentication, as documented for the existing COM2
service.

| Inventory | Snapshot capacity | Fixed strings |
| --- | ---: | --- |
| Devices | 24 rows | ID 40, name 64, driver 32, location 32 bytes |
| Disks | 16 rows | ID 40, name 48 bytes |
| Partitions | 4 per disk | Bounded numeric/type fields |
| Volumes | 8 rows | ID 40, mount path 64, backing-disk ID 40 bytes |

Each snapshot carries backend, state, generation, represented and total
counts, and a truncation flag. The UI says when additional inventory is not
shown. Text copied from fixed-size kernel fields is scanned only up to the
source field's capacity, terminated in the snapshot, and marked truncated
when necessary. Client decoding validates capacities, strings, enum values,
counts, and unused rows.

Device and storage snapshot generations advance when their observed contents
change. Stable IDs identify rows within that inventory. Block-device registry
slots now also carry lifecycle generations; disk IDs include that generation
so a replacement in a reused slot cannot inherit the old disk identity. VFS
mounts capture the block slot generation and are omitted if the slot has since
been reused. Detail views bind selection to snapshot generation and stable ID;
after an inventory change they show a removed/changed state instead of
following a stale slot.

## Truthful fields and UI behavior

Devices are categorized as Input, Network, Display, Storage, Audio, USB, or
Other. Device details show only available driver, PCI identity, location,
status, and stable identity. Status is conservative: network link-down is
Disconnected; link-up is reported as Driver loaded, not Working properly;
initialized audio/GPU/block owners can report Driver loaded; USB and PCI VGA
without a health signal report Status unavailable. Enumeration is not treated
as proof of healthy operation. Unavailable provider, empty inventory,
filtered-empty results, and truncation have separate UI messages. The source
label distinguishes kernel inventory, unavailable provider, and deterministic
test data.

Storage details show disk capacity only when sector count × sector size is
representable, logical sector size, known transport, block-layer write
capability, partition-table status, bounded MBR partition type/start/size, and
any matched mounted VFS volume. The UI does not claim fixed/removable status;
USB transport alone does not prove removability. Write support reflects the
block device's `writeFn`, and does not add a write action in Settings.

The branch's partition read supports MBR primary entries only. GPT is reported
as detected with details unavailable. Filesystems are not probed from
partitions. VFS volume rows show mount path and filesystem type. The mount
table cannot provide a reliable volume extent, and `free_space()` currently
uses an unsupported sentinel, so volume capacity and free/used values remain
unavailable. No usage bar is drawn. Capacity formatting reuses S4's
overflow-safe KiB/MiB/GiB/TiB formatter.

Devices rows open bounded detail views and can link to Network, Display, or
Storage Settings. Storage includes disk and mounted-volume rows, bounded disk
details, and an Open Disk Manager action. Search covers implemented device,
disk/partition, volume, filesystem, and Disk Manager destinations. Internal
routes include `settings://devices` and category filters such as
`settings://devices/network`, plus `settings://storage`,
`settings://storage/disks`, and `settings://storage/volumes`. The System page
links to Devices and Storage.

Long rows are clipped to the available text width. The inventory list uses a
bounded viewport; mouse-wheel and arrow-key movement, keyboard focus visibility,
and hit tests are limited to visible rows. Disk detail rows scroll as well.
The same layout adapts to the compact Settings size.

## Verification

- Settings Center model: **89/89 passed** (S4 baseline preserved).
- Device inventory model: **21/21 passed**.
- Storage inventory model: **24/24 passed**.
- Block slot lifecycle generations: **5/5 passed**.
- System-service bridge: **78/78 passed**, comprising Device **13/13** and
  Storage **12/12** bridge checks.
- Network Settings contract: **45/45 passed**; configuration transaction:
  **21/21 passed**.
- Wallpaper owner regression and hosted background-phase smoke: **passed**.
- AMD64 kernel Make build: **passed**.
- Direct MinGW syntax compile of `settings_center.cpp`: **passed**, with only
  existing header warnings.
- Full Visual Studio Release build: **blocked by existing project-wide
  issues**. It selects the kernel `stdio.h` in place of CRT headers and reports
  duplicate-basename output collisions; the same CRT issue was reproduced
  against the parent-S2 Settings source. No broad build-system changes were
  included.
- Hosted Settings runtime: **not run** because the blocked full build did not
  produce a current hosted executable. Model and syntax checks are not
  presented as live-window proof.
- Physical hardware: **not tested**.

The final QEMU run used the production runtime client and matched the kernel
provider's serial markers field for field:

```text
GET_DEVICE_SNAPSHOT: generation=1, devices=5/5, truncated=0, backend=Kernel
  id=pci:00:03.0:mac:525400123456, category=Network, status=DriverLoaded
GET_STORAGE_SNAPSHOT: generation=1, disks=3, volumes=0, truncated=0, backend=Kernel
  diskId=block:1:0:0:0000000000000001
  capacityBytes=0x1F800000, sectorSize=512, partitions=1, table=ValidMbr
```

The COM2 loopback proof completed successfully. Serial evidence is in
`out/validation/system-service-smoke/20260928-222540/guest-com1.serial.log`;
QEMU stderr is alongside it. This proves real QEMU/kernel inventory reached
the production client. It does not claim physical-hardware coverage or a live
hosted Settings window.
