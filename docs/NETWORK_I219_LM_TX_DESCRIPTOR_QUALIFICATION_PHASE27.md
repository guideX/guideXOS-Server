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
- proven non-remapped DMA behavior: a readable matching DRHD with translation
  disabled. An absent or malformed DMAR table, unknown DRHD coverage, an
  unreadable/invalid register snapshot, or active translation refuses
  transmission because this kernel cannot inspect the I219 context entry or
  prove its IOVA mapping. DMAR absence is diagnostic evidence, not proof that
  translation is disabled or that DMA addresses are identity mapped.

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

## Original AIDA_LPT handoff (superseded by PHASE27R)

The read-only preflight and isolated command were executed on AIDA_LPT as
recorded below. Do not repeat the isolated command using the original Phase 27
ISO. Its no-DMAR handling is superseded by the PHASE27R safety correction.
After producing an image with the corrected source, the next useful physical
check is read-only `nicinfo tx iommu` output to confirm the unknown-mapping
gate. No physical transmission is authorized by this audit update.

The historical Phase 27 procedure was to boot the ISO named in the artifact
addendum on AIDA_LPT with the Ethernet link up, use a passive tap or isolated
managed-switch mirror if available, and capture the broadcast fixture with
EtherType `0x88B5` and marker `GXOS-I219-P17`.

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
network sender to repair a failed gate. The historical procedure then allowed
a single isolated probe after preflight passed. That step is suspended pending
qualification of the corrected unknown-IOMMU gate and an independently
justified DMA-translation safety model:

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

## PHASE27R operator evidence and command-path reconciliation

On 2026-10-20 the operator confirmed running the complete Phase 27 command
sequence, including both `nicinfo tx iommu` and
`nicinfo tx iommu isolated`. The supplied observation summary reports
`descriptor-final-DD=not-submitted`, no TX timeout/error snapshot, and the
IOMMU reason `ACPI DMAR table is not present`. No independent frame emission
was demonstrated.

The two command paths are distinct. `nicinfo tx iommu` dispatches to the
read-only Phase 27 preflight; `nicinfo tx iommu isolated` dispatches to the
same function with the isolated-network confirmation flag set. The shell
parser distinguishes the exact third token `isolated`. Both reports call the
same `vtd::discover` audit used by `nicinfo dma brief`, so the ACPI/DMAR
classification and failure reason can look similar, but the TX IOMMU command
has its own output and safety gate path.

When the validated root has no DMAR entry, the audit leaves DRHD count at zero
and matching-DRHD false; those are consequences of the missing table. In
`nicinfo dma brief`, the audit reason is printed as `failure=ACPI DMAR table is
not present`. In the Phase 27 report, that reason is printed as
`IOMMU evidence-unavailable`; its `failure=` field reports a probe gate or TX
helper result. This label difference distinguishes overlapping audit data
from an accidentally shared command handler.

The UEFI loader selects an RSDP pointer from the ACPI 2.0/1.0 EFI configuration
table and passes it in BootInfo. The kernel validates the RSDP and selected
RSDT/XSDT checksums and bounds, then scans the root entries. The exact
`ACPI DMAR table is not present` result is emitted only after that traversal
completes without finding a DMAR signature. This supports “no DMAR in the
firmware-supplied root table,” rather than an RSDP/root discovery or validation
failure; it does not establish the hardware translation state.

In the original Phase 27 source, a validated ACPI root with no DMAR entry was
marked as a known DMA-mapping state. That did not prove translation was
disabled or that the active device context was identity mapped. The gate now
requires a readable matching DRHD and a valid snapshot showing translation
disabled. Missing DMAR remains a diagnostic limitation and now fails the
unknown-mapping safety gate; a separate, independently justified no-remapping
proof could support a future safe path.

`descriptor-final-DD=not-submitted` means the Phase 27 software path did not
record its publication fence and descriptor-publication counter together, so
it did not reach its TDT publication point. This establishes no Phase 27
descriptor publication; it does not identify the earlier gate or report
whether the one-shot latch was consumed. The summary supplied here omits the
isolated output's `failure=`, `probe-consumed=`, `preflight=`, and `attempt=`
values. In the original implementation the latch was set after preflight and
operator confirmation, immediately before entering the TX helper, so a late
pre-publication refusal could consume the latch while still reporting
`not-submitted`. An early safety-gate refusal leaves it unconsumed.

The phrase `ACPI DMAR table is not present` is the VT-d audit reason, not the
Phase 27 safety-gate string. If earlier gates pass, the tightened IOMMU gate
reports `active or unknown IOMMU mapping cannot be proven safe`; the exact
gate reported by the already-run isolated command cannot be recovered from the
summary alone. Keep the phase open and do not repeat a physical probe to
recover the omitted output.

The PHASE27R hosted Phase 11–27 regression chain passed. The AMD64 freestanding
kernel and UEFI loader built successfully with the Phase 27 I219 diagnostic
configuration enabled. A second AMD64 kernel/UEFI build also passed with TX
DMA placement and LAN_INIT diagnostics disabled. Existing unrelated compiler
warnings remain. No replacement physical ISO was packaged; the artifact in the
addendum contains the original Phase 27 behavior and must not be used for a
repeat probe.

## Physical status and phase tracking

The Phase 27 command sequence was run on AIDA_LPT, but the isolated probe
reported no descriptor submission. No outcome A, B, C or D is claimed because
the probe did not publish a descriptor. Descriptor consumption, external frame
emission and peer receipt are all unverified. Do not advance this state until
the safety path is qualified and physical evidence satisfies the Phase 27
acceptance criteria:

```text
last_completed=PHASE26
next_expected=PHASE27
```

Do not begin Phase 28 automatically.

## Artifact addendum

The Phase 27 implementation source commit is
`0d2dfb15be27eb8a31179dbe927787f3e289bd5d` on
`NAVIGATOR_GENERAL_IMPROVEMENTS`.

- ISO: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS\dist\guideXOS-Server-v0.1.0-navgen-phase27-i219-tx-consumption-amd64.iso`
- Size: `91,293,696` bytes.
- SHA-256: `c8efccec1fd471a1a4f799bdd728f04a8c1720d28c74ff60ed4bc1b08e860a87`.
- Checksum sidecar: `dist/guideXOS-Server-v0.1.0-navgen-phase27-i219-tx-consumption-amd64.iso.sha256`; its digest matches the ISO.
- Manifest: `dist/guideXOS-Server-v0.1.0-navgen-phase27-i219-tx-consumption-amd64.manifest.json`; `sourceGitCommit` is the implementation commit above.
- Build: AMD64 UEFI, Phase 5 stage 8, Phase 6 stage 0, Phase 7 stage 4 (`register`), constrained-low I219 TX DMA placement enabled, LAN_INIT diagnostics enabled, PacMan package rebuild skipped/reused, PyCdlib backend.
- Packaged kernel SHA-256: `d99e73466c081ad474b17dbc826791ff0d6fe8b0010ff849273b4afaaf3e1d48`.
- Packaged `BOOTX64.EFI` SHA-256: `5104d62c2a7616545729553f9a62e5f56767aafddd2877348b2e49953094a07d`.

The release verifier passed. It recorded a bootable UEFI El Torito image with
platform ID `0xEF`, no-emulation media, a `91,226,112`-byte EFI boot image, and
the expected ISO size. The isolated QEMU readiness test booted this exact ISO
using QEMU's generic `e1000` device. Its serial log contained the firmware DVD
boot entry, guideXOS UEFI bootloader, kernel load, 64 MiB ramdisk load, desktop
event loop ready marker, and kernel main-loop marker. QEMU was stopped after
the required bounded readiness grace period. This is boot/package evidence;
the generic virtual NIC does not emulate or qualify the physical I219-LM.

At packaging start, the release manifest reported a dirty worktree containing
only `M ESP/build-identity.txt`. The canonical build had regenerated this
tracked file with the Phase 27 build flags, unique build ID, timestamp, and
UEFI/kernel hashes; the file was restored to its original committed content
after packaging. The manifest records the exact generated identity file hash
inside the ISO and the clean implementation source commit. No implementation
source was uncommitted at packaging time.

No AIDA_LPT physical transmission was performed. There is no descriptor
consumption, DD completion, external frame capture, or peer-receipt evidence;
the audit's phase tracker remains `last_completed=PHASE26` and
`next_expected=PHASE27` until the physical procedure above is run and evidence
is supplied.
