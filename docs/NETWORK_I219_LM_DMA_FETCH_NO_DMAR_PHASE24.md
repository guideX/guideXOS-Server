# AIDA_LPT Intel I219-LM DMA-fetch audit — Phase 24

Status: diagnostic change and build validation are recorded below. The next
AIDA_LPT run is required to capture the one-shot TX result. This phase does not
claim a TX fix or identify the hardware root cause.

## Phase 23 physical result carried forward

AIDA_LPT booted with I219 link up, and the earlier PCI memory-space/bus-master
and Phase 22 PCH register readbacks were confirmed. On this boot, `nicinfo dma
brief` found an ACPI RSDP but no DMAR table, no DRHD, and no readable VT-d unit
registers. It reported `TX_IOMMU_DIAGNOSTIC_UNAVAILABLE`,
`rmrr-applicable=no`, and `ACPI DMAR table is not present`. Phase 23's
`nicinfo tx iommu` path stopped at that diagnostic gate, so its intended TX
publication/fetch experiment did not run.

The absent DMAR weakens active VT-d remapping as the explanation for the frozen
TX head on that boot. It is not a TX fix and does not eliminate every possible
DMA issue.

## Phase 24 behavior

`nicinfo tx iommu` still requires a fresh, completed reset/rearm, the
constrained-low DMA ring, and an unpoisoned ring. It runs the normal raw TX
boundary at most once. VT-d discovery is captured independently:

- If DMAR is absent, output says `DMAR=absent iommu-applicable=no`, gives the
  parser's reason, emits no GCMD/GSTS/RTADDR values, and continues to the
  physical-address TX attempt.
- If DMAR is present and a DRHD matches the I219, Phase 23's bounded before/after
  register and fault evidence remains read-only and is printed when available.
- If a DMAR/DRHD or register read is unavailable, the report says why. An
  unavailable snapshot does not print zero placeholders as register evidence
  or classify translation as disabled.

The pre-TDT capture is taken before the existing `sfence` immediately adjacent
to the single TDT write. It records ring/descriptor/buffer addresses, EFI
memory type and attributes, identity-map and below-4-GiB classifications,
TDBAL/H/TDLEN/TDH/TDT/TXDCTL/TCTL/PCI-CMD, the raw 16 descriptor bytes in memory
order, and the first 32 packet-buffer bytes. It then records the immediate TDT
readback, polls only to the existing timeout, and captures final TX registers,
descriptor bytes and DD. The existing timeout poison/guard and no-retry behavior
remain in force.

The report explicitly compares `TDBA == TX_RING_DMA_ADDRESS` and labels the
result. Its outcome field distinguishes:

- `A_TDBA_MISMATCH`: readable TDBA differs from the captured TX ring DMA address.
- `B_TDT_PUBLISHED_TDH_STATIONARY_DD_CLEAR`: TDT readback matches while TDH is
  stationary and DD remains clear.
- `C_DESCRIPTOR_DD_SET`: descriptor completion DD is set.
- `D_DESCRIPTOR_CHANGED_HEAD_STATIONARY`: descriptor bytes changed while TDH
  remained stationary and DD is clear.
- `HEAD_ADVANCED_DD_CLEAR`: head moved without descriptor DD.

These are observations only. Below-4-GiB placement, physical contiguity, and
identity mapping do not by themselves prove device accessibility.

## Regression, build, and QEMU validation

Run the Phase 11–23 chain plus Phase 24 source/output checks:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-network-phase24-tests.ps1
```

Build and package the AMD64 UEFI AIDA_LPT image:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-phase24-aida-i219-dma-fetch-iso.ps1
```

Then run the repository release ISO smoke on the produced ISO:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-release-iso.ps1 -IsoPath .\dist\guideXOS-Server-v0.1.0-phase24-aida-i219-dma-fetch-amd64.iso
```

QEMU checks bootability and the emulated E1000 path. It is not physical I219
DMA evidence.

## One-attempt AIDA_LPT procedure

Boot the Phase 24 ISO from a fresh power-on with the Ethernet cable connected
and link up. Record the build identity and complete console output. Run these
commands in order:

```text
nicinfo brief
nicinfo tx brief
nicinfo dma brief
nicinfo tx reset brief
nicinfo tx rearm
nicinfo tx brief
nicinfo tx iommu
```

Confirm reset/rearm reports complete and `nicinfo tx brief` shows an unpoisoned
ring before the final command. Run `nicinfo tx iommu` once only. If the command
times out or reports a failure, preserve the complete output and stop; do not
run raw TX, DHCP, or another TX command on that boot. The command itself issues
one TDT write at most and never retries after a timeout.

Compare the exact TDBA/ring PA line first, then the pre-TDT, immediate-readback,
and final TDH/TDT/DD/raw-descriptor fields. Use the printed outcome to separate
the address mismatch, stationary published descriptor, completion, descriptor
memory change without head movement, and other head/DD transitions. Do not
interpret the address provenance flags alone as proof of device accessibility.

## Artifact

The Phase 24 AMD64 UEFI build and ISO passed structural verification.

- ISO: `dist/guideXOS-Server-v0.1.0-phase24-aida-i219-dma-fetch-amd64.iso`
- Size: `91,293,696` bytes
- SHA-256: `3ecf031dc5fb6f56e483e9804f6ff8ae9a166659c423cc0aa02348659e8d894f`
- Manifest: `dist/guideXOS-Server-v0.1.0-phase24-aida-i219-dma-fetch-amd64.manifest.json`
- Manifest build identity: `GXOS-P7-4-f49d7a994cf3-f7b5ec79-82a6-408a-b80e-25a95da25aad`
- ESP `build-identity.txt` ID: `GXOS-P7-4-36f568003ced45fca3e3ecec620b2759`
- Kernel SHA-256: `d2fa7d729c97572abb9fa1e88212975b282ee6c53be1df891983dac88372f146`
- Source commit: `f49d7a994cf3c04dd1eeef58255572da448ae129`
- Regression: Phase 11–23 chain and Phase 24 no-DMAR checks passed.
- QEMU: all firmware, bootloader, kernel, ramdisk, desktop-ready and main-loop
  markers observed. Serial log:
  `out/release-iso/qemu-test-531ee355a8b6400e9e3814269698464c/serial.log`
- QEMU serial log SHA-256:
  `17362773601134494008fb47f419a6706c35807527e0fd9bd71c931935ba659b`

The release manifest records `worktreeCleanAtPackagingStart=false` because the
build refreshed the tracked `ESP/build-identity.txt`. Its `sourceGitCommit`
points at the Phase 24 implementation commit above. QEMU validates bootability
and the emulated E1000 path only; the physical AIDA_LPT result remains pending.
