# guideXOS Server — AIDA I219-LM Phase 21: VT-d / DMA-remapping audit

Status: implementation complete; physical VT-d evidence remains pending.

This phase continues the Phase 20 AIDA I219-LM/PCH post-reset rearm work. It
adds a bounded, read-only ACPI DMAR and Intel VT-d register audit and a
controlled single-attempt observation around the already-proven normal raw-TX
fixture. It does not disable VT-d, program a root/context table, clear fault
status, alter PCI ownership, or add a retry path.

## Scope and decision boundary

The target is the Intel I219-LM/PCH function already selected by the Phase 20
image (`8086:156F`, typically PCI `00:1f.6`). The image keeps the exact Phase
20 settings:

- I219 Phase 5 stage `8`
- I219 Phase 6 micro-stage `0`
- I219 Phase 7 stage `4`
- constrained-low two-page TX DMA experiment enabled
- legacy 16-byte TX descriptor format

The new command surface is:

```text
nicinfo dma
nicinfo dma brief
nicinfo tx iommu
```

`nicinfo dma` and `nicinfo dma brief` are read-only. They validate the ACPI
RSDP/root-table path, locate a single bounded DMAR table, parse DRHD/RMRR
structures, match the I219 segment/BDF, and capture the matching VT-d unit's
version, capability, extended capability, command/status, root-table address,
fault status, and selected fault record.

`nicinfo tx iommu` refuses to attempt DMA unless the active I219 is in the
constrained-low mode and the Phase 20 reset and post-reset rearm diagnostics
both report completion. It captures the VT-d and TX state, invokes exactly one
existing `send_raw_diagnostic_frame(TxRawPath::Normal)` attempt, captures the
same state afterward, and preserves the Phase 20 poison/no-retry behavior.

The phase intentionally stops short of 21C. No physical evidence currently
proves that VT-d is the cause of the I219 failure, so there is no VT-d disable
or bypass implementation.

## Research basis

The implementation follows the published interfaces rather than guessing at
platform registers:

- [Intel VT-d Architecture Specification](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf)
- [UEFI/ACPI Specification 6.6](https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf)
- [Linux Intel IOMMU implementation](https://github.com/torvalds/linux/blob/master/drivers/iommu/intel/iommu.c)
- [Linux Intel IOMMU register definitions](https://github.com/torvalds/linux/blob/master/drivers/iommu/intel/iommu.h)
- [Linux x86 IOMMU documentation](https://www.kernel.org/doc/html/v5.19/x86/iommu.html)
- [Linux e1000e hardware definitions](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h)
- [Linux e1000e I219/PCH definitions](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/e1000.h)

The audit decodes GSTS.TES as the translation-enabled observation, FSTS.PPF
and FRI as the primary-fault/index observation, and the selected fault record
source ID/reason/page address. These are observations only; no command/status
write is issued.

## Implementation

- `kernel/core/include/kernel/vtd.h` contains freestanding-safe DMAR parsing,
  VT-d register decoders, fault-address classification, and shell-facing
  audit types.
- `kernel/core/vtd.cpp` walks the handed-off RSDP/XSDT or RSDT and validates
  bounded table lengths/checksums before reading the matching DRHD registers.
- `guideXOSBootLoader/main.cpp` identity-maps the ACPI root/table bodies and
  maps discovered DRHD register windows uncached before ExitBootServices.
- `kernel/core/nic.cpp` records the single controlled TX observation and
  retains descriptor/TDBA/TDLEN/TDH/TDT/VT-d evidence.
- `kernel/core/shell.cpp` exposes the three bounded command forms.

Classification is deliberately evidence-based:

```text
no RSDP/DMAR/matching DRHD/registers
        -> TX_IOMMU_DIAGNOSTIC_UNAVAILABLE
matching unit, TES clear
        -> TX_IOMMU_TRANSLATION_DISABLED
matching unit, TES set, no new matching fault
        -> TX_IOMMU_TRANSLATION_ACTIVE
new fault after one attempt
        -> DMA_FAULT / SOURCE_MATCH / RING_ADDRESS_FAULT / BUFFER_ADDRESS_FAULT
```

The code does not infer a blocking fault from a timeout alone. A fault must be
new relative to the pre-attempt capture, and source/ring/buffer labels require
the corresponding VT-d fault evidence.

## Verification

The deterministic Phase 21 test covers:

- valid explicit-scope DRHD and applicable RMRR parsing;
- include-all DRHD and segment mismatch behavior;
- malformed DMAR length/structure rejection;
- GSTS.TES, FSTS.PPF/FRI, fault-record source/reason/page decoding;
- ring/buffer/other address classification;
- `nicinfo dma`, `nicinfo dma brief`, and `nicinfo tx iommu` parser contracts;
- read-only source guards, legacy descriptor preservation, one-attempt/no-retry
  structure, and loader ACPI/VT-d mapping presence.

The full regression entry point is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-network-phase21-tests.ps1
```

It chains the existing Phase 11–20 network tests before the new Phase 21 test.

## Physical procedure

Use a fresh boot of the Phase 21 AMD64 ISO. Do not issue DHCP or any normal
network traffic after a poisoned raw-TX result. Photograph the complete output
of this exact sequence:

```text
nicinfo brief
nicinfo dma brief
nicinfo tx reset brief
nicinfo tx rearm
nicinfo tx brief
nicinfo tx iommu
```

Interpretation must preserve the existing Phase 20 evidence: PCI command and
DRV_LOAD ownership, constrained-low ring/buffer provenance, TDBA/TDLEN/TXDCTL/
TCTL/TARC/IOSFPC, legacy descriptor words, TDT/TDH, DD, bounded polling,
poison state, and the rearm completion checkpoint. The new VT-d result is
supporting evidence only until a fresh boot reproduces a new matching fault.

## Artifact

The packaging script is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-phase21-aida-i219-vtd-dma-audit-iso.ps1
```

Expected release names:

```text
dist/guideXOS-Server-v0.1.0-phase21-aida-i219-vtd-dma-audit-amd64.iso
dist/guideXOS-Server-v0.1.0-phase21-aida-i219-vtd-dma-audit-amd64.iso.sha256
dist/guideXOS-Server-v0.1.0-phase21-aida-i219-vtd-dma-audit-amd64.manifest.txt
```

QEMU is a regression check for bootability and unchanged non-I219 behavior; it
cannot prove physical I219 VT-d behavior. Physical evidence is therefore
explicitly a follow-up to this artifact, not silently substituted by QEMU.
