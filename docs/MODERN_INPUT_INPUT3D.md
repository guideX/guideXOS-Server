# guideXOS Modern Input — INPUT3D hardware diagnostics

INPUT3D adds a boot-time diagnostic snapshot and a persistent desktop overlay
for investigating USB/xHCI input failures. It does not implement the xHCI HID
transport.

## Production boot path audited

On the AMD64 bare-metal path, `kernel_main()` initializes serial logging and
the diagnostic snapshot, processes the UEFI BootInfo or Multiboot handoff,
initializes the framebuffer and interrupt/timer infrastructure, then calls
`usb::diagnostics::scan_host_controllers()`. The scan classifies PCI USB host
controllers and reads xHCI BAR/capability data. It is followed by
`usb::xhci::controller::init()` and one bounded
`usb::xhci::controller::enumerate_device()` attempt. The xHCI path is therefore
linked into and invoked by an ordinary AMD64 desktop boot; it is not merely
present in the repository.

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

The audit also found two concrete hazards in the existing xHCI path: several
DMA register writes used kernel virtual addresses instead of translated
physical addresses, and the initial eight-byte device descriptor prefix was
validated with the full eighteen-byte rule. DMA translation/alignment/address
width checks and complete 64-bit register writes now guard initialization;
the descriptor prefix has its own validator. Root-port reset writes avoid
clearing unrelated write-one-to-clear status bits. IOMMU presence and physical
qualification still require real-machine evidence.

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
