# AIDA_LPT Intel I219-LM DMA/fetch audit — Phase 23

Status: bounded audit implementation and validation are recorded below. The
next AIDA_LPT capture is required to determine the physical TX fetch boundary.
No DMA defect or hardware fix is claimed.

## Phase 22 result carried forward

On a fresh AIDA_LPT boot with Ethernet link up, the exact I219-LM `8086:156F`
was driver-ready, initialization reached `final`, and initialization failure
was `none`. Phase 22 physically read back `TARC0=0x2D800403`,
`TARC1=0x55000403`, `RFCTL=0x000000C0`, and `PCI-CMD=0x0006`. This proves the
intended PCH initialization and both PCI memory/bus-master command bits. One
controlled raw TX still timed out with `ICR=0`, `IMS=0`, `STATUS=0x00080683`,
`CTRL=0x00100240`, the stated PCH fields, and unchanged raw descriptor
contents. The prior Phase 17 observation had TDT advance `0 -> 1` while TDH
stayed zero and DD stayed clear.

Phase 22 therefore tested its register policy successfully and weakened the
hypothesis that those omitted PCH fields caused the frozen TX engine. It did
not test the exact DMA mapping or prove an IOMMU fault.

## Source audit before instrumentation

The Phase 22 build enables the Phase 16 constrained-low TX region. UEFI
allocates two contiguous `EfiLoaderData` pages using `AllocateMaxAddress` with
maximum address `0xFFFFFFFF`; the actual base is firmware-selected and is not
recorded in the repository's Phase 22 physical note. At runtime the layout is:

| Object | CPU virtual address | CPU physical/DMA address |
| --- | --- | --- |
| Ring | `TxDmaRegion.Base` | `TxDmaRegion.Base` |
| Current descriptor `i` | `Base + i*16` | `Base + i*16` |
| TX buffer | `Base + 0x1000` | `Base + 0x1000` |

The ring uses 64 legacy descriptors × 16 bytes = `TDLEN=0x400`. Its allocation,
including the 1518-byte buffer, is below 4 GiB. Ring and buffer are each
physically contiguous and lie in the same contiguous two-page allocation.
The bootloader includes that allocation in the new identity page tables before
`ExitBootServices` and retains it only if the final memory map still describes
the whole extent as `EfiLoaderData`. It publishes base, size, flags, and memory
type through `BootInfo.TxDmaRegion`; BootInfo also passes the final memory-map
pointer, count, and descriptor stride.

The kernel already validates ownership, geometry, below-4-GiB placement,
identity VA=PA, contiguity, cacheable handoff flag, and non-overlap before
selecting the region. TDBAL/TDBAH are formed from the translated ring PA;
initialization reads back TDBA and `TDLEN=0x400`. `TDH=TDT=0` at setup. TXDCTL
sets COUNT_DESC, PTHRESH=31, WTHRESH=1, and GRAN=1 while preserving HTHRESH;
the exact readback value depends on preserved HTHRESH. PCI memory-space and
bus-master enable are checked before initialization and again at submission.
The physical Phase 22 `PCI-CMD=0x0006` corroborates those bits for its captured
state.

The page-table builder maps ordinary RAM as identity with PWT=0 and PCD=0; it
uses PCD/PWT only for the NIC and VT-d MMIO windows. The Phase 16 handoff's
`CACHEABLE` bit is a guideXOS contract, not the raw UEFI memory descriptor
attribute. Phase 23 now captures the raw `EFI_MEMORY_DESCRIPTOR.Attribute`
when its descriptor stride includes that field and prints the loader cache
mapping policy. The exact runtime addresses, raw memory attributes, and
TDBAL/H values must come from the next physical output.

The descriptor and payload are written to ordinary RAM. The path already used
an AMD64 `sfence` before taking the pre-doorbell MMIO snapshot. Phase 23 keeps
that snapshot and adds a second `sfence` immediately before the TDT MMIO write;
the diagnostic marks that point and captures 32 CPU-visible packet bytes plus
the raw 16-byte descriptor on both sides of TDT publication. It does not add a
cache flush or change the memory type.

## Phase 21 VT-d audit limits

Phase 21 code parses the handed-off ACPI RSDP/root table, DMAR DRHDs and RMRRs,
matches a DRHD to the target BDF, and reads that unit's GCMD/GSTS, RTADDR, FSTS,
and selected fault record. It classifies translation from GSTS.TES and compares
pre/post-attempt fault evidence without writing IOMMU state. It does not walk
the memory addressed by RTADDR or inspect a device context entry. The loader
maps the ACPI tables and VT-d MMIO registers, but does not promise that the
firmware root/context table pages are mapped into the kernel. Therefore the
existing evidence cannot establish a present context or usable mapping for
`8086:156F` on AIDA_LPT. This remains `unknown` in Phase 23; RTADDR is a pointer,
not proof of a valid device context.

There is no Phase 21 physical capture in the repository. DMAR presence, number
and scope of DRHDs, live TES, RMRR applicability, RTADDR contents, and matching
fault state are therefore not physically established here. The Phase 23
`nicinfo tx iommu` output shows before/after register snapshots, DMAR/DRHD
summary, parsed RMRR ranges, and fault state. FSTS and fault records are read
without writes; a timeout alone is never promoted to a DMA/IOMMU fault.

TDBA is programmed with the CPU's calculated physical ring address. If a
matching VT-d unit is translating this device through a valid context, the
address on the PCIe request is interpreted by that translation domain as an
IOVA. The software's `TDBA-match=yes` proves only the register matches the
calculated CPU physical address; it does not prove identity IOVA mapping.

## Hypotheses and discriminating observations

| Hypothesis | Evidence that would support it | What does not prove it |
| --- | --- | --- |
| A. TDBA is wrong | TDBA differs from the captured ring PA or descriptor PA is not `ringPA + index*16`. | TDH frozen by itself. |
| B. Active VT-d translation disagrees with CPU PA | TES is set, an active device context is independently proven, and a new matching fault names the ring/buffer address; or a safe mapping inspection proves the IOVA mapping absent. | DMAR present, TES set, or an old fault alone. Phase 23 does not inspect context pages. |
| C. Ring/buffer inaccessible because of mapping/cache attributes | Handoff/map provenance fails, raw EFI attributes conflict with the intended RAM policy, or actual runtime mapping evidence differs from the identity WB loader path. | The handoff's `cacheable=yes` flag alone. |
| D. Descriptor writes not visible before TDT | Captured CPU bytes/raw descriptor are absent or differ before publication, or a completion/fetch transition appears only after the explicit adjacent `sfence` in a later controlled experiment. | An unchanged CPU readback alone does not prove device visibility. |
| E. Fetch occurs but TDH/DD do not advance | TDH advances while DD does not, or other independent evidence shows descriptor consumption without writeback. | TDT readback only. |
| F. Another device prerequisite remains | Proven address/mapping, visible descriptor, bus master, stable queue readback, no relevant VT-d fault, yet no consumption. | Link-up and PCH register readback alone. |

The physical outcome remains open until AIDA_LPT supplies the Phase 23 output.

## Bounded diagnostic

The existing `nicinfo tx iommu` command remains the one-shot entry point and
retains its fresh reset/rearm guard, poison behavior, and no-retry rule. Its
report now contains:

- ring, current descriptor, and TX-buffer CPU VA and DMA/physical address;
- raw EFI memory-map attribute plus the handoff ownership/mapping flags;
- TDBAL, TDBAH, TDLEN, TDH, TDT, TXDCTL, and PCI command before and after TDT;
- raw descriptor bytes and the first 32 TX-buffer bytes before and after TDT;
- the explicit adjacent `sfence` publication point;
- DMAR/DRHD/RMRR summary, RTADDR/TES/FSTS/fault state before and after;
- an explicit `active-device-context=unknown` qualification.

The output limit is bounded at 64 logical lines, including up to 16 parsed
RMRR entries. VT-d remains read-only, and the command calls the normal raw TX
boundary once at most.

## Validation

Run the Phase 11–22 regression chain plus the new Phase 23 checks:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-network-phase23-tests.ps1
```

Build and package the Phase 23 AMD64 image:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\create-phase23-aida-i219-dma-fetch-iso.ps1
```

Run the repository QEMU release smoke against the produced ISO. QEMU validates
bootability and the unchanged emulated E1000 path; it is not I219 DMA evidence.

## One-attempt AIDA_LPT procedure

Use a fresh boot of the Phase 23 ISO with the Ethernet cable connected and
link up. Photograph/record the build identity and complete console output.
The commands before the final one are read-only or perform the existing
resetless rearm; only the final command submits a packet:

```text
nicinfo brief
nicinfo tx brief
nicinfo dma brief
nicinfo tx reset brief
nicinfo tx rearm
nicinfo tx brief
nicinfo tx iommu
```

Confirm `tx rearm` reports complete and `nicinfo tx brief` shows an unpoisoned
ring before running `nicinfo tx iommu`. Run `nicinfo tx iommu` once only. If it
times out, preserve the full screen/serial output and stop; do not run raw TX,
DHCP, or another TX command on that boot. The output must retain pre/post
TDT/TDH/DD, addresses, queue registers, PCI command, byte snapshots, memory
attributes, VT-d state/faults, and no-retry status.

If TDBA differs from the physical ring PA, follow hypothesis A. If TES is set
and a new matching fault reports a ring/buffer address, that is direct evidence
for B. If TES is active but no fault appears, the device context/mapping remains
unknown and must not be called a fault. If addresses, mapping provenance,
attributes, and queue state are consistent while TDH/DD remain frozen, the
result shifts weight toward C/F or an unobserved pre-completion fetch failure;
it still does not identify a root cause. If TDH advances but DD stays clear,
descriptor consumption is no longer the boundary and writeback visibility is
the next question.

## Artifact

Packaged artifact:

- ISO: `dist/guideXOS-Server-v0.1.0-phase23-aida-i219-dma-fetch-audit-amd64.iso`
- Size: `91,293,696` bytes
- SHA-256: `206cde13aba26ea86b6ad0eb878cfbb381a26422beaccbdc3216680d953e5d47`
- Manifest: `dist/guideXOS-Server-v0.1.0-phase23-aida-i219-dma-fetch-audit-amd64.manifest.json`
- Source commit used for the image: `eac39c031d8c370b2f92fcd5af8438271e9bcaac`
- Build identity: `GXOS-P7-4-eac39c031d8c-4861ed26-3ddc-4741-ac90-17f83ea33766`

The ISO passed structural UEFI boot-image verification and the repository
QEMU release smoke. The smoke reached firmware boot entry, bootloader, kernel,
ramdisk, desktop readiness, and kernel main loop. This is an emulated boot
check, not a physical I219 result. Its serial log is under
`out/release-iso/qemu-test-e9aa9e45053547b3a0547270c782fd0b/serial.log`.

The DMA diagnostic code and image inputs were committed with the source commit
above. A documentation-only completion update follows in the local history;
the final push status is reported with that commit.
