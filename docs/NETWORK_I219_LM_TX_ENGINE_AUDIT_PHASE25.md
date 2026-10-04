# AIDA_LPT Intel I219-LM TX engine audit — Phase 25

Status: source audit and one-variable artifact are prepared. The next AIDA_LPT
run must capture the physical GCR state and, only if the TX no-snoop bits are
set, perform one bounded TX attempt. This report does not claim physical TX
success or a hardware root cause. The phase marker remains `PHASE25` until that
physical result is collected.

## Starting evidence and limits

The supplied Phase 24 evidence is retained as reported: PCI `8086:156F`, link
up, PCI command `0x0006`, ring rearm complete, `TDLEN=0x400`, `TXDCTL=0x0141001F`,
`TCTL=0x0003F0FA`, and pre-attempt `TDH=TDT=0`. The one-shot attempt ended with
descriptor `DD=no`. ACPI DMAR and a matching DRHD were absent, so Phase 24
classified IOMMU applicability as no and did not obtain a VT-d fault
comparison.

The available Phase 24 photographs do not contain a complete transcript or all
16 pre-TDT descriptor bytes. This report therefore does not claim the exact
Phase 24 descriptor, TDBA equality, immediate TDT readback, or final TDH/TDT
values. Phase 25 prints the live raw 16-byte descriptor and its decoded fields
immediately before its single TDT publication.

## Authoritative hardware path

Linux `e1000e` defines `E1000_DEV_ID_PCH_SPT_I219_LM` as `0x156F` (“SPT PCH”),
maps it to `board_pch_spt`, and binds that board to MAC type `e1000_pch_spt`,
the ICH8LAN MAC/PHY operations, and the SPT NVM operations. Thus the relevant
path is `e1000_reset_hw_ich8lan` → `e1000_init_hw_ich8lan` and
`e1000_configure_tx` → normal legacy TX descriptor publication. It is not the
generic e1000, igb, or igc path. See the [device definition](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h),
[PCI-to-board mapping](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c),
[ICH8LAN/PCH reset and initialization](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c),
and [TX ring setup/publication](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c).

### Ordered comparison through the first TDT

| Stage / state | Linux SPT/I219 `e1000e` | guideXOS sequence and difference | Classification and reasoning |
|---|---|---|---|
| PCI memory and bus mastering | PCI core enables the device before MAC setup. | Physical evidence reads `PCI-CMD=0x0006`; the TX path checks memory-space and bus-master enable before rearm/publication. | **FETCH-CRITICAL** prerequisite, currently satisfied by observed evidence. |
| Reset quiesce | `e1000e_disable_pcie_master` requests `CTRL.GIO_MASTER_DISABLE` and polls `STATUS.GIO_MASTER_ENABLE`; masks interrupts; disables RCTL and TCTL; reads/flushes; waits 10–11 ms. | Phase 7 masks/drains causes, disables RCTL and TCTL (leaving PSP), reads STATUS as a posted-write flush, waits 10 ms, then requests reset. It does not disable/poll PCIe master first. | **UNKNOWN** for first fetch: Linux uses it to quiesce outstanding TLPs before reset. Reset completion, later PCI bus-master state, and link are proven; missing proof is the pre-reset GIO-master state and whether any transaction remained outstanding. |
| MAC/PHY reset | Preserves CTRL, adds PHY reset if reset is not blocked, acquires the ICH software flag, writes CTRL.RST, intentionally does not flush after reset because that can hang, waits 20 ms, then performs config-done and post-PHY-reset work. | The PCH reset path adds CTRL.RST and waits 20 ms before polling CTRL; it does not assert PHY_RST or take SWFLAG. It separately handles the selected DRV_LOAD ownership path and ring-flush/rearm lifecycle. | **UNKNOWN** as a TX-fetch cause. The documented PHY/reset steps primarily gate correct PHY setup; the physical link-up and completed reset argue against a broad initialization failure. No same-boot TX-engine read proves whether a particular reset-state difference matters. |
| STATUS and LAN_INIT_DONE | For ICH10 and newer MACs, including SPT, `e1000_get_cfg_done_ich8lan` waits for `STATUS.LAN_INIT_DONE`, then clears that indication. | guideXOS captures STATUS for reset flushing and checks the I219 link, but does not separately wait for or report the LAN_INIT_DONE bit. | **UNKNOWN**. Linux treats it as basic configuration completion before PHY configuration; link-up is evidence against a PHY-readiness problem, but the bit itself was not captured and no direct descriptor-fetch dependency is established. |
| CTRL / CTRL_EXT / DRV_LOAD | ICH8LAN initialization preserves and sets generation-specific CTRL_EXT bits; the `board_pch_spt` flags include AMT and CTRL_EXT-on-load ownership handling. | guideXOS records CTRL/CTRL_EXT/FWSM and uses the selected PCH `CTRL_EXT.DRV_LOAD` read-modify-write ownership procedure while preserving other bits. | **PERFORMANCE/OPTIONAL** for remaining CTRL_EXT power bits; ownership is already established. No missing `DRV_LOAD` prerequisite is indicated by Phase 24. |
| FWSM / ME arbitration | The `FWSM.PCIM2PCI` wait is added to `ew32` only when `FLAG2_PCIM2PCI_ARBITER_WA` is set. Linux sets that flag only for `e1000_pch2lan` with `FWSM_FW_VALID`, not `e1000_pch_spt`. | guideXOS snapshots FWSM and does not insert a wait before every CSR write. | No SPT difference: the special arbitration workaround does **not** apply to `8086:156F`. Do not transplant the PCH2 workaround into this test. |
| MAC address / RAL / RAH | `e1000_init_hw_ich8lan` initializes receive-address registers and multicast state. | guideXOS reads and retains the firmware/NVM RAL0/RAH0 address, verifies it after reset, and does not rewrite it during rearm. | **RX-ONLY** for filtering/address acceptance. The valid station address and physical link are already established; it does not explain a frozen TX head. |
| PBA | The SPT board descriptor supplies PBA metadata; the audited SPT reset/init and TX ring functions do not identify PBA as a descriptor-fetch gate. | guideXOS captures PBA and leaves its current allocation intact. | **PERFORMANCE/OPTIONAL** for packet-buffer allocation; no evidence that PBA blocks descriptor 0 fetch. |
| PHY setup and link | `setup_link` runs during ICH8LAN init; SPT-specific copper-link handling adjusts PHY/link parameters according to negotiated speed and duplex. | guideXOS prepares CTRL.SLU/ASDE, uses MDIC for PHY/link state, and the supplied physical run reports link up. | **WIRE-TRANSMIT-ONLY** for link/timing choices once the MAC accepts descriptors; existing link-up is direct evidence these steps progressed. |
| TIPG | PCH link setup adjusts the IPG according to speed/duplex, including SPT cases. | guideXOS writes the documented full-duplex default TIPG before enabling TCTL. | **WIRE-TRANSMIT-ONLY**: inter-packet spacing can affect frames on the wire, not whether descriptor 0 is fetched. |
| TCTL / TCTL_EXT | `e1000_configure_tx` sets PSP, RTLC, and collision threshold, then programs collision distance. SPT does not use a separate TCTL_EXT operation in this path. | guideXOS initializes PSP/CT/COLD, keeps TCTL disabled while PCH policy is set, then enables it. It does not set RTLC. It reports `TCTL_EXT=N/A-SPT-e1000e`. | Missing RTLC is **WIRE-TRANSMIT-ONLY** (late-collision retransmit behavior). TCTL enable is **FETCH-CRITICAL** and is read back enabled. TCTL_EXT is not an applicable difference. |
| TXDCTL0 / TXDCTL1 | `e1000_initialize_hw_bits_ich8lan` sets COUNT_DESC for both queues. `e1000_init_hw_ich8lan` sets PTHRESH=31, WTHRESH=1 with descriptor granularity for both queues and preserves HTHRESH. | guideXOS performs the same field policy for both queues and validates readback. Supplied Phase 22 evidence has `TXDCTL0=0x0141001F`. | No policy difference identified; these fields govern prefetch/writeback behavior and are initialized before TCTL is enabled. |
| TARC0 / TARC1 / IOSFPC | SPT `e1000_configure_tx` sets IOSFPC.RDMTS and changes TARC0 from three to two outstanding requests to avoid a TX hang/data corruption. | guideXOS applies and snapshots IOSFPC, TARC0, and a PCH TARC1 policy while TCTL is off. Supplied Phase 22 readbacks include TARC0 `0x2D800403`, TARC1 `0x55000403`, IOSFPC policy, and RFCTL `0xC0`. | IOSFPC/TARC0 are matched. TARC1 is an extra measured local policy; no audited SPT source identifies it as a missing first-fetch prerequisite. The known TARC0 workaround avoids a later TX hang, so it is not a strong explanation for no descriptor consumption. |
| RFCTL | Linux configures RFCTL in receive setup/workarounds, outside the SPT TX ring publication. | guideXOS applies its recorded PCH RFCTL values and captures the register. | **RX-ONLY** for receive filtering/workaround fields; not a TX-fetch candidate. |
| TDBAL / TDBAH / TDLEN / TDH / TDT setup | `e1000_configure_tx` writes base low/high, ring length, head=0, tail=0; PCH2-only arbiter workaround may reissue TDT. | guideXOS writes TDBAL/H, TDLEN, TDH=0, TDT=0, and validates the ring readback before submission. | Base/length/head/TDT are **FETCH-CRITICAL** and match in operation. Phase 24 photographs do not prove exact TDBA equality; Phase 25 prints both ring PA and TDBA registers. |
| TIDV / TADV | Linux writes TX interrupt delay and absolute delay. | guideXOS leaves the reset values and now prints both. TX completion is polled synchronously with interrupts disabled for this experiment. | **PERFORMANCE/OPTIONAL**: interrupt moderation does not gate descriptor fetch or the bounded poll. |
| GCR / PCIe no-snoop | ICH8LAN `e1000_init_hw_ich8lan` calls `e1000e_set_pcie_no_snoop` for non-ICH8 MACs with the complement of `PCIE_NO_SNOOP_ALL`, clearing the six RX/TX no-snoop controls. SPT therefore has the three TX control bits clear in this policy. | Before Phase 25, guideXOS neither captured nor configured GCR. Phase 25 measures GCR and, only when any TX bit is set, clears only TXD/TXDSCW/TXDSCR no-snoop bits; it preserves RX and all unrelated bits and reads back the result. | **FETCH-CRITICAL / COMPLETION-CRITICAL** hypothesis. If the CPU has dirty cache lines for descriptors or packet data and the NIC reads without snooping, the device could see stale descriptor contents and leave TDH frozen/DD clear. A TX writeback/cache-visibility issue could also leave DD unobserved. Actual GCR state and cache/coherency behavior are the missing proof. |
| PCH power / DMA-clock controls | The no-DMA-clock-gating `FFLT_DBG` workaround is conditional on MAC type `>= e1000_pch_tgp`; clearing CTRL_EXT.DPG_EN is conditional on `>= e1000_pch_ptp`. Neither condition includes SPT. | guideXOS does not apply these later-generation controls; the P25 snapshot includes GCR but not FFLT_DBG/DPG_EN as candidate writes. | No missing SPT workaround established. These conditions are **not applicable** to `e1000_pch_spt`; changing them would be an unsupported experiment. |
| Descriptor and first TDT | Linux publishes a legacy TX descriptor after DMA mapping and a write memory barrier, then writes TDT. SPT has no special TDT flush path from the PCH2-only arbiter flag. | guideXOS completely initializes the 16-byte legacy descriptor, applies the CPU/DMA publication fence immediately before one TDT MMIO write, reads TDT immediately after as the posted-write/readback observation, then bounded-polls TDH/DD. | Descriptor format and memory ordering appear structurally correct. The TDT readback proves the doorbell register observed the value; it does **not** prove the NIC fetched the descriptor. `sfence` orders CPU memory writes and is not itself a posted-MMIO flush. |

## Descriptor audit

The diagnostic constructs the legacy 16-byte descriptor with a 64-bit DMA
buffer address followed by little-endian `length`, `cso`, `cmd`, `status`,
`css`, and 16-bit `special`. It sets `cso=0`, status/CSS/special to zero, and
`cmd=EOP|IFCS|RS` (the simplest non-offloaded single-buffer transmit request).
The Phase 25 serial evidence prints all 16 bytes in memory order and decodes
each field immediately before TDT. The ring and packet-buffer physical
addresses are printed alongside it. The source checks that descriptor 0 maps
to the physical ring slot programmed in TDBAL/H and writes a memory-ordering
barrier before TDT.

This is a source-level audit of the Phase 25 fixture; it is not a retroactive
decode of the incomplete Phase 24 photographs. QEMU acceptance would still not
prove physical I219 DMA behavior.

## Ranked hypotheses and selected experiment

| Rank | Hypothesis; support and evidence against | Exact missing proof | Could explain stationary TDH? | Could explain DD clear? |
|---|---|---|---|---|
| 1 | **GCR TX no-snoop policy.** Linux clears TXD/TXDSCW/TXDSCR no-snoop on SPT; guideXOS previously had no GCR capture/write. Against: the physical pre-change GCR is unknown, and Linux programs this in a general init path rather than documenting it as a required TX start bit. If those TX bits are already clear, there is no experiment to run. | Same-boot old GCR, changed-bit mask, direct readback, unrelated-bit preservation, and the one-attempt TDH/TDT/DD/raw-descriptor result. | Yes, if stale descriptor reads prevent the engine from seeing a valid descriptor. | Yes, if stale reads prevent execution; a related writeback visibility/coherency issue could also hide DD. Neither mechanism is proven here. |
| 2 | **LAN_INIT_DONE sequencing.** Linux waits and clears this status indication for SPT before PHY configuration; guideXOS does not separately report it. Against: AIDA_LPT already reports link up, which is evidence that basic PHY setup progressed. | Post-reset STATUS.LAN_INIT_DONE observation and whether its state correlates with the TX engine on this machine. | Possible but unproven; this indication is documented around basic/PHY configuration, not descriptor DMA. | Possible only indirectly; no direct completion dependency is established. |
| 3 | **PCIe-master/reset quiesce difference.** Linux disables/polls PCIe master before reset and flushes/waits; guideXOS uses a STATUS flush and the delays but omits GIO-master disable/poll. Against: reset completed, PCI command reads `0x0006` afterward, link is up, and ring rearm readback passes. | Pre-reset STATUS.GIO_MASTER_ENABLE and outstanding-transaction behavior across reset. | It could leave reset state wrong, but evidence is weak after successful reset/rearm. | Likewise possible only indirectly; no Phase 24 descriptor completion occurred. |

The single Phase 25 experiment is the rank-1 GCR policy, limited to the three
TX no-snoop bits. It is reversible by reboot and independently measured. The
routine latches its one-shot guard before any candidate operation. If the TX
bits already read clear, it reports `OUTCOME_D_CANDIDATE_ALREADY_COMPLIANT` and
does not write GCR or publish a descriptor. If the candidate write/readback
fails, it reports `OUTCOME_D_CANDIDATE_NOT_APPLIED` and does not transmit. No
VT-d discovery or writes are part of this phase.

The candidate does not copy the full Linux GCR value; it changes only the TX
mask, preserving RX and unrelated state to isolate one variable. If the bits
were already clear or if the applied change leaves TDH/DD unchanged, the
hypothesis is weakened. No additional workaround is bundled with it.

## MMIO and publication findings

Linux e1000e `ew32` reaches `writel`. Its FWSM/ME arbitration wait is conditional
on a board flag that Linux assigns only to PCH2 with valid firmware, not SPT.
Linux uses explicit register-read flushes in reset/address and other sequences;
the SPT reset path specifically avoids a flush immediately after CTRL.RST.
The local reset path has a STATUS flush before reset, a 10 ms pre-delay, a 20 ms
post-delay, and bounded CTRL reset polling. For TDT, the Phase 25 path reads
TDT immediately after its one write, which checks the posted register write;
it does not treat that as proof of DMA fetch. The CPU publication fence remains
separate from the MMIO readback.

## Physical procedure and interpretation

Use the Phase 25 ISO from a fresh power-on with cable connected and link up.
Record the build identity and full console output. Run these commands once, in
order:

```text
nicinfo brief
nicinfo tx brief
nicinfo dma brief
nicinfo tx reset brief
nicinfo tx rearm
nicinfo tx brief
nicinfo tx phase25
```

The final command is the only TX attempt. It requires fresh reset/rearm,
constrained-low DMA placement, an unpoisoned ring, and a readable I219 GCR. It
prints pre-candidate registers, candidate old/requested/readback values, the
full pre-TDT register snapshot, ring and buffer PAs, raw16 and decoded legacy
descriptor, one TDT write and immediate TDT readback, bounded final TDH/TDT/DD,
final raw16 and GCR readback, classification, and timeout poison/no-retry
state. Stop after this command; do not retry, run DHCP, or run another TX
command on that boot.

- **Outcome A — physical TX consumption proven:** TDH advances and/or DD sets.
  This proves descriptor consumption only; it does not prove DHCP.
- **Outcome B — candidate applied, TX not consumed:** GCR readback is correct,
  but TDH is stationary and DD remains clear.
- **Outcome C — diagnostic foundation improved:** the fresh evidence is
  captured but no sufficiently supported change is available or applicable.
- **Outcome D — proposed experiment invalid:** GCR already matches the SPT TX
  policy, or the requested bits fail readback. The command skips the TX attempt
  when it cannot apply a real candidate change.

## Validation and artifact

The Phase 11–24 regression chain, the focused Phase 25 policy/parser test, and
the Phase 25 source guards pass. The AMD64 UEFI build, ISO structural
verification, and standard QEMU readiness smoke pass. QEMU demonstrates
virtual boot/readiness only; it is not physical I219 TX evidence.

This build reuses the tracked AMD64 PacMan NativeElf package. The configured
sibling PacMan source now requires `sdk/include/guidexos/audio.h`, which is not
present in this checkout, so its independent package rebuild cannot currently
link. The build identity and ISO manifest record the package-reuse mode.

- ISO: `dist/guideXOS-Server-v0.1.0-phase25-aida-i219-spt-tx-snoop-amd64.iso`
- Size: `91,293,696` bytes
- SHA-256: `7450263cfed9d8e951b85bd2b68fd7b0d82911f6f8e41780d5e315a4fd7c60e5`
- SHA-256 sidecar: `dist/guideXOS-Server-v0.1.0-phase25-aida-i219-spt-tx-snoop-amd64.iso.sha256`
- Manifest: `dist/guideXOS-Server-v0.1.0-phase25-aida-i219-spt-tx-snoop-amd64.manifest.json`
- Manifest build identity: `GXOS-P7-4-66a510f5708c-0393d81b-80a9-41a2-ae1a-59269196179c`
- Manifest source commit: `66a510f5708c67ee28fe6a9db8dc01ff22b4db86`
- ESP `build-identity.txt` ID: `GXOS-P7-4-371bbe10419941599b8cc89f7f4f977f`
- Kernel SHA-256: `c8632230ce6a79588882f9d2893e88b49522a86f12c99384f41bdbb90f044877`
- Structural verification: PASS; UEFI El Torito platform ID `0xEF`, bootable no-emulation image.
- Build settings: AMD64, Phase 5 stage 8, Phase 6 stage 0, Phase 7 stage 4, constrained-low TX DMA experiment; tracked PacMan package reused.
- QEMU: all seven firmware, bootloader, kernel, ramdisk, desktop-ready, and main-loop markers observed. Serial log: `out/release-iso/qemu-test-927fb90741894f5db2a61d69a8020307/serial.log`
- QEMU serial log SHA-256: `35252377e720588b4f668e15147112351fef06d5f67b4dba4fbf8e85240a78c0`

The phase marker is intentionally not advanced while AIDA_LPT physical
evidence is pending. Next expected phase: **PHASE25 physical test completion**.
