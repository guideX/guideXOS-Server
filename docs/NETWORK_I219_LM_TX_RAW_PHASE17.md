# AIDA_LPT I219 minimal raw TX / descriptor-fetch experiment — Phase 17

Status: implementation and hosted validation complete; AMD64 artifact/QEMU
validation are performed by the release steps below; physical AIDA_LPT
validation remains pending in this environment.

## Outcome

No physical AIDA_LPT result is claimed here. The implementation is ready to
answer the Phase 17 question on a fresh boot:

> If DHCP, IPv4, UDP, ARP, and normal protocol construction are removed, does
> one fixed Ethernet-II descriptor get fetched and completed by the I219?

The software experiment has two bounded commands:

```text
nicinfo tx raw
nicinfo tx raw direct
nicinfo tx raw status
```

`raw` builds the fixture and enters the exported `nic::send_frame()` boundary.
`raw direct` builds the same fixture and enters the same guarded descriptor
submission primitive directly. There is no second ring, buffer, descriptor
ABI, retry path, or register policy. Because the existing `send_frame()`
wrapper has no higher-layer semantics, the two variants are expected to be
equivalent on this revision; the distinction is retained to make that fact
observable and to leave a clean comparison boundary.

## Exact Phase 16 physical result incorporated

The fresh AIDA_LPT Phase 16 boot physically reproduced:

```text
machine: AIDA_LPT
NIC: Intel I219-LM / PCH
PCI: 8086:156F, subsystem 103C:8079, revision 21
MAC: EC-8E-B5-9F-36-38
PCI-CMD: 0x0006 (Memory Space Enable and Bus Master Enable)
Driver Ready: YES
Link: UP
hardware: PASS
TXDCTL1: 0x0141001F, valid=yes
TCTL: enabled
TIPG: programmed
TARC0: 0x20000403
TARC1: 0x00000403
IOSFPC: 0x01011108
CTRL_EXT: 0x014A1027
PBA: 0x000E0012
FWSM: 0xE001C25C
TDT: 0 -> 1, readback remains 1
TDH: remains 0
DD: absent
polls: 1000000
timeout: yes
poison: yes
failure: TX_DESCRIPTOR_NOT_CONSUMED
DHCP: built=1 attempts=1 submitted=1 complete=0 offer=0 request=0 ack=0 nak=0 fail=1
```

Phase 16 constrained placement therefore did not visibly change the failure
boundary. It preserved the Phase 15 engine/register state but did not produce
descriptor consumption.

The physical evidence qualification from Phase 16 is retained. The supplied
photographs clearly showed the failure and engine state, but the upper
`nicinfo tx brief` lines containing every explicit Phase 16 experimental-mode
field were not clearly visible. The implementation audit shows that the ISO
was built with `GXOS_I219_TX_DMA_PLACEMENT_EXPERIMENT`, I219 initialization
selects constrained storage only after the loader handoff validates, and an
invalid/missing/unowned/unmapped handoff fails closed. There is no silent
fallback to kernel-image TX storage. That establishes “the experimental path
was active by software design/runtime state”; it does not upgrade the
photographs into visual proof of every upper diagnostic field.

## Raw frame fixture

The frame is constructed in `build_raw_tx_frame()` and contains no DHCP, IPv4,
UDP, ARP, route, DNS, or peer-reply dependency:

| field | value |
| --- | --- |
| destination | `ff:ff:ff:ff:ff:ff` |
| source | the initialized physical adapter MAC |
| EtherType | `0x88B5`, encoded on wire as bytes `88 B5` |
| payload marker | ASCII `GXOS-I219-P17` |
| padding | zero-filled deterministic padding |
| frame length | 60 bytes, excluding FCS |
| FCS | omitted from buffer; `IFCS` asks hardware to append it |

The marker is 13 bytes. The Ethernet header is 14 bytes, so the marker begins
at byte 14 and the remaining bytes through byte 59 are zero. The builder
rejects a null buffer, insufficient capacity, null/zero/broadcast/multicast
source address, or an invalid output pointer. The physical source used by the
current AIDA_LPT evidence is `ec:8e:b5:9f:36:38`.

The one legacy data descriptor is intentionally minimal:

```text
buffer address: exact translated PA of the shared Phase 16 TX buffer
length:         60
cso:            0
cmd:            EOP | IFCS | RS = 0x0B
status:         0 before publication
css:            0
special:        0
context:        none
VLAN:           none
checksum:       none
segmentation:   none
```

The descriptor index is the existing `s_txCur` value, descriptor 0 on a fresh
boot. The ring remains 64 entries and `TDLEN` remains 1024 bytes. The direct
path uses the same shared Phase 16 ring and buffer, then the same publication
fence, TDT write/readback, DD polling, and timeout poisoning.

## Normal versus direct architecture

```text
nicinfo tx raw
  -> build_raw_tx_frame
  -> nic::send_raw_diagnostic_frame(Normal)
  -> exported nic::send_frame
  -> submit_frame
  -> shared descriptor/ring/register/polling policy

nicinfo tx raw direct
  -> build_raw_tx_frame
  -> nic::send_raw_diagnostic_frame(Direct)
  -> submit_frame
  -> shared descriptor/ring/register/polling policy
```

Both paths retain checks for initialization/active state, PCI Memory Space and
Bus Master Enable, poisoned-ring state, descriptor ownership/DD availability,
DMA translation, descriptor ring address and alignment, TDBA/ring length,
buffer PA, buffer-to-descriptor match, publication ordering, TDT readback, and
bounded DD completion. A timeout poisons the shared ring and the raw command
does not retry. The raw wrapper refuses to touch descriptor/TDT state if the
constrained experiment is inactive and reports:

```text
failure=TX_DMA_EXPERIMENT_NOT_ACTIVE
```

The ISO is built with constrained placement enabled, so the expected baseline
diagnostic is:

```text
dma-mode=constrained-low experiment-active=yes
```

## Descriptor-mode audit

The exact I219 PCI ID is upstream e1000e’s `E1000_DEV_ID_PCH_SPT_I219_LM`
`0x156F`, mapped to the SPT PCH MAC family. Upstream’s hardware definitions
include a 16-byte `struct e1000_tx_desc` with buffer address, length, CSO,
command, status, CSS, and special fields—the same legacy layout retained by
guideXOS. The same driver’s transmit path uses `EOP | IFCS | RS`, and its SPT
initialization programs the TX descriptor-control policy and TARC/IOSFPC
workaround used by this experiment. See the primary upstream sources:

- [Linux e1000e hardware definitions](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/hw.h)
- [Linux e1000e transmit path](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/netdev.c)
- [Linux e1000e ICH8/PCH hardware initialization](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/ethernet/intel/e1000e/ich8lan.c)
- [Intel PCIe GbE controller open-source programming manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/pcie-gbe-controllers-open-source-manual.pdf)

The Intel I219 connection datasheet available for this audit is an electrical,
NVM, and platform-connection document rather than a complete MAC descriptor
programming reference. It does not provide an I219-specific statement that
DEXT=0 is forbidden, nor does it state that an advanced descriptor is required
for a one-buffer transmit. The upstream e1000e implementation defines and
uses both legacy and advanced/context forms because its driver supports
offloads and its general transmit architecture; that choice alone is not
evidence that this minimal I219 mode requires advanced descriptors.

Conclusion for Phase 17: legacy 16-byte descriptors remain the evidence-backed
continuity choice. They are not switched to advanced descriptors by
speculation. No authoritative source reviewed here proves legacy descriptors
invalid for the configured I219/SPT mode, and no authoritative source proves
that an advanced descriptor is mandatory for this no-offload one-packet case.
An advanced legacy/advanced A/B belongs in Phase 18 if both raw variants fail.
No advanced descriptor implementation is included in this phase.

## Preserved Phase 15/16 state

The raw fixture changes only the frame and submission boundary. It preserves:

```text
TX descriptors: 64
descriptor ABI: legacy 16-byte
TDLEN: 1024 bytes
TX DMA storage: Phase 16 constrained, loader-owned, below 4 GiB handoff
TCTL: enabled with existing policy
TIPG: existing programmed value
TXDCTL0/TXDCTL1: existing SPT policy; TXDCTL1 validity check retained
TARC0/TARC1: existing values
IOSFPC: existing value
PCI command: existing Memory Space + Bus Master checks
barriers: existing publication/completion barriers
polling: existing bounded 1,000,000 iteration loop
ring lifecycle: one attempt, timeout poisons, no unsafe retry
RX ring and RX buffers: unchanged
PHY/link refresh: unchanged
DHCP path: unchanged
```

The generic descriptor setup now explicitly clears `cso`, `css`, and `special`
before publication. This is descriptor hygiene for both existing TX and the
raw fixture; it does not enable an offload or change the DHCP frame path.

## Diagnostics

`nicinfo tx brief` remains capped at 20 logical lines and now exposes
`experiment-active=yes|no` beside the DMA mode. `nicinfo tx raw status` is a
bounded 20-line-class report containing:

```text
mode/path/attempt/result
dma-mode/experiment-active
frame length/valid/destination/source/EtherType/marker
descriptor index/length/command/prepared
status before/final/DD
TDT before/written/final
TDH before/final
polls/timeout/poison/failure
ring PA/buffer PA
TDBA match/buffer match
TDBAL/TDBAH/TDLEN
TXDCTL1 validity/TCTL
TIPG/TARC0/IOSFPC
PCI command/ownership/mapping
raw descriptor word 0/word 1 before TDT
raw descriptor word 0/word 1 after TDT
raw descriptor word 0/word 1 final
```

The raw snapshots are CPU-visible 16-byte descriptor words represented as two
64-bit values. The first is the buffer PA. The second encodes length, CSO,
command, status, CSS, and special in the legacy layout. They are retained
separately from later DHCP diagnostics so a later command cannot overwrite the
raw experiment evidence. Before TDT is captured after descriptor preparation
and before the doorbell; after TDT is captured after the TDT write/readback;
final is captured at DD completion or timeout.

## Failure taxonomy and poisoning

Phase 17 adds `TX_RAW_FRAME_INVALID` and reuses the Phase 16
`TX_DMA_EXPERIMENT_NOT_ACTIVE` classification. The raw report still exposes
the existing classes:

```text
TX_DESCRIPTOR_NOT_CONSUMED
TX_COMPLETION_TIMEOUT
TX_DOORBELL_NOT_OBSERVED
TX_DESCRIPTOR_INVALID
TX_DMA_ENGINE_DISABLED
TX_FETCH_CONTROL_INVALID
TX_DMA_* handoff/provenance failures
```

If a previous attempt timed out, `s_txPoisoned` and `ringPoisoned` reject a
subsequent submission with no descriptor or buffer overwrite. The physical
procedure therefore requires a fresh boot before each raw experiment and says
to stop after a raw timeout.

## Tests and build validation

`scripts/run-network-phase17-tests.ps1` runs the complete inherited chain and
the new `tests/network_tx_phase17_raw_tx_test.cpp`. The new deterministic suite
covers:

- broadcast destination and exact source-MAC copy;
- EtherType `0x88B5` and marker `GXOS-I219-P17`;
- 60-byte minimum frame and deterministic zero padding;
- invalid source/capacity/null-input rejection;
- descriptor length, `EOP|IFCS|RS = 0x0B`, cleared status;
- zero checksum/VLAN fields and exact legacy raw-word encoding;
- descriptor buffer-PA match/mismatch;
- constrained-mode activation predicate and mode names;
- raw normal/direct/status command parsing;
- inactive-experiment and raw-frame failure names;
- bounded diagnostic contracts and unchanged DHCP sender reference.

The hosted suite does not fake a hardware DD completion. Descriptor completion,
TDH movement, and wire behavior remain QEMU/physical observations.

The full AMD64 freestanding build uses the existing Phase 16 controls:

```text
-I219Phase5Stage 8
-I219Phase6Stage 0
-I219Phase7Stage 4
-I219TxDmaPlacementExperiment
```

The release packager verifies the FAT32 ESP, UEFI boot structure, ISO sector
alignment, SHA-256 sidecar, and manifest. The Phase 17 packaging script is
`scripts/create-phase17-aida-i219-raw-tx-iso.ps1`.

## QEMU control

The existing release QEMU control uses the E1000 model and performs fresh
UEFI/ISO boots with disposable writable state. It validates firmware entry,
bootloader, kernel load, ramdisk load, desktop readiness, NIC initialization,
and kernel main-loop readiness. This is useful shared-path regression evidence.

Three fresh boots of the Phase 17 ISO passed those readiness checks. The
serial logs reached firmware entry, `guideXOS UEFI Bootloader`, kernel/ramdisk
load, desktop readiness, NIC initialization, and the kernel main loop.

The current harness has no reliable serial shell-input injection, so this phase
does not claim that `nicinfo tx raw` was executed inside QEMU. No QEMU DD or
TDT result is inferred from boot readiness. If command injection is added later,
the exact control is `nicinfo tx raw` before any DHCP command; the expected
E1000 control result is descriptor preparation followed by TDT advancement and
DD completion.

## ISO metadata

The release step records the exact values here after packaging:

```text
path:     D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase17-aida-i219-raw-tx-amd64.iso
filename: guideXOS-Server-v0.1.0-phase17-aida-i219-raw-tx-amd64.iso
size:     91293696 bytes
SHA-256:  19cd8131c689241388efd5db25d53e396a23aed6f2a1a390493e6c637043cb75
manifest: D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase17-aida-i219-raw-tx-amd64.manifest.json
checksum: D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-phase17-aida-i219-raw-tx-amd64.iso.sha256
```

The manifest must show the Phase 16 constrained-DMA switch, Phase 5 stage 8,
Phase 6 stage 0, Phase 7 stage 4, AMD64, and the PyCdlib backend. The raw
fixture is compiled into that image; no silent kernel-image fallback is
acceptable for the physical test.

## Physical AIDA_LPT procedure

Ethernet must be connected before power-on. Use a fresh boot and do not run
DHCP before the raw attempt:

1. Run `nicinfo brief`; confirm `Driver Ready: YES` and `Link: UP`.
2. Run `nicinfo tx brief`; photograph the baseline. Confirm
   `experiment-active=yes`, constrained DMA mode, ownership/mapping validity,
   below-4G geometry, TDBA match, buffer match, `TXDCTL1 valid=yes`, and
   `timeout=no`, `poison=no`.
3. Run `nicinfo tx raw`.
4. Run `nicinfo tx raw status`.
5. Run `nicinfo tx brief`; photograph the decisive state.
6. If raw completed, optionally run `dhcp /discover` and `dhcp status` on the
   same unpoisoned boot for comparison.
7. If raw timed out, stop. Do not run DHCP on the poisoned ring.

Record the exact frame fields, descriptor index, length, command, status,
before/after/final raw words, ring/buffer PA, TDBA and buffer matches, TDT,
TDH, DD, polls, timeout, poison, and failure classification.

## Interpretation and recommended next phase

| physical observation | interpretation | next phase |
| --- | --- | --- |
| Raw completes; DD appears and timeout/poison remain clear | Current constrained placement and descriptor fetch are sufficient for one packet. | Compare raw and DHCP descriptor/frame/buffer lifecycle; stop changing TX engine state. |
| Raw normal fails; raw direct succeeds | Difference is at the public helper boundary or buffer lifecycle. | Isolate only that helper/publication difference. |
| Raw normal and direct fail identically | DHCP and higher protocol construction are eliminated. | Narrow to descriptor interpretation/fetch-mode or a documented descriptor-format A/B. |
| Legacy fails; bounded advanced raw succeeds | Descriptor format is causally implicated. | Qualify advanced one-packet TX repeatedly before any stack rewrite. |
| Both raw variants yield `TDT=1`, `TDH=0`, DD absent | Descriptor is published but still not consumed; placement, ring size, Phase 15 policy, and DHCP are strongly rejected. | Inspect only documented descriptor interpretation/fetch-mode state; consider Phase 18 legacy/advanced A/B. |
| TDH moves but DD is absent | Fetch progressed; completion writeback/status is the boundary. | Investigate writeback/status semantics only. |
| DD appears but software reports failure | Hardware completed; polling/status observation is the boundary. | Fix observation/barrier handling only. |
| Experiment inactive | The artifact is misleading for physical comparison. | Fix only constrained-DMA activation/ownership and rebuild; do not use old placement silently. |

The central acceptance question is local descriptor progress, not wire
visibility. No second host, packet capture, promiscuous mode, or peer reply is
required for Phase 17.
