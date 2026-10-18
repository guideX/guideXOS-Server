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
  controller. The first `FAIL` or `TIMEOUT` includes a copied controller
  identity and stage result and survives later status updates or enumeration.
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
  the linker-defined image span bounds every DMA structure. The kernel does
  not discover IOMMU state, so the panel reports IOMMU/addressability as
  unknown where it cannot be proven.
  Scratchpad buffers are unsupported and block xHCI DMA setup when required.
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

Use the same INPUT3D ISO on each machine. Do not write it to a USB drive from
the build procedure; boot it using the machine's normal removable-media path.

### SARAH_DT

1. Boot the ISO and wait for the guideXOS desktop.
2. Photograph the diagnostic panel and record controller PCI identity, xHCI
   lifecycle statuses, first failure, connected-port count, and enumeration
   result.
3. If safe and practical, move an ordinary wired USB mouse and type on the
   wired keyboard; report whether the panel or desktop reacts.

### ASUS VivoBook

1. Boot the same ISO and photograph the same panel fields.
2. Check the built-in keyboard and touchpad independently.
3. If an external wired USB keyboard and mouse are available, test them
   separately. A built-in touchpad may use I2C-HID, so its failure alone does
   not establish a USB transport failure.

### LEON2_RDT

When ready, boot the same ISO locally and photograph the same fields. This
HP ProDesk 600 G6 MT has the known Intel Q470 xHCI controller `8086:06ED` and
known external USB keyboard and mouse, making it the comparison system. No
remote reboot is part of this procedure.

Record whether PCI discovery, BAR/capability reads, ownership, halt/reset,
DMA, ring startup, controller running, port reset, Enable Slot, Address Device,
and descriptor retrieval reached `PASS`, `FAIL`, `TIMEOUT`, `BLOCKED`,
`UNSUPPORTED`, or remained `NOT ATTEMPTED`/`UNKNOWN`. Return the panel photo and
any input observations for the next driver change.
