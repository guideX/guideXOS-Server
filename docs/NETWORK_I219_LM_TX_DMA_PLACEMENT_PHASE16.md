# AIDA_LPT I219 controlled TX DMA-placement experiment — Phase 16

Status: implementation, host tests, full AMD64 build, ISO packaging, structural verification, and fresh E1000 QEMU regression are complete. Physical AIDA_LPT validation is pending.

## Outcome

The implementation is ready for the requested A/B experiment. The physical outcome is not claimed as Case A, B, C, or D here because no AIDA_LPT console or physical NIC was available in this environment.

The experiment is opt-in and I219-only:

```text
default / Phase 15 control:  I219 TX ring + buffer in kernel-image .bss
Phase 16 candidate:          I219 TX ring + buffer in loader-owned constrained-low region
QEMU E1000 path:             unchanged kernel-image placement
```

For an I219 boot, an invalid, missing, unowned, unmapped, or non-constrained reservation makes TX initialization fail closed. The candidate does not silently fall back to the Phase 15 placement while reporting that the experiment ran.

## Phase 15 physical control carried forward

The exact Phase 15 evidence incorporated into this experiment is:

```text
Physical machine: AIDA_LPT
NIC: Intel I219-LM / PCH, PCI 8086:156F, subsystem 103C:8079, revision 21
revision: 21
MAC: EC-8E-B5-9F-36-38
PCI-CMD=0x0006; Memory Space Enable=yes; Bus Master Enable=yes
Driver Ready: YES
Link: UP
hardware: PASS
TXDCTL1=0x0141001F
TXDCTL1 valid=yes
TCTL enabled; TIPG programmed
TARC0=0x20000403
TARC1=0x00000403
IOSFPC=0x01011108
CTRL_EXT=0x014A1027
PBA=0x000E0012
FWSM=0xE001C25C
TDT 0 -> 1; TDT readback=1
TDH remained 0; DD never appeared
polls=1000000
timeout=yes
poison=yes
fail=TX_DESCRIPTOR_NOT_CONSUMED
dhcp: built=1 attempts=1 submitted=1 complete=0 offer=0 request=0 ack=0 nak=0 fail=1
```

The Phase 15 `TXDCTL` correction was physically active and read back as `0x0141001F`, but it was insufficient to make the I219 consume descriptor 0. Phase 16 therefore changes only TX memory placement/provenance. It does not add another speculative Intel register policy.

## Current boundary and experimental hypothesis

The known physical sequence is:

```text
TX ring valid
  -> descriptor built
  -> descriptor published
  -> PCI bus master enabled
  -> TCTL enabled
  -> SPT TXDCTL configured
  -> TARC/IOSFPC workaround present
  -> TDT 0 -> 1
  -> TDT readback=1
  -> TDH remains 0
  -> DD absent
  -> TX_DESCRIPTOR_NOT_CONSUMED
```

Phase 16 asks one question:

> When only the TX ring and packet buffer move from the static kernel-image placement to an explicitly owned, physically contiguous, identity-mapped region below 4 GiB, does I219 begin consuming descriptor 0?

The 64-descriptor legacy ABI, 1024-byte ring, descriptor fields, DHCP frame, TCTL, TIPG, TXDCTL, TARC/IOSFPC, barriers, polling, and poisoning policy are retained.

## Phase 15 placement and address provenance

The Phase 15 mechanism is static kernel-image storage. The TX objects are `s_kernelImageTxDescs[64]` and `s_kernelImageTxBuffer[1518]` in kernel `.bss` storage. The loader allocates the complete ELF image contiguously and passes its physical backing base in `BootInfo.KernelPhysicalBase`. The runtime affine translation is:

```text
PA = BootInfo.KernelPhysicalBase + (VA - 0x00100000)
```

`KERNEL_LINK_VIRTUAL_BASE` is `0x00100000`. The old runtime physical address is therefore not a fixed constant: it depends on the UEFI-selected kernel allocation. It can be below or above 4 GiB depending on firmware placement and image size. Static `.bss` does not imply low physical memory.

The Phase 14/15 documentation recorded representative linked values from the control image:

| object | linked/runtime VA example | affine offset from `0x100000` | physical result |
| --- | ---: | ---: | --- |
| TX ring | `0x21781880` | `0x21681880` | `KernelPhysicalBase + 0x21681880` |
| TX packet buffer | `0x21771290` | `0x21671290` | `KernelPhysicalBase + 0x21671290` |
| RX buffers | `0x21771880` | `0x21671880` | `KernelPhysicalBase + 0x21671880` |
| RX ring | `0x21781C80` | `0x21681C80` | `KernelPhysicalBase + 0x21681C80` |

The exact AIDA_LPT Phase 15 runtime PA must be read from its captured `nicinfo tx` output. Phase 16 retains the linked-image range and `KernelPhysicalBase` diagnostics so the old address class is visible rather than inferred.

## Phase 16 reservation and handoff

The loader allocates exactly two pages for the I219 candidate:

```text
UEFI allocation: AllocatePages(AllocateMaxAddress, EfiLoaderData, 2 pages)
maximum address: 0xFFFFFFFF
region size:     0x2000 bytes
ring offset:     0x0000 (64 * 16 = 0x400 bytes)
buffer offset:   0x1000 (1518 bytes used)
```

`AllocateMaxAddress` selects free UEFI memory; no hard-coded physical address is used. The loader zeroes the exact allocation and publishes a `TxDmaRegionDescriptor` in the existing BootInfo v2 handoff. The final pre-`ExitBootServices` memory map must still contain the complete range as `EfiLoaderData`; otherwise the descriptor is cleared.

The loader performs an explicit overlap audit against the kernel physical allocation, the page-rounded linked kernel virtual range, BootInfo, ramdisk, stack, trampoline, and framebuffer. UEFI allocation itself remains responsible for excluding firmware-owned, ACPI, runtime, and other allocated ranges. The candidate is mapped as cacheable identity memory before handoff.

The kernel validates all of the following before selecting it:

- nonzero page-aligned base and exact two-page geometry;
- ring and 1518-byte buffer fully inside the region and non-overlapping;
- physical end at or below `0x100000000`;
- `EfiLoaderData` memory type and complete containment in the final UEFI map;
- `VALID`, `OWNED`, `CONTIGUOUS`, `IDENTITY`, `BELOW_4G`, and `CACHEABLE` flags;
- CPU VA equals physical PA for the identity mapping;
- TX pointers equal the published ring and buffer addresses;
- TX does not overlap the unchanged RX ring or buffers.

The candidate therefore has this intended provenance:

```text
ring VA = ring PA = region base
buffer VA = buffer PA = region base + 0x1000
owner = guideXOS loader / EfiLoaderData reservation
contiguous = yes
cacheable = yes
lifetime = from allocation through NIC lifetime after ExitBootServices
```

No general-purpose allocator is changed. RX remains static kernel-image storage and continues using the original `KernelPhysicalBase` affine translation.

## Loader and memory-map audit

The bootloader’s current handoff provides enough ownership information for this bounded reservation:

- `LoadElf()` allocates the contiguous kernel image and records `KernelPhysicalBase`.
- ramdisk, stack, trampoline, BootInfo, and the final memory-map buffer are separately allocated before the candidate reservation.
- the candidate uses UEFI `AllocateMaxAddress` with `EfiLoaderData`, so firmware selects an available extent below 4 GiB.
- the candidate is included in the pre-EBS identity-map ranges.
- the final map is copied with its firmware descriptor stride and passed through BootInfo.
- `MemoryMapContainsLoaderData()` rejects a handoff unless the complete candidate range is inside an `EfiLoaderData` descriptor.

The loader does not reserve ACPI reclaim, runtime, framebuffer, firmware, or bootloader memory by guessing an address. It rejects overlaps with all explicitly known handoff objects and relies on the UEFI page allocator for the remaining firmware memory-map exclusions.

## RX placement control

RX is deliberately unchanged. The RX ring and all 32 RX buffers remain the original static image objects, use the original affine translation, and are initialized before TX. The diagnostic comparison prints:

```text
RX ring PA
RX buffer PA range
TX experimental ring PA
TX experimental buffer PA
```

This makes the intentional asymmetry visible. RX physical success remains a control for broad PCI, MMIO, bus-master, and image-mapping failures; it is not treated as proof that TX must work.

## IOMMU / VT-d audit

No guideXOS VT-d or IOMMU programming was found in the Phase 13–15 path, and Phase 16 does not add any. Firmware may expose VT-d capability, but this handoff does not publish active translation tables or a guideXOS DMA-remapping domain. The fact that RX DMA physically accepts frames makes a broad IOMMU block unlikely, but it does not rule out a range-specific mapping issue. The old and experimental TX ranges, and the unchanged RX range, are therefore printed for the physical comparison. VT-d is not disabled speculatively.

## Preserved TX semantics

The following are intentionally identical to the Phase 15 control:

```text
descriptor count: 64
descriptor ABI: 16-byte legacy e1000/e1000e descriptor
TDLEN: 1024 bytes (0x400)
descriptor commands: EOP | IFCS | RS
packet: same DHCP DISCOVER construction and length
TCTL: same enable/configuration
TIPG: same programmed value
TXDCTL0/TXDCTL1: same Phase 15 SPT policy
TARC0/IOSFPC: same Phase 15 workaround
publication: same sfence / descriptor ordering
doorbell: same TDT write and readback
completion: same lfence polling and 1,000,000-iteration bound
failure handling: same one-attempt safe poisoning
```

No DHCP payload, PHY access, link refresh, RX initialization, descriptor format, ring count, ring length, or Intel register write policy was changed to make the experiment pass.

## Code changes

- `guidexOSBootLoader/guidexOSBootInfo.h`: added the compact TX DMA reservation descriptor and shared provenance flags.
- `guideXOSBootLoader/main.cpp`: added bounded `AllocateMaxAddress`/`EfiLoaderData` reservation, protected-range checks, final memory-map ownership reconciliation, identity mapping, and serial handoff evidence.
- `kernel/core/main.cpp`: passes the reservation and final UEFI memory-map metadata into the NIC driver.
- `kernel/core/include/kernel/nic.h`: added DMA-region geometry/ownership helpers, mode and failure taxonomy, ring-length constant, and diagnostics.
- `kernel/core/nic.cpp`: keeps static RX and Phase 15 control storage, selects constrained TX storage only for an opt-in I219 build, validates VA/PA/mapping/overlap, and leaves TX engine setup/order unchanged.
- `kernel/core/shell.cpp`: added mode, region, VA/PA, RX comparison, TDBA reconstruction, buffer match, ownership, mapping, alignment, and compact post-attempt evidence.
- `build.ps1`, the bootloader project, and `scripts/create-release-iso.ps1`: propagate and record the explicit experiment switch.
- `scripts/run-network-phase16-tests.ps1` and `tests/network_tx_phase16_dma_placement_test.cpp`: deterministic host/structural coverage.
- `scripts/create-phase16-aida-i219-tx-dma-placement-iso.ps1`: reproducible Phase 16 AMD64 artifact entry point.

The compile-time switch defaults to zero. The Phase 16 artifact uses `-I219TxDmaPlacementExperiment`, Phase 5 stage 8, Phase 6 stage 0, and Phase 7 stage 4. Non-I219/QEMU keeps the kernel-image control even when the experiment switch is present.

## Diagnostics

`nicinfo tx brief` remains bounded to 20 logical lines and now includes:

```text
mode=constrained-low region=... owned=yes mapped=yes contig=yes cache=yes below4G=yes geometry=yes
ringVA=... ringPA=... match=yes map=yes
descPA=... ring+offset=yes bufVA=...
bufPA=... hwBuf=... match=yes rxPA=...
ring=count=64 len=1024 align=yes valid=yes
TDBA=... match=yes
TDLEN=... TDH/TDT=... stable=...
descriptor status / DD
raw published / doorbell / final words
TCTL and enable fields
TXDCTL0 and TXDCTL1
TIPG
TARC0 / TARC1 / IOSFPC
CTRL_EXT / PBA / FWSM / PCI
polls / timeout / poison / failure
```

The full `nicinfo tx` command additionally prints the linked image range, `KernelPhysicalBase`, descriptor VA/PA, buffer VA/PA, RX range, reconstructed TDBA, and the initialization/doorbell/final register snapshots.

## Failure taxonomy

The Phase 16 handoff adds these explicit names:

```text
TX_DMA_REGION_UNAVAILABLE
TX_DMA_REGION_OVERLAP
TX_DMA_REGION_MAPPING_INVALID
TX_DMA_REGION_NOT_OWNED
TX_DMA_ADDRESS_WIDTH_MISMATCH
TX_DMA_EXPERIMENT_NOT_ACTIVE
```

The existing boundary remains visible and unchanged:

```text
TX_DESCRIPTOR_NOT_CONSUMED
TX_COMPLETION_TIMEOUT
TX_FETCH_CONTROL_INVALID
TX_DMA_ENGINE_DISABLED
```

An unavailable I219 reservation is not converted into a kernel-image success claim. Safe poisoning after an accepted ambiguous TDT remains enabled; the driver does not retry the descriptor or packet in place.

## Tests and QEMU

The serial regression script ran the complete chain:

```text
Phase 11 network tests PASS
Phase 12 network/link tests PASS
Phase 13 TX tests PASS
Phase 14 TX DMA/fetch provenance tests PASS
Phase 15 I219 TX-engine tests PASS (brief newline sites=20)
Phase 16 DMA-placement tests PASS (brief newline sites=20)
```

Phase 16 host tests cover valid/two-page allocation geometry, alignment, below-4G rejection, overflow, overlap, loader-memory-map ownership, identity mapping, TDBA encoding, descriptor buffer encoding, count/length/ABI preservation, mode names, required flags, and fail-closed contracts. They do not fake descriptor consumption.

The full AMD64 freestanding build completed with the expected pre-existing warning set. The release packager structurally verified the FAT32 ESP and UEFI ISO. The exact-ISO QEMU check observed firmware boot, bootloader, kernel load, ramdisk load, desktop readiness, and kernel main loop.

Three additional fresh QEMU E1000 boots were run with disposable copies of the ESP. Each reached:

```text
guideXOS UEFI Bootloader
[KERNEL] guideXOS kernel_main entered
[KERNEL] NIC initialized from BootInfo successfully
[AIDA-PHASE3] checkpoint=network-ready
```

QEMU evidence is only a regression check. It does not answer the AIDA_LPT descriptor-fetch question.

## ISO metadata

```text
path:     D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase16-aida-i219-tx-dma-placement-amd64.iso
filename: guideXOS-Server-v0.1.0-phase16-aida-i219-tx-dma-placement-amd64.iso
size:     91293696 bytes
SHA-256:  b0a0f3ed1c2ca1a17b1317e3c255ae70dbd561eb7757a7df5921fc646da0313b
manifest: D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase16-aida-i219-tx-dma-placement-amd64.manifest.json
checksum: D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase16-aida-i219-tx-dma-placement-amd64.iso.sha256
```

The manifest records `i219TxDmaPlacementExperiment=true`, Phase 5 stage 8, Phase 6 stage 0, Phase 7 stage 4 (`register`), source commit `36aa6f4cbb4f544da7122fbde6bd981afdfd2123`, and the PyCdlib ISO backend. The package was created before the final Phase 16 documentation/commit, so the manifest records the source worktree as dirty as expected for this local artifact build.

## Physical AIDA_LPT procedure

Boot AIDA_LPT with Ethernet connected before power-on.

1. Run `nicinfo brief`; confirm `Driver Ready YES` and `Link UP`.
2. Run `nicinfo tx brief`; photograph the baseline. Confirm `mode=constrained-low`, nonzero region range, `owned=yes`, `mapped=yes`, `contig=yes`, `cache=yes`, `below4G=yes`, `geometry=yes`, ring/buffer VA/PA matches, `TDBA match=yes`, ring count 64, length 1024, and no pre-existing failure.
3. Run `dhcp /discover`.
4. Run `dhcp status`.
5. Run `nicinfo tx brief`; photograph the complete post-attempt output.
6. Run `netdiag`.
7. Run `ipconfig /all`.

Record the exact ring PA, buffer PA, region base/end, RX ring PA, RX buffer PA range, TDBAL/H reconstruction, descriptor hardware buffer address, TDH/TDT, DD, timeout, poison, and failure. Do not infer the result from link state or DHCP construction alone.

## Interpretation and next phase

| physical result | conclusion | recommended next phase |
| --- | --- | --- |
| Case A: TDH advances and/or DD appears; `complete=1`, timeout=no, poison=no | Constrained placement caused descriptor consumption. | Stop changing TX placement. Isolate the differing property: address width, translation, ownership, region, or alignment; if DHCP completes, proceed to IPv4 validation. |
| Case B: below-4G placement fixes TX | The high-address/kernel-image class is implicated, but DMA32 is not yet proven as the permanent requirement. | Compare old/new PA high dwords and mapping/ownership; distinguish DMA-width limitation from bad high-address translation or firmware/IOMMU provenance. |
| Case C: RX-like placement class fixes TX | Memory-placement/provenance is implicated more strongly than an arbitrary low address. | Place only TX in the same allocator class as the working RX in a new controlled comparison, preserving RX itself. |
| Case D: compliant constrained region, TDT=1, TDH=0, DD absent | DMA placement is rejected as the next broad hypothesis. | Use one descriptor-format/fetch-mode or minimal raw-transmit fixture experiment; do not add unrelated register writes. |
| Case E: region cannot be safely allocated | Ownership/provenance boundary is not strong enough. | Improve the boot-time memory-map/reservation handoff; do not guess a physical address or use the old placement silently. |
| Case F: TDH advances but DD is absent | Descriptor fetch is proven; completion writeback/status is now the boundary. | Investigate writeback/status semantics only. |
| Case G: DD appears but software misses it | Hardware completed; software observation is the boundary. | Fix polling/status observation and barrier interpretation only. |

The Phase 16 result is causally useful only if the physical photographs preserve the same Phase 15 register/descriptor evidence while showing the new explicit DMA provenance.
