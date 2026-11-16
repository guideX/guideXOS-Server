# guideXOS Modern Input — INPUT3D hardware diagnostics

INPUT3D adds a boot-time diagnostic snapshot and a persistent desktop overlay
for investigating USB/xHCI input failures. It does not implement the xHCI HID
transport.

## Production boot path audited

The AMD64 UEFI path is: UEFI loads the kernel and BootInfo, establishes
identity-mapped page tables, exits boot services, and jumps through the handoff
trampoline; AMD64 `_start` disables interrupts, installs the kernel stack, and
calls `kernel_main()`. The Multiboot path enters the same kernel entry with a
different handoff and framebuffer initializer. The kernel accepts that memory
context, initializes the framebuffer, probes VirtIO-GPU, initializes the
desktop, and paints the first desktop frame before enabling IDT/PIC interrupts
and the 100 Hz PIT.

After the first paint, the kernel initializes PCI audio (including its PCI
scan), calls the modern-input PCI USB-host scan, initializes the selected xHCI
controller, and attempts one bounded device descriptor enumeration. It then
initializes storage and VFS, refreshes the desktop, initializes NIC/network
services (including DHCP where applicable), initializes PS/2 and the input
manager, prints feature diagnostics, and enters the main event loop. The main
loop does not run concurrently with boot initialization; timer interrupts can
run after PIT setup, but the event loop begins only after those synchronous
boot services return. Storage, network, and input setup can therefore delay
main-loop entry after the xHCI panel has already been painted.

The framebuffer path is the existing desktop back buffer and `present()` call.
The first frame predates PCI audio and modern USB discovery. Later explicit
desktop paints replace the whole software canvas, including the earlier
diagnostic panel; no early framebuffer draw competes with the compositor. The
physical and QEMU production paths share this ordering. UEFI versus Multiboot
changes the handoff/framebuffer setup and physical-base configuration, not the
ordering after the handoff.

PCI audio discovery is a separate PCI scan before the modern-input USB-host
scan. `BootStage::PciDiscoveryEntered/Completed` bracket PCI audio
initialization. The panel's `PCI ... scan=...` fields describe the later
bounded USB-host PCI scan, whose entry is published by
`scan_host_controllers()` itself. A completed audio scan therefore remains
distinct from a not-yet-invoked USB-host scan.

Later, `kernel_main()` initializes the input manager and its main loop calls
`input::poll()`. The generic USB HID decoder is present and is initialized by
the input manager, but the xHCI code does not discover HID interfaces, submit
interrupt reports, decode xHCI reports, or route them into the input manager.
Those stages are displayed as `UNSUPPORTED`; zero report/event counters are
not treated as transport evidence.

## Diagnostic behavior and limits

- The fixed-size, lock-protected snapshot is shared by PCI/xHCI startup and the
  existing desktop renderer. The desktop reads a copy; it does not drive the
  controller. The first `FAIL`, `TIMEOUT`, `BLOCKED`, or unsupported DMA
  prerequisite includes a copied controller identity and stage result and
  survives later status updates or enumeration. Unsupported HID stages remain
  implementation frontiers and do not become hardware failures.
- Up to eight USB controllers retain independent PCI and initialization
  results. Aggregate counts include additional controllers, which are
  reported as omitted. The existing xHCI driver selects and initializes only
  the first xHCI controller in PCI discovery order; the remaining controllers
  are shown as not attempted. If that controller cannot be represented in the
  bounded snapshot, initialization is blocked rather than run without a
  corresponding diagnostic record.
- DMA prerequisites check each static ring, context, and control buffer for
  physical translation within linker-defined kernel image bounds, required
  alignment, and the controller's AC64 limit. UEFI supplies the physical base;
  the bootloader allocates the complete kernel image span in one contiguous
  `AllocatePages` allocation and loads PT_LOAD segments at offsets within it.
  This provides the bounded backing contract for the static xHCI regions; it is
  not a general-purpose DMA allocator. The kernel does not equate MMIO virtual
  addresses with DMA addresses.
- INPUT3D-R5 reads the controller's advertised scratchpad count and the
  operational PAGESIZE bitmap. It reserves a fixed 1.0625 MiB static pool for
  at most 16 pages of at most 64 KiB each, including alignment slack. It
  constructs the scratchpad array only after validating physical alignment,
  non-overlap, range overflow, and AC64 addressability. Unsupported count or
  page requirements fail closed before xHCI ownership, halt, or reset. The
  array is registered through DCBAA[0]; zero-scratchpad controllers leave that
  entry zero. Storage remains static for kernel lifetime.
- The UEFI bootloader checksum-validates a bounded ACPI RSDP/root-table scan
  for DMAR/IVRS. A valid scan with neither table permits the direct-physical
  path; an unknown scan or advertised remapping table blocks controller DMA
  because no IOMMU domain/mapping contract exists. The scan does not inspect
  remapping-unit enable registers, so `NO ACPI REMAP TABLE` is evidence about
  firmware description, not a hardware register proof that translation is
  disabled. Active IOMMU state remains unknown on the VivoBook until a
  standard platform contract or physical read-only measurement supplies it.
  No IOMMU is disabled or bypassed by software.
- The direct path currently assumes the standard coherent x86 PCIe DMA model
  and the existing WB mapping of the loaded kernel image. DMA publication and
  consumption use the architecture's `sfence`/`lfence` helpers; there is no
  cache-maintenance API for non-coherent systems. Such systems and
  non-x86 architectures remain unsupported by this contract.
- Command/Event Ring, ERST, DCBAA, context, EP0 ring, and control-buffer
  physical addresses are cached only after complete range/alignment/AC64
  validation. The command Link TRB cycle is published before its doorbell on
  wrap. Polling event consumption uses bounded cycle checks and advances ERDP
  with EHB after consumption. Controller-owned memory is static and currently
  scoped to the first selected controller; other discovered controllers retain
  independent diagnostic identities and remain not attempted.
- After Run/Stop succeeds, the selected controller reads every advertised
  PORTSC and logs BDF, port index/count, CCS/PED, speed, link state, the full
  raw status word, and the relevant change/error bits. The aggregate snapshot
  records connected count and first connected port. A failed PORTSC read now
  fails the root-port stage instead of being reported as a successful scan.
- Enumeration remains limited to the first connected root port and one device
  descriptor. Completion status and descriptor identity are recorded only
  after observed transfers. A missing xHCI controller is shown by the real PCI
  counts; it is not presented as a failed initialization attempt.
- The panel is compiled only for an AMD64 diagnostic build, appears on normal
  desktop redraws, and uses the existing framebuffer/desktop renderer. It
  needs no input, filesystem, or network services. COM1 logging remains
  available before the desktop.

## INPUT3D-R2: PCI discovery visibility

The production AMD64 call path was present in both `370e4958dc5b` and
`b14073a16da0`: after diagnostic initialization, framebuffer setup, the first
desktop paint, interrupt setup, and PCI audio initialization,
`kernel_main()` calls `usb::diagnostics::scan_host_controllers()`, then xHCI
controller initialization. The scan itself is not conditional on
`GXOS_INPUT3D_DIAGNOSTICS`; that define selects the overlay renderer. The R1
USB scan still enumerated PCI devices and published its result. Its replacement
of the old direct BAR capability read with validated xHCI MMIO mapping did not
remove or guard the scan call.

The first desktop paint happened before the scan. R1 only requested a later
redraw, which was painted after the remaining boot initialization. The panel
could therefore continue showing its initial snapshot while boot was still in
progress. In addition, a successful
zero-controller scan did not update the `Latest` stage, which could leave that
line saying `PCI discovery NOT ATTEMPTED` after the aggregate scan result had
changed. The R1 ISO manifest says it was packaged with `-SkipBuild`; its
embedded kernel hash matched the ESP build identity and the manifest, but that
package step did not compile the source revision again.

R2 publishes bounded scan states (`NOT INVOKED`, `ENTRY REACHED`,
`SCAN COMPLETE`, `NO CONTROLLERS`, or `FAILURE`), records the completed PCI
stage even when no USB controller is found, and paints the entry, discovery,
and xHCI results before unrelated boot services can delay the next desktop
loop. First-failure evidence remains sticky. The R2 kernel was force-rebuilt
with diagnostics enabled and its hash was checked against the ESP build
identity before packaging. The ISO packaging step used `-SkipBuild` because
the full wrapper build stopped in an unrelated PacMan native-package link;
the package manifest verifies the fresh embedded kernel and release ISO boot
structure. QEMU evidence does not establish physical hardware acceptance.

The audit also found two concrete hazards in the existing xHCI path: several
DMA register writes used kernel virtual addresses instead of translated
physical addresses, and the initial eight-byte device descriptor prefix was
validated with the full eighteen-byte rule. DMA translation/alignment/address
width checks and complete 64-bit register writes now guard initialization;
the descriptor prefix has its own validator. Root-port reset writes avoid
clearing unrelated write-one-to-clear status bits. IOMMU presence and physical
qualification still require real-machine evidence.

## INPUT3D-R3: execution, publication, and presentation

R3 adds a fixed-size boot-progress record to the existing diagnostic snapshot.
Each executed milestone advances a boot sequence. Each diagnostic writer
advances a separate snapshot sequence under the snapshot lock. `initialize()`
is called once at kernel entry; the PCI scan is no longer marked `ENTRY
REACHED` by a caller before the scan function is entered. `begin_pci_enumeration`
is owned by `scan_host_controllers()`, and the first-failure record is not reset
by that transition. `copy_snapshot()` remains a bounded value copy of the one
shared instance.

The desktop panel shows the latest boot stage, boot sequence, snapshot
sequence, and displayed frame sequence. After the existing
`framebuffer::present()` returns, the renderer acknowledges the snapshot
sequence and emits a COM1 `INPUT3D: panel presented` breadcrumb. In the normal
main-loop cadence, `presentation_pending()` requests a redraw only when a
newer diagnostic snapshot exists. This uses the existing redraw flag and full
desktop renderer; there is no diagnostic paint loop, no interrupt-context
rendering, and no dependency on pointer or keyboard activity. During
synchronous hardware scans, the display cannot repaint until the call returns.
The serial milestone and snapshot sequence still show execution advancing
while the visible frame retains its earlier sequence.

The first desktop paint happens before PCI audio and USB discovery. R3 does not
infer a hang from a quiet panel. A screenshot that says `NOT INVOKED` is
consistent with a capture before the USB scan; without a capture time series
or serial output from that physical boot, whether progress later stopped is
undetermined. If a boot reaches the main loop, snapshot changes are repainted
without input. If it stops during a synchronous hardware stage, the last
presented milestone remains visible and COM1 identifies the most recently
executed boundary.

Hosted tests cover milestone ordering, state names, sequence monotonicity,
empty and successful PCI scans, multiple controllers, first-failure
retention, repeated publication, reset behavior, stale presentation, early
paint followed by controller failure, formatting termination, and small-panel
layout. These tests validate snapshot state, not pixels. A post-`present()`
serial acknowledgment is software render-state evidence; it is not physical
scan-out or bare-metal USB qualification.

## INPUT3D-R4: PCI startup frontier and controlled audio isolation

The reproduced physical frontier is still the VivoBook report at build
`27980591f302`: the panel reached `PCI_DISCOVERY_ENTERED`, then remained there
for about five minutes with USB scan `NOT INVOKED`, zero xHCI controllers, and
no first failure. That evidence locates the last published broad boot stage;
it does not identify PCI audio or prove an xHCI fault.

### Audited AMD64 call path

In `kernel/core/main.cpp::kernel_main`, after the first desktop presentation,
IDT/PIC initialization, PIT setup and IRQ0 registration, startup records
`PciDiscoveryEntered`. UEFI physical-base setup for PCI audio, native audio,
and xHCI is immediately before that marker. The kernel paints the panel, then
calls `kernel::pci_audio::init()`. When that returns, it records
`PciDiscoveryCompleted`, logs the audio controller count, paints again, and
calls `kernel::usb::diagnostics::scan_host_controllers()`. After the USB scan
returns, it paints and calls `kernel::usb::xhci::controller::init()`, then
continues to storage, networking, input initialization and the main loop.

`PCI_DISCOVERY_ENTERED` is a broad `kernel_main` stage boundary immediately
before PCI audio initialization. It is not an internal PCI-audio scan marker.
The original R3 panel therefore could not tell whether execution entered the
audio function, scanned PCI, probed an HDA controller, or returned from audio.

The relevant source call graph is:

```text
kernel_main (kernel/core/main.cpp)
  ├─ set PCI audio / native audio / xHCI physical bases (UEFI path)
  ├─ record PciDiscoveryEntered; desktop::draw()
  ├─ pci_audio::init()                         [normal diagnostic variant]
  │    ├─ scan_pci_audio()                     [pci_audio.cpp]
  │    │    ├─ 256 buses × 32 devices × 8 functions
  │    │    ├─ CF8/CFC vendor and class reads
  │    │    ├─ multimedia class filter: HDA 04/03, AC'97 04/01
  │    │    ├─ BAR0 and BAR1 (for a 64-bit BAR)
  │    │    └─ PCI command update: IO, memory, bus master
  │    └─ each discovered HDA controller:
  │         ├─ hda_reset()
  │         ├─ hda_init_corb_rirb()
  │         └─ hda_enumerate_codecs() → hda_send_verb()
  ├─ record PciDiscoveryCompleted; desktop::draw()
  ├─ scan_host_controllers()                    [usb_controller.cpp]
  ├─ desktop::draw()
  └─ xhci::controller::init()                   [xhci.cpp]
```

The PCI audio scan is synchronous and checks 65,536 BDF functions. On AMD64,
configuration accesses use direct CF8/CFC port I/O. There is no software
timeout or interrupt wait around a synchronous port instruction. The driver
does not take a startup lock, register an audio interrupt handler, allocate
memory, or wait for an audio interrupt during this path. The diagnostic
snapshot lock is held only while copying or publishing fixed-size state; no
controller access or framebuffer paint occurs while it is held.

For an audio-class device, the scan reads BAR0 and the upper BAR dword when the
BAR declares a 64-bit memory resource, then sets PCI command bits `0x0007`.
HDA BAR access in `pci_audio.cpp` casts the BAR physical value directly to a
volatile pointer. Unlike the R1 xHCI path, that audio code does not validate a
mapped virtual span before its first register access. A bad or absent mapping
can therefore cause a synchronous fault before later USB discovery. The audio
scan also enables BAR decoding before testing an HDA MMIO register.

The HDA reset has fixed spin delays and a 1,000-read CRST poll. HDA verb
response polling is capped at 100,000 iterations with a bounded delay per
iteration. Codec node and widget ranges are checked using 16-bit end
arithmetic; malformed ranges that exceed the 8-bit node space are rejected,
and the loops cannot wrap an 8-bit counter indefinitely. Verb response
failures now propagate to the codec-enumeration and probe result. These
polling paths can time out and return; raw PCI reads and direct HDA MMIO reads
cannot safely be declared timed out by checking a clock around them.

### Diagnostic frontier and bypass

Diagnostic builds keep a fixed 16-record startup trace. Each record has a
monotonic enter sequence, a separate return sequence, operation/status,
optional BDF, IDs, class, config offset, result and error code. The snapshot
preserves the most recently entered operation separately from the most
recently returned operation. `IN PROGRESS` remains distinct from `TIMED OUT`;
the code only reports a timeout when a bounded polling operation actually
expires.

PCI config transactions are recorded for the sparse class-filter read of a
present PCI function and for transactions on a known audio-class function.
The scan's exhaustive vendor-ID probes (including 65,536 empty BDFs) are not
written one by one to serial or the bounded trace. A stopped vendor-ID access
before a PCI identity is known would therefore leave the last visible
frontier at `PCI_AUDIO_SCAN_ENTER`; the hardware instruction cannot be
interrupted safely. This keeps ordinary probing from generating unbounded
text or per-read rendering while still showing the full BDF/ID/class context
for audio BAR, command, reset, ring and codec boundaries.

Before selected candidate PCI transactions, HDA reset/response boundaries,
USB scan entry, and xHCI initialization entry, diagnostic code publishes the
operation and uses the established desktop renderer outside snapshot locks
and interrupt context. It emits matching COM1 `INPUT3D` breadcrumbs. Completion
records the returned result/status; the next safe paint exposes it. No main
loop repaint is needed to show an entry frontier. The panel retains R3 boot,
snapshot and presentation reporting, sticky first-failure state, and now adds
the last entered/returned operation, BDF identity, config offset, and audio,
USB and xHCI status. A static screen is not itself reported as a timeout.

The bypass is safe for this comparison because the USB scan and xHCI driver do
not consume state created by `pci_audio::init()`. The
`GXOS_INPUT3D_SKIP_PCI_AUDIO=1` define is rejected unless the diagnostic define
is also present and AMD64 is selected. It skips only the call to
`pci_audio::init()` after shared physical-base setup, explicitly records and
prints `PCI AUDIO: DIAGNOSTICALLY SKIPPED`, and continues through the same USB
scan and xHCI calls. It is disabled by default; the normal diagnostic and
production path still calls `pci_audio::init()`.

Variant A is the normal instrumented startup. Variant B is built from the
same source commit and package inputs with only the diagnostic audio-skip
define added. The ISO manifest and embedded build identity record the active
flags and bypass state. Neither variant writes to removable media, and neither
is physical qualification. The packaging manifests record artifact hashes,
embedded file identities, source revision and compile flags; the final handoff
reports those values alongside the QEMU evidence and physical test steps.

## INPUT3D-R1: BAR/MMIO failure frontier

On the 2026-10-11 physical boot, the recorded result `0xe1020004` was the raw
BAR0 dword passed to the generic stage formatter, not an xHCI error code. Its
low attribute bits identify a 64-bit memory BAR, and its address bits decode
to `0x00000000e1020000` when BAR1 is zero. The former validator rejected both
BAR1 zero and BAR1 all-ones as unassigned, even though zero is a valid upper
dword for a 64-bit BAR below 4 GiB. The old overlay did not retain BAR1, so
this boot proves that the initial BAR-validation branch failed before any
capability access, but it cannot distinguish which of those two BAR1 values
was read. INPUT3D-R1 records both raw dwords and the exact validation substage
and reason for the next boot.

The second defect was independent of BAR decoding: xHCI initialization cast
the decoded physical BAR address directly to a C++ pointer. The UEFI loader
builds new four-level page tables from a bounded list of identity-mapped
ranges (kernel, boot data, framebuffer, and selected devices); it does not add
the xHCI BAR. Therefore physical and virtual addresses could not be assumed
identical. PCI scanning also had an earlier read-only capability probe that
dereferenced the BAR before xHCI validation.

INPUT3D-R1 now decodes all defined PCI memory BAR types, masks address and
attribute fields, validates the full xHCI register span for overflow, verifies
PCI memory-space decoding, maps the physical BAR into the kernel's AMD64 UC,
non-executable MMIO window, and checks the returned physical-to-virtual
translation and covered span before the first register read. The xHCI path
performs no raw BAR dereference. The overlay reports BAR0/BAR1, decoded base,
type/attributes, initial and current PCI memory-enable state, required span,
mapping result, validation substage, and reason. CAPLENGTH and HCIVERSION are
published only after the initial capability range maps and passes validation.
BAR1 zero is accepted as address data; invalid or unmapppable resources still
stop initialization with their reason intact. No new physical boot has been
performed, so this repair is not yet physical acceptance and makes no HID
input claim.

## Physical comparison procedure

Do not write either ISO to removable media automatically. Boot only with the
machine's normal removable-media procedure and only when the user is ready to
authorize that physical boot.

### Test 1 — ASUS VivoBook, normal R4

1. Boot `guideXOS-ModernInput-INPUT3D-R4.iso`.
2. Wait about 60 seconds and photograph the panel. Record the last entered
   operation, last returned operation, operation sequences, PCI identity,
   audio status, USB scan status, xHCI status and first failure.
3. If the frontier is unchanged, leave it for up to five minutes only if that
   adds useful evidence. Do not repeatedly power-cycle the machine.
4. Check the built-in keyboard and touchpad independently. If external wired
   USB devices are available, test them separately; the built-in touchpad may
   use I2C-HID.

### Test 2 — ASUS VivoBook, audio-bypass R4

Run this comparison only if Test 1 remains blocked in the PCI audio stage.

1. Boot `guideXOS-ModernInput-INPUT3D-R4-AudioBypass.iso`.
2. Verify the panel or COM1 explicitly reports
   `PCI AUDIO: DIAGNOSTICALLY SKIPPED`.
3. Observe whether USB-host discovery begins; photograph the frontier and any
   xHCI failure.

If the bypass reaches USB discovery while normal R4 does not, that strongly
implicates the skipped audio stage, but additional evidence is needed to name
the exact defective operation.

### Test 3 — SARAH_DT or LEON2_RDT

After the VivoBook comparison, boot the normal R4 ISO on one second machine
when authorized. LEON2_RDT is the HP ProDesk 600 G6 MT with known Intel Q470
xHCI controller `8086:06ED` and external wired keyboard and mouse. SARAH_DT is
the other comparison system from the earlier qualification plan.

For any machine, record whether PCI discovery, BAR/capability reads, ownership,
halt/reset, DMA, ring startup, controller running, port reset, Enable Slot,
Address Device and descriptor retrieval reached `PASS`, `FAIL`, `TIMEOUT`,
`BLOCKED`, `UNSUPPORTED`, or remained `NOT ATTEMPTED`/`UNKNOWN`. Include a
panel photo and input observations. QEMU results do not replace these physical
observations.

## INPUT3D-R6: AMD IOMMU discovery and xHCI routing

R6 adds a bounded, allocation-free IVRS parser and read-only IOMMU snapshot for
the AMD64 UEFI path. The layout follows the [AMD I/O Virtualization Technology
(IOMMU) Specification, 48882 Rev 3.09-PUB, October 2023](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/specifications/48882_IOMMU.pdf)
and the ACPI table/header rules in [ACPI Specification 6.6](https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf).
The loader checks the RSDP, RSDT/XSDT, table spans, and checksums while UEFI
memory services are still available. It copies exactly one IVRS table into
loader-owned pages before `ExitBootServices`; the kernel reads this copy and
does not follow the firmware RSDP pointer.

The R6 BootInfo handoff was version 3. R7 advances it to version 4 and retains
the IVRS copy physical base, byte length, and table count, plus flags for a
valid ACPI remap scan, any DMAR/IVRS table, a valid IVRS copy, and multiple
IVRS tables. The loader identity-maps the copy before handoff. Both the shared BootInfo validator and
the kernel's early validation check alignment, bounds, count/flag consistency,
scan validity, overflow, version, exact structure size, and checksum before the
kernel casts the physical base to a pointer. A malformed scan or a DMAR and
IVRS coexistence keeps the table state unknown/unparsed and blocks xHCI DMA.

### Parser bounds and supported records

The parser accepts IVRS tables up to 1 MiB, at most eight IVHD units, 96 device
rules per unit, and 64 IVMD ranges. ACPI checksum, table length, subtable
length, entry length, range ordering, and arithmetic overflow are checked
before any record is retained. It understands IVHD block types `0x10`, `0x11`,
and `0x40` (24-byte and 40-byte fixed headers), and IVMD types `0x20`, `0x21`,
and `0x22` (32-byte records). It retains IVMD range attributes for diagnostics;
it does not apply them to page tables or create DMA permissions.

Supported IVHD device records include PAD4, ALL, SELECT, START/END RANGE,
PAD8, ALIAS SELECT/RANGE, EXTENDED SELECT/RANGE, SPECIAL DEVICE, and the
variable-length ACPI HID entry. The alias UsedID field is read at byte offset
4 for alias entries; the special-device variety and UsedID are at offsets 4
and 5. ACPI HID records use UID format at offset 20, UID length at 21, and UID
bytes from 22 onward. Unrecognized structures or device entries are skipped
only when their encoded span is bounded; the topology is then marked
unsupported and no controller route is accepted. Truncated or inconsistent
records invalidate the table. Multiple IVRS tables, conflicting unit
coverage, conflicting aliases, and unmatched devices remain ambiguous or
unresolved; the parser never picks the first matching unit.

The controller requester ID is formed as `(bus << 8) | (device << 3) |
function`. R6 currently routes PCI segment 0 because the existing PCI config
reader has no segment argument. Nonzero-segment IOMMU units cannot be inspected
through that reader and remain unresolved/unsupported.

### Read-only IOMMU register qualification

Before mapping an IOMMU register window, R6 validates the unit's PCI identity,
class `08:06:00`, PCI capability-list presence, capability ID `0x0f`, IOMMU
capability type, capability revision, and enabled Base field against the IVHD
unit's MMIO address. It rejects unsupported PCI segments, misaligned or
overflowing bases, insufficient register spans, and inconsistent resources.
For a supported revision, it maps a 16 KiB span as uncached, supervisor-only,
non-executable device MMIO. It then performs volatile 64-bit reads of the
Control register at offset `0x0018` and Status register at `0x2020`. Only
Control bit 0 (`IommuEn`) is interpreted as enabled/disabled; if identity,
revision, mapping, or reads cannot be validated, translation state stays
unknown. The panel and serial log report the read status and state, not the raw
register values. The code does not write PCI configuration, IOMMU registers,
command buffers, event logs, or device-table entries; it does not enable,
disable, reset, or program an IOMMU.

The overlay adds IVRS presence/status, table and unit counts, per-xHCI segment,
requester ID, effective alias ID, resolved IOMMU identity/base, register
inspection state, translation observation, and the DMA block reason. Each
xHCI controller keeps independent route and inspection status. The selected
controller's first failure remains visible. A clear `IommuEn` bit is an
observation only and does not authorize direct DMA: R6 has no IOVA allocator,
domain setup, page-table ownership contract, invalidation path, or validated
device DMA mapping. The existing xHCI DMA prerequisite stays blocked unless
the preexisting direct-physical DMA contract is established with a valid ACPI
remap scan showing no remapping table. An absent/invalid/ambiguous/unsupported
table, unmatched route, unreadable IOMMU, or unestablished mapping remains
blocked with a specific reason.

### R6 physical observation — pending

Use `guideXOS-ModernInput-INPUT3D-R6-AudioBypass.iso` only when an authorized
physical boot is available. Capture the build identity, ACPI scan/IVRS state,
each xHCI requester and selected route, IOMMU identity and register-read state,
DMA block reason, and the first failure. Record each controller's state
separately. Do not change firmware settings or write the ISO to USB as part of
this software qualification. A panel or QEMU log demonstrates diagnostics;
only observed device behavior on the target machine can establish hardware
acceptance.

R6 does not implement HID input, alter firmware settings, reconfigure the
IOMMU, write USB media, or establish physical hardware acceptance. QEMU
qualification verifies the software path only. Physical acceptance still
requires the per-system procedure above and observed keyboard/mouse behavior.
R7 should define and validate the complete IOVA/domain mapping contract before
any remapped xHCI controller can be permitted to DMA.

## INPUT3D-R7: Intel scratchpads and VT-d diagnostics

R7 adds the Intel path alongside the existing AMD IVRS implementation. It
keeps the AMD parser and read-only inspection source unchanged. The loader
copies a single checksummed DMAR table into loader-owned identity-mapped pages
and supplies its base, length, and count through BootInfo v4. Multiple tables,
malformed handoff fields, or an unsupported table revision never provide a
route.

### Scratchpad interpretation and bounded storage

The xHCI decoder reads HCSPARAMS2 bits 25:21 as the high field and bits 31:27
as the low field, then calculates `(high << 5) | low`. The architectural field
can represent 0 through 1023. R6's software pool was capped at 16 buffers; that
cap was a static software limit, not an xHCI hardware limit. The R7 pool holds
34 scratchpad pages, matching the count in the earlier physical log. That log
did not include the raw HCSPARAMS2 dword, so R7 records the raw value and both
fields on the next boot instead of inventing one from the count.

Each page is selected from the controller's PAGESIZE bitmap, up to 64 KiB. A
34-page pool plus one maximum-page alignment margin reserves about 2.19 MiB of
static kernel storage. The xHCI driver validates the full backing span against
the loaded kernel image, translates it through the loader-provided physical
base, checks alignment, contiguity and AC64 addressability, clears the pages,
and validates the entire Scratchpad Buffer Array before setting DCBAA[0]. A
failed check clears the array and leaves DCBAA[0] zero. The driver still has a
single-controller DMA workspace; it does not share active mutable rings across
simultaneous xHCI instances. Counts above 34 remain blocked until a verified
DMA page allocator and per-controller lifetime model exist.

The field definitions and allocation procedure follow the [Intel xHCI
specification, sections 4.20, 5.3.4, 5.4.3 and 6.6](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf).

### DMAR parser and routing

The bounded parser supports DRHD, RMRR, ATSR, RHSA, and PCI device scopes for
endpoints and bridges. It also validates the known I/O APIC, HPET, and ACPI
namespace scope encodings. It checks ACPI checksum, table and substructure
lengths, ordering, reserved fields, register-range arithmetic, PCI path bounds,
RMRR page boundaries, duplicate DRHD/RMRR/RHSA identities, and RHSA-to-DRHD
association. Unknown DMAR structures, unsupported scope records, and
unsupported topologies remain visible as unsupported and prevent a route from
being accepted.

For segment 0, the kernel reconstructs the target xHCI bridge path from bounded
PCI configuration reads. Each parent must be a unique PCI-to-PCI bridge whose
primary/secondary bus numbers match the discovered chain. Explicit endpoint
and bridge scopes are matched against that path; a segment-matching
INCLUDE_PCI_ALL unit is only a fallback and must follow other DRHDs for that
segment. Overlapping explicit coverage is ambiguous. The current PCI config
reader cannot address nonzero segments, so those routes remain unsupported.

DMAR definitions and routing follow the [Intel VT-d Architecture
Specification, Rev. 4.0, sections 8.1–8.6](https://cdrdv2-public.intel.com/671081/vt-directed-io-spec.pdf).

### Read-only VT-d state and DMA eligibility

Before MMIO mapping, R7 validates the DMAR-declared register-set size, required
register span, alignment, and address overflow. It uses the existing UC,
kernel-only, non-executable MMIO mapper. It reads VER first and stops if the
major version is unsupported. For a supported unit it reads CAP, ECAP, GCMD,
GSTS, RTADDR and FSTS through volatile accesses. It does not write or clear
any VT-d register. Translation enable is reported from GSTS.TES; RTADDR
reserved bits, table mode, and whether a nonzero root address is configured
are classified separately. CAP.MGAW and CAP.SAGAW are shown as observed
capabilities, not proof of a per-device mapping.

An Intel xHCI routed to a DMAR unit remains blocked even when registers are
readable, translation is enabled, the root address is configured, and its
scratchpad pages are physically valid. R7 does not set up an IOVA domain,
program a translation root, satisfy RMRR mappings, or establish device-visible
addresses. It therefore reports `DEVICE_DMA_MAPPING_NOT_ESTABLISHED` (or the
more specific routing/register/RMRR blocker) and never authorizes the remapped
Intel controller to DMA. A disabled translation observation is not permission
to bypass VT-d. The preexisting valid-scan/no-remapping direct-DMA contract is
unchanged; AMD systems with IVRS continue on their IVRS safety path. If an AMD
platform lacks IVRS but advertises DMAR, the DMAR path remains fail-closed.

When a DMAR table is present, read-only route diagnostics also inspect xHCI
devices with non-Intel PCI vendor IDs. This lets virtual and mixed-vendor
systems report DMAR coverage without changing AMD DMA eligibility, which still
uses the IVRS path.

The overlay now shows platform, raw HCSPARAMS2 and decoded fields, required and
supported scratchpad capacity, allocation status, IVRS/DMAR summaries,
per-controller VT-d route, register/translation/root-table state, DMA reason,
and the existing first-failure and controller stages. Short layouts use a
bounded concise view and the displayed snapshot generation reflects the actual
last-presented sequence.

### R7 qualification and physical observations

Hosted tests cover HCSPARAMS2 decoding, the 34-page backing boundary, page
selection, failure clearing, DMAR bounds and checksums, scopes and bridge
routing, INCLUDE_PCI_ALL precedence, RMRR/ATSR/RHSA validation, unsupported
structures, VT-d resource/register classification, multi-controller diagnostic
publication, and DMA refusal without a mapping. These tests use synthetic
firmware and register data; they do not establish SARAH_DT or LEON2_RDT's raw
HCSPARAMS2, DMAR topology, VT-d state, or xHCI Running state.

The R6 physical log reports 34 scratchpads on SARAH_DT (`00:14.0 8086:06ED`)
but does not contain a raw HCSPARAMS2 sample. Capture the R7 serial log and
panel on SARAH_DT to establish the exact dword, hi/lo fields, DMAR table count,
DRHD units, route, read-only register state, and first remaining DMA blocker.
Do not retry controller halt/reset while DMA eligibility remains blocked. After
SARAH_DT evidence is recorded, repeat the same comparison on LEON2_RDT; do not
assume its firmware tables match because the chassis model is the same.

R7 did not implement USB HID delivery, reconfigure VT-d, alter BIOS settings,
disable audio in production, or claim physical acceptance. R8 adds conditional
direct-physical qualification; it does not enable Intel IOVA domains. See the
R8 section below for the remaining physical and mapping limits.

## INPUT3D-R8: conditional Intel direct-physical DMA

R8 replaces the R7 refusal for Intel hardware whose validated DMAR route
reports translation disabled. The former first blocker was
`VTD_TRANSLATION_DISABLED_MAPPING_UNAVAILABLE`. That reason did not establish
that direct DMA was safe; it identified that R7 had no device-visible address
contract. R8 adds a narrow admission contract and continues to block whenever
one of its independent proofs is missing.

### Specification basis

- The reviewed Intel VT-d source is the **Intel Virtualization Technology for
  Directed I/O Architecture Specification, Revision 4.1, Order Number
  D51397-016**. Sections 8.1–8.3 describe remapping-unit scope and PCI topology;
  section 8.4 defines RMRR reporting and requires its memory-map range to be
  reported reserved or as EFI runtime; sections 11.4.4.1 and 11.4.4.2 define
  GCMD and GSTS. GCMD.TE is write-only and a read is undefined; GSTS.TES is the
  read-only translation status. R8 therefore bases the observed state only on
  GSTS.TES and no longer reads GCMD. [Intel VT-d Architecture Specification,
  Rev. 4.1](https://cdrdv2-public.intel.com/774206/vt-directed-io-spec%20.pdf)
- The reviewed xHCI source is the **Intel xHCI Requirements Specification,
  Revision 1.2, May 2019**. Relevant sections include 4.2 (initialization),
  4.20 (scratchpads), 4.21.2 (PCI Bus Master Enable), 4.22.1 (pre-OS handoff),
  5.3.4 (HCSPARAMS2), 5.4.3 (PAGESIZE), 5.4.5–5.4.6 (CRCR/DCBAAP), 5.5.2.3
  (event-ring registers), 6.1 (DCBAA), 6.3 (TRB rings), 6.5 (ERST), and 6.6
  (Scratchpad Buffer Array). [Intel xHCI Requirements Specification,
  Rev. 1.2](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf)
- The reviewed firmware contract is **UEFI Specification 2.10 Errata A**,
  sections 7.2.1 (page allocation), 7.2.3 (`GetMemoryMap` and descriptor
  stride), and 7.4.6 (`ExitBootServices`). The loader passes the descriptor
  stride and map into BootInfo, and the kernel validates the stride rather
  than assuming a fixed descriptor size. [UEFI 2.10 Errata A, Boot Services](https://uefi.org/specs/UEFI/2.10_A/07_Services_Boot_Services.html)

### Admission contract

`DMA_VERIFIED_DIRECT_PHYSICAL` is published only after all of these conditions
pass:

1. DMAR parsed and checksummed as valid; the selected xHCI has an unambiguous
   segment-0 DRHD route. Unsupported scopes/topology, uncovered or ambiguous
   routes, and nonzero PCI segments fail closed.
2. The saved requester identity matches fresh PCI vendor, device, class and
   programming-interface reads. The bounded bridge path is reconstructed
   again and must match the path used for DMAR scope resolution.
3. The routed VT-d register set is valid, readable, and reports supported
   capabilities. Translation is disabled only when GSTS.TES is readable and
   clear. GSTS.TES set blocks direct DMA; unreadable or invalid register state
   remains unknown/blocked. The root-table register must not have invalid or
   unknown encoding. No VT-d register is written, and existing firmware
   mappings are never replaced.
4. Every static xHCI DMA workspace address is derived from the kernel's
   validated physical load base, lies inside the loaded kernel image, satisfies
   the xHCI alignment and AC64 checks, and has complete UEFI `EfiLoaderData`
   coverage. The workspace includes command/event rings, ERST, DCBAA, input and
   device contexts, EP0 ring, control buffer, scratchpad storage and array.
   These objects remain static for the kernel lifetime and are reserved to the
   one selected controller.
5. Every RMRR has a valid 4 KiB-aligned range and scope. Its memory-map
   coverage must be `EfiReservedMemoryType`, `EfiRuntimeServicesCode`, or
   `EfiRuntimeServicesData` with `EFI_MEMORY_RUNTIME` set for runtime types.
   Gaps, ambiguous descriptors, other memory types, and any overlap with the
   xHCI workspace block admission. Each entry logs segment, base, limit,
   whether its scope covers the selected xHCI, ownership result, and
   workspace-overlap result. Unrelated RMRRs are still validated and retained;
   none are rewritten or reused.
6. PCI bus mastering is observed disabled before enabling PCI memory decoding
   and before DMA qualification. If it is already enabled, R8 blocks rather
   than trying to clear it while firmware ownership or controller activity is
   unknown. After qualification, ownership
   handoff, halt, reset, CNR clear, and ring configuration, R8 enables BME with
   a 16-bit PCI command write and verifies readback before setting Run/Stop.
7. If Running times out, the timeout snapshot remains the sticky first
   failure. R8 makes one bounded attempt to clear Run/Stop and observe
   HCHalted; only after halt is confirmed does it clear and verify PCI BME.
   If halt cannot be confirmed, BME remains enabled because the controller may
   still be issuing DMA. R8 does not retry reset or restart the controller.

This mode means the xHCI is given vetted **system physical** addresses while
VT-d translation is disabled. It does not provide IOMMU isolation from a
misbehaving or compromised controller. The contract relies on the statically
owned xHCI structures, validated address widths, the supported x86 PCIe DMA
coherency model, and the controller's descriptor-following behavior. It is not
a general DMA allocator or a claim that arbitrary physical memory is safe to
expose. Intel devices with enabled translation still require a future explicit
DMA domain/IOVA implementation.

### RMRR evidence and platform limit

The accepted SARAH_DT R7 transcript reports **three RMRR regions**, but it
does not contain their segment, base, limit, or device-scope entries. R8 does
not infer those values or claim which entries apply to `00:14.0`. The next
physical boot will log each raw region and its current applicability and
ownership result. Until that boot, SARAH_DT's exact RMRR ownership and direct
DMA eligibility remain unobserved. An RMRR that overlaps the workspace, lacks
reserved/runtime map ownership, or has unsupported scope blocks DMA.

For future VT-d enablement, each selected-device RMRR must remain reserved and
be identity-mapped into that device's remapping domain as required by the
platform contract; its pages cannot be allocated to controller rings or other
kernel data. R8 does not enable translation, build a domain, or perform
invalidation.

### Controller state and diagnostics

The one-shot controller state machine remains DMA approval → ownership
resolution → halt → reset → CNR clear → ring setup → BME enable → Run/Stop.
Each wait remains bounded. No second controller can reuse the active static
workspace, and a failed initialization is not blindly retried. After Running,
the existing code samples and reports each root port without clearing
write-one-to-clear status bits. Running, root-port connection, USB descriptor
completion, and HID delivery remain separate outcomes.

The overlay now reports physical-backing and reserved-memory validation
independently, in addition to VT-d route, RMRR relevant/unrelated counts,
GSTS.TES, DMA decision reason, controller identity, first failure, and stage
results. `NOT ATTEMPTED` remains distinct from a successful result.

### R8 verification and physical procedure

Hosted tests cover GSTS-only translation classification; enabled, disabled,
unreadable and unsupported states; exact requester and route decisions;
physical range alignment, overflow, missing/partial backing, ownership type,
and addressability; relevant/unrelated and overlapping RMRRs; BME state; and
exclusive workspace ownership. They use synthetic maps and register values,
not SARAH_DT measurements.

For a single controlled SARAH_DT boot, record the source/build identity,
selected xHCI BDF and vendor/device, raw HCSPARAMS2 and scratchpad fields, DMAR
status and DRHD route, every logged RMRR entry, GSTS.TES and root-table state,
physical-backing and reserved-memory results, final DMA decision, and exact
first failure. Only if DMA is verified should the log proceed through ownership,
halt/reset/CNR, Running, and root-port status. Do not change BIOS settings or
disable VT-d. A blocked result is valid evidence; a Running result does not
prove keyboard/mouse HID delivery without observed HID events. No physical
boot was performed during source qualification, so SARAH_DT controller Running
remains unverified.
