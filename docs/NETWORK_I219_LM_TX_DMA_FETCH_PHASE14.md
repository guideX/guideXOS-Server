# I219-LM TX DMA / descriptor-fetch bring-up — Phase 14

Status: source, host tests, AMD64 build, and ISO packaging are complete; AIDA_LPT physical validation is pending.

## Scope and Phase 13 boundary

Phase 13 established this physical result on AIDA_LPT (Intel I219-LM/PCH, PCI `8086:156F`, subsystem `103C:8079`, revision `21`, MAC `EC-8E-B5-9F-36-38`) with the cable connected:

```text
built=1 attempts=1 submitted=1 complete=0 offer=0 request=0 ack=0 nak=0 fail=1
TX completion
NIC TX descriptor completion timed out
```

The exact observed boundary was:

```text
descriptor 0 prepared -> TDT written 0 -> 1 -> TDT readback = 1
-> final TDT = 1 -> TDH remained 0 -> DD never appeared
-> completion polling timed out -> ring poisoned
```

The failure was `TX_DESCRIPTOR_NOT_CONSUMED`. PCI command was physically observed as `0x0006` (Memory Space Enable plus Bus Master Enable). The I219/PCH workaround state was present. This phase does not investigate DHCP wire behavior.

The exact numeric AIDA_LPT Phase 13 values for TCTL, TIPG, TXDCTL, TARC0, IOSFPC, TDBAL/H, TDLEN, descriptor address, buffer address, and poll count were not included in the repository report; this phase preserves the source-defined values and adds a runtime snapshot that prints them exactly with `nicinfo tx`.

## Answer to the Phase 14 question

The source audit proves the intended address chain and makes it fail closed if the chain is not true:

```text
linked VA = 0x00100000 + object offset
loaded PA = BootInfo.KernelPhysicalBase + object offset
TDBAL/H   = 64-bit loaded PA of s_txDescs[0]
desc 0 PA = TDBA + index * 16
desc.buf  = translated physical address of s_txBuffer
```

The loader allocates the complete ELF image as one contiguous physical allocation, copies each `PT_LOAD` at `p_vaddr - minLoadVaddr`, publishes that physical base in `BootInfo.KernelPhysicalBase`, and maps the image at its linked virtual range. The AMD64 linker starts at `0x100000`; the runtime validator also checks the linker symbols and every DMA object’s full range before ring initialization. The kernel is therefore not assumed to be identity mapped: it uses the explicit affine translation above.

Final ELF audit:

| Object | Linked VA | VA offset from `0x100000` | Physical address formula |
| --- | ---: | ---: | --- |
| TX ring | `0x21781880` | `0x21681880` | `KernelPhysicalBase + 0x21681880` |
| TX packet buffer | `0x21771290` | `0x21671290` | `KernelPhysicalBase + 0x21671290` |
| RX buffers array | `0x21771880` | `0x21671880` | `KernelPhysicalBase + 0x21671880` |
| RX ring | `0x21781C80` | `0x21681C80` | `KernelPhysicalBase + 0x21681C80` |

The linked image range is `0x00100000 .. 0x2284CA60` (end exclusive). All four objects are in `.bss` within that range. The TX ring is 128-byte aligned in the final image, and its 1024-byte extent remains within the same page and within the image range.

The exact translated physical address and physically observed TDBAL/H remain pending the AIDA_LPT boot, because `BootInfo.KernelPhysicalBase` is selected by that machine’s UEFI allocation. `nicinfo tx` now prints the exact values needed to compare them.

## Ring-size and descriptor-placement audit

The [Intel PCIe GbE Controllers Open Source Software Developer’s Manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/pcie-gbe-controllers-open-source-manual.pdf) requires a 16-byte-aligned descriptor base and a TDLEN that is a multiple of 128 bytes. It does not state a larger hardware minimum descriptor count or a 4 KiB/64 KiB boundary restriction in the examined TX-ring register definition. Eight legacy descriptors / 128 bytes is therefore register-legal by that manual, but it is below the [upstream e1000e supported minimum of 64 TX descriptors](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/e1000.h).

Phase 14 changes only the TX ring to the smallest established upstream-supported configuration:

```text
descriptor format = legacy 16-byte
descriptor count  = 64
TDLEN             = 1024 bytes (0x400)
base alignment    = 16 bytes required; final linked placement is 128-byte aligned
count granularity = 8 descriptors, implied by 128-byte TDLEN granularity
```

No speculative DMA32/bounce allocator was added. No unsubstantiated 64 KiB boundary rule was enforced. The final address chain is checked for overflow, image membership, ring length, alignment, overlap, and TDBAL/H readback.

## Loader and translation provenance

`guideXOSBootLoader/elf.cpp` computes `minLoadVaddr` and `maxLoadVaddr`, allocates one contiguous page range, zeroes it, and copies every loadable segment at `p_vaddr - minLoadVaddr`. `guideXOSBootLoader/main.cpp` stores the allocation base in `BootInfo.KernelPhysicalBase` and maps the kernel’s linked range to that physical base. The final ELF has `minLoadVaddr = 0x100000`; the actual runtime backing is the BootInfo allocation base.

The kernel now records and validates:

- `__kernel_start == KERNEL_LINK_VIRTUAL_BASE == 0x100000`;
- `__kernel_end > __kernel_start`;
- the entire TX ring, TX buffer, RX ring, and RX buffer array lie in the linked image range;
- translated range ends do not overflow 64 bits;
- all DMA objects are non-overlapping;
- TX descriptor 0’s translated PA equals `TDBA + 0 * sizeof(TxDescriptor)`;
- the descriptor’s buffer address field equals the translated TX buffer PA.

TDBAL/H encoding is explicit: `TDBAL = low32(ringPA) & 0xFFFFFFF0`, `TDBAH = high32(ringPA)`. The low four bits are ignored by the controller, and the runtime readback is decoded back to the expected ring PA.

## RX comparison

RX is a useful control, not proof of TX fetch correctness:

| Property | RX | TX after Phase 14 |
| --- | --- | --- |
| ring VA | `0x21781C80` | `0x21781880` |
| ring PA | `KernelPhysicalBase + 0x21681C80` | `KernelPhysicalBase + 0x21681880` |
| descriptor size | 16 bytes | 16 bytes |
| count / length | 32 / 512 bytes | 64 / 1024 bytes |
| buffers | static `.bss`, 32 × 2048 | static `.bss`, one 1518-byte buffer |
| translation | `translate_kernel_dma_address` | `translate_kernel_dma_address` |
| base register | RDBAL/RDBAH | TDBAL/TDBAH |
| proven physical behavior | frames accepted into RX buffers | fetch/completion still pending |

The shared translation and contiguous `.bss` provenance narrow the issue away from a broad “PCIe DMA cannot reach kernel memory” failure, while the changed TX ring size isolates the TX minimum-ring compatibility hypothesis.

## Address width and visibility

All ring and buffer addresses use `uint64_t`; TDBAL/H and descriptor buffer fields preserve both dwords. No cast truncates a physical address to 32 bits. The final physical below/above-4-GiB classification is deliberately left to the AIDA_LPT `nicinfo tx` capture because the runtime UEFI allocation base is not known in this repository. The code supports either result without a bounce allocator.

The publication sequence is:

```text
copy frame to static TX buffer
translate descriptor VA and buffer VA
write buffer address, length, command, status = 0
capture raw descriptor words
sfence + compiler memory clobber
snapshot registers
write TDT
read TDT back
poll volatile status with lfence before each load
```

On AMD64 this is a coherent PCIe platform; no cache flush was added. The `sfence` is the CPU publication barrier, while the compiler clobber prevents compiler motion. Descriptor initialization sets every status to DD once; each submission requires DD, then clears it exactly once before publication.

## TXDCTL, queue enable, and sequencing audit

The [Intel register definition](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/pcie-gbe-controllers-open-source-manual.pdf) defines TXDCTL threshold fields (PTHRESH, HTHRESH, WTHRESH, GRAN and writeback-related controls); it does not define a queue-enable bit for this path. The [upstream e1000e TX setup](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c) likewise has no `TXDCTL_QUEUE_ENABLE` symbol or queue-enable poll. Phase 14 does not invent `TX_QUEUE_DISABLED` or `TX_QUEUE_ENABLE_TIMEOUT`.

The transmitter gate is `TCTL.EN`, which is read back and reported as `engine=enabled|disabled`. `nicinfo tx` prints the raw TXDCTL and decoded threshold fields, with `queue-enable=not-applicable`.

Current guideXOS ordering remains narrowly scoped:

```text
TDBAL/H -> TDLEN -> TDH/TDT = 0
TIPG -> TCTL (EN, PSP, CT, COLD)
I219/PCH IOSFPC/TARC0 workaround
MMIO readback snapshot and validation
descriptor publication -> TDT doorbell -> completion poll
```

The I219 workaround remains:

```text
IOSFPC <- IOSFPC | RDMTS_HEX
TARC0  <- (TARC0 & ~CB_MULTIQ_3_REQ) | CB_MULTIQ_2_REQ
```

TCTL source value remains `0x0003F0FA`; TIPG source value remains `0x00A0280A`. The physical post-workaround values are emitted by the final register snapshot.

## Diagnostics and failure taxonomy

`nicinfo tx` now reports ring VA/PA, kernel physical base, descriptor VA/PA, buffer VA/PA, descriptor hardware buffer address, raw descriptor words, image range, provenance match flags, ring validity, TDBAL/H/TDLEN/TDH/TDT snapshots, TCTL/TIPG/TXDCTL/TARC0/IOSFPC, PCI command, poll count, timeout, poison state, and failure reason.

The added distinguishable failures are:

```text
TX_DMA_TRANSLATION_INVALID
TX_RING_ADDRESS_MISMATCH
TX_RING_ALIGNMENT_INVALID
TX_RING_LENGTH_INVALID
```

Existing `TX_DESCRIPTOR_NOT_CONSUMED`, `TX_COMPLETION_TIMEOUT`, `TX_ENGINE_DISABLED`, and other Phase 13 classifications remain. There is no queue-disabled classification because the authoritative audit found no queue-enable state to observe separately from TCTL.EN.

Phase 13 poisoning is preserved: after an accepted TDT write with ambiguous ownership, timeout sets the ring poisoned; a later send refuses to overwrite the potentially hardware-owned descriptor or payload buffer. There are no repeated ambiguous submissions.

## Validation

Completed:

- Phase 11 DHCP tests;
- Phase 12 link tests;
- Phase 13 TX tests;
- Phase 14 DMA/fetch provenance tests;
- full AMD64 freestanding build;
- final ELF program-header, section, linker-symbol, and DMA-object audit.

The Phase 14 host test covers translation, range membership, overflow rejection, ring-base encoding, descriptor-slot arithmetic, buffer-address equality, raw descriptor ABI, ring-size/alignment rules, engine-enable interpretation, failure names, and poison evidence. QEMU regression is required before physical interpretation; QEMU success is not treated as I219 success.

QEMU validation used the exact release ISO with the emulated Intel E1000 attached. One first boot was stopped by the harness’s 45-second timeout after its serial log reached kernel load but before all desktop markers; no guest error was reported. Two subsequent independent fresh boots with a 180-second bound reached all firmware, bootloader, kernel, ramdisk, desktop, and main-loop readiness markers. The QEMU runs did not inject a shell command, so they are boot/regression evidence rather than a claimed TX descriptor-completion result.

## Artifact

The Phase 14 ISO is produced by `scripts/create-phase14-aida-i219-tx-dma-fetch-iso.ps1` with Phase 5 stage 8, Phase 6 micro-stage 0, and Phase 7 stage 4. Final artifact metadata is recorded here after packaging:

- path: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase14-aida-i219-tx-dma-fetch-amd64.iso`
- filename: `guideXOS-Server-v0.1.0-phase14-aida-i219-tx-dma-fetch-amd64.iso`
- size: `91,293,696` bytes
- SHA-256: `8a5a736bb25fd8f19e31e8fe3232deb2f368203371fb7defefaca7bca2d53d2d`
- manifest: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase14-aida-i219-tx-dma-fetch-amd64.manifest.json`

## Physical AIDA_LPT procedure

Boot with Ethernet connected before power-on.

```text
nicinfo brief
nicinfo tx
dhcp /discover
dhcp status
nicinfo tx
netdiag
ipconfig /all
```

Capture the initial and post-attempt screens. The decisive Phase 14 fields are `ringVA`, `ringPA`, `kernelPA`, `descVA`, `descPA`, `bufVA`, `bufPA`, `hwBuf`, `raw0`, `raw1`, `ringMatch`, `bufferMatch`, `imageVA`, `valid`, `ringAlign`, `ringLen`, TDBAL/H, TDLEN, TDH/TDT, TCTL, TXDCTL, TARC0, IOSFPC, PCI-CMD, poll count, poison state, and failure.

Acceptance interpretation:

- descriptor consumed/completion succeeds: stop changing TX; next phase is DHCP wire/RX/server validation if OFFER remains zero;
- queue-enable fix success: not applicable to this audit unless a future authoritative I219 source identifies a distinct queue state;
- ring-size/alignment fix success: record old/new count, TDLEN, and TDH/DD progress;
- DMA mismatch: correct only the translation/load-provenance issue and prove TDBAL/H now reference the real descriptor PA;
- compliant ring, correct DMA, enabled engine, but no consumption: Phase 15 investigates the remaining I219 fetch/TX-engine prerequisite;
- TDH moves but DD is absent: target writeback/status semantics;
- DD appears but software misses it: target completion observation/barrier/status interpretation.

PHY, link refresh, DHCP packet construction, IPv4 provenance, static IP behavior, and RX were not speculatively changed.
