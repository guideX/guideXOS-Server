# AIDA_LPT I219 TX engine / descriptor fetch — Phase 15

Status: source audit, host tests, AMD64 build, QEMU regression, and ISO packaging are complete; physical AIDA_LPT validation is pending.

## Outcome and Phase 14 evidence

Current outcome: C. The Phase 14 physical result exposed a specific missing SPT/PCH TX descriptor-control initialization, and the smallest authoritative correction is implemented, but no new AIDA_LPT boot has yet confirmed that it makes descriptor 0 fetch. The artifact remains diagnostic if the correction is insufficient.

The exact Phase 14 evidence carried forward is:

```text
PCI 8086:156F, subsystem 103C:8079, revision 21, MAC EC-8E-B5-9F-36-38
Driver Ready YES, Link UP, hardware PASS
DHCP built=1 attempts=1 submitted=1 complete=0 offer=0 request=0 ack=0 nak=0 fail=1
TDT 0 -> 1, readback 1; final TDH 0; final TDT 1; descriptor DD absent
TCTL 0x0003F0FA; TX engine enabled; TIPG 0x00A0280A; TXDCTL 0x00000000
queue-enable not applicable; TARC0/IOSFPC workaround values present
PCI CMD 0x0006; bounded polling exhausted; timeout yes; poisoned yes
failure TX_DESCRIPTOR_NOT_CONSUMED
```

The 8-descriptor/128-byte to 64-descriptor/1024-byte change was physically rejected as the root-cause hypothesis: the 64/1024 ring produced the same TDT 0→1, TDH 0, DD-absent boundary. Phase 15 retains 64 descriptors and `TDLEN=0x400`.

## I219 generation classification

PCI `8086:156F` is upstream `E1000_DEV_ID_PCH_SPT_I219_LM`, labeled SPT PCH, and maps to the `e1000_pch_spt` MAC type. This is not the generic 8254x or the earlier PCH LPT/I217 branch. Reference: [upstream e1000e device IDs and MAC types](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h#L62-L150).

## Phase 14 provenance verification

The source enforces the requested checks before descriptor publication:

- `validate_dma_layout()` requires `__kernel_start == 0x00100000`, a valid image range, and complete TX/RX rings and buffers inside that image.
- `dma_address_range()` rejects null translations and 64-bit physical-range overflow; all objects use loader-provided `KernelPhysicalBase` translation.
- TDBAL/TDBAH are formed from the translated TX ring PA and read back into a reconstructed 64-bit address.
- The submitted descriptor PA is checked against `ringPA + descriptorIndex * 16` and against the full 1024-byte ring.
- The descriptor buffer field is checked against the translated TX packet-buffer PA.
- Image ownership, overflow, alignment, and pairwise non-overlap are enforced.
- `NUM_TX_DESC=64`, the legacy descriptor size is 16 bytes, and `TDLEN=1024` are static/runtime requirements.
- PCI Memory Space Enable and Bus Master Enable are checked before initialization and rechecked at every TX submission boundary before descriptor publication.

Thus the green provenance diagnostic is backed by enforcement. Full `nicinfo tx` retains linked VA range, loaded physical base, ring/buffer VA/PA, TDBA reconstruction, slot/buffer matches, ring validity, register snapshots, and PCI command.

## GuideXOS versus upstream TX initialization

This comparison uses upstream `e1000_configure_tx()`, `e1000_init_hw_ich8lan()`, and `e1000_initialize_hw_bits_ich8lan()`: [TX configuration](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c#L2733-L2834), [PCH initialization](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c#L4641-L4812), and [TXDCTL definitions](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/defines.h#L412-L420).

| guideXOS operation | upstream/documented equivalent | state | significance |
| --- | --- | --- | --- |
| Zero static legacy ring; unused descriptors DD | Allocate/zero legacy `e1000_tx_desc` ring | present | CPU ownership/ABI is established |
| TDBAL/H from translated ring PA | TDBAL/H from `tx_ring->dma` | present, same order | hardware base is proven |
| TDLEN=0x400; TDH/TDT=0 | ring count × 16; clear head/tail | present, same order | Phase 14 size hypothesis is retained/rejected physically |
| TXDCTL read-only in Phase 14 | SPT `COUNT_DESC`; PCH `PTHRESH=31`, `WTHRESH=1`, `GRAN=1`, both queues | missing in Phase 14; corrected | direct match to physical TXDCTL=0; selected fetch-control correction |
| TIPG=0x00A0280A before TCTL | documented TIPG fields; PCH link code can alter IPGT by speed/duplex | different but retained | no evidence this timing state gates descriptor fetch |
| TCTL EN/PSP/CT15/COLD63 | upstream sets PSP/RTLC/CT15 and preserves engine state | present except RTLC | RTLC is late-collision behavior, not an evidenced fetch gate |
| SPT IOSFPC and TARC0 3→2 workaround | exact upstream SPT/KBL erratum sequence | present, unchanged | protects known outstanding-request hang |
| No TARC1/CTRL_EXT/PBA/FWSM writes | upstream PCH helper configures TARC1/CTRL_EXT; reset writes PBA; FWSM wait is conditional | writes remain absent; read-only diagnostics added | broader state is exposed without speculative changes |
| PCI command check at init | memory + bus-master prerequisite | present and now per-submit | distinguishes broad DMA disablement |
| TDT readback and DD polling | tail write; descriptor DD is completion state | present; IRQ-independent | doorbell acceptance is not consumption |

The only production row changed in Phase 15 is TXDCTL. No unrelated power, FIFO, TARC1, firmware, PHY, or interrupt write was bundled into this experiment.

## Exact order and MMIO flush audit

The I219 order is now: validate image/ring/buffer provenance; zero 64 legacy descriptors and set unused status=DD; TDBAL → TDBAH → TDLEN → TDH=0 → TDT=0; TXDCTL0/TXDCTL1 SPT field configuration with readback; TIPG → TCTL; IOSFPC RDMTS_HEX and TARC0 outstanding-request workaround; full register snapshot; descriptor fields → sfence → pre-doorbell snapshot → TDT → readback; volatile DD polling with lfence → final register/raw snapshots.

`mmio_write32()` has compiler barriers around volatile writes; `mmio_read32()` is the existing posted-write observation primitive. Phase 14 already read the TX state after initialization and after TDT. Phase 15 adds explicit TXDCTL readbacks immediately after each queue-control write, without reading every unrelated register. The publication fence and completion fence remain unchanged.

## TXDCTL audit and correction

Zero is not the complete upstream SPT state. Upstream defines PTHRESH bits 0–5, HTHRESH bits 8–13, WTHRESH bits 16–21, GRAN bit 24, and COUNT_DESC bit 22. It sets COUNT_DESC, then replaces WTHRESH with full-descriptor writeback (`WTHRESH=1`, `GRAN=1`) and PTHRESH with maximum prefetch (`PTHRESH=31`, `GRAN=1`) for both queues; HTHRESH is not set in that PCH helper. The optional generic DMA-burst path is separate.

Phase 15 adds `i219_spt_txdctl_configuration()`, applies it only to `8086:156F`, writes both queue registers, and validates the relevant readbacks. From physical zero the expected state is COUNT_DESC=1, PTHRESH=31, HTHRESH preserved, WTHRESH=1, GRAN=1. There is no separate authoritative queue-enable bit. A readback failure is `TX_FETCH_CONTROL_INVALID`.

## TCTL and TIPG audit

Physical `TCTL=0x0003F0FA` decodes to `EN=1, PSP=1, CT=15, COLD=63, RTLC=0, MULR=0`. Upstream definitions identify RTLC as bit 24 and MULR as bit 28; upstream ordinary TX setup adds RTLC, so the comparable value would be `0x0103F0FA`. guideXOS leaves RTLC unchanged because it controls late-collision retransmit and is not evidenced as a fetch gate. Reserved bits are not synthesized.

Physical `TIPG=0x00A0280A` decodes to `IPGT=10, IPGR1=10, IPGR2=10`. Upstream exposes older generic defaults 8/8/6 and has PCH speed/duplex IPGT adjustments. The guideXOS value is recorded as shared timing state, not claimed as SPT-specific, and is not changed.

## PCH, packet-buffer, and DMA audits

Existing SPT workaround remains exactly `IOSFPC <- IOSFPC | RDMTS_HEX` and `TARC0 <- (TARC0 & ~CB_MULTIQ_3_REQ) | CB_MULTIQ_2_REQ`.

Upstream’s broader PCH helper also sets CTRL_EXT bit 22, TXDCTL bit 22, TARC0 bits 23/24/26/27, and TARC1 bits 24/26/30 plus MULR-dependent bit 28. Phase 15 reads TARC1 and CTRL_EXT for evidence but does not write those broader fields. This keeps the TXDCTL experiment single-variable.

Upstream SPT board data uses PBA allocation 26 KB and reset writes PBA. guideXOS did not program PBA; Phase 15 captures PBA read-only so a later boot can distinguish FIFO state from descriptor fetch. No TXPBS/DTXCTL write is added because the examined e1000e SPT path does not establish either as a required descriptor-fetch enable. FWSM is captured read-only; no SWFLAG, ME/AMT, NVM, or firmware ownership change is made.

The only TX-specific DMA control correction is TXDCTL. PCI bus master/memory space are separately checked. No interrupt configuration is involved in descriptor fetch or polling DD.

## RX versus TX

| property | RX | TX |
| --- | --- | --- |
| ring | static image, 32 × 16-byte | static image, 64 × 16-byte legacy |
| base/length | RDBAL/H, 512 bytes | TDBAL/H, 1024 bytes |
| pointers | RDH=0, RDT=31 | TDH=0, TDT=0; TDT=1 after submit |
| gate | RCTL.EN | TCTL.EN plus TXDCTL PCH state |
| buffers | 32 translated 2048-byte buffers | one translated 1518-byte buffer |
| physical result | real frames accepted | doorbell accepted; descriptor not consumed |

The key asymmetry was explicit RCTL/ring setup versus pre-Phase-15 TXDCTL read-only state. RX success is not TX proof, but rules out a broad PCI/image DMA failure.

## Descriptor raw memory and ring persistence

The two 64-bit words of the legacy 16-byte descriptor are captured before publication, immediately after TDT, and at final completion/timeout. The existing published `lastDescriptorRaw0/1` remains. Full `nicinfo tx` prints pre, doorbell, and final raw words; `nicinfo tx brief` prints published, doorbell, and final words. The final capture shows whether CPU-visible memory changed while hardware ignored or consumed the descriptor.

Initial, before, pre-doorbell, after-doorbell, and final snapshots all retain TDBAL/H and TDLEN. `tx_ring_registers_persisted()` requires every snapshot to reconstruct the same ring PA and exact 1024-byte length, distinguishing an overwrite/reset from a stable unconsumed ring.

## Legacy descriptors, interrupts, power, and failures

e1000e supports the 16-byte legacy descriptor layout. guideXOS uses EOP/IFCS/RS only and does not set DEXT or rewrite the TX layer to advanced descriptors. Polling DD is interrupt-independent; I219 interrupts remain masked at the existing registration boundary.

New distinguishable categories are `TX_FETCH_CONTROL_INVALID` (required SPT TXDCTL fields failed readback) and `TX_DMA_ENGINE_DISABLED` (PCI memory-space or bus-master bits absent at submit). No packet-buffer-disabled or generic engine-incomplete category is invented before it can be distinguished. Existing `TX_DESCRIPTOR_NOT_CONSUMED`, timeout classification, and safe poisoning remain. After an accepted ambiguous TDT, no descriptor/buffer retry occurs until reinitialization.

## Tests, build, QEMU, and artifact

`scripts/run-network-phase15-tests.ps1` runs Phase 11 DHCP, Phase 12 link, Phase 13 TX, Phase 14 DMA/fetch, and new Phase 15 semantic tests. New tests cover SPT classification, TXDCTL field semantics, TCTL/TIPG decoding, register identity, PCI DMA bits, ring-register persistence, raw descriptor ABI/evidence, failure names, `nicinfo tx brief` line bound, and poisoning. Structural verification checks the full AMD64 ELF, linker/image ownership, DMA objects, and the TXDCTL-before-TIPG-before-TCTL lifecycle ordering.

QEMU uses the emulated E1000 device and is only a regression/control. Two fresh ISO boots reached all existing readiness markers (`firmwareBootEntry`, `bootloader`, `kernelLoaded`, `ramdiskSize`, `ramdiskLoaded`, `desktopReady`, and `kernelMainLoop`). The serial evidence is recorded at `out/release-iso/qemu-test-aa6acabafca942c5a06b52facd20c078/serial.log` and `out/release-iso/qemu-test-668b553d966b4786a4c1e0a6d5210fb4/serial.log`. No QEMU run is counted as physical I219 proof unless it injects TX and observes DD. A bounded TX command exercise was not claimed for this phase because the available harness does not inject a shell command and observe completion.

The packaged artifact is:

```text
path:     D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase15-aida-i219-tx-engine-amd64.iso
filename: guideXOS-Server-v0.1.0-phase15-aida-i219-tx-engine-amd64.iso
size:     91293696 bytes
SHA-256:  59be4c3f132fa36a62a21aefb71fc2a8461dfe5d611003de7dff7ebb0090def4
manifest: D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase15-aida-i219-tx-engine-amd64.manifest.json
```

## Physical procedure and interpretation

Boot AIDA_LPT with Ethernet connected before power-on. Run `nicinfo brief`, `nicinfo tx brief`, `dhcp /discover`, `dhcp status`, `nicinfo tx brief`, `netdiag`, and `ipconfig /all`. If the compact command is unavailable, use `nicinfo tx`. Photograph baseline and post-attempt screens, especially TXDCTL0/1, TARC0/1, IOSFPC, CTRL_EXT, PBA, FWSM, TDBAL/H, TDLEN, TDH/TDT, raw descriptor stages, DD, timeout, poison, and failure.

Interpretation: completion and DHCP success means stop changing TX; completion but no OFFER means investigate DHCP wire/RX/server response, not TX enablement; one PCH prerequisite fixing fetch becomes the root cause with before/after registers; a new exact engine-state fault is the only Phase 16 target; compliant production state with no consumption requires a controlled single-variable DMA/fetch experiment; TDH advance without DD means investigate writeback/status semantics; DD with a software miss means fix status observation/barrier interpretation only.
