# guideXOS Modern Input — INPUT2

**Phase:** INPUT2
**Workstream:** guideXOS modern keyboard/mouse input for physical hardware
**Branch:** `MODERN_INPUT`
**Status:** implementation complete, hosted tests pass (see section 12)

This document separates **observed evidence** from **inference** and
**unknowns**. Nothing here claims physical guideXOS xHCI success; that
requires a real bare-metal boot capture (section 8).

---

## 1. Purpose

INPUT1 established that LEON2_RDT has a single Intel xHCI controller
(`PCI\VEN_8086&DEV_06ED`, class `0C/03/30`) and that guideXOS has no xHCI
driver. INPUT2 implements a bounded xHCI controller bring-up: PCI/BAR
validation, BIOS/OS ownership handoff, safe halt/reset/start state machine,
DMA-safe controller structures (DCBAA, command ring, event ring), command
ring foundation, event ring/interrupter (polling mode), root-port
enumeration/status, and port status change events.

INPUT2 does **not** implement full USB enumeration, device addressing,
endpoint configuration, or HID report delivery. Those are INPUT3 scope.

---

## 2. Repository baseline (observed)

- Repository: `guideXOSServer_MODERN_INPUT` (`guideXOS/guideXOS-Server`)
- Branch: `MODERN_INPUT`
- Starting HEAD: `4ecefc2d` (INPUT1 completion)
- Upstream: `origin/MODERN_INPUT` (0 ahead / 1 behind at start)
- Working tree: clean at start
- `.phase-modern-input`: `last_completed=INPUT1`, `next_expected=INPUT2`

---

## 3. SARAH_DT topology

SARAH_DT is **not accessible** from the current environment (powered off,
no SSH known_hosts entry, no SSH config). Per the INPUT2 prompt, this
does not block the phase.

**Classification:** SARAH_DT topology remains **unknown**. The user
reports it is "pretty much the same as LEON2_RDT." If true, the xHCI-first
strategy is reinforced. No SARAH-specific production hacks are permitted.

---

## 4. Mbed TLS bootstrap

The INPUT1 full-link blocker was the absent pinned `third_party/mbedtls`.
The repository's authoritative bootstrap script
(`scripts/bootstrap-mbedtls.ps1`) was inspected and found safe:

- Clones from pinned upstream repos at pinned commits
  (mbedtls 4.1.0 @ `0fe989b6`, TF-PSA-Crypto 1.1.0 @ `29160dd`)
- Applies tracked patch series
- Verifies profile SHA-256 checksums
- Does not change global Git authentication/configuration
- Does not overwrite unrelated source

Bootstrap was executed with `-Install` after installing Python dependencies
(`jsonschema`, `jinja2`). Result: **PASS**. The pinned Mbed TLS tree is
now present at `third_party/mbedtls`.

---

## 5. LEON2_RDT pre-mutation diagnostics

Physical boot of LEON2_RDT with INPUT1 diagnostics was **not performed**
during this run. The INPUT1 diagnostic milestones (PCI discovery, BAR
report, capability/protocol parsing) are expected to produce:

```text
[USB] host-controller scan begin
[USB] controller 0 bdf=00:14.0 ven=0x8086 dev=0x06ed class=0c/03/30 type=xHCI
[USB]   bar0=mmio64 base=0x........
[USB]   xHCI caps caplen=0x20 hciver=0x0110 slots=64 ports=16 ...
[USB]   xHCI proto name=USB3 major=3 ... ports=1-8 ...
[USB]   xHCI proto name=USB2 major=2 ... ports=9-16 ...
[USB] host-controller scan end controllers=1 xhci=1
```

This evidence is **not captured** and must be obtained from a real
bare-metal boot.

---

## 6. xHCI implementation

### 6.1 New module

- `kernel/core/include/kernel/xhci.h` — architecture-independent types,
  constants, and pure logic:
  - TRB types, completion codes, and structures
  - Operational/runtime/doorbell/port register offsets and bit masks
  - Event Ring Segment Table (ERST) entry
  - USB Legacy Support extended capability constants
  - Controller state machine and ownership state enums
  - Port status snapshot struct
  - Pure logic helpers: TRB decoding, port status decoding, ring helpers,
    ERST helpers, PCI BAR validation, ownership detection, name helpers
- `kernel/core/xhci.cpp` — controller bring-up implementation:
  - PCI discovery (class 0C/03/30)
  - BAR validation (32-bit, 64-bit, I/O rejection, zero/all-ones rejection)
  - Capability parsing (reuses INPUT1 parser)
  - BIOS/OS ownership handoff (bounded timeout)
  - Safe halt/reset/start state machine (all waits bounded)
  - Page size validation (4K required)
  - MaxSlots configuration
  - DCBAA initialization
  - Command ring initialization (with link TRB wrap)
  - Event ring initialization (ERST, ERSTBA, ERDP)
  - Interrupter configuration (polling mode)
  - Controller start (Run/Stop)
  - Event processing (port status change, command completion, transfer event)
  - Root port status reading

### 6.2 Boot integration

`kernel/core/main.cpp` calls `kernel::usb::xhci::controller::init()` after
the INPUT1 diagnostics scan. The xHCI init is **not** gated on a feature
macro; it runs on any architecture with PCI port I/O access. On
architectures without PCI config access, the init is a no-op.

### 6.3 What INPUT2 does **not** do

- No USB device enumeration (Enable Slot, Address Device, etc.)
- No endpoint configuration or control transfers
- No HID report delivery
- No MSI/MSI-X interrupt-driven event delivery (polling mode only)
- No hub enumeration
- No device context population beyond DCBAA zero-initialization

---

## 7. DMA / controller structures

All DMA structures are statically allocated with proper alignment:

| Structure | Alignment | Size | Initialization |
|---|---|---|---|
| Command ring | 64 bytes | 64 TRBs | Zeroed + link TRB |
| Event ring | 64 bytes | 64 TRBs | Zeroed |
| ERST | 64 bytes | 1 entry | Base addr + size |
| DCBAA | 64 bytes | 256 entries | Zeroed |

Physical addresses are obtained via `reinterpret_cast<uint64_t>` of the
static arrays. The implementation assumes identity mapping (physical ==
virtual) which is valid for the guideXOS kernel's current memory model.
A full implementation would use the kernel's physical-to-virtual translation
helpers.

---

## 8. Real-hardware acceptance target

The strongest INPUT2 result on LEON2_RDT is:

1. guideXOS boots,
2. xHCI BAR/capabilities are sane,
3. firmware ownership is resolved if necessary,
4. controller safely resets,
5. controller reaches Running state,
6. event ring is functional (polling mode),
7. root-port status is readable,
8. at least one connected device/port is observed,
9. guideXOS remains alive and responsive through serial/diagnostic output,
10. repeated reboot produces the same result.

**This evidence is not yet captured.** A physical boot on LEON2_RDT is
required.

---

## 9. Hosted tests

`tests/xhci_test.cpp` + `scripts/run-xhci-test.ps1` exercise:

- **BAR validation:** 32-bit MMIO, 64-bit MMIO, I/O rejection, zero,
  all-ones, reserved type, 64-bit with zero/all-ones upper dword
- **Ownership detection:** no capability, BIOS-owned, OS-owned, both set
- **Ring helpers:** size validation, power-of-two check, index wrapping
- **ERST helpers:** size validation, base address encoding
- **Port status decoding:** connected/enabled/powered, disconnected,
  all change bits
- **TRB decoding:** type, cycle bit, completion code, event data,
  port status change detection
- **Name helpers:** link state, speed, completion code, TRB type
- **PORTSC bit tests:** all individual bits
- **PLS/speed extraction:** all link states and speeds

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run-xhci-test.ps1
```

Result: **PASS** (100+ assertions).

---

## 10. Diagnostic milestones

The bare-metal log distinguishes:

```text
[XHCI] init begin
[XHCI] PCI device found
[XHCI] capability registers valid
[XHCI]   caplen=0x20 hciver=0x0110 slots=64 ports=16 ...
[XHCI] MMIO BAR validated base=0x........ (64-bit)
[XHCI] ownership not required / already OS-owned / acquired / timed out
[XHCI] controller already halted / halted
[XHCI] controller reset complete
[XHCI] page size validated (4K)
[XHCI] MaxSlots configured: 64
[XHCI] DCBAA configured
[XHCI] command ring configured
[XHCI] event ring configured
[XHCI] interrupter/polling configured
[XHCI] controller running
[XHCI] port status change port=N cc=Success
```

Failures identify the stage and relevant status/register values.

---

## 11. Regressions

- PS/2 keyboard/mouse: **preserved** (no changes to PS/2 code)
- UHCI behavior: **preserved** (no changes to UHCI code)
- Generic HID decoding: **preserved** (no changes to HID code)
- VirtIO input: **preserved** (no changes to VirtIO code)
- Storage USB code: **preserved** (no changes to storage code)
- The xHCI init is **not** activated on hardware that is not xHCI
  (PCI class check)

---

## 12. INPUT2 acceptance classification

**Outcome B — implementation materially advanced but hardware/prerequisite gap remains.**

- Controller implementation is materially complete for INPUT2
- Hosted tests pass
- Mbed TLS bootstrap succeeded
- No physical boot could be performed on LEON2_RDT during this run
- The implementation is **hosted-only** until a real bare-metal boot
  proves controller bring-up and root-port observation

---

## 13. Recommended INPUT3 scope

1. **Physical boot capture on LEON2_RDT** with INPUT2 diagnostics.
2. **USB enumeration:** Enable Slot, Address Device, GET_DESCRIPTOR,
   configuration descriptor walking.
3. **Endpoint zero control-transfer framework.**
4. **HID interface selection and interrupt endpoint scheduling.**
5. **HID report delivery** into the existing input subsystem.
6. **Interrupt-driven event delivery** (MSI or MSI-X) as a replacement
   for polling mode.
7. **Hub enumeration** if root ports are not directly connected to
   keyboard/mouse.

---

## 14. xHCI specification reference

Implementation follows the **eXtensible Host Controller Interface for
USB (xHCI) specification, revision 1.1** (May 2011):

- Section 5.3: Capability Registers
- Section 5.4: Operational Registers
- Section 5.5: Runtime Registers
- Section 5.6: Doorbell Registers
- Section 6.1: Device Context Base Address Array
- Section 6.2.5: Command Ring
- Section 6.2.6: Event Ring
- Section 7.2: USB Legacy Support (extended capability)
- Section 7.4: USB Supported Protocol (extended capability)

No Linux/FreeBSD/other kernel code was copied. All logic was written
from the specification.
