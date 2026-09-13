# AIDA_LPT I219-LM host/firmware ownership — Phase 18

## Outcome and scope

Phase 18 adds the one upstream-documented host-driver ownership transition
that was absent from the guideXOS AIDA_LPT I219 path:

```text
CTRL_EXT = read CTRL_EXT
CTRL_EXT |= CTRL_EXT.DRV_LOAD
write CTRL_EXT
read CTRL_EXT back
```

The transition is exact-device and exact-lifecycle scoped. It runs only for
Intel PCI `8086:156F` on the permanent I219 SPT path (`Phase 5=8`,
`Phase 6=0`, `Phase 7>=4`), immediately after the guideXOS MAC reset and
before MAC/PHY/TX configuration. The write preserves the complete read value
and adds only bit 28. The final ready gate requires successful readback and a
second final-state observation with the bit still set.

No physical AIDA_LPT result is claimed by this repository artifact. The
decisive question remains pending the fresh-boot procedure in this document.

## Phase 17 physical boundary carried forward

Phase 17 established the following on AIDA_LPT:

- Intel I219-LM/PCH, PCI `8086:156F`, subsystem `103C:8079`, revision `21`;
- MAC `EC-8E-B5-9F-36-38`;
- PCI command `0x0006`, MMIO and bus mastering enabled;
- PHY MDIC, MAC acquisition, RX DMA, stable link, TX ring setup, and Driver
  Ready all passed;
- 64-entry TX ring, 16-byte legacy descriptors, `TDLEN=0x00000400`;
- constrained, loader-owned, below-4-GiB TX DMA region remained active;
- `TXDCTL1=0x0141001F`, `TCTL=0x0003F0FA`, `TIPG=0x00A0280A`;
- SPT `TARC`/`IOSFPC` workaround remained active;
- TDT publication advanced and read back `0 -> 1`;
- TDH stayed `0`, the descriptor status stayed `0`, and DD never appeared;
- timeout poisoning classified the boundary as
  `TX_DESCRIPTOR_NOT_CONSUMED` without retrying the ring.

The raw fixture removed DHCP, IPv4, UDP, ARP, routing, and normal protocol
construction from the transmit question. RX success and the raw TX timeout
therefore leave descriptor consumption, rather than high-level packet
construction, as the tested boundary.

## Exact Phase 17 raw fixture

The Phase 18 image retains the Phase 17 fixture without modification:

| Field | Value |
|---|---|
| Destination | `FF:FF:FF:FF:FF:FF` |
| Source | Physical NIC MAC |
| EtherType | `0x88B5` |
| Payload marker | `GXOS-I219-P17` |
| Padding | Zero padding after the marker |
| Frame length | 60 bytes excluding FCS |
| FCS | Appended by hardware through IFCS |

The exact raw descriptor remains a legacy 16-byte descriptor. On the Phase 17
physical observation its fields were approximately:

```text
qword 0: 0x00000000AAB03000
qword 1: 0x000000000B00003C
```

Thus the descriptor contained buffer PA `0xAAB03000`, length `0x003C` (60),
command `0x0B` (`EOP | IFCS | RS`), and status `0`. The guideXOS descriptor
layout remains buffer address, length, CSO, command, status, CSS, and special;
the raw words are captured before TDT, after TDT, and at final observation.

## Authoritative SPT identification and board flags

The current upstream e1000e PCI table maps `E1000_DEV_ID_PCH_SPT_I219_LM`
(`0x156F`) to `board_pch_spt`. The board data selects the
`e1000_pch_spt` MAC generation. Relevant source links:

- [upstream e1000e PCI table](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c)
- [upstream SPT board data](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)
- [upstream I219 device ID](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/hw.h)

The `e1000_pch_spt_info` board record includes:

```text
FLAG_IS_ICH
FLAG_HAS_WOL
FLAG_HAS_HW_TIMESTAMP
FLAG_HAS_CTRLEXT_ON_LOAD
FLAG_HAS_AMT
FLAG_HAS_FLASH
FLAG_HAS_JUMBO_FRAMES
FLAG_APME_IN_WUC
```

Its relevant ownership result is therefore:

```text
AMT: yes
load notification mechanism: CTRL_EXT
SWSM-on-load: not selected
```

The flag definitions are in [upstream e1000.h](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/e1000.h).

## Upstream ownership lifecycle audit

The current upstream `e1000e_get_hw_control()` is a flag-selected helper.
Conceptually it does:

```c
if (FLAG_HAS_SWSM_ON_LOAD)
    SWSM |= SWSM_DRV_LOAD;
else if (FLAG_HAS_CTRLEXT_ON_LOAD)
    CTRL_EXT |= CTRL_EXT_DRV_LOAD;
```

The exact definition is `E1000_CTRL_EXT_DRV_LOAD = 0x10000000`, documented as
the driver-loaded bit for firmware. The alternative
`E1000_SWSM_DRV_LOAD = 0x00000008` is not the selected mechanism for SPT.
Both definitions are visible in [upstream defines.h](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/defines.h).

The exact open/reset flow is:

1. PCI probe identifies the board and initializes the adapter. Probe performs
   the reset path, but the final ownership notification is not installed by
   the probe branch for AMT hardware.
2. `e1000_open()` allocates the rings. For `FLAG_HAS_AMT`, it calls
   `e1000e_get_hw_control()` before calling `e1000e_reset()`.
3. `e1000e_reset()` calls the MAC `reset_hw` operation. For AMT hardware it
   then calls `e1000e_get_hw_control()` again immediately after MAC reset.
4. The reset returns to the normal configure path: TX registers and rings are
   configured, RX is configured, and the interface is brought up.
5. `e1000e_close()` releases ownership with `e1000e_release_hw_control()`
   after the interface is taken down. The error path also releases it.

This answers the lifecycle questions:

| Question | Result |
|---|---|
| When is DRV_LOAD asserted for SPT? | Before reset in AMT `e1000_open()`, and again immediately after reset inside `e1000e_reset()`. |
| Before or after MAC reset? | Both lifecycle points occur; the post-reset assertion is the meaningful retained state. |
| Does reset preserve it? | Upstream does not rely on preservation. It reasserts immediately after reset. The source does not claim a universal SPT reset-persistence guarantee. |
| Is it checked/reasserted after reset? | It is reasserted by the AMT branch in `e1000e_reset()`; upstream helper itself does not perform a readback check. |
| When is it cleared? | On close/error teardown through `e1000e_release_hw_control()` for AMT hardware. |
| Why does AMT matter? | AMT causes the open path to notify firmware before reset and causes the reset path to re-notify after reset. |
| CTRL_EXT or SWSM for `8086:156F`? | CTRL_EXT. The SPT flags select `FLAG_HAS_CTRLEXT_ON_LOAD`, not `FLAG_HAS_SWSM_ON_LOAD`. |

The PCH reset helper also has upstream SWFLAG/semaphore operations used to
serialize reset. Phase 18 does not copy those operations: they are separate
firmware/resource arbitration behavior and are outside this one-variable
DRV_LOAD experiment.

Relevant source is in [upstream netdev.c](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c), especially the
`e1000e_get_hw_control`, `e1000e_reset`, `e1000e_open`, `e1000e_close`, and
`e1000e_probe` paths, and in [upstream ich8lan.c](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/ich8lan.c) for the SPT reset/MAC generation.

## GuideXOS omission and Phase 18 correction

The Phase 13–17 guideXOS audit found:

- `CTRL_EXT` was read in TX snapshots and printed by `nicinfo tx`;
- `FWSM` was read-only context;
- no `E1000_CTRL_EXT_DRV_LOAD` constant or write existed;
- no `driver-loaded`, host-control, or firmware-ownership transition existed;
- no `SWSM.DRV_LOAD` path existed;
- no FWSM, ME, AMT, SWFLAG, NVM, management VLAN, PHY ownership, or VT-d
  operation was bundled with the TX experiments.

The physical Phase 15/16/17 value was `CTRL_EXT=0x014A1027`. Since bit 28 is
`0x10000000`, the observed value had DRV_LOAD clear. It would become
`0x114A1027` if every other readback bit stayed unchanged.

GuideXOS has no separate `e1000_open()` equivalent: its permanent P7 path
performs the MAC reset, then directly performs MAC/PHY/ring initialization.
The Phase 18 implementation consequently uses one post-reset ownership
transition, corresponding to upstream's immediate AMT reassertion in
`e1000e_reset()`. The prior reset-only P7 stages remain unchanged and do not
perform this production transition.

The exact correction is:

```text
after i219_pch_reset succeeds:
    before = read CTRL_EXT
    requested = before | 0x10000000
    write CTRL_EXT(requested)
    after = read CTRL_EXT
    verify after & 0x10000000 != 0
```

The volatile CTRL_EXT readback is the posted-write observation. No complete
register constant is written, no unrelated CTRL_EXT bit is synthesized, and
FWSM is read only for context. If the read is all-ones or bit 28 is absent,
initialization fails closed with `I219_CTRL_EXT_READ_FAILED` or
`I219_DRV_LOAD_READBACK_FAILED`; `driverReady` is not published and the TX
completion failure field is not used to mislabel the ownership failure.

## Bounded ownership diagnostics

The device record keeps one bounded set of ownership snapshots:

```text
initial bind/MMIO read
after MAC reset / before DRV_LOAD
immediate post-write readback
after TX initialization
before raw TX
final / raw completion or timeout
```

Each snapshot retains CTRL_EXT and FWSM. `nicinfo tx owner` is cache-only and
prints a compact record containing:

```text
family
AMT
hw-control
CTRL_EXT
DRV_LOAD
ownership-requested
ownership-readback
before / after
after-reset / reset persistence
FWSM
DriverReady / Link
stage / failure
```

The command is bounded to 20 lines and does not repeat the write. Existing
`nicinfo tx brief` remains the Phase 17 descriptor/TX view and remains capped
at 20 lines.

## Legacy descriptor audit

An advanced descriptor is not required for the ordinary Phase 18 raw frame.
The current upstream `e1000_tx_queue()` initializes `txd_lower` with IFCS.
It adds DEXT/data-descriptor mode only when the TX flags require TSO,
checksum offload, or hardware timestamping. VLAN insertion is separately
encoded. The ordinary final descriptor command policy is EOP + IFCS + RS.

This is a source-level audit of the active upstream path, not an inference
from QEMU. See the current [upstream e1000_tx_queue() path](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/netdev.c) and the [upstream descriptor definitions](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e1000e/defines.h).

### GuideXOS versus upstream ordinary descriptor

| Field | Upstream plain non-offloaded packet | GuideXOS Phase 17/18 raw fixture | Match |
|---|---|---|---|
| Buffer address | DMA-mapped packet buffer address | Physical packet buffer PA, translated and verified | Yes |
| Length | Packet length | `60` / `0x003C` | Yes |
| CSO | Zero when checksum offload is not selected | `0` | Yes |
| CMD.EOP | Set on final descriptor | Set | Yes |
| CMD.IFCS | Set unless no-FCS requested | Set | Yes |
| CMD.RS | Set by normal status-report policy | Set | Yes |
| CMD.DEXT | Clear for ordinary non-offloaded data | Clear (`0x0B` command byte) | Yes |
| Status | Hardware-owned before completion | `0` before publication | Yes |
| CSS | Zero without checksum offload | `0` | Yes |
| Special | Zero without VLAN/special operation | `0` | Yes |

The raw descriptor therefore remains structurally consistent with the
ordinary upstream legacy path. Advanced descriptor A/B is reserved for a
later phase if Phase 18 produces Case B; it is not implemented here.

## TCTL and other TX state

Upstream ordinary TX setup also sets RTLC in TCTL. RTLC controls late-collision
retransmission behavior, not an established descriptor-fetch gate. GuideXOS
therefore retains the physically established `TCTL=0x0003F0FA` and does not
bundle RTLC into the ownership experiment. TXDCTL0/1, TIPG, TARC0/1, IOSFPC,
TD base/length, PCI command, ring size, DMA placement, fences, TDT
publication, DD polling, and poisoning remain unchanged.

## Tests and validation

`scripts/run-network-phase18-tests.ps1` runs the complete Phase 11–17 chain
and the new Phase 18 test. The new deterministic test covers:

- exact `8086:156F` generation classification;
- SPT AMT and CTRL_EXT-on-load policy;
- `CTRL_EXT.DRV_LOAD=0x10000000`;
- unrelated CTRL_EXT bit preservation and readback validation;
- the post-reset lifecycle selector and generic E1000/QEMU exclusion;
- fail-closed ready-state gating and separate ownership failure names;
- bounded `nicinfo tx owner` parsing/line contract;
- the unchanged raw frame, legacy descriptor, ring length/count, constrained
  DMA geometry, and timeout/poison evidence.

The tests do not emulate hardware, write an MMIO register, or fake TX success.
The full AMD64 freestanding build, ISO structural verification, and fresh
QEMU E1000 boots are regression checks only. QEMU does not answer the
physical I219 descriptor-fetch question.

## ISO artifact

The Phase 18 packaging script is:

```text
scripts/create-phase18-aida-i219-hw-control-iso.ps1
```

It builds AMD64 with Phase 5 stage 8, Phase 6 stage 0, Phase 7 stage 4, and
the existing constrained TX DMA handoff, then invokes the canonical release
packager. The expected artifact is:

```text
guideXOS-Server-v0.1.0-phase18-aida-i219-hw-control-amd64.iso
```

The exact absolute path, byte size, SHA-256, and generated manifest are
reported with the release result and are authoritative over this static
document.

## Physical AIDA_LPT procedure

Use a fresh boot with Ethernet connected before power-on. Do not reuse a
poisoned ring and do not begin with DHCP.

1. Run `nicinfo brief`; confirm `Driver Ready YES` and `Link UP`.
2. Run `nicinfo tx owner`; photograph family, CTRL_EXT, DRV_LOAD,
   ownership requested/readback, FWSM, stage, and failure. The expected new
   state is `DRV_LOAD=yes`.
3. Run `nicinfo tx brief`; confirm `timeout=no`, `poison=no`, the constrained
   DMA experiment is active, and TDBA/buffer provenance matches.
4. Run `nicinfo tx raw`.
5. Run `nicinfo tx raw status`.
6. Run `nicinfo tx brief`; photograph TDT, TDH, DD, timeout, poison, raw words,
   and the final ownership fields.
7. Only if raw TX succeeds without poisoning, optionally run `dhcp /discover`
   and `dhcp status`.
8. If raw TX times out, stop and do not reuse the ring.

Capture `CTRL_EXT`/FWSM at the bounded initial, after-reset, after-write,
after-TX-init, before-raw, and final boundaries. The key observation is
DRV_LOAD readback plus descriptor behavior, not an inferred result from link,
DHCP construction, or QEMU.

## Case interpretation and next phase

| Case | Meaning | Follow-up |
|---|---|---|
| A: TDT advances, TDH advances and/or DD appears; raw complete, no timeout/poison | Ownership was causally implicated in descriptor consumption. | Qualify across fresh boots, then test DHCP through the now-working low-level path. Do not broaden the ownership change. |
| B: DRV_LOAD reads back, but TDT=1, TDH=0, DD absent | Ownership was a real missing upstream behavior but not the fetch blocker. | Keep the correction; Phase 19 performs the documented legacy-vs-extended/data descriptor or other single fetch-state A/B. |
| C: DRV_LOAD is not retained across reset | Ordering/persistence differs at the hardware boundary. | Use the bounded before/after-reset evidence to correct ordering only; do not add unrelated TX state. |
| D: DRV_LOAD does not read back | Host-control/firmware arbitration is unresolved. | Investigate CTRL_EXT ownership/readback only; do not proceed as if ownership was acquired. |
| E: raw TX succeeds but DHCP fails | Low-level TX is solved. | Compare raw versus DHCP publication and lifecycle only; stop changing descriptor-fetch/engine state. |
| F: TDH advances but DD is absent | Fetch is solved; writeback/status is the boundary. | Investigate status/writeback semantics only. |
| G: DD appears but guideXOS misses it | Hardware completed; software observation is the boundary. | Fix completion observation/barrier handling only. |

No firmware/ME disablement, SWFLAG seizure, SWSM write, FWSM write, NVM
change, management-filter change, PHY ownership change, IOMMU change, RTLC
change, advanced descriptor change, DHCP change, or general DMA redesign is
part of Phase 18.
