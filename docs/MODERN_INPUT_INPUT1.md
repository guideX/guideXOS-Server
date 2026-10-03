# guideXOS Modern Input — INPUT1

**Phase:** INPUT1
**Workstream:** guideXOS modern keyboard/mouse input for physical hardware
**Branch:** `MODERN_INPUT`
**Status:** implementation foundation complete (see section 12)

This document separates **observed evidence** from **inference** and
**unknowns**. Nothing here claims physical guideXOS input success; that
requires a real bare-metal boot capture (section 8).

---

## 1. Purpose

GuideXOS currently boots on physical machines (notably `LEON2_RDT`) without
usable keyboard or pointer input. INPUT1 establishes the evidence base,
audits the existing input stack, and lands the smallest reusable,
standards-based foundation that makes the later bare-metal failure frontier
visible.

The ASUS VivoBook is an acceptance target that is **not** available for
inspection. No ASUS-specific hardware facts are asserted here.

---

## 2. Repository baseline (observed)

- Repository: `guideXOSServer_MODERN_INPUT` (`guideXOS/guideXOS-Server`)
- Branch: `MODERN_INPUT`
- Starting HEAD: `efd65fdd180e585a85080b2506876d4000673943`
- Upstream: `origin/MODERN_INPUT` (0 ahead / 0 behind at start)
- Working tree: clean
- Existing input/USB/ACPI work: a real UHCI driver, a real generic USB HID
  class driver with report-descriptor parsing, PS/2 keyboard/mouse drivers,
  a pointer mapping layer, and an MSI/MSI-X config-space library — but the
  USB stack is unwired (section 4).

---

## 3. LEON2_RDT hardware evidence (observed)

`LEON2_RDT` is the Windows machine used for this reconnaissance. All data
below comes from read-only Windows PnP/CIM queries. No device was disabled,
no driver was changed, no firmware was touched.

### 3.1 Platform

| Field | Value |
|---|---|
| Manufacturer / Model | HP / HP ProDesk 600 G6 Microtower PC |
| Baseboard | HP 8712 |
| BIOS | HP S02 Ver. 02.26.00 |
| CPU | Intel Core i5-10500 (Comet Lake) |
| Chipset LPC/eSPI | Intel Q470 (`PCI\VEN_8086&DEV_0687`, class 06/01/00) |

### 3.2 Keyboard

| Field | Value |
|---|---|
| Active keyboard | HID Keyboard Device |
| Instance | `HID\VID_17EF&PID_6099&MI_00\7&3F710AC&0&0000` |
| USB parent | `USB\VID_17EF&PID_6099&MI_00\6&197b5c4f&1&0000` |
| Composite parent | `USB\VID_17EF&PID_6099\5&18a845db&0&9` (Port_#0009, root hub) |
| Service | `kbdhid` (child of `HidUsb`) |
| HID usage | `UP:0001_U:0006` (Generic Desktop / Keyboard) |
| Hardware ID | `HID\VID_17EF&PID_6099&REV_0104&MI_00` |
| Vendor | `VID_17EF` (Lenovo) |

Legacy PS/2 keyboard node:

| Field | Value |
|---|---|
| Device | Standard PS/2 Keyboard (`ACPI\HPQ8002`, compatible `*PNP0303`) |
| Parent | LPC controller `PCI\VEN_8086&DEV_0687` |
| Service | `i8042prt` |
| Status | **Error** |

### 3.3 Pointer

| Field | Value |
|---|---|
| Active pointer | HID-compliant mouse |
| Instance | `HID\VID_046D&PID_C077\6&1DC15C22&0&0000` |
| USB parent | `USB\VID_046D&PID_C077\5&18a845db&0&2` (Port_#0002, root hub) |
| Service | `mouhid` (child of `HidUsb`) |
| Compatible IDs | `USB\Class_03&SubClass_01&Prot_02` → HID boot-protocol mouse |
| Vendor | `VID_046D` (Logitech) |

Legacy PS/2 pointer node:

| Field | Value |
|---|---|
| Device | PS/2 Compatible Mouse (`ACPI\PNP0F13`, compatible `*SYN0100`/`*SYN0002`) |
| Parent | LPC controller `PCI\VEN_8086&DEV_0687` |
| Service | `i8042prt` |
| Status | **Error** |

### 3.4 USB host controllers

| Field | Value |
|---|---|
| Controller | Intel USB 3.1 eXtensible Host Controller – 1.10 |
| Instance | `PCI\VEN_8086&DEV_06ED&SUBSYS_8712103C&REV_00\3&11583659&0&A0` |
| Class | `CC_0C0330` → base `0C`, subclass `03`, prog-if `30` (xHCI) |
| Location | PCI bus 0, device 20, function 0 |
| Parent | `ACPI\PNP0A08` (PCIe root complex) |
| Service | `USBXHCI` |

**This is the only USB host controller on the machine.** There is no UHCI,
OHCI, or EHCI controller. Both active input devices reach the OS through
this single xHCI controller and its USB 3.0 root hub
(`USB\ROOT_HUB30&VID8086&PID06ED`).

### 3.5 I2C / ACPI input evidence

- No `PNP0C50` (HID-over-I2C) devices.
- No Intel/AMD I2C or Serial-IO controllers.
- No touchpad/touchscreen/pen devices.
- No evidence of an ACPI-described I2C input transport.
- ACPI provides only the legacy PS/2 nodes (`HPQ8002`, `PNP0F13`) on the
  Q470 LPC controller, both reporting **Error** (no active PS/2 hardware).

### 3.6 Evidence table

```text
Physical/Logical Device
  -> Parent
  -> Controller
  -> Bus/Transport
  -> Windows service/driver
  -> Hardware IDs
  -> Relevance to guideXOS

HID Keyboard (Lenovo)
  -> USB composite USB\VID_17EF&PID_6099 (root hub Port_#0009)
  -> Intel xHCI PCI\VEN_8086&DEV_06ED (CC_0C0330)
  -> USB 2.0/3.x HID
  -> HidUsb -> kbdhid
  -> HID\VID_17EF&PID_6099&REV_0104&MI_00
  -> PRIMARY keyboard path guideXOS must support

HID Mouse (Logitech)
  -> USB\VID_046D&PID_C077 (root hub Port_#0002)
  -> Intel xHCI PCI\VEN_8086&DEV_06ED (CC_0C0330)
  -> USB 2.0 HID (boot protocol)
  -> HidUsb -> mouhid
  -> HID\VID_046D&PID_C077&REV_7200
  -> PRIMARY pointer path guideXOS must support

Standard PS/2 Keyboard
  -> LPC PCI\VEN_8086&DEV_0687
  -> i8042
  -> i8042prt
  -> ACPI\HPQ8002 (*PNP0303)
  -> legacy fallback; currently Error (no active device)

PS/2 Compatible Mouse
  -> LPC PCI\VEN_8086&DEV_0687
  -> i8042
  -> i8042prt
  -> ACPI\PNP0F13 (*SYN0100)
  -> legacy fallback; currently Error (no active device)
```

### 3.7 Confirmed facts / strong inference / unknowns

**Confirmed facts**
- The active keyboard and mouse are USB HID devices.
- The only USB host controller is Intel xHCI (`0C/03/30`).
- There is no UHCI/OHCI/EHCI controller.
- The ACPI PS/2 nodes exist but report Error; no I2C-HID devices exist.

**Strong inference**
- guideXOS needs an xHCI host controller driver plus USB HID to receive
  input from LEON2_RDT. PS/2 alone cannot provide input on this machine.

**Unknowns**
- Whether guideXOS's boot environment exposes the xHCI MMIO BAR (physical
  address identity mapping) — cannot be determined without a guideXOS boot.
- Whether the Lenovo keyboard interface advertises the HID boot subclass
  (Windows reports usage-page classification; boot subclass was not
  independently read from the descriptor on the Windows side).
- Whether `SARAH_DT` shares this topology (must be inspected).

---

## 4. Existing guideXOS input architecture (audited)

### 4.1 Pipeline (as implemented)

```text
hardware
  -> controller driver (PS/2 i8042, or per-arch USB HCI)
  -> transport (PS/2 IRQ1/IRQ12, USB control/bulk/interrupt transfers)
  -> HID/input decoder (usb_hid boot protocol + report-descriptor parser)
  -> kernel input contract (input_manager: polling + state snapshot)
  -> desktop/window manager (desktop::handle_mouse/handle_key)
  -> compositor (KernelCompositor::handleMouse*/handleKey*)
  -> application (KernelApp::onMouse*/onKey*)
```

The keyboard path bypasses `input_manager`: PS/2 keyboard has its own
lossless ring buffer consumed directly by the desktop main loop.

### 4.2 What exists

- **PS/2 keyboard** (`kernel/core/ps2keyboard.cpp`) with IRQ1 and a
  lossless event ring buffer.
- **PS/2 mouse** (`kernel/core/ps2mouse.cpp`) with IRQ12.
- **UHCI driver** (`kernel/arch/amd64/usb_hci.cpp`, `arch/x86/...`) — a real
  driver (frame list, QH/TD scheduling, control/bulk/interrupt transfers).
- **Generic USB core** (`kernel/core/usb.cpp`) — descriptor parsing and
  enumeration over the `usb::hci` abstraction.
- **Generic USB HID class driver** (`kernel/core/usb_hid.cpp`) — boot
  protocol plus a report-descriptor parser; not tied to UHCI.
- **Pointer mapping** (`kernel/core/display_input_mapper.cpp`) and
  **compositor routing** (`kernel/core/kernel_compositor.cpp`).
- **MSI/MSI-X config-space library** (`kernel/core/msi.cpp`), mostly unused.

### 4.3 What is missing or unwired

1. **No xHCI or EHCI implementation.** The only host-controller code on
   x86/amd64 is UHCI. LoongArch merely *classifies* xHCI/EHCI and has no
   bring-up or transfers.
2. **The USB stack is not started on amd64/x86.** `usb::init()`,
   `usb_hid::probe()`, and `usb::poll()` have no callers; `main.cpp` never
   touches USB. The feature macros `KERNEL_HAS_USB_HID` and
   `KERNEL_HAS_VIRTIO_INPUT` are never defined, so the USB-HID branches in
   `input_manager.cpp` are compiled out.
3. **VirtIO input is a stub** (detection/setup/poll all return false/no-op).
4. **No central PCI subsystem.** Every driver re-implements PCI config
   access; there is no shared device list or class registry.
5. **No ACPI table parsing** (no RSDP/FADT/MADT/MCFG) and no AML. The
   hardware support report's ACPI claims are not reflected in the code.
6. **No I2C / HID-over-I2C.**
7. **Input is legacy 8259 PIC only.** No IOAPIC/APIC in the input path; MSI
   routing and MSI-X table programming are stubs.

---

## 5. Failure frontier for LEON2_RDT (inferred)

Comparing the machine against the implementation:

- **Does guideXOS have xHCI support?** No.
- **Does it detect xHCI?** Not on x86/amd64 (UHCI only); LoongArch detects
  but cannot drive it.
- **Generic USB HID keyboard/mouse?** The code exists but is not started and
  has no controller to feed it on this machine.
- **Is HID parsing tied to UHCI?** No — it sits on the `usb::hci`
  abstraction — but no working HCI feeds it here.
- **EHCI?** No.
- **I2C / HID-over-I2C?** No; and the machine does not use them.
- **Could PS/2 initialization be failing?** On this machine there is no
  active PS/2 device (Windows reports the PS/2 nodes as Error), so PS/2
  cannot supply input regardless of driver health.
- **Interrupt assumptions?** Input currently depends on legacy PIC IRQ1/12;
  xHCI would typically need MSI/MSI-X or a shared legacy IRQ, neither of
  which is wired to input today.

**Exact missing layer:** an xHCI host controller driver (PCI discovery →
BAR → capability parsing → rings/ports → enumeration → interrupt transfers)
feeding the existing generic USB HID decoder and the existing input
subsystem.

The symptom ("no keyboard/mouse on newer machines") should **not** be
assumed to have the same cause on every machine until each is inspected.

---

## 6. INPUT1 changes

### 6.1 New standards-based module

- `kernel/core/include/kernel/usb_controller.h` — architecture-independent,
  pure logic:
  - PCI USB controller classification from class/subclass/prog-if
    (`00h` UHCI, `10h` OHCI, `20h` EHCI, `30h` xHCI, `80h` no-specific,
    `FEh` USB device).
  - xHCI capability-register parsing (CAPLENGTH, HCIVERSION, HCSPARAMS1/2,
    HCCPARAMS1/2, DBOFF, RTSOFF, xECP) with derived operational/runtime/
    doorbell/xECP byte offsets.
  - xHCI supported-protocol extended-capability parsing (bounded linked-list
    walk; major/minor revision, port range, PSIC, name).
  - Diagnostic formatting helpers.
- `kernel/core/usb_controller.cpp` — read-only kernel diagnostics:
  enumerates PCI USB controllers, classifies them, reports BDF, vendor/
  device, class/prog-if, BAR0 type/address, and for xHCI reads the
  **read-only** capability block and reports capability + supported-protocol
  milestones to COM1.

### 6.2 Boot integration

`kernel/core/main.cpp` calls `kernel::usb::diagnostics::scan_host_controllers()`
during hardware discovery. It does not initialize, reset, or modify any
controller; it only reads PCI config space and the xHCI capability block.

### 6.3 What INPUT1 does **not** do

- No xHCI initialization, rings, port enumeration, or transfers.
- No USB stack wiring into `input_manager`.
- No ACPI/I2C-HID.
- No ASUS-specific code, no per-machine PCI-ID special cases.

---

## 7. Tests

- `tests/usb_controller_test.cpp` + `scripts/run-usb-controller-test.ps1`
  exercise classification (all prog-if values, wrong class/subclass),
  capability parsing (valid, short buffer, null, all-ones, zero ports/rings,
  split scratchpad count), protocol parsing (valid, terminator, truncated
  entry, off-buffer next pointer, capacity clamp), combined snapshot
  parsing, and diagnostic formatting including small-buffer truncation.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run-usb-controller-test.ps1
```

Result: PASS.

### Build note

The full kernel link requires the pinned Mbed TLS tree under
`third_party/mbedtls`, which is not present in this checkout; `make` stops
with:

```text
guideXOS Mbed TLS dependency/profile is incomplete or generated files are
missing; run ../scripts/bootstrap-mbedtls.ps1 -Install
```

Bootstrapping it clones external repositories, so it was not performed
during INPUT1. The two touched translation units (`kernel/core/main.cpp`
and `kernel/core/usb_controller.cpp`) were compiled with the exact kernel
flags and are warning-free.

---

## 8. What must be captured from a real guideXOS boot

A later physical boot on `LEON2_RDT` should capture the serial (COM1)
output. INPUT1 makes the following milestones visible:

```text
[USB] host-controller scan begin
[USB] controller N bdf=.. ven=.. dev=.. class=0c/03/30 type=xHCI
[USB]   bar0=mmio64 base=0x........
[USB]   xHCI caps caplen=.. hciver=.. slots=.. ports=.. dboff=.. rtsoff=.. xecp=..
[USB]   xHCI proto name=USB3 major=3 ... ports=..
[USB]   xHCI proto name=USB2 major=2 ... ports=..
[USB] host-controller scan end controllers=.. xhci=..
```

This distinguishes: PCI controller discovered → type identified → BAR
reported → capability block read/valid → supported protocols parsed. The
next milestone (controller initialized, ports, devices, HID reports) does
not exist yet and is INPUT2 scope.

---

## 9. Future `SARAH_DT` comparison

`SARAH_DT` is expected to behave like `LEON2_RDT` but has not been
inspected. Before or during INPUT2, run the same read-only Windows queries
(keyboard/mouse/USB controllers/I2C) and diff the topology. Do not assume
identical causes from identical symptoms.

---

## 10. Future ASUS VivoBook comparison

The ASUS VivoBook is an acceptance target that is currently unavailable.
No ASUS hardware facts are asserted. When available, capture its topology
and a guideXOS boot log. If it turns out to use I2C-HID or a different
controller family, that must be handled by a generic transport, not by
machine-specific code.

---

## 11. Recommended INPUT2 scope

1. Bare-metal capture on `LEON2_RDT` using the INPUT1 diagnostics.
2. `SARAH_DT` topology comparison.
3. Bounded xHCI bring-up: controller reset/run, command/event/transfer
   rings, device slots, port enumeration — using the parsed capability
   layout from INPUT1.
4. Wire enumerated USB HID devices into the existing `usb_hid` decoder and
   the existing input subsystem (define `KERNEL_HAS_USB_HID` and start the
   stack), preserving PS/2 and VirtIO behavior.
5. Interrupt strategy for xHCI (MSI-X preferred; document legacy fallback).

---

## 12. INPUT1 acceptance classification

**Outcome A — useful implementation foundation complete.**

- LEON2_RDT topology characterized (section 3).
- guideXOS input architecture audited (section 4).
- Missing capability identified: xHCI + unwired USB HID (section 5).
- Reusable, standards-based implementation landed: classification,
  xHCI capability/protocol parsing, read-only boot diagnostics (section 6).
- Tests pass; touched kernel TUs compile cleanly (section 7).
- Bare-metal diagnostics materially improved (section 8).

This does **not** claim that the physical keyboard or mouse works yet.
