# Intel I219-LM TX Descriptor-Consumption Qualification — Phase 27

**Status:** bounded probe implementation and hosted qualification are complete.
No physical AIDA_LPT transmission has been run. The Phase 27 ISO and QEMU
readiness qualification are recorded in the final artifact addendum below.
Physical descriptor consumption and wire emission remain unverified.

## Repository gate and phase authority

- Required branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`.
- Starting HEAD: `2b1d0fd14d323d200aee5b217e4caf952e61ea10`.
- Starting worktree: clean; the branch was one local commit ahead of its remote.
- Phase 23, 24, 25 and accepted Phase 26/26Q records were reviewed.
- The accepted Phase 26Q closeout records `last_completed=PHASE26` and
  `next_expected=PHASE27`.
- No prior Phase 27 implementation or report existed. No `.phase` file was
  introduced; phase authority remains audit-document based.

## Accepted Phase 26 baseline

Phase 26R established the working physical initialization path on AIDA_LPT:
Intel `8086:156F` I219-LM, constrained-low DMA placement enabled, TX ring READY,
NIC registered and active, Driver Ready YES, hardware initialization PASS,
PHY/MAC link UP, PCI command `0x0006`, valid TXDCTL1 `0x0141001F`, and
LAN_INIT_DONE set in the visible PRE and POST samples. No Phase 26 STATUS write
or physical packet transmission qualification occurred. The source of the old
`TX_DMA_REGION_UNAVAILABLE` state was a disabled configuration gate, not a
demonstrated allocation failure.

This baseline establishes initialization and ring provisioning only. It does
not prove that the I219 fetched a descriptor, set DD, emitted a frame, or
delivered a packet to a peer.

## Hypothesis and result vocabulary

The experiment asks whether one valid descriptor published to the initialized
I219 TX ring is consumed and completed. It does not change DMA placement, PHY
handling, STATUS, or reset sequencing.

- **A — no demonstrated consumption:** TDH remains unchanged, DD is clear after
  the bounded observation, and no independent evidence establishes a fetch.
  This does not prove the hardware never fetched the descriptor.
- **B — progress without completion:** TDH advances or descriptor memory shows
  device-side progress, but DD is not established.
- **C — completion-observation discrepancy:** DD is visible in the final
  descriptor read but software did not safely retire the descriptor.
- **D — descriptor completion successful:** software observed a valid DD and
  safely retired the descriptor. This does not prove wire emission.
- **PREREQUISITE_NOT_MET:** the command refused before submission and names the
  failed safety gate.
- **INCONCLUSIVE:** required observations are missing or invalid.

The independent claims remain separate: descriptor submitted, descriptor
consumed, descriptor marked complete, frame seen on an external tap, and packet
received by a peer.

## Existing `nicinfo tx iommu` path and safety changes

The prior command was explicitly user-invoked, built the established 60-byte
Phase 17 fixture, called the normal TX boundary once, wrote TDT at most once,
used the existing one-million-iteration poll limit, and did not retry or reset
after a timeout. It required the constrained-low TX DMA mode and retained the
ring poison state after an ambiguous timeout.

Before Phase 27 it did not require current driver readiness, live link-up,
current TDH/TDT ownership, an available slot at the current software index, or
zero previous TX activity. It also allowed transmission when active VT-d
translation had an unread I219 context, and it had no one-attempt-per-boot
latch. Those were safety and evidence gaps in the demonstrated path.

The existing command is now split into two modes:

- `nicinfo tx iommu` performs read-only preflight and never publishes a packet.
- `nicinfo tx iommu isolated` performs the same gates and may submit one
  descriptor only after the operator confirms an isolated test segment with no
  competing sender.

The diagnostic-only latch is consumed immediately before entering the existing
normal raw-TX boundary. It prevents a second Phase 27 probe during that boot,
including after a late pre-publication failure. Other TX/RX implementation
paths were not changed.

## Pre-transmission gates

The probe refuses unless all inspectable conditions pass:

- exact PCI identity `8086:156F` and `I219/PCH` family selection;
- `is_driver_ready`, completed hardware initialization, completed reset/rearm,
  no reset timeout, and no stronger-recovery requirement;
- cached link UP and live MAC STATUS link-up bit;
- Phase 16 constrained-low DMA handoff with valid/owned/contiguous/identity/
  below-4-GiB/cacheable flags, `EfiLoaderData` ownership, expected two-page
  geometry, and no overlapping DMA objects;
- fresh ring and selected-slot DMA translations, exact ring/descriptor/buffer
  addresses, ring alignment and length, and valid packet-buffer range;
- a valid read-only TX-register snapshot, PCI memory and bus-master enablement,
  TDBA/TDLEN match, enabled TX engine, valid TXDCTL0/TXDCTL1 policy, and TIPG;
- TDH and TDT both equal the software ring index and the selected descriptor has
  DD set before reuse;
- no prior software TX attempt/publication/completion during this boot, no
  poisoned ring, and no prior Phase 27 attempt;
- known DMA translation behavior: DMAR absent, or a readable matching DRHD with
  translation disabled. Active translation, an unknown matching-unit state, an
  unreadable register snapshot, or unknown DRHD coverage refuses transmission
  because this kernel cannot inspect the I219 context entry or prove its IOVA
  mapping.

The typed `isolated` token records the operator confirmation for the external
network condition. Software counters can establish that guideXOS has not
previously submitted TX on this boot; they cannot detect another machine on the
cable. The user must use an isolated test segment and stop other local senders.
No failed prerequisite is repaired automatically.

## Baseline, one-shot attempt, and timeout behavior

The read-only baseline is captured before the probe changes the shared payload
buffer or descriptor. Output includes TDBAL/H, TDLEN, TDH, TDT, TCTL, TXDCTL0/1,
PCI command, STATUS, link state, current descriptor bytes and status, ring and
buffer CPU/DMA addresses, preflight validation results, DMA handoff provenance,
EFI memory attributes, and existing TX counters.

After passing the gates, the established descriptor publisher prepares the one
60-byte fixture (`ff:ff:ff:ff:ff:ff`, EtherType `0x88B5`, marker
`GXOS-I219-P17`), publishes one descriptor, performs one TDT write, and polls
DD up to `1,000,000` iterations. The poll limit is finite but is not a measured
wall-clock duration. The existing `sfence` publication barrier and completion
barrier remain in use. The path adds no cache flush, reset, PHY setup, STATUS
write, or PCI command change.

The command captures pre-TDT, immediate TDT readback, post-doorbell registers,
final TDH/TDT, final descriptor bytes/DD, existing TX counters, and available
timeout error indicators. ICR is only read by the existing timeout-boundary
capture, not by the pre-transmission snapshot. A timeout does not retry, force
DD, free or reuse potentially device-owned DMA memory, or reset the NIC. The
existing poison state preserves ambiguous ownership and requires a cold boot
before another TX investigation.

## Hosted qualification

`scripts/run-network-phase27-tests.ps1` runs the existing Phase 11–26R network
regression chain, compiles and runs `tests/network_tx_phase27_test.cpp`, and
checks the Phase 27 source gates. Hosted cases cover A/B/C/D and inconclusive
classification; unsupported identity; family/driver/init/link failures; DMA
region, ring, buffer and IOMMU gates; ring ownership; unavailable descriptor;
prior TX, recovery and missing isolation confirmation; and the one-shot
preflight parser. Static checks enforce one call to the existing TX boundary,
the existing finite poll bound, no retry, no reset/rearm, no STATUS write, no
resource release, pre-publication DMA validation, and an unchanged production
`send_frame` wrapper.

**Result:** Phase 11–26R regression chain PASS; Phase 27 hosted decision tests
PASS. These are hosted logic and source-contract results, not physical I219
evidence.

The AMD64 kernel target compiled successfully with TX DMA placement and
LAN_INIT diagnostics enabled. The UEFI loader compiled successfully with
Visual Studio. Existing unrelated compiler warnings remain in other kernel
areas; the Phase 27 code introduced no reported warnings. No STL, `std::`,
exceptions, RTTI, dynamic allocation, unbounded polling, or unbounded diagnostic
storage was added.

## ISO qualification

The Phase 27 diagnostic image is built by
`scripts/create-phase27-aida-i219-tx-consumption-iso.ps1`. It keeps the accepted
Phase 26R build configuration enabled: AMD64 UEFI, Phase 5 stage 8, Phase 6
stage 0, Phase 7 stage 4, `-I219TxDmaPlacementExperiment`,
`-I219LanInitDiagnostics`, and the existing PacMan package reused. The ISO
manifest identifies the implementation source commit. Structural verification
and the isolated QEMU boot/readiness result are recorded in the artifact
addendum.

QEMU's generic e1000 boot is a readiness and packaging check only. It is not
evidence of physical I219 descriptor consumption.

## AIDA_LPT handoff

The physical test remains pending. Boot the Phase 27 ISO named in the artifact
addendum on AIDA_LPT with the Ethernet link up. Use a passive tap or isolated
managed-switch mirror if available. An external capture should identify the
single broadcast destination, source MAC, EtherType `0x88B5`, marker
`GXOS-I219-P17`, frame count and timestamp.

Run these read-only commands first and photograph the complete output:

```text
nicinfo laninit
nicinfo brief
nicinfo link
nicinfo tx brief
nicinfo dma brief
nicinfo tx reset brief
nicinfo tx iommu
```

Abort if any safety gate is not met, if another TX attempt has occurred, if the
ring is poisoned/not idle, if the link is down, or if active/unknown IOMMU
translation is reported. Do not run a reset, rearm, raw TX, DHCP, or another
network sender to repair a failed gate. If the preflight passes and the segment
is isolated with no competing sender, run exactly once:

```text
nicinfo tx iommu isolated
```

Photograph the complete command output, including `outcome`, before/after
TDH/TDT, descriptor status, poll count, DMA addresses, gate/counter lines and
VT-d state. On timeout, preserve the output and shut down; do not retry, free
DMA memory, or reset on that boot. After either completion or timeout, stop
network activity and enter `shutdown` in the shell (or use the OS shutdown
control). Use a cold boot before further TX investigation. Report wire
transmission as unverified unless a passive external capture sees the exact
fixture; descriptor completion alone does not prove DHCP/TCP/HTTPS
functionality.

## Physical status and phase tracking

No AIDA_LPT physical experiment has been performed for Phase 27. No outcome A,
B, C or D is claimed. Descriptor consumption, external frame emission and peer
receipt are all unverified. Do not advance this state until the requested
physical evidence satisfies the Phase 27 acceptance criteria:

```text
last_completed=PHASE26
next_expected=PHASE27
```

Do not begin Phase 28 automatically.

## Artifact addendum

To be completed after the Phase 27 source commit is packaged and the image
passes structural and QEMU readiness checks.
