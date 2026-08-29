# AIDA_LPT I219-LM Driver Bind/Init — Phase 4

Status: source repair complete; fresh physical AIDA_LPT verification remains
pending. This record covers the confirmed software frontier and the automated
validation performed before the next hardware boot.

## Target and confirmed failure

The target is the wired Intel controller reported by AIDA_LPT as:

```text
vendor/device: 8086:156F
subsystem:     103C:8079
class:         02/00/00/21
```

The previous physical capture reached PCI identity enumeration, but the
controller was reported as `unsupported (identity only)`. Network diagnostics
therefore had no initialized NIC structure. DHCP, DNS, TCP, and link-layer
traffic were not the failure frontier.

The source audit identifies category **A** as the exact cause: `0x156F` was
declared and an I219-specific initialization branch already existed, but the
effective match predicates in the bootloader, kernel NIC initializer, and
network-status surface accepted only `0x100E`, `0x10D3`, and `0x153A`. The
bootloader consequently did not select the physical device for the supported
NIC handoff, so no mapped BAR or kernel NIC object could be reached.

This was not diagnosed as a BAR, reset, PHY, or DHCP failure; those stages had
not been reached on the physical machine.

## Repair architecture

The repair enables only Intel vendor `8086`, Ethernet subclass `00`, device
`156F`. The Intel wireless device `8086:24F3` remains outside the matcher.
The subsystem ID is retained for diagnostics and is not used as a laptop-only
special case.

The existing architecture is retained:

1. The UEFI bootloader enumerates PCI, reports all network identities, selects
   the exact Ethernet match, scans the conventional BARs read-only, enables
   PCI memory space and bus mastering, and maps the bounded register window.
2. The kernel verifies the handoff and PCI command bits before MMIO access.
3. The I219 branch reads the station address from `RAL0/RAH0`, requires the
   receive-address-valid bit, and accesses the integrated PHY through MDIC at
   PHY address 1, including PHY status register 26.
4. Reset, EEPROM/MDIC polling, DMA-address checks, and ring setup remain
   bounded. RX/TX rings are initialized before NIC registration, and interrupt
   causes remain masked until the existing IRQ registration point.
5. A successful device continues through the existing Ethernet, ARP, IPv4,
   UDP, DHCP, and Navigator/main-loop paths. A failed stage remains visible and
   fail-safe instead of becoming an unbounded wait or a fake NIC.

The hardware identity and PCH family mapping are consistent with the Linux
e1000e hardware definitions for `E1000_DEV_ID_PCH_SPT_I219_LM` (`0x156F`).
This repair uses the repository's existing bounded E1000e-compatible path; it
does not import a full Linux driver or claim that unverified PCH-specific
workarounds are complete.

## Diagnostics

The next boot can distinguish the following stages without indefinite output:

```text
PCI match: accepted/rejected; driver=...
MMIO mapping: accepted size=...
PCI command: ... (memory+bus-master enabled)
MAC reset: complete (bounded)
MAC acquisition: RAL0/RAH0 valid
I219 PHY discovery: MDIC address=1 status=26 valid
RX ring setup: 32 descriptors ready
TX ring setup: 8 descriptors ready
NIC registration: complete; interrupt causes remain masked
```

Existing bounded failure-stage reporting, `lspci`, `netdiag`/`nicinfo`,
`ipconfig /all`, cached link-state reporting, deferred boot DHCP, and the
Phase-3 startup checkpoints are preserved. Normal boot is not flooded with a
continuous diagnostic loop.

## Validation

Completed automated checks include:

- canonical amd64 build and ESP staging;
- hosted `network_diagnostics_test` with exact `8086:156F` acceptance,
  `intel-i219-lm (PCH)` naming, and `8086:24F3` rejection;
- freestanding compilation of the modified kernel NIC translation unit;
- build-wrapper, URL-resolution, resource-diagnostics, resource-scheduler,
  and secure-random smoke tests;
- hosted Navigator smoke;
- deterministic kernel Navigator smoke across all 16 selected scenarios;
- synthetic release-ISO packaging verification;
- existing QEMU E1000 and virtio-rng regression coverage.

The physical AIDA_LPT levels remain pending until the next machine boot. In
particular, QEMU validates the existing E1000 path and Phase-3 responsiveness,
but does not prove the I219 RAR/MDIC/PHY path.

## Next physical test

Connect the Ethernet cable before powering on. Boot the fresh Phase-4 ISO,
wait for the desktop, open a terminal, and capture:

```text
lspci
netdiag
ipconfig /all
```

The first milestone is `8086:156F` showing an accepted
`intel-i219-lm (PCH)` match and `Driver Bound: YES`. If initialization stops,
photograph the last `[NIC]` diagnostic and the complete `netdiag` output. Run
`dhcp /discover` only after the NIC has registered; DHCP success is a later
milestone, not evidence of driver binding by itself.
