# AIDA_LPT Intel I219-LM PCH TX initialization — Phase 22

Status: bounded initialization and timeout evidence implemented; physical
AIDA_LPT verification is pending. No hardware fix is claimed.

## Audited boundary and decision

The prior physical result remains:

- Intel I219-LM, PCI `8086:156F` (SPT/PCH);
- physical link is up;
- DHCP reaches `INIT` and receives no lease;
- TX tail advances `0 -> 1`, while TDH stays `0`;
- descriptor DD remains clear and the attempt ends as
  `TX_DESCRIPTOR_NOT_CONSUMED`.

That evidence locates the failure at or before descriptor consumption. It does
not identify an initialization bit as the cause.

The source audit found that guideXOS already applies the SPT IOSFPC workaround
and changes TARC0 from three outstanding requests to two by clearing
`CB_MULTIQ_3_REQ` and setting `CB_MULTIQ_2_REQ`. It did not set TARC0 bits 23,
24, 26, and 27; TARC1 bits 24, 26, and 30 with bit 28 conditioned on
`TCTL.MULR`; or the RFCTL NFS filter disable bits.

Current Linux e1000e maps `8086:156F` to its SPT board and uses the ICH8LAN
hardware initializer. That initializer read-modify-writes the proposed TARC0,
TARC1, and RFCTL fields. Its TARC0 code also has an old ICH8-only branch that
sets bits 28/29; Phase 22 deliberately does not copy that branch, so the
existing SPT two-request policy remains authoritative. Linux documents the
RFCTL bits as the workaround for NFSv2 UDP descriptor data corruption; this
receive-filter workaround is included for family initialization completeness
and is not evidence that it caused this TX hang.

References:

- [Linux e1000e ICH8LAN hardware-bit initialization](https://codebrowser.dev/linux/linux/drivers/net/ethernet/intel/e1000e/ich8lan.c.html#L5052)
- [Linux e1000e I219 SPT PCI table entry](https://codebrowser.dev/linux/linux/drivers/net/ethernet/intel/e1000e/netdev.c.html#L7887)
- [Linux e1000e RFCTL definitions](https://codebrowser.dev/linux/linux/drivers/net/ethernet/intel/e1000e/defines.h.html#L303)
- [Linux e1000e register offsets](https://codebrowser.dev/linux/linux/drivers/net/ethernet/intel/e1000e/regs.h.html#L190)

## Bounded implementation

For exact PCI device `8086:156F`, TX remains disabled while setup read-modify-
writes:

- TARC0: set bits 23, 24, 26, 27; clear the existing combined
  `CB_MULTIQ_3_REQ` mask, then set `CB_MULTIQ_2_REQ`;
- TARC1: set bits 24, 26, 30; set bit 28 when `TCTL.MULR` is clear and clear
  bit 28 when `TCTL.MULR` is set;
- RFCTL: set `NFSW_DIS` and `NFSR_DIS` (`0xC0`).

Unrelated bits are preserved. TX is then enabled using the same TCTL value as
before. All other NICs retain their prior TCTL write sequence and register
policy. Descriptor format, ring, DMA placement, DHCP, and networking layers are
unchanged.

On an I219 TX timeout, one bounded diagnostic record captures ICR, IMS,
STATUS, CTRL, TARC0, TARC1, and RFCTL. ICR is read exactly once at this timeout
boundary because it is read-to-clear. The record is printed to serial and
retained for `nicinfo tx raw status`; normal register snapshots do not read
ICR.

## Validation

The complete NIC/network regression chain is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-network-phase22-tests.ps1
```

It runs Phase 11–21 tests and adds deterministic policy checks for TARC0,
TARC1/MULR, RFCTL preservation, and timeout diagnostic coverage. These tests
do not emulate physical descriptor consumption.

## AIDA_LPT physical capture

Boot a fresh Phase 22 image with Ethernet connected. Record the image manifest
and build identity, then use this sequence:

```text
nicinfo brief
nicinfo tx owner
nicinfo tx brief
nicinfo tx raw
nicinfo tx raw status
dhcp status
```

Photograph the entire `nicinfo tx raw status` output and serial output. It must
show:

- TDT before, written, and final; TDH before and final;
- descriptor status before/final and `DD`;
- TARC0, TARC1, RFCTL, STATUS, CTRL, ICR, and IMS;
- the TX timeout/poison/failure classification;
- DHCP state, lease/result, discover/offer/request/ack counters, last TX
  completion result, and failure reason.

Expected initialization field checks are:

```text
(TARC0 & 0x0D800000) == 0x0D800000
(TARC0 & 0x30000000) == 0x20000000   # CB_MULTIQ_2_REQ, not CB_MULTIQ_3_REQ
(TARC1 & 0x45000000) == 0x45000000
TARC1 bit 28 == (TCTL.MULR == 0)
(RFCTL & 0x000000C0) == 0x000000C0
```

`dhcp status` is read-only. If raw TX still times out and poisons the ring,
capture that status and stop; do not invoke DHCP discovery or reuse the ring.
If raw TX completes with DD, DHCP discovery may be attempted once and its
result then captured with `dhcp status`.

## Interpretation and next boundary

The hypothesis is supported if the fresh physical run retains the programmed
fields and the same raw descriptor now advances TDH and sets DD. A completed
raw frame followed by DHCP progress would provide additional end-to-end
evidence. This still needs repeatable fresh-boot confirmation before calling
the hardware issue fixed.

The hypothesis is weakened if all initialization fields read back as expected
but the fresh raw test again reports `TDT=1`, `TDH=0`, DD clear, and
`TX_DESCRIPTOR_NOT_CONSUMED`. If the fields do not read back, the intended
initialization did not reach/hold in the device, so the physical run has not
tested the policy successfully.

If TDH remains frozen after this phase, the next diagnostic boundary is actual
PCIe DMA fetch/remapping visibility: correlate the exact descriptor-ring PA,
TDBA/TDLEN, TXDCTL and PCI bus-master state with the Phase 21 VT-d context and
fault evidence. Do not infer a DMA fault from a timeout alone; capture fresh
before/after VT-d evidence around one controlled attempt. If TDH advances but
DD stays clear, move to descriptor writeback/DMA visibility instead of fetch.

QEMU validates the image boots and preserves the virtual E1000 path; it cannot
confirm this I219 hypothesis.

## Artifact

Build and package with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-phase22-aida-i219-pch-tx-init-iso.ps1
```

This produces the AMD64 UEFI image using Phase 5 stage 8, Phase 6 stage 0,
Phase 7 stage 4, and the established constrained-low TX DMA experiment. The
exact ISO path, byte size, SHA-256, manifest, and QEMU smoke result are recorded
with the Phase 22 validation result.
